using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Management;
using System.Net.Http;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace WuWaVR.Manager
{
    public sealed class RecoveryCandidate
    {
        public int Pid { get; set; }
        public int ParentPid { get; set; }
        public string StartedUtc { get; set; }
        public string Executable { get; set; }
        public string Script { get; set; }
        public string Kind { get; set; }
        public bool Eligible { get; set; }
        public string Reason { get; set; }
        public string Outcome { get; set; }
        // These fields make a selected identity specific; the worker reads them again.
        public string CommandLine { get; set; }
        public string UserSid { get; set; }
        public int SessionId { get; set; }
        internal bool HelperUnresponsive;
    }
    public sealed class RecoveryReport
    {
        public List<RecoveryCandidate> Candidates { get; set; } = new List<RecoveryCandidate>();
        public string Message { get; set; }
        public bool Cancelled { get; set; }
        public string ScannedUtc { get; set; }
        internal string Directory, ManagerData, HelperData;
    }

    // No process-name/tree kill. UAC is requested only by explicit Scan/Stop UI actions.
    public static class LauncherProcessRecovery
    {
        const int Limit = 1024 * 1024;
        const uint Query = 0x1000, Terminate = 0x0001, Synchronize = 0x00100000;
        static readonly string[] LaunchScripts = { "start-wuwa-build.ps1", "start-wuwa-rendering-test.ps1", "sim-run.ps1" };
        internal sealed class Request
        {
            public Request() { }
            public string Operation { get; set; }
            public string Nonce { get; set; }
            public string ManagerData { get; set; }
            public string HelperData { get; set; }
            public int ParentPid { get; set; }
            public string ParentStartedUtc { get; set; }
            public string Sid { get; set; }
            public int Session { get; set; }
            public string CreatedUtc { get; set; }
            public string ScanNonce { get; set; }
            public string ScanHash { get; set; }
            public List<int> Selected { get; set; }
        }
        internal sealed class ProcessEntry { public int Pid, Parent; public string Name; }
        internal sealed class Root { public string Path; public Dictionary<string, string> Files; }

        public static Task<RecoveryReport> ScanAsync(string managerData, string helperData, IEnumerable<string> packageRoots, CancellationToken cancel)
        {
            // Supplied paths are deliberately not a trust list. The elevated worker
            // derives roots from this manager's installed records and helper receipt.
            return Dispatch("scan", managerData, helperData, null, null, cancel);
        }
        public static Task<RecoveryReport> StopAsync(RecoveryReport scan, IEnumerable<int> selectedPids, CancellationToken cancel)
        {
            if (scan == null || String.IsNullOrEmpty(scan.Directory)) throw new InvalidOperationException("Scan launcher processes first.");
            var selected = (selectedPids ?? Enumerable.Empty<int>()).Distinct().ToList();
            if (selected.Count == 0 || selected.Count > 32 || selected.Any(id => !scan.Candidates.Any(c => c.Pid == id && c.Eligible)))
                throw new InvalidOperationException("Select only eligible processes from the scan.");
            return Dispatch("stop", scan.ManagerData, scan.HelperData, scan, selected, cancel);
        }
        static async Task<RecoveryReport> Dispatch(string operation, string manager, string helper, RecoveryReport scan, List<int> selected, CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            manager = Canonical(manager); helper = Canonical(helper);
            string nonce = Guid.NewGuid().ToString("N"), folder = Paths.Inside(manager, "recovery/" + nonce);
            var sid = WindowsIdentity.GetCurrent().User;
            var security = new DirectorySecurity();
            security.SetAccessRuleProtection(true, false); security.SetOwner(sid);
            foreach (var account in new[] { sid, new SecurityIdentifier(WellKnownSidType.LocalSystemSid, null), new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null) })
                security.AddAccessRule(new FileSystemAccessRule(account, FileSystemRights.FullControl, InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
            Directory.CreateDirectory(folder, security);
            using (var current = Process.GetCurrentProcess())
            {
                var request = new Request { Operation = operation, Nonce = nonce, ManagerData = manager, HelperData = helper,
                    ParentPid = current.Id, ParentStartedUtc = current.StartTime.ToUniversalTime().ToString("o"), Sid = sid.Value,
                    Session = current.SessionId, CreatedUtc = DateTime.UtcNow.ToString("o"), Selected = selected,
                    ScanNonce = scan == null ? null : Path.GetFileName(scan.Directory),
                    ScanHash = scan == null ? null : RepoClient.Hash(Paths.Inside(scan.Directory, "result.json")) };
                string requestPath = Paths.Inside(folder, "request.json"); Json.Save(requestPath, request);
                var info = new ProcessStartInfo(current.MainModule.FileName, "--process-recovery \"" + requestPath + "\"")
                { UseShellExecute = true, Verb = "runas", WindowStyle = ProcessWindowStyle.Hidden, WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory };
                try
                {
                    using (var worker = Process.Start(info))
                    {
                        var clock = Stopwatch.StartNew();
                        while (!worker.HasExited)
                        {
                            if (cancel.IsCancellationRequested || clock.Elapsed > TimeSpan.FromSeconds(75))
                            {
                                File.WriteAllText(Paths.Inside(folder, "cancel.request"), "cancel");
                                if (cancel.IsCancellationRequested) cancel.ThrowIfCancellationRequested();
                                throw new TimeoutException("Recovery is taking longer than expected. Cancellation was requested; no new process stops will be attempted.");
                            }
                            await Task.Delay(100, CancellationToken.None);
                        }
                    }
                    var report = Read<RecoveryReport>(Paths.Inside(folder, "result.json"));
                    report.Directory = folder; report.ManagerData = manager; report.HelperData = helper;
                    return report;
                }
                catch (Win32Exception error) when (error.NativeErrorCode == 1223)
                { return new RecoveryReport { Cancelled = true, Message = "Windows permission was cancelled. No processes were stopped." }; }
            }
        }

        // Called before normal app startup/mutex. Only our same-user live launcher
        // can request this mode; copied requests expire and cannot redirect results.
        public static bool TryRun(string[] args)
        {
            if (args.Length == 0 || args[0] != "--process-recovery") return false;
            if (args.Length != 2) return true;
            string folder = null;
            try
            {
                string path = Canonical(args[1]); var request = Read<Request>(path);
                ValidateRequest(path, request); folder = Path.GetDirectoryName(path);
                try { Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.BelowNormal; } catch (Win32Exception) { }
                RecoveryReport report;
                if (Cancelled(folder, request)) report = new RecoveryReport { Cancelled = true, Message = "Recovery cancelled." };
                else report = Run(request, folder);
                Json.Save(Paths.Inside(folder, "result.json"), report);
            }
            catch (Exception error)
            {
                // Until request provenance is checked, write nowhere.
                if (folder != null)
                    try { Json.Save(Paths.Inside(folder, "result.json"), new RecoveryReport { Message = "Recovery failed: " + error.Message }); } catch { }
            }
            return true;
        }
        static void ValidateRequest(string path, Request request)
        {
            if (request == null || (request.Operation != "scan" && request.Operation != "stop") || !Nonce(request.Nonce)) throw new InvalidDataException("Invalid recovery request.");
            string expected = Paths.Inside(Canonical(request.ManagerData), "recovery/" + request.Nonce + "/request.json");
            if (!EqualPath(path, expected)) throw new InvalidDataException("Recovery request escaped its private folder.");
            Canonical(request.HelperData);
            if (DateTime.UtcNow - DateTime.Parse(request.CreatedUtc).ToUniversalTime() > TimeSpan.FromMinutes(2) || DateTime.Parse(request.CreatedUtc).ToUniversalTime() > DateTime.UtcNow.AddSeconds(5))
                throw new InvalidDataException("Recovery request expired.");
            if (WindowsIdentity.GetCurrent().User.Value != request.Sid || Process.GetCurrentProcess().SessionId != request.Session)
                throw new InvalidDataException("Use the same Windows account/session to grant recovery permission.");
            var owner = File.GetAccessControl(path).GetOwner(typeof(SecurityIdentifier)).Value;
            if (owner != request.Sid) throw new InvalidDataException("Recovery request owner changed.");
            using (var handle = Open(request.ParentPid, false))
            {
                var parent = Inspect(handle, request.ParentPid, 0);
                if (parent.UserSid != request.Sid || parent.SessionId != request.Session || !SameTime(parent.StartedUtc, request.ParentStartedUtc) ||
                    !EqualPath(parent.Executable, Process.GetCurrentProcess().MainModule.FileName)) throw new InvalidDataException("The requesting launcher changed or closed.");
            }
        }
        static RecoveryReport Run(Request request, string folder)
        {
            var roots = Roots(request.ManagerData, request.HelperData);
            var entries = Inventory();
            var report = Scan(request, roots, entries, folder);
            if (request.Operation == "scan" || report.Cancelled) return report;
            if (!Nonce(request.ScanNonce) || request.Selected == null || request.Selected.Count == 0 || request.Selected.Count > 32 || request.Selected.Distinct().Count() != request.Selected.Count)
                throw new InvalidDataException("Invalid selected recovery identities.");
            string oldPath = Paths.Inside(request.ManagerData, "recovery/" + request.ScanNonce + "/result.json");
            if (!String.Equals(RepoClient.Hash(oldPath), request.ScanHash, StringComparison.Ordinal)) throw new InvalidDataException("The scan changed. Scan again before stopping anything.");
            var previous = Read<RecoveryReport>(oldPath);
            if (DateTime.UtcNow - DateTime.Parse(previous.ScannedUtc).ToUniversalTime() > TimeSpan.FromMinutes(10)) throw new InvalidDataException("Scan expired; scan again.");
            foreach (int pid in request.Selected)
            {
                if (Cancelled(folder, request)) { report.Cancelled = true; break; }
                var before = previous.Candidates.SingleOrDefault(c => c.Pid == pid);
                var row = report.Candidates.SingleOrDefault(c => c.Pid == pid);
                if (before == null || !before.Eligible) throw new InvalidDataException("Process was not eligible in the confirmed scan.");
                if (row == null) { report.Candidates.Add(MissingCandidateResult(before, entries)); continue; }
                try
                {
                    if (!row.Eligible || !SameIdentity(before, row)) throw new InvalidOperationException("Process identity or activity changed. Scan again.");
                    using (var handle = Open(pid, true))
                    {
                        var verified = Inspect(handle, pid, row.ParentPid);
                        if (!SameIdentity(row, verified)) throw new InvalidOperationException("Process identity changed before recovery.");
                        // A marker asks supported workers to cancel first. Old workers
                        // need no new receipt to be recognized, and remain explicitly stoppable.
                        if (row.Kind == "Launcher helper" && TryStopResponsiveHelper(row, request.HelperData, () => Cancelled(folder, request)))
                        {
                            if (WaitForSingleObject(handle, 5000) != 0) throw new IOException("Helper accepted shutdown but has not exited. No force stop was attempted.");
                            row.Outcome = "Stopped"; row.Eligible = false; row.Reason = "Launcher helper closed through its own activity-checked stop endpoint."; continue;
                        }
                        TryCancelOwnedLaunch(request.HelperData, row, () => Cancelled(folder, request));
                        if (WaitForSingleObject(handle, 1200) == 0) { row.Outcome = "Exited"; row.Eligible = false; continue; }
                        if (Cancelled(folder, request)) { report.Cancelled = true; break; }
                        var latest = Inventory();
                        ApplyActivity(row, latest, request.HelperData);
                        if (!row.Eligible) throw new InvalidOperationException(row.Reason);
                        AssertForceStopSafe(row);
                        var final = Inspect(handle, pid, row.ParentPid);
                        if (!Classify(final, Roots(request.ManagerData, request.HelperData)) || final.Kind != row.Kind || !SameIdentity(row, final))
                            throw new InvalidOperationException("Process or package identity changed immediately before recovery.");
                        string newActivity = ActivityBlocker(row, Inventory());
                        if (newActivity != null) throw new InvalidOperationException(newActivity);
                        if (Cancelled(folder, request)) { report.Cancelled = true; break; }
                        TerminateVerified(handle, row);
                        row.Outcome = "Stopped"; row.Eligible = false; row.Reason = "Stopped only this selected launcher process. No game, injector, Steam or VR runtime was stopped.";
                    }
                }
                catch (OperationCanceledException) { row.Outcome = "Refused"; row.Eligible = false; row.Reason = "Recovery cancelled before this stop request."; report.Cancelled = true; break; }
                catch (Exception error) { row.Outcome = "Refused"; row.Eligible = false; row.Reason = error.Message; }
            }
            report.Message = report.Cancelled ? "Recovery cancelled; see individual results." : "Recovery finished. See individual results, then retry the launcher.";
            return report;
        }
        static RecoveryReport Scan(Request request, List<Root> roots, List<ProcessEntry> entries, string folder)
        {
            var report = new RecoveryReport { ScannedUtc = DateTime.UtcNow.ToString("o") };
            foreach (var entry in entries.Where(e => Interesting(e.Name)))
            {
                if (Cancelled(folder, request)) { report.Cancelled = true; break; }
                if (report.Candidates.Count >= 32) break;
                try
                {
                    using (var handle = Open(entry.Pid, false))
                    {
                        var row = Inspect(handle, entry.Pid, entry.Parent);
                        if (row.UserSid != request.Sid || row.SessionId != request.Session) continue;
                        if (!Classify(row, roots)) continue;
                        ApplyActivity(row, entries, request.HelperData); report.Candidates.Add(row);
                    }
                }
                catch (Win32Exception) { /* inaccessible unrelated processes are not eligible */ }
                catch (InvalidDataException) { }
            }
            report.Message = report.Cancelled ? "Scan cancelled or its time limit was reached. No processes were stopped." :
                report.Candidates.Count == 0 ? "No verified WuWa launcher processes found. Games, injectors, Steam and VR runtimes are excluded." :
                report.Candidates.Count >= 32 ? "The first 32 verified launcher identities are shown. Nothing has been stopped; scan again after recovery for remaining processes." : "Review the verified launcher identities. Nothing has been stopped.";
            return report;
        }

        static List<Root> Roots(string manager, string helper)
        {
            var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var statePath = Paths.Inside(manager, "manager.json");
            if (File.Exists(statePath))
            {
                var state = Read<ManagerState>(statePath);
                if (state.installed == null || state.installed.Count > 100) throw new InvalidDataException("Invalid installed package inventory.");
                foreach (var item in state.installed) { Paths.Id(item.folder); item.release.Validate(); paths.Add(Paths.Inside(manager, "versions/" + item.folder)); }
            }
            var receiptPath = Paths.Inside(helper, "launcher.json");
            if (File.Exists(receiptPath))
            {
                var receipt = Read<Dictionary<string, object>>(receiptPath);
                var app = Json.Text(receipt, "app");
                if (!String.IsNullOrWhiteSpace(app) && Path.GetFileName(Canonical(app)).Equals("app", StringComparison.OrdinalIgnoreCase)) paths.Add(Path.GetDirectoryName(Canonical(app)));
            }
            var roots = new List<Root>();
            foreach (var path in paths)
            {
                try
                {
                    var manifest = Read<PackageManifest>(Paths.Inside(path, "manifest.json"));
                    var portable = Read<Dictionary<string, object>>(Paths.Inside(path, "app/portable.json"));
                    if (manifest.files == null || manifest.files.Count > 20000 || !Regex.IsMatch(manifest.packageId ?? "", "^wuwa-vr-launcher-[a-z0-9-]+$") ||
                        Json.Text(portable, "packageId") != manifest.packageId) continue;
                    roots.Add(new Root { Path = path, Files = manifest.files });
                }
                catch (Exception error) when (error is IOException || error is InvalidDataException || error is UnauthorizedAccessException || error is ArgumentException || error is InvalidOperationException) { }
            }
            return roots;
        }
        internal static bool Classify(RecoveryCandidate row, List<Root> roots)
        {
            var args = SplitArguments(row.CommandLine);
            if (args.Length < 2) return false;
            bool powershell = IsSystemPowerShell(row.Executable);
            foreach (var root in roots)
            {
                string script = null;
                if (powershell)
                {
                    int file = -1;
                    for (int i = 1; i < args.Length; i++)
                    {
                        if (args[i].Equals("-File", StringComparison.OrdinalIgnoreCase)) { file = i; break; }
                        if (args[i].Equals("-NoProfile", StringComparison.OrdinalIgnoreCase) || args[i].Equals("-NonInteractive", StringComparison.OrdinalIgnoreCase) || args[i].Equals("-NoLogo", StringComparison.OrdinalIgnoreCase)) continue;
                        if (args[i].Equals("-ExecutionPolicy", StringComparison.OrdinalIgnoreCase) && i + 1 < args.Length && args[++i].Equals("Bypass", StringComparison.OrdinalIgnoreCase)) continue;
                        return false; // No -Command/-EncodedCommand/abbreviated script switches.
                    }
                    if (file < 0 || file + 1 >= args.Length || !Path.IsPathRooted(args[file + 1])) return false;
                    script = Canonical(args[file + 1]);
                    var name = Path.GetFileName(script);
                    bool startup = LaunchScripts.Contains(name, StringComparer.OrdinalIgnoreCase);
                    bool mutator = name.Equals("wuwa-runtime.ps1", StringComparison.OrdinalIgnoreCase) || name.Equals("wuwa-build.ps1", StringComparison.OrdinalIgnoreCase);
                    if (!startup && !mutator) return false;
                    if (!EqualPath(script, Paths.Inside(root.Path, "app/dev/" + name)) || !VerifiedFile(root, "app/dev/" + name)) continue;
                    row.Kind = mutator ? "Runtime/profile worker" : "Startup worker";
                    if (args.Skip(file + 2).Any(a => {
                        string option = a.Split(':')[0];
                        return option.Length > 1 && option.StartsWith("-", StringComparison.Ordinal) &&
                            new[] { "-Activate", "-Deactivate", "-Stop", "-Force" }.Any(full => full.StartsWith(option, StringComparison.OrdinalIgnoreCase));
                    })) row.Kind = "Runtime/profile worker";
                }
                else
                {
                    if (!EqualPath(row.Executable, Paths.Inside(root.Path, "python/pythonw.exe")) && !EqualPath(row.Executable, Paths.Inside(root.Path, "python/python.exe"))) continue;
                    int index = Array.FindIndex(args, 1, a => EqualPathSafe(a, Paths.Inside(root.Path, "app/dev/wuwa_player.py")));
                    if (index < 1 || !VerifiedFile(root, "app/dev/wuwa_player.py") || !VerifiedFile(root, "python/" + Path.GetFileName(row.Executable))) continue;
                    for (int i = 1; i < index; i++)
                    {
                        if (args[i] == "-I" || args[i] == "-B" || args[i] == "-u") continue;
                        if (args[i] == "-X" && i + 1 < index && args[++i] == "utf8") continue;
                        return false;
                    }
                    script = Canonical(args[index]); row.Kind = "Launcher helper";
                }
                row.Script = script; row.Outcome = ""; return true;
            }
            return false;
        }
        static bool VerifiedFile(Root root, string relative)
        {
            string hash; if (!root.Files.TryGetValue(relative, out hash) || !Regex.IsMatch(hash ?? "", "^[a-fA-F0-9]{64}$")) return false;
            string path = Paths.Inside(root.Path, relative);
            return File.Exists(path) && new FileInfo(path).Length < 64 * 1024 * 1024 && String.Equals(RepoClient.Hash(path), hash, StringComparison.OrdinalIgnoreCase);
        }
        internal static string ActivityBlocker(RecoveryCandidate row, List<ProcessEntry> entries)
        {
            if (row.Kind == "Runtime/profile worker") return "Runtime/profile changes cannot be interrupted by recovery.";
            if (entries.Any(e => e.Name.Equals("Client-Win64-Shipping.exe", StringComparison.OrdinalIgnoreCase) || e.Name.Equals("Wuthering Waves.exe", StringComparison.OrdinalIgnoreCase) || e.Name.Equals("Custom_UEVR_Injector.exe", StringComparison.OrdinalIgnoreCase) || e.Name.Equals("UEVRInjector.exe", StringComparison.OrdinalIgnoreCase)))
                return "Close the game and injector before recovering launcher processes. They will never be stopped here.";
            // Unknown children may be recording, transcription or a runtime/profile
            // transaction. No blanket tree kill, and do not orphan such work.
            if (entries.Any(e => e.Parent == row.Pid)) return "This process has active child work. Finish that work before recovery.";
            return null;
        }
        static void ApplyActivity(RecoveryCandidate row, List<ProcessEntry> entries, string helper)
        {
            row.Eligible = false;
            string blocker = ActivityBlocker(row, entries);
            if (blocker != null) { row.Reason = blocker; return; }
            if (row.Kind == "Launcher helper")
            {
                string activeReason; bool? idle = HelperIdle(row, helper, out activeReason);
                row.HelperUnresponsive = idle == null;
                if (idle == false) { row.Reason = activeReason; return; }
                row.Reason = idle == true ? "Verified idle launcher helper." : "Helper is not responding; stopping may interrupt pending launcher work. No child process was found.";
            }
            else row.Reason = "Verified startup script. Cancellation will be requested first when a matching launch receipt is available.";
            row.Eligible = true;
        }
        static bool? HelperIdle(RecoveryCandidate row, string helper, out string reason)
        {
            reason = "Helper activity could not be verified.";
            try
            {
                string address = HelperAddress(row, helper);
                if (address == null) return null;
                using (var http = new HttpClient(new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false }) { Timeout = TimeSpan.FromSeconds(2), MaxResponseContentBufferSize = Limit })
                {
                    var identity = Json.Read<Dictionary<string, object>>(http.GetStringAsync(address + "/api/identity").GetAwaiter().GetResult());
                    if (Json.Text(identity, "app") != "wuwa-vr-player-launcher" || Json.Text(identity, "pid") != row.Pid.ToString() || !EqualPath(Json.Text(identity, "root"), Path.GetDirectoryName(Path.GetDirectoryName(row.Script))))
                    { reason = "Helper endpoint identity changed. Scan again."; return false; }
                    var status = Json.Read<Dictionary<string, object>>(http.GetStringAsync(address + "/api/status").GetAwaiter().GetResult());
                    if (!KnownFalse(Json.Child(status, "job"), "running") || !KnownFalse(Json.Child(status, "recording"), "running") || Json.Flag(Json.Child(status, "launch"), "running"))
                    { reason = "Launcher reports active or unknown startup/recording work. Use its normal stop controls first."; return false; }
                    return true;
                }
            }
            catch (Exception error) when (error is IOException || error is InvalidDataException || error is HttpRequestException || error is TaskCanceledException || error is InvalidOperationException || error is ArgumentException) { return null; }
        }
        static bool KnownFalse(Dictionary<string, object> value, string key)
        { object flag; return value != null && value.TryGetValue(key, out flag) && flag is bool && !(bool)flag; }
        static string HelperAddress(RecoveryCandidate row, string helper)
        {
            var receipt = Read<Dictionary<string, object>>(Paths.Inside(helper, "launcher.json"));
            int pid; Uri uri;
            if (!int.TryParse(Json.Text(receipt, "pid"), out pid) || pid != row.Pid || !EqualPath(Json.Text(receipt, "app"), Path.GetDirectoryName(Path.GetDirectoryName(row.Script))) ||
                !Uri.TryCreate(Json.Text(receipt, "url"), UriKind.Absolute, out uri) || uri.Scheme != "http" || uri.Host != "127.0.0.1" || uri.AbsolutePath != "/" || uri.UserInfo != "" || uri.Query != "" || uri.Fragment != "") return null;
            return uri.GetLeftPart(UriPartial.Authority);
        }
        // Responsive helpers enforce their own recording/voice-note/job lock at
        // the stop endpoint. Do not force-kill one that explicitly refuses.
        static bool TryStopResponsiveHelper(RecoveryCandidate row, string helper, Func<bool> cancelled)
        {
            string unused;
            bool? idle = HelperIdle(row, helper, out unused);
            if (idle == false) throw new InvalidOperationException(unused);
            if (idle == null) return false;
            string address = HelperAddress(row, helper);
            if (address == null) throw new InvalidOperationException("The helper receipt changed. Scan again.");
            using (var http = new HttpClient(new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false }) { Timeout = TimeSpan.FromSeconds(2), MaxResponseContentBufferSize = Limit })
            {
                var identity = Json.Read<Dictionary<string, object>>(http.GetStringAsync(address + "/api/identity").GetAwaiter().GetResult());
                if (Json.Text(identity, "app") != "wuwa-vr-player-launcher" || Json.Text(identity, "pid") != row.Pid.ToString() || !EqualPath(Json.Text(identity, "root"), Path.GetDirectoryName(Path.GetDirectoryName(row.Script))))
                    throw new InvalidOperationException("Helper identity changed before shutdown.");
                string html = http.GetStringAsync(address + "/").GetAwaiter().GetResult();
                var token = Regex.Match(html, "<meta name=\"wuwa-token\" content=\"([A-Za-z0-9_-]+)\">");
                if (!token.Success) throw new InvalidDataException("Helper stop token is unavailable. Nothing was stopped.");
                using (var message = new HttpRequestMessage(HttpMethod.Post, address + "/api/stop"))
                {
                    message.Headers.Add("Origin", address); message.Headers.Add("X-WuWa-Token", token.Groups[1].Value);
                    message.Content = new StringContent("{}", Encoding.UTF8, "application/json");
                    ThrowIfCancelled(cancelled);
                    using (var response = http.SendAsync(message).GetAwaiter().GetResult())
                    {
                        response.EnsureSuccessStatusCode();
                        var result = Json.Read<Dictionary<string, object>>(response.Content.ReadAsStringAsync().GetAwaiter().GetResult());
                        if (!Json.Flag(result, "ok")) throw new InvalidOperationException("The launcher refused shutdown because its activity changed.");
                    }
                }
            }
            return true;
        }
        static void TryCancelOwnedLaunch(string helper, RecoveryCandidate row, Func<bool> cancelled)
        {
            if (row.Kind != "Startup worker") return;
            try
            {
                var state = Read<Dictionary<string, object>>(Paths.Inside(helper, "launch-state.json"));
                int pid; string time = Json.Text(state, "ownerStartedUtc");
                if (!int.TryParse(Json.Text(state, "ownerPid"), out pid) || pid != row.Pid || !SameTime(time, row.StartedUtc)) return;
                var path = Canonical(Json.Text(state, "cancelPath")); string runs = Paths.Inside(helper, "runs") + Path.DirectorySeparatorChar;
                if (!path.StartsWith(runs, StringComparison.OrdinalIgnoreCase) || Path.GetFileName(path) != "cancel.request" || !Directory.Exists(Path.GetDirectoryName(path))) return;
                ThrowIfCancelled(cancelled);
                File.WriteAllText(path, "Explicit launcher recovery cancellation");
            }
            catch (Exception error) when (error is IOException || error is InvalidDataException || error is ArgumentException || error is InvalidOperationException || error is UnauthorizedAccessException) { }
        }
        internal static void ThrowIfCancelled(Func<bool> cancelled)
        { if (cancelled()) throw new OperationCanceledException("Recovery was cancelled before dispatch."); }
        internal static void AssertForceStopSafe(RecoveryCandidate row)
        {
            if (row.Kind == "Launcher helper" && !row.HelperUnresponsive)
                throw new InvalidOperationException("The helper is responding again. Retry its normal stop so recording and voice-note checks can run.");
        }

        internal static bool SameIdentity(RecoveryCandidate a, RecoveryCandidate b)
        { return a != null && b != null && a.Pid == b.Pid && SameTime(a.StartedUtc, b.StartedUtc) && EqualPathSafe(a.Executable, b.Executable) && a.CommandLine == b.CommandLine && a.UserSid == b.UserSid && a.SessionId == b.SessionId; }
        internal static RecoveryCandidate MissingCandidateResult(RecoveryCandidate before, List<ProcessEntry> inventory)
        {
            bool present = inventory.Any(e => e.Pid == before.Pid);
            before.Eligible = false; before.Outcome = present ? "Refused" : "Exited";
            before.Reason = present ? "This PID is still present but its launcher identity can no longer be verified. Nothing was stopped; scan again." : "The selected process was absent from the complete process inventory; it already exited.";
            return before;
        }
        static bool SameTime(string a, string b)
        { DateTimeOffset left, right; return DateTimeOffset.TryParse(a, out left) && DateTimeOffset.TryParse(b, out right) && left.UtcDateTime.Ticks == right.UtcDateTime.Ticks; }
        static bool Interesting(string name) { return new[] { "powershell.exe", "python.exe", "pythonw.exe" }.Contains(name, StringComparer.OrdinalIgnoreCase); }
        static bool IsSystemPowerShell(string path)
        { return EqualPath(path, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32/WindowsPowerShell/v1.0/powershell.exe")) || EqualPath(path, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "SysWOW64/WindowsPowerShell/v1.0/powershell.exe")); }
        static bool Nonce(string value) { return Regex.IsMatch(value ?? "", "^[a-f0-9]{32}$"); }
        static bool Cancelled(string folder, Request request)
        { return File.Exists(Paths.Inside(folder, "cancel.request")) || DateTime.UtcNow - DateTime.Parse(request.CreatedUtc).ToUniversalTime() > TimeSpan.FromSeconds(75); }
        static T Read<T>(string path)
        { Canonical(path); if (!File.Exists(path) || new FileInfo(path).Length > Limit) throw new InvalidDataException("Recovery metadata is missing or too large."); return Json.Read<T>(File.ReadAllText(path)); }
        static string Canonical(string path)
        {
            if (String.IsNullOrWhiteSpace(path) || !Path.IsPathRooted(path) || path.StartsWith(@"\\") || path.IndexOfAny(new[] { '"', '\r', '\n' }) >= 0) throw new InvalidDataException("Invalid recovery path.");
            string full = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
            for (string parent = full; parent != null; parent = Path.GetDirectoryName(parent)) Paths.NoLinks(parent);
            return full;
        }
        static bool EqualPath(string a, string b) { return String.Equals(Canonical(a), Canonical(b), StringComparison.OrdinalIgnoreCase); }
        static bool EqualPathSafe(string a, string b) { try { return EqualPath(a, b); } catch (Exception e) when (e is IOException || e is InvalidDataException || e is ArgumentException || e is NotSupportedException) { return false; } }
        internal static string[] SplitArguments(string text)
        {
            if (String.IsNullOrWhiteSpace(text) || text.Length > 32768) throw new InvalidDataException("Process command line unavailable.");
            int count; IntPtr pointer = CommandLineToArgvW(text, out count);
            if (pointer == IntPtr.Zero || count < 1 || count > 128) throw new InvalidDataException("Invalid process arguments.");
            try { var result = new string[count]; for (int i = 0; i < count; i++) result[i] = Marshal.PtrToStringUni(Marshal.ReadIntPtr(pointer, i * IntPtr.Size)); return result; }
            finally { LocalFree(pointer); }
        }
        static List<ProcessEntry> Inventory()
        {
            var result = new List<ProcessEntry>();
            using (var search = new ManagementObjectSearcher(new ManagementScope(@"\\.\root\cimv2"), new ObjectQuery("SELECT ProcessId, ParentProcessId, Name FROM Win32_Process"), new EnumerationOptions { Timeout = TimeSpan.FromSeconds(6), ReturnImmediately = false }))
            using (var items = search.Get())
                foreach (ManagementObject item in items)
                    using (item) { if (result.Count >= 4096) throw new InvalidDataException("Process inventory exceeded its safety limit."); result.Add(new ProcessEntry { Pid = Convert.ToInt32(item["ProcessId"]), Parent = Convert.ToInt32(item["ParentProcessId"]), Name = Convert.ToString(item["Name"]) }); }
            return result;
        }
        internal static SafeProcessHandle Open(int pid, bool stop)
        { var handle = OpenProcess(Query | Synchronize | (stop ? Terminate : 0), false, pid); if (handle.IsInvalid) { handle.Dispose(); throw new Win32Exception(Marshal.GetLastWin32Error()); } return handle; }
        static RecoveryCandidate Inspect(SafeProcessHandle handle, int pid, int parent)
        {
            long created, exited, kernel, user; if (!GetProcessTimes(handle, out created, out exited, out kernel, out user)) throw new Win32Exception(Marshal.GetLastWin32Error());
            var image = new StringBuilder(32768); int length = image.Capacity;
            if (!QueryFullProcessImageName(handle, 0, image, ref length)) throw new Win32Exception(Marshal.GetLastWin32Error());
            IntPtr token; if (!OpenProcessToken(handle, 8, out token)) throw new Win32Exception(Marshal.GetLastWin32Error());
            string sid; int session;
            try
            {
                int needed; GetTokenInformation(token, 1, IntPtr.Zero, 0, out needed);
                if (needed < IntPtr.Size || needed > 65536) throw new InvalidDataException("Process token unavailable.");
                IntPtr buffer = Marshal.AllocHGlobal(needed);
                try { if (!GetTokenInformation(token, 1, buffer, needed, out needed)) throw new Win32Exception(Marshal.GetLastWin32Error()); sid = new SecurityIdentifier(Marshal.ReadIntPtr(buffer)).Value; }
                finally { Marshal.FreeHGlobal(buffer); }
                buffer = Marshal.AllocHGlobal(4);
                try { if (!GetTokenInformation(token, 12, buffer, 4, out needed)) throw new Win32Exception(Marshal.GetLastWin32Error()); session = Marshal.ReadInt32(buffer); }
                finally { Marshal.FreeHGlobal(buffer); }
            }
            finally { CloseHandle(token); }
            return new RecoveryCandidate { Pid = pid, ParentPid = parent, StartedUtc = DateTime.FromFileTimeUtc(created).ToString("o"), Executable = Canonical(image.ToString()), CommandLine = ReadCommandLine(handle), UserSid = sid, SessionId = session };
        }
        internal static RecoveryCandidate InspectProcess(int pid)
        { using (var handle = Open(pid, false)) return Inspect(handle, pid, 0); }
        internal static void TerminateVerified(SafeProcessHandle handle, RecoveryCandidate expected)
        {
            if (!SameIdentity(expected, Inspect(handle, expected.Pid, expected.ParentPid))) throw new InvalidOperationException("Process identity changed immediately before recovery.");
            if (!TerminateProcess(handle, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (WaitForSingleObject(handle, 3000) != 0) throw new IOException("Windows accepted stop but process exit was not confirmed.");
        }
        static string ReadCommandLine(SafeProcessHandle handle)
        {
            int needed; NtQueryInformationProcess(handle, 60, IntPtr.Zero, 0, out needed);
            if (needed < Marshal.SizeOf(typeof(UnicodeString)) || needed > 131072) throw new InvalidDataException("Process command line cannot be safely queried.");
            IntPtr memory = Marshal.AllocHGlobal(needed);
            try
            {
                int actual;
                if (NtQueryInformationProcess(handle, 60, memory, needed, out actual) < 0) throw new InvalidDataException("Process command line query failed.");
                var value = (UnicodeString)Marshal.PtrToStructure(memory, typeof(UnicodeString));
                long offset = value.Buffer.ToInt64() - memory.ToInt64();
                if (value.Length % 2 != 0 || offset < Marshal.SizeOf(typeof(UnicodeString)) || offset > needed || value.Length > needed - offset) throw new InvalidDataException("Invalid process command line buffer.");
                return Marshal.PtrToStringUni(value.Buffer, value.Length / 2);
            }
            finally { Marshal.FreeHGlobal(memory); }
        }
        [StructLayout(LayoutKind.Sequential)] struct UnicodeString { public ushort Length, MaximumLength; public IntPtr Buffer; }
        [DllImport("kernel32.dll", SetLastError = true)] static extern SafeProcessHandle OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetProcessTimes(SafeProcessHandle h, out long created, out long exited, out long kernel, out long user);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern bool QueryFullProcessImageName(SafeProcessHandle h, int flags, StringBuilder path, ref int size);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateProcess(SafeProcessHandle h, uint code);
        [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(SafeProcessHandle h, uint milliseconds);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
        [DllImport("advapi32.dll", SetLastError = true)] static extern bool OpenProcessToken(SafeProcessHandle h, uint access, out IntPtr token);
        [DllImport("advapi32.dll", SetLastError = true)] static extern bool GetTokenInformation(IntPtr token, int type, IntPtr buffer, int length, out int returned);
        [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(SafeProcessHandle h, int infoClass, IntPtr buffer, int length, out int returned);
        [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr CommandLineToArgvW(string text, out int count);
    }
}
