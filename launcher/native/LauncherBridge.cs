using System;
using System.Collections.Generic;
using System.Collections;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace WuWaVR.Manager
{
    // Calls the existing loopback service; never duplicates injection or edits UEVR settings itself.
    public sealed class LauncherBridge : IDisposable
    {
        public sealed class ExistingLauncher
        {
            public string Address { get; private set; }
            public string AppRoot { get; private set; }
            public int Pid { get; private set; }
            internal ExistingLauncher(string address, string appRoot, int pid)
            { Address = address; AppRoot = appRoot; Pid = pid; }
        }
        readonly HttpClient http;
        readonly Func<string[]> processNames;
        readonly Func<int, bool?> pidAlive;
        readonly TimeSpan requestTimeout, connectTimeout;
        readonly Func<ProcessStartInfo, Process> startHelper;
        public readonly string Data;
        readonly string isolatedAppData;
        public string Address { get; private set; }
        public string AppRoot { get; private set; }
        public ExistingLauncher Conflict { get; private set; }
        string token;
        // Recovery invalidates only this connection. Starting a replacement
        // helper remains an explicit Retry connection action.
        public void ForgetConnection()
        { Address = null; AppRoot = null; token = null; Conflict = null; }
        public LauncherBridge(string data, string isolatedAppData = null, HttpMessageHandler handler = null)
            : this(data, isolatedAppData, handler, ReadProcessNames, ReadPidAlive) { }
        internal LauncherBridge(string data, string isolatedAppData, HttpMessageHandler handler,
            Func<string[]> processNames, Func<int, bool?> pidAlive)
            : this(data, isolatedAppData, handler, processNames, pidAlive,
                TimeSpan.FromSeconds(10), TimeSpan.FromSeconds(20), Process.Start) { }
        internal LauncherBridge(string data, string isolatedAppData, HttpMessageHandler handler,
            Func<string[]> processNames, Func<int, bool?> pidAlive, TimeSpan requestTimeout,
            TimeSpan connectTimeout, Func<ProcessStartInfo, Process> startHelper)
        {
            Data = Path.GetFullPath(data); this.isolatedAppData = isolatedAppData;
            this.processNames = processNames ?? throw new ArgumentNullException(nameof(processNames));
            this.pidAlive = pidAlive ?? throw new ArgumentNullException(nameof(pidAlive));
            if (requestTimeout <= TimeSpan.Zero || connectTimeout <= TimeSpan.Zero) throw new ArgumentOutOfRangeException("timeout");
            this.requestTimeout = requestTimeout; this.connectTimeout = connectTimeout;
            this.startHelper = startHelper ?? throw new ArgumentNullException(nameof(startHelper));
            // One explicit deadline covers headers AND streamed content, rather than
            // HttpClient's ResponseHeadersRead timeout ending as soon as headers arrive.
            http = new HttpClient(handler ?? new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false }) { Timeout = Timeout.InfiniteTimeSpan };
        }
        static string[] ReadProcessNames()
        {
            var processes = Process.GetProcesses();
            try
            {
                var names = new string[processes.Length];
                for (int i = 0; i < processes.Length; i++) names[i] = processes[i].ProcessName;
                return names;
            }
            finally { foreach (var process in processes) process.Dispose(); }
        }
        static bool? ReadPidAlive(int pid)
        {
            try { using (var process = Process.GetProcessById(pid)) return !process.HasExited; }
            catch (ArgumentException) { return false; }
            catch (InvalidOperationException) { return null; }
            catch (Win32Exception) { return null; }
        }
        void RequireNativeIdle(CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            var names = processNames();
            if (names == null) throw new IOException("Cannot check running game processes. Retry before changing packages.");
            foreach (var name in names)
            {
                cancel.ThrowIfCancellationRequested();
                if (String.IsNullOrWhiteSpace(name)) throw new IOException("Cannot identify a running process. Retry before changing packages.");
                string image = name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? name.Substring(0, name.Length - 4) : name;
                // Same four image names as wuwa_player.py GAME_PROCESSES.
                switch (image.ToLowerInvariant())
                {
                    case "client-win64-shipping": case "wuthering waves": case "custom_uevr_injector": case "uevrinjector":
                        throw new InvalidOperationException("Close WuWa and its injector before changing packages.");
                }
            }
        }
        static string ReadReceiptText(string path)
        {
            try { return File.ReadAllText(path); }
            catch (FileNotFoundException) { return null; }
            catch (DirectoryNotFoundException) { return null; }
        }
        static void RequireUnchangedReceipt(string path, string before)
        {
            if (ReadReceiptText(path) != before)
                throw new InvalidOperationException("The launcher changed while preparing the package. Retry before changing packages.");
        }
        public Task PreparePackageChange(string package, CancellationToken cancel)
        { return PrepareHelperStop(package, cancel, false); }
        // App exit may stop an idle launcher service while the already-started game
        // continues. Package replacement retains its stricter game/injector guard.
        public Task CloseHelper(string package, CancellationToken cancel)
        { return PrepareHelperStop(package, cancel, true); }
        async Task PrepareHelperStop(string package, CancellationToken cancel, bool allowRunningGame)
        {
            string expected = Path.GetFullPath(Path.Combine(package, "app"));
            cancel.ThrowIfCancellationRequested();
            if (!allowRunningGame) RequireNativeIdle(cancel);
            string receiptPath = Path.Combine(Data, "launcher.json"), before = ReadReceiptText(receiptPath);
            if (before == null && Address != null)
                throw new InvalidOperationException("The connected helper receipt is missing. Retry connection before stopping it.");
            if (before != null)
            {
                var receipt = Json.Read<Dictionary<string, object>>(before);
                int pid;
                if (receipt == null || !int.TryParse(Json.Text(receipt, "pid"), out pid) || pid <= 0)
                    throw new InvalidOperationException("Cannot verify the existing launcher process. Retry before changing packages.");
                // Attach only inspects a receipt and existing HTTP service. Unlike
                // Connect, it never starts the selected package's possibly broken helper.
                if (await Attach(receiptPath, expected, cancel))
                {
                    RequireUnchangedReceipt(receiptPath, before);
                    var existing = new ExistingLauncher(Address, AppRoot, pid);
                    string accessToken = token;
                    var current = await Status(cancel);
                    RequireKnownIdle(current, allowRunningGame);
                    await ValidateIdentity(existing, cancel);
                    if (!allowRunningGame) RequireNativeIdle(cancel);
                    RequireUnchangedReceipt(receiptPath, before);
                    var reply = await PostTo(existing.Address, accessToken, "/api/stop", new { }, cancel);
                    if (reply == null || !Json.Flag(reply, "ok")) throw new IOException("The launcher did not confirm the stop request.");
                    await WaitForExit(existing.Pid, cancel);
                    string after = ReadReceiptText(receiptPath);
                    if (after != null && after != before)
                        throw new InvalidOperationException("Another launcher started while preparing the package. Retry connection before changing packages.");
                }
                else
                {
                    RequireUnchangedReceipt(receiptPath, before);
                    cancel.ThrowIfCancellationRequested();
                    if (pidAlive(pid) != false)
                        throw new InvalidOperationException("The launcher process is still running or its state is unknown. Retry connection before changing packages.");
                    if (!allowRunningGame) RequireNativeIdle(cancel);
                    RequireUnchangedReceipt(receiptPath, before);
                }
            }
            else
            {
                if (!allowRunningGame) RequireNativeIdle(cancel);
                RequireUnchangedReceipt(receiptPath, null);
            }
            cancel.ThrowIfCancellationRequested();
            if (!allowRunningGame) RequireNativeIdle(cancel);
            Address = null; AppRoot = null; token = null; Conflict = null;
        }
        // An app update must be safe even before a mod is selected, or when
        // the selected helper is broken. This is read-only: it neither launches
        // nor stops a helper and does not alter the current attachment.
        public async Task RequireLauncherUpdateIdle(CancellationToken cancel)
        {
            RequireNativeIdle(cancel);
            string path = Path.Combine(Data, "launcher.json"), before = ReadReceiptText(path);
            if (before == null && Address != null)
                throw new InvalidOperationException("The connected helper receipt is missing. Retry connection before updating the app.");
            if (before != null)
            {
                if (before.Length > 1024 * 1024) throw new InvalidDataException("The launcher receipt is too large.");
                var receipt = Json.Read<Dictionary<string, object>>(before);
                int pid; Uri uri;
                if (receipt == null || !int.TryParse(Json.Text(receipt, "pid"), out pid) || pid <= 0 ||
                    !Uri.TryCreate(Json.Text(receipt, "url"), UriKind.Absolute, out uri) || uri.Scheme != "http" ||
                    uri.Host != "127.0.0.1" || uri.AbsolutePath != "/" || uri.Query != "" || uri.Fragment != "" || uri.UserInfo != "")
                    throw new InvalidOperationException("Cannot verify the existing launcher. Retry before updating the app.");
                string address = uri.GetLeftPart(UriPartial.Authority);
                Dictionary<string, object> identity = null;
                try { identity = Json.Read<Dictionary<string, object>>(await GetText(address + "/api/identity", cancel)); }
                catch (HttpRequestException) { }
                catch (TaskCanceledException) { cancel.ThrowIfCancellationRequested(); }
                RequireUnchangedReceipt(path, before);
                if (identity == null)
                {
                    if (pidAlive(pid) != false)
                        throw new InvalidOperationException("The launcher helper is running or its state is unknown. Retry before updating the app.");
                }
                else
                {
                    int actualPid;
                    string root = Json.Text(identity, "root");
                    if (Json.Text(identity, "app") != "wuwa-vr-player-launcher" ||
                        !int.TryParse(Json.Text(identity, "pid"), out actualPid) || actualPid != pid || String.IsNullOrWhiteSpace(root))
                        throw new InvalidOperationException("The launcher helper identity changed. Retry before updating the app.");
                    var existing = new ExistingLauncher(address, Path.GetFullPath(root), pid);
                    if (!String.IsNullOrWhiteSpace(Json.Text(receipt, "app")) && !SameRoot(Json.Text(receipt, "app"), existing.AppRoot))
                        throw new InvalidOperationException("The launcher receipt belongs to another helper. Retry before updating the app.");
                    RequireKnownIdle(Json.Read<Dictionary<string, object>>(await GetText(address + "/api/status", cancel)));
                    await ValidateIdentity(existing, cancel);
                }
            }
            RequireNativeIdle(cancel);
            RequireUnchangedReceipt(path, before);
            cancel.ThrowIfCancellationRequested();
        }
        public async Task Connect(string package, CancellationToken cancel)
        {
            using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancel))
            {
                deadline.CancelAfter(connectTimeout);
                try { await ConnectWithinDeadline(package, deadline.Token); }
                catch (OperationCanceledException e) when (!cancel.IsCancellationRequested)
                { throw new TimeoutException("The launcher helper did not become ready in time. Use Retry connection; if it fails again, open Troubleshooting and copy diagnostics.", e); }
            }
        }
        async Task ConnectWithinDeadline(string package, CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            var expected = Path.GetFullPath(Path.Combine(package, "app"));
            if (Address != null && Conflict == null && String.Equals(AppRoot, expected, StringComparison.OrdinalIgnoreCase))
            {
                try { await Status(cancel); Conflict = null; return; }
                catch (HttpRequestException) { }
                catch (TaskCanceledException) { cancel.ThrowIfCancellationRequested(); }
                catch (TimeoutException) { cancel.ThrowIfCancellationRequested(); }
                // A stopped helper may have a new address. Revalidate its identity
                // before attaching or restarting. Retain the old connection on
                // recovery failure so package changes still require an idle check.
            }
            var receiptPath = Path.Combine(Data, "launcher.json");
            if (await Attach(receiptPath, expected, cancel)) return;
            var start = new ProcessStartInfo(Paths.Inside(package, "python/pythonw.exe"),
                "-I -B -X utf8 \"" + Paths.Inside(package, "app/dev/wuwa_player.py") + "\" --no-open")
            { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = expected, WindowStyle = ProcessWindowStyle.Hidden };
            start.EnvironmentVariables["WUWA_VR_DATA"] = Data;
            start.EnvironmentVariables["WUWA_VR_NO_DIALOG"] = "1";
            if (isolatedAppData != null) start.EnvironmentVariables["APPDATA"] = Path.GetFullPath(isolatedAppData);
            cancel.ThrowIfCancellationRequested();
            using (var process = startHelper(start))
            {
                if (process == null) throw new IOException("The launcher helper could not be started. Open Troubleshooting and copy diagnostics.");
                while (true)
                {
                    cancel.ThrowIfCancellationRequested();
                    if (await Attach(receiptPath, expected, cancel)) return;
                    if (process.HasExited) throw new IOException("Launcher helper stopped before becoming ready (exit " + process.ExitCode + "). Open Troubleshooting and copy diagnostics.");
                    await Task.Delay(TimeSpan.FromMilliseconds(Math.Min(250, connectTimeout.TotalMilliseconds / 4)), cancel);
                }
            }
        }
        async Task<bool> Attach(string receiptPath, string expected, CancellationToken cancel)
        {
            if (!File.Exists(receiptPath)) return false;
            Dictionary<string, object> receipt;
            try { receipt = Json.Read<Dictionary<string, object>>(File.ReadAllText(receiptPath)); }
            catch (IOException) { return false; }
            catch (ArgumentException) { return false; }
            Uri uri;
            if (!Uri.TryCreate(Json.Text(receipt, "url"), UriKind.Absolute, out uri) || uri.Scheme != "http" || uri.Host != "127.0.0.1" ||
                uri.Port < 1 || uri.AbsolutePath != "/" || uri.Query != "" || uri.UserInfo != "") return false;
            string address = uri.GetLeftPart(UriPartial.Authority);
            Dictionary<string, object> identity;
            try { identity = Json.Read<Dictionary<string, object>>(await GetText(address + "/api/identity", cancel)); }
            catch (HttpRequestException) { return false; }
            catch (TaskCanceledException) { cancel.ThrowIfCancellationRequested(); return false; }
            catch (TimeoutException) { cancel.ThrowIfCancellationRequested(); return false; }
            if (Json.Text(identity, "app") != "wuwa-vr-player-launcher" || Json.Text(identity, "pid") != Json.Text(receipt, "pid")) return false;
            string actualRoot = Path.GetFullPath(Json.Text(identity, "root"));
            if (!String.Equals(actualRoot, expected, StringComparison.OrdinalIgnoreCase))
            {
                int pid;
                if (!int.TryParse(Json.Text(identity, "pid"), out pid) || pid <= 0)
                    throw new InvalidDataException("The existing launcher did not report a valid process identity.");
                Conflict = new ExistingLauncher(address, actualRoot, pid);
                throw new InvalidOperationException("Another WuWa launcher is running from another package. Use the recovery options on Setup.");
            }
            string accessToken = await ReadToken(address, cancel);
            Address = address; AppRoot = expected; token = accessToken; Conflict = null; return true;
        }
        async Task<string> ReadToken(string address, CancellationToken cancel)
        {
            string html = await GetText(address + "/", cancel);
            var match = Regex.Match(html, "<meta name=\"wuwa-token\" content=\"([A-Za-z0-9_-]+)\">");
            if (!match.Success) throw new InvalidDataException("This package does not expose the supported launcher interface.");
            return match.Groups[1].Value;
        }
        void CheckConflictReceipt(ExistingLauncher existing)
        {
            if (!Object.ReferenceEquals(existing, Conflict))
                throw new InvalidOperationException("The existing launcher changed. Retry connection before choosing recovery.");
            var receipt = Json.Read<Dictionary<string, object>>(File.ReadAllText(Path.Combine(Data, "launcher.json")));
            Uri uri; int pid;
            if (receipt == null || !Uri.TryCreate(Json.Text(receipt, "url"), UriKind.Absolute, out uri) || uri.Scheme != "http" || uri.Host != "127.0.0.1" ||
                uri.AbsolutePath != "/" || uri.Query != "" || uri.Fragment != "" || uri.UserInfo != "" ||
                uri.GetLeftPart(UriPartial.Authority) != existing.Address ||
                !int.TryParse(Json.Text(receipt, "pid"), out pid) || pid != existing.Pid ||
                !SameRoot(Json.Text(receipt, "app"), existing.AppRoot))
                throw new InvalidOperationException("The existing launcher changed. Retry connection before choosing recovery.");
        }
        static bool SameRoot(string actual, string expected)
        {
            if (String.IsNullOrWhiteSpace(actual)) return false;
            try { return String.Equals(Path.GetFullPath(actual), expected, StringComparison.OrdinalIgnoreCase); }
            catch (ArgumentException) { return false; }
            catch (NotSupportedException) { return false; }
            catch (PathTooLongException) { return false; }
        }
        async Task ValidateConflict(ExistingLauncher existing, CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested(); CheckConflictReceipt(existing);
            await ValidateIdentity(existing, cancel);
            // The receipt may have changed while the identity request was in flight.
            cancel.ThrowIfCancellationRequested(); CheckConflictReceipt(existing);
        }
        async Task ValidateIdentity(ExistingLauncher existing, CancellationToken cancel)
        {
            var identity = Json.Read<Dictionary<string, object>>(await GetText(existing.Address + "/api/identity", cancel));
            int pid;
            if (identity == null || Json.Text(identity, "app") != "wuwa-vr-player-launcher" ||
                !int.TryParse(Json.Text(identity, "pid"), out pid) || pid != existing.Pid || !SameRoot(Json.Text(identity, "root"), existing.AppRoot))
                throw new InvalidOperationException("The existing launcher identity changed. Retry connection before choosing recovery.");
        }
        ExistingLauncher RequireConflict()
        {
            if (Conflict == null) throw new InvalidOperationException("No other launcher is waiting for recovery. Retry connection.");
            return Conflict;
        }
        public async Task<string> OpenConflictAddress(CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            var existing = RequireConflict();
            await ValidateConflict(existing, cancel); return existing.Address;
        }
        static bool KnownActivity(Dictionary<string, object> value, string key)
        {
            object activity;
            if (value == null || !value.TryGetValue(key, out activity) || !(activity is bool))
                throw new InvalidOperationException("Cannot confirm whether the existing launcher is idle. Retry after checking its status.");
            return (bool)activity;
        }
        static void RequireKnownIdle(Dictionary<string, object> current, bool allowRunningGame = false)
        {
            bool game = KnownActivity(current, "gameRunning"), injector = KnownActivity(current, "injectorRunning"),
                job = KnownActivity(Json.Child(current, "job"), "running"), recording = KnownActivity(Json.Child(current, "recording"), "running");
            if ((!allowRunningGame && (game || injector)) || job || recording || LaunchWorkerRunning(current))
                throw new InvalidOperationException(allowRunningGame ? "Startup, recording or another operation is still active. Use Stop waiting or finish the operation before closing." :
                    "Close WuWa and its injector, and finish the current operation and recording before switching launchers.");
        }
        public async Task StopConflict(CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            var existing = RequireConflict();
            await ValidateConflict(existing, cancel);
            string accessToken = await ReadToken(existing.Address, cancel);
            var current = Json.Read<Dictionary<string, object>>(await GetText(existing.Address + "/api/status", cancel));
            // Missing activity fields are unknown, not permission to stop a helper.
            RequireKnownIdle(current);
            await ValidateConflict(existing, cancel);
            var reply = await PostTo(existing.Address, accessToken, "/api/stop", new { }, cancel);
            if (reply == null || !Json.Flag(reply, "ok")) throw new IOException("The existing launcher did not confirm the stop request.");
            await WaitForExit(existing.Pid, cancel);
            if (Address == existing.Address) { Address = null; AppRoot = null; token = null; }
            if (Object.ReferenceEquals(existing, Conflict)) Conflict = null;
        }
        // Cancellation also bounds a handler/stream that fails to observe its token.
        // Observe/dispose a late response without permitting any automatic retry of POST.
        static async Task<T> BoundedAwait<T>(Task<T> work, CancellationToken cancel, Action<T> discard = null)
        {
            var cancelled = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            using (cancel.Register(() => cancelled.TrySetResult(true)))
            {
                if (await Task.WhenAny(work, cancelled.Task) != work)
                {
                    work.ContinueWith(done => {
                        if (done.IsFaulted) { var observed = done.Exception; }
                        else if (done.Status == TaskStatus.RanToCompletion && discard != null) discard(done.Result);
                    }, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                    throw new OperationCanceledException(cancel);
                }
                return await work;
            }
        }
        async Task<T> Request<T>(HttpRequestMessage request,
            Func<HttpResponseMessage, CancellationToken, Task<T>> read, CancellationToken cancel)
        {
            using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancel))
            {
                deadline.CancelAfter(requestTimeout);
                try
                {
                    using (var response = await BoundedAwait(http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, deadline.Token), deadline.Token, r => r.Dispose()))
                        return await BoundedAwait(read(response, deadline.Token), deadline.Token);
                }
                catch (OperationCanceledException e) when (!cancel.IsCancellationRequested)
                {
                    string recovery = request.Method == HttpMethod.Post ?
                        "The request may still be running. Check launcher status before retrying; use Stop waiting if a launch is still pending." :
                        "Use Retry connection; if it fails again, open Troubleshooting and copy diagnostics.";
                    throw new TimeoutException("The launcher helper timed out at " + request.RequestUri.AbsolutePath + ". " + recovery, e);
                }
            }
        }
        async Task<string> GetText(string url, CancellationToken cancel)
        {
            using (var request = new HttpRequestMessage(HttpMethod.Get, url))
                return await Request(request, async (response, token) => {
                    response.EnsureSuccessStatusCode(); return await RepoClient.BoundedText(response, 1024 * 1024, token);
                }, cancel);
        }
        public async Task<Dictionary<string, object>> Status(CancellationToken cancel)
        {
            if (Address == null) throw new InvalidOperationException("Connect the installed package first.");
            using (var request = new HttpRequestMessage(HttpMethod.Get, Address + "/api/status"))
                return await Request(request, async (response, token) => {
                // An HTTP error still proves a server answered. Do not mistake it
                // for a dead helper and start another process during Connect.
                if (!response.IsSuccessStatusCode) throw new InvalidOperationException("Launcher helper status failed: HTTP " + (int)response.StatusCode + ".");
                return Json.Read<Dictionary<string, object>>(await RepoClient.BoundedText(response, 1024 * 1024, token));
            }, cancel);
        }
        public async Task<Dictionary<string, object>> Post(string path, object body, CancellationToken cancel)
        {
            if (Address == null || !path.StartsWith("/api/", StringComparison.Ordinal)) throw new InvalidOperationException("Launcher is not connected.");
            return await PostTo(Address, token, path, body, cancel);
        }
        async Task<Dictionary<string, object>> PostTo(string address, string accessToken, string path, object body, CancellationToken cancel)
        {
            using (var request = new HttpRequestMessage(HttpMethod.Post, address + path))
            {
                request.Headers.Add("Origin", address); request.Headers.Add("X-WuWa-Token", accessToken);
                request.Content = new StringContent(Json.Write(body), Encoding.UTF8, "application/json");
                return await Request(request, async (response, token) => {
                    var value = Json.Read<Dictionary<string, object>>(await RepoClient.BoundedText(response, 1024 * 1024, token));
                    if (!response.IsSuccessStatusCode) throw new InvalidOperationException(Json.Text(value, "error"));
                    return value;
                }, cancel);
            }
        }
        public async Task<string> Diagnostics(CancellationToken cancel)
        {
            return await GetText(Address + "/api/diagnostics", cancel);
        }
        public async Task Stop(CancellationToken cancel)
        {
            if (Address == null) return;
            // Losing the helper says nothing about the game. Recover the same
            // package connection and obtain fresh activity before allowing stop.
            await Connect(Path.GetDirectoryName(AppRoot), cancel);
            var status = await Status(cancel);
            if (Json.Flag(status, "gameRunning") || Json.Flag(status, "injectorRunning") || Json.Flag(Json.Child(status, "job"), "running") || Json.Flag(Json.Child(status, "recording"), "running") || LaunchWorkerRunning(status))
                throw new InvalidOperationException("Close WuWa and its injector, and finish recording before changing packages.");
            int pid = 0;
            try { var identity = Json.Read<Dictionary<string, object>>(await GetText(Address + "/api/identity", cancel)); int.TryParse(Json.Text(identity, "pid"), out pid); } catch (HttpRequestException) { }
            await Post("/api/stop", new { }, cancel);
            await WaitForExit(pid, cancel);
            Address = null; AppRoot = null; token = null;
        }
        async Task WaitForExit(int pid, CancellationToken cancel)
        {
            if (pid > 0)
            {
                for (int i = 0; i < 40; i++)
                {
                    cancel.ThrowIfCancellationRequested();
                    bool? alive = pidAlive(pid);
                    if (alive == false) break;
                    if (alive == null) throw new IOException("Cannot confirm that the launcher helper exited. Retry before changing packages.");
                    if (i == 39) throw new IOException("Launcher helper has not exited; package files were left in place.");
                    await Task.Delay(250, cancel);
                }
            }
        }
        public static bool RemovalNeedsRestore(Dictionary<string, object> status)
        {
            return Json.Flag(Json.Child(status, "openxr"), "isSimulator") ||
                (Json.Flag(status, "originalBackup") &&
                (String.IsNullOrEmpty(Json.Text(status, "restoredOriginalAt")) || !String.IsNullOrEmpty(Json.Text(status, "selected"))));
        }
        public static bool CurrentLaunch(Dictionary<string, object> status)
        {
            var launch = Json.Child(status, "launch");
            if (launch.ContainsKey("current")) return Json.Flag(launch, "current");
            // Older helpers have no worker correlation. Never revive a saved terminal
            // state, but retain progress/cancel support for their active launch job.
            var job = Json.Child(status, "job");
            return Json.Text(job, "kind") == "launch" && Json.Flag(job, "running");
        }
        public static bool LaunchWorkerRunning(Dictionary<string, object> status)
        {
            var launch = Json.Child(status, "launch");
            // Inaccessible ownership is not proof of exit. Keep mutation/stop
            // guards while the UI offers explicit recovery and window-only exit.
            return CurrentLaunch(status) && (Json.Flag(launch, "running") || Json.Flag(launch, "activityUnknown"));
        }
        public static bool CanCancelLaunch(Dictionary<string, object> status)
        {
            return LauncherPresentation.CanCancelBackend(Json.Child(status, "job")) ||
                (LaunchWorkerRunning(status) && Json.Flag(Json.Child(status, "launch"), "cancellable"));
        }
        public static string LaunchSummary(Dictionary<string, object> status, Func<string, string> text)
        {
            var launch = Json.Child(status, "launch");
            var job = Json.Child(status, "job");
            if ((CurrentLaunch(status) && Json.Text(launch, "phase") == "failed") || (Json.Text(job, "kind") == "launch" && Json.Flag(job, "error"))) return text("launchFailed");
            if (CurrentLaunch(status) && Json.Text(launch, "phase") == "cancelled") return text("launchCancelled");
            if (LaunchWorkerRunning(status) && !String.IsNullOrWhiteSpace(Json.Text(launch, "message"))) return Json.Text(launch, "message");
            if (!Json.Flag(status, "gameRunning")) return text("gameNotRunning");
            // A saved first-frame flag alone is historical evidence. Match its build and
            // injector PID to the current status before presenting it as this launch.
            int injectorPid;
            object processes;
            if (Json.Flag(launch, "firstFrameSeen") && Json.Flag(launch, "injectorRunning") && Json.Flag(status, "injectorRunning") &&
                Json.Flag(status, "selectionMatches") && Json.Text(status, "selected") == Json.Text(launch, "buildId") &&
                int.TryParse(Json.Text(launch, "injectorPid"), out injectorPid) && injectorPid > 0 &&
                status.TryGetValue("processes", out processes) && processes is IEnumerable)
                foreach (var process in (IEnumerable)processes)
                {
                    var p = Json.Object(process); string name = Json.Text(p, "name").ToLowerInvariant();
                    if (Json.Text(p, "pid") == injectorPid.ToString() && (name == "custom_uevr_injector.exe" || name == "uevrinjector.exe")) return text("stereoSeen");
                }
            return text("awaitingStereo");
        }
        public void Dispose() { http.Dispose(); }
    }
}
