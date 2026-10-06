using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using WuWaVR.Manager;

// Actual WPF control events, entirely offscreen. No Show, HWND creation, UI
// Automation, synthesized desktop input, clipboard, network or helper processes.
public static class WindowTests
{
    const BindingFlags PrivateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
    static void Check(bool value, string message) { if (!value) throw new Exception("Window regression: " + message); }
    static T Field<T>(MainWindow window, string name)
    { return (T)typeof(MainWindow).GetField(name, PrivateInstance).GetValue(window); }
    static void Set(MainWindow window, string name, object value)
    { typeof(MainWindow).GetField(name, PrivateInstance).SetValue(window, value); }
    static object Invoke(MainWindow window, string method, params object[] arguments)
    { return typeof(MainWindow).GetMethod(method, PrivateInstance).Invoke(window, arguments); }
    static Button Button(MainWindow window, string key)
    { return Field<List<Button>>(window, "actions").Single(b => Convert.ToString(b.Tag) == key); }
    static void Offscreen(MainWindow window)
    { Check(!window.IsVisible && PresentationSource.FromVisual(window) == null, "test window acquired a visible presentation source"); }
    static IEnumerable<DependencyObject> Tree(DependencyObject root)
    {
        yield return root;
        foreach (var child in LogicalTreeHelper.GetChildren(root).OfType<DependencyObject>())
            foreach (var item in Tree(child)) yield return item;
    }
    static void PumpUntil(Func<bool> complete, string operation)
    {
        if (complete()) return;
        var frame = new DispatcherFrame(); var elapsed = Stopwatch.StartNew();
        var timer = new DispatcherTimer(DispatcherPriority.Background) { Interval = TimeSpan.FromMilliseconds(10) };
        timer.Tick += (s, e) => { if (complete() || elapsed.Elapsed > TimeSpan.FromSeconds(10)) frame.Continue = false; };
        timer.Start();
        try { Dispatcher.PushFrame(frame); }
        finally { timer.Stop(); }
        Check(complete(), operation + " did not finish within ten seconds");
    }
    static void Drain()
    {
        bool done = false;
        Dispatcher.CurrentDispatcher.BeginInvoke(DispatcherPriority.ContextIdle, new Action(() => done = true));
        PumpUntil(() => done, "offscreen layout callbacks");
    }
    static void Click(MainWindow window, Button button)
    {
        Check(button.IsEnabled, "button unexpectedly disabled: " + button.Content);
        button.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
        PumpUntil(() => Field<CancellationTokenSource>(window, "operation") == null && !Field<bool>(window, "polling"), "button " + button.Content);
        Drain(); Offscreen(window);
    }
    static void Connect(MainWindow window)
    {
        var task = (Task)Invoke(window, "Connect", CancellationToken.None);
        PumpUntil(() => task.IsCompleted, "fixture helper connection"); task.GetAwaiter().GetResult();
        Offscreen(window);
    }
    static void Await(MainWindow window, string method, params object[] arguments)
    {
        var task = (Task)Invoke(window, method, arguments);
        PumpUntil(() => task.IsCompleted, method); task.GetAwaiter().GetResult(); Offscreen(window);
    }
    static void Load(MainWindow window)
    {
        window.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent));
        PumpUntil(() => Field<CancellationTokenSource>(window, "operation") == null && !Field<bool>(window, "polling"), "startup");
        Field<DispatcherTimer>(window, "poll").Stop(); Offscreen(window);
    }
    static void Refresh(MainWindow window)
    {
        var task = (Task)Invoke(window, "RefreshStatus");
        PumpUntil(() => task.IsCompleted, "fixture status refresh"); task.GetAwaiter().GetResult();
    }
    static RecoveryReport RecoveryFixture()
    {
        return new RecoveryReport { ScannedUtc = "2026-10-06T20:00:00Z", Message = "Fixture identity scan", Candidates = new List<RecoveryCandidate> {
            new RecoveryCandidate { Pid = 101, StartedUtc = "2026-10-06T19:00:00Z", Executable = @"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe", Script = @"C:\fixture\app\dev\start-wuwa-build.ps1", CommandLine = "private-command-line-token", Kind = "Startup worker", Eligible = true, Reason = "Fixture exact identity" },
            new RecoveryCandidate { Pid = 202, StartedUtc = "2026-10-06T19:01:00Z", Executable = @"C:\fixture\python\pythonw.exe", Script = @"C:\fixture\app\dev\wuwa_player.py", Kind = "Launcher helper", Eligible = true, Reason = "Fixture idle helper" },
            new RecoveryCandidate { Pid = 303, StartedUtc = "2026-10-06T19:02:00Z", Executable = @"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe", Script = @"C:\fixture\app\dev\wuwa-runtime.ps1", Kind = "Runtime/profile worker", Eligible = false, Reason = "Fixture runtime mutation is protected" }
        } };
    }
    static ControllerDiagnostics.Report ControllerSnapshot()
    {
        return new ControllerDiagnostics.Report {
            CheckedAtUtc = new DateTime(2026, 10, 4, 2, 0, 0, DateTimeKind.Utc), XInputStatus = "available", HelpersStatus = "available",
            Slots = Enumerable.Range(0, 4).Select(i => new ControllerDiagnostics.Slot { Index = i, Status = i == 0 ? "connected" : "disconnected" }).ToList(),
            HidHide = new ControllerDiagnostics.HidHideReport { Status = "available", Installed = true, Cloaking = true, Inverse = false,
                HiddenDeviceCount = 2, ApplicationRuleCount = 1, XboxCompatibleHiddenCount = 2, ExplicitVirtualHiddenCount = 1,
                GameRulePresent = false, ApplicationRules = new[] { "Reality Runner", @"C:\Users\private-person\secret-serial.exe" } },
            Helpers = new[] { "Reality Runner", @"C:\Users\private-person\secret-serial.exe" },
            Findings = new[] { "hidhide_active", "shared_xinput_visibility", "secret-serial" }
        };
    }

    sealed class NoDownload : HttpMessageHandler
    {
        public int Requests;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        { Requests++; throw new InvalidOperationException("An offscreen test attempted a download: " + request.RequestUri); }
    }
    sealed class HelperHttp : HttpMessageHandler
    {
        readonly PackageStore store;
        public int Stops, RecordStarts, RecordStops, Cancels, RuntimeChanges, ReadinessChecks, Resets, Launches, RiskAcknowledgements;
        public string RuntimeBody, LaunchBody, RecordingBody, IdentityRoot, OfflineFolder, RejectedTokenFolder;
        public string DiagnosticsReply = "Fixture backend diagnostics", DiagnosticsFailure;
        public int IdentityPid = int.MaxValue;
        public bool RejectRecordStart, AllowRecordStart, AllowLaunch, RejectLaunch, AllowGameSettings, RejectGameSettings;
        public int GameSettingsWrites;
        public string SettingsFile;
        public readonly List<string> Writes = new List<string>();
        public Dictionary<string, object> Status = new Dictionary<string, object> {
            { "game", new Dictionary<string, object> { { "saved", true }, { "mode", "manual" }, { "launcher", "" }, { "problem", "" } } },
            { "job", new Dictionary<string, object> { { "running", false } } }, { "recording", new Dictionary<string, object> { { "running", false }, { "available", true }, { "sources", new Dictionary<string, object> { { "simulator", true }, { "steamvr", true } } } } },
            { "gameRunning", false }, { "injectorRunning", false }
        };
        public HelperHttp(PackageStore store) { this.store = store; }
        static Task<HttpResponseMessage> Reply(object value, HttpStatusCode code = HttpStatusCode.OK)
        { return Task.FromResult(new HttpResponseMessage(code) { Content = new StringContent(Json.Write(value), Encoding.UTF8, "application/json") }); }
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested();
            if (store.Selected != null && store.Selected.folder == OfflineFolder) throw new HttpRequestException("Fixture helper is absent.");
            Check(request.RequestUri.GetLeftPart(UriPartial.Authority) == "http://127.0.0.1:34671", "unexpected helper address");
            if (request.Method == HttpMethod.Get)
            {
                switch (request.RequestUri.AbsolutePath)
                {
                    case "/api/identity":
                        Check(store.Selected != null, "identity requested with no selected package");
                        // A nonexistent positive PID keeps stop waits off real processes.
                        return Reply(new { app = "wuwa-vr-player-launcher", root = IdentityRoot ?? Path.Combine(store.Folder(store.Selected), "app"), pid = IdentityPid });
                    case "/": return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent(
                        store.Selected != null && store.Selected.folder == RejectedTokenFolder ? "Fixture unsupported helper interface" : "<meta name=\"wuwa-token\" content=\"window_fixture\">") });
                    case "/api/status": return Reply(Status);
                    case "/api/diagnostics":
                        if (DiagnosticsFailure != null) throw new HttpRequestException(DiagnosticsFailure);
                        return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent(DiagnosticsReply) });
                }
            }
            if (request.Method == HttpMethod.Post)
            {
                Check(request.Headers.GetValues("Origin").Single() == "http://127.0.0.1:34671", "missing fixture origin");
                Check(request.Headers.GetValues("X-WuWa-Token").Single() == "window_fixture", "missing fixture token");
                Writes.Add(request.RequestUri.AbsolutePath);
                switch (request.RequestUri.AbsolutePath)
                {
                    case "/api/language":
                        var language = Json.Read<Dictionary<string, object>>(request.Content.ReadAsStringAsync().GetAwaiter().GetResult());
                        Check(language.Count == 1 && new[] { "en", "zh-Hans" }.Contains(Json.Text(language, "language")), "unexpected language write");
                        return Reply(new { ok = true });
                    case "/api/stop": Stops++; return Reply(new { ok = true });
                    case "/api/settings":
                        var settings = Json.Read<Dictionary<string, object>>(request.Content.ReadAsStringAsync().GetAwaiter().GetResult());
                        if (AllowGameSettings && settings.Count == 2 && (Json.Text(settings, "gameStart") == "launcher" || Json.Text(settings, "gameStart") == "steam"))
                        {
                            GameSettingsWrites++;
                            if (RejectGameSettings) return Reply(new { error = "Fixture game choice rejected" }, HttpStatusCode.BadRequest);
                            string launcher = Json.Text(settings, "gameLauncher"), mode = Json.Text(settings, "gameStart");
                            Status["game"] = new Dictionary<string, object> { { "saved", true }, { "mode", mode }, { "launcher", launcher }, { "problem", "" } };
                            if (SettingsFile != null) Json.Save(SettingsFile, new { gameStart = mode, gameLauncher = launcher });
                            return Reply(new { ok = true });
                        }
                        Check(AllowLaunch && settings.Count == 1 && Json.Flag(settings, "riskAcknowledged"), "unexpected settings write: only explicit launch risk acceptance is supported");
                        RiskAcknowledgements++; Status["riskAcknowledged"] = true; return Reply(new { ok = true });
                    case "/api/launch":
                        Check(AllowLaunch, "launch route was not enabled for this test");
                        LaunchBody = request.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                        var launch = Json.Read<Dictionary<string, object>>(LaunchBody);
                        Check(launch.Count == 1 && Json.Text(launch, "id") == store.Selected.release.buildId, "launch did not target only the current installed build");
                        Check(Json.Flag(Status, "riskAcknowledged") && Writes.Count >= 2 && Writes[Writes.Count - 2] == "/api/settings", "launch did not follow explicit risk acknowledgement");
                        Launches++;
                        if (RejectLaunch) return Reply(new { error = "Fixture Windows permission was declined" }, HttpStatusCode.BadRequest);
                        Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", true }, { "message", "Fixture launch waiting for the game" } };
                        Status["launch"] = new Dictionary<string, object> { { "phase", "waiting" }, { "firstFrameSeen", false } };
                        return Reply(new { ok = true });
                    case "/api/record-start":
                        Check(AllowRecordStart || RejectRecordStart, "recording route was not enabled for this test");
                        RecordingBody = request.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                        var capture = Json.Read<Dictionary<string, object>>(RecordingBody);
                        Check(capture.Count == 4 && capture.ContainsKey("videoOnly") && capture["videoOnly"] is bool &&
                            new[] { "30", "45", "60" }.Contains(Json.Text(capture, "fps")) &&
                            new[] { "720", "1024", "1280" }.Contains(Json.Text(capture, "eyeWidth")) &&
                            new[] { "auto", "steamvr", "simulator" }.Contains(Json.Text(capture, "source")), "invalid recording options sent");
                        RecordStarts++;
                        if (RejectRecordStart) return Reply(new { error = "Fixture recorder unavailable" }, HttpStatusCode.BadRequest);
                        Json.Child(Status, "recording")["running"] = true;
                        Status["job"] = new Dictionary<string, object> { { "kind", "recording" }, { "running", true }, { "message", "Fixture recording in progress" } };
                        return Reply(new { ok = true });
                    case "/api/record-stop":
                        Check(Json.Flag(Json.Child(Status, "recording"), "running"), "stop requested without an active recording");
                        // The real endpoint requests stop; the recording job finishes later.
                        RecordStops++; Json.Child(Status, "recording")["stopping"] = true;
                        return Reply(new { ok = true });
                    case "/api/cancel":
                        Check(LauncherBridge.CanCancelLaunch(Status), "cancellation sent outside a waiting launch");
                        Cancels++;
                        Status["launch"] = new Dictionary<string, object> { { "current", true }, { "running", false }, { "cancellable", false }, { "phase", "cancelled" }, { "message", "Fixture launch cancelled before the game started" }, { "firstFrameSeen", false } };
                        Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", false }, { "code", 0 }, { "output", "Fixture launch cancelled before the game started" } };
                        return Reply(new { ok = true });
                    case "/api/check":
                        ReadinessChecks++;
                        Status["job"] = new Dictionary<string, object> { { "kind", "check" }, { "running", false }, { "code", 0 }, { "output", "Ready to apply. This build is not applied yet." } };
                        return Reply(new { ok = true });
                    case "/api/runtime":
                        RuntimeChanges++; RuntimeBody = request.Content.ReadAsStringAsync().GetAwaiter().GetResult();
                        var body = Json.Read<Dictionary<string, object>>(RuntimeBody);
                        bool simulator = Json.Text(body, "mode") == "simulator";
                        Check(Json.Text(body, "expectedActive") == Json.Text(Json.Child(Status, "openxr"), "manifest"), "runtime request omitted current manifest race guard");
                        Status["openxr"] = new Dictionary<string, object> { { "name", simulator ? "OpenXR Simulator" : "SteamVR" }, { "available", true }, { "canHeadset", true }, { "canSimulator", true }, { "isSimulator", simulator }, { "isBundledSimulator", simulator }, { "manifest", simulator ? "fixture-simulator.json" : "fixture-headset.json" } };
                        Status["job"] = new Dictionary<string, object> { { "kind", "runtime" }, { "running", false }, { "code", 0 }, { "output", "Fixture runtime changed." } };
                        return Reply(new { ok = true });
                    case "/api/apply":
                        var reset = Json.Read<Dictionary<string, object>>(request.Content.ReadAsStringAsync().GetAwaiter().GetResult());
                        Check(Json.Flag(reset, "reset") && Json.Text(reset, "id") == store.Selected.release.buildId, "reset sent without explicit flag and active build");
                        Resets++; return Reply(new { ok = true });
                }
            }
            // Routes and request bodies are intentionally narrow. An unexpected
            // write fails inside this handler before any real I/O is possible.
            throw new InvalidOperationException("Unexpected fixture request: " + request.Method + " " + request.RequestUri);
        }
    }
    sealed class Fixture : IDisposable
    {
        public readonly PackageStore Store;
        public readonly MainWindow Window;
        public readonly HelperHttp Http;
        public readonly NoDownload Downloads = new NoDownload();
        public readonly Installed A, B;
        public Fixture(string root, bool install = true, bool oldSelectedHelper = false)
        {
            string folder = Path.Combine(root, "window-ui", Guid.NewGuid().ToString("N"));
            Store = new PackageStore(Path.Combine(folder, "manager"));
            if (install) { A = AddCandidate(folder, "a"); B = AddCandidate(folder, "b", !oldSelectedHelper); }
            string data = Path.Combine(folder, "backend");
            Json.Save(Path.Combine(data, "settings.json"), new { gameStart = "manual", gameLauncher = "" });
            Json.Save(Path.Combine(data, "launcher.json"), new { url = "http://127.0.0.1:34671/", pid = int.MaxValue });
            Window = new MainWindow(Store, data, false, "en");
            Set(Window, "guideDirectoryOverride", Store.Root);
            Field<LauncherBridge>(Window, "bridge").Dispose();
            Field<RepoClient>(Window, "repo").Dispose();
            Http = new HelperHttp(Store) { SettingsFile = Path.Combine(data, "settings.json") };
            Set(Window, "bridge", (LauncherBridge)Activator.CreateInstance(typeof(LauncherBridge), BindingFlags.NonPublic | BindingFlags.Instance, null,
                new object[] { data, null, Http, new Func<string[]>(() => new string[0]), new Func<int, bool?>(pid => false) }, null));
            Set(Window, "repo", new RepoClient(Downloads));
            Set(Window, "discovery", new GameDiscovery.Result { Message = "gameManualSaved" });
            Set(Window, "status", Http.Status);
            Invoke(Window, "ShowStatus"); Offscreen(Window);
        }
        Installed AddCandidate(string folder, string suffix, bool steamSupport = true)
        {
            string id = "candidate-window-" + suffix, build = "window-" + suffix;
            var files = new Dictionary<string, byte[]> {
                { "python/pythonw.exe", Encoding.UTF8.GetBytes("Inert fixture. Never execute.") },
                { "app/dev/wuwa_player.py", Encoding.UTF8.GetBytes("# inert window fixture " + suffix) },
                { "app/dev/wuwa-builds.json", Encoding.UTF8.GetBytes("{}") },
                { "app/portable.json", Encoding.UTF8.GetBytes(Json.Write(new { packageId = "wuwa-vr-launcher-" + id, defaultBuild = build })) }
            };
            if (steamSupport)
            {
                files.Add("app/dev/wuwa_game_start.py", Encoding.UTF8.GetBytes("# inert Steam identity fixture"));
                files.Add("app/dev/WuWaSteamStart.ps1", Encoding.UTF8.GetBytes("# inert Steam worker fixture"));
            }
            var hashes = new Dictionary<string, string>();
            foreach (var item in files)
                using (var sha = System.Security.Cryptography.SHA256.Create()) hashes[item.Key] = BitConverter.ToString(sha.ComputeHash(item.Value)).Replace("-", "").ToLowerInvariant();
            string archive = Path.Combine(folder, id + ".zip");
            using (var zip = ZipFile.Open(archive, ZipArchiveMode.Create))
            {
                foreach (var item in files) using (var output = zip.CreateEntry("WuWa VR/" + item.Key).Open()) output.Write(item.Value, 0, item.Value.Length);
                using (var output = new StreamWriter(zip.CreateEntry("WuWa VR/manifest.json").Open())) output.Write(Json.Write(new { packageId = "wuwa-vr-launcher-" + id, defaultBuild = build, files = hashes }));
            }
            var release = new Release { id = id, buildId = build, channel = "candidate", gameVersion = "3.7", created = "2026-10-03T00:00:00Z", size = new FileInfo(archive).Length, sha256 = RepoClient.Hash(archive) };
            Directory.CreateDirectory(Store.Cache); File.Copy(archive, Path.Combine(Store.Cache, release.sha256 + ".zip"));
            return Store.Install(archive, release, CancellationToken.None);
        }
        public void Dispose()
        {
            var operation = Field<CancellationTokenSource>(Window, "operation");
            Set(Window, "allowClose", true); // Fixture disposal is not a user exit action.
            if (operation == null) Window.Close();
            else
            {
                // Never open MainWindow's close-busy MessageBox on a failing test.
                operation.Cancel(); Field<DispatcherTimer>(Window, "poll").Stop();
                Field<LauncherBridge>(Window, "bridge").Dispose(); Field<RepoClient>(Window, "repo").Dispose();
            }
        }
    }

    public static void Run(string root)
    {
        Check(Thread.CurrentThread.GetApartmentState() == ApartmentState.STA, "Tests.Main must have [STAThread]");
        var previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext(Dispatcher.CurrentDispatcher));
        try
        {
            using (var f = new Fixture(root))
            {
                var w = f.Window; var report = RecoveryFixture(); int scans = 0, stops = 0; string confirmation = null;
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", true } }; Invoke(w, "ShowStatus");
                Set(w, "processRecoveryScanOverride", new Func<CancellationToken, Task<RecoveryReport>>(c => { scans++; return Task.FromResult(report); }));
                Set(w, "processRecoveryStopOverride", new Func<RecoveryReport, IEnumerable<int>, CancellationToken, Task<RecoveryReport>>((scan, pids, c) => {
                    stops++; Check(Object.ReferenceEquals(scan, report) && pids.OrderBy(x => x).SequenceEqual(new[] { 101, 202 }), "recovery stop changed confirmed identities or included protected rows");
                    var result = RecoveryFixture(); result.Candidates[0].Outcome = "Stopped"; result.Candidates[1].Outcome = "Failed";
                    result.Candidates[1].Reason = "Fixture access denied after revalidation"; return Task.FromResult(result);
                }));
                Check(!Field<Expander>(w, "processRecoveryPanel").IsExpanded && Button(w, "processRecoveryScan").IsEnabled,
                    "recovery cluttered setup or requires a connected/idle backend");
                Invoke(w, "ShowPage", true); Field<Expander>(w, "processRecoveryPanel").IsExpanded = true;
                Click(w, Button(w, "processRecoveryScan"));
                var choices = Tree(Field<StackPanel>(w, "processRecoveryRows")).OfType<CheckBox>().ToArray();
                Check(scans == 1 && f.Http.Writes.Count == 0 && Field<LauncherBridge>(w, "bridge").Address == null && choices.Length == 3 && choices.All(x => x.IsChecked != true),
                    "scan attached a helper, changed it, hid candidates or automatically selected processes");
                Check(!choices.Single(x => ((RecoveryCandidate)x.Tag).Pid == 303).IsEnabled && !Button(w, "processRecoveryStop").IsEnabled,
                    "protected runtime process was selectable or empty selection could stop");
                Check(Tree(Field<StackPanel>(w, "processRecoveryRows")).OfType<TextBlock>().Any(x => x.Text.Contains(@"C:\fixture\python\pythonw.exe")), "scan omitted executable identity");
                Check(!Tree(Field<StackPanel>(w, "processRecoveryRows")).OfType<TextBlock>().Any(x => x.Text.Contains("private-command-line-token")), "scan exposed raw command-line data");
                choices.Single(x => ((RecoveryCandidate)x.Tag).Pid == 101).IsChecked = true;
                Field<ComboBox>(w, "languages").SelectedIndex = 1; Drain();
                choices = Tree(Field<StackPanel>(w, "processRecoveryRows")).OfType<CheckBox>().ToArray();
                Check(Field<Expander>(w, "processRecoveryPanel").IsExpanded && choices.Count(x => x.IsChecked == true) == 1 && scans == 1,
                    "language change lost explicit process selection or silently rescanned");
                Set(w, "processRecoveryConfirmOverride", new Func<string, bool>(message => { confirmation = message; return false; }));
                Click(w, Button(w, "processRecoveryStop"));
                Check(stops == 0 && confirmation.Contains("PID 101") && confirmation.Contains("2026-10-06T19:00:00Z") && confirmation.Contains("Steam") && !confirmation.Contains("PID 303"),
                    "declined confirmation stopped a process or did not show precise selected identity and effect");
                choices.Single(x => ((RecoveryCandidate)x.Tag).Pid == 202).IsChecked = true;
                Set(w, "processRecoveryConfirmOverride", new Func<string, bool>(message => { confirmation = message; return true; }));
                Click(w, Button(w, "processRecoveryStop"));
                var zh = new Strings { Language = "zh-Hans" };
                Check(stops == 1 && confirmation.Contains("PID 101") && confirmation.Contains("PID 202") && !confirmation.Contains("PID 303") &&
                    Field<TextBlock>(w, "processRecoveryState").Text.Contains(zh["processRecoveryPartial"]), "partial recovery was reported as complete or changed the selection");
                Check(!Field<bool>(w, "connectionReady") && Field<LauncherBridge>(w, "bridge").Address == null && f.Http.Writes.Count == 0 && f.Http.Launches == 0 &&
                    !Button(w, "processRecoveryStop").IsEnabled && Button(w, "retryConnection").IsEnabled, "recovery auto-connected/launched or reused a consumed scan");
                Check(Tree(Field<StackPanel>(w, "processRecoveryRows")).OfType<TextBlock>().Any(x => x.Text.Contains("Fixture access denied")), "partial stop error omitted from its row");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW process recovery works disconnected/busy, starts unchecked, protects rows, confirms exact selection, retains partial results and requires explicit reconnect (inert service only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; int stops = 0;
                Set(w, "processRecoveryScanOverride", new Func<CancellationToken, Task<RecoveryReport>>(c => { throw new InvalidOperationException("Fixture Windows permission declined"); }));
                Set(w, "processRecoveryStopOverride", new Func<RecoveryReport, IEnumerable<int>, CancellationToken, Task<RecoveryReport>>((scan, ids, c) => { stops++; throw new Exception("must not stop"); }));
                Click(w, Button(w, "processRecoveryScan"));
                Check(stops == 0 && Field<TextBox>(w, "details").Text.Contains("permission declined") && !Button(w, "processRecoveryStop").IsEnabled &&
                    Button(w, "processRecoveryScan").IsEnabled && f.Http.Writes.Count == 0, "failed scan hid retry or enabled a stop");
                Set(w, "processRecoveryScanOverride", new Func<CancellationToken, Task<RecoveryReport>>(c => { var cancelled = RecoveryFixture(); cancelled.Cancelled = true; return Task.FromResult(cancelled); }));
                Click(w, Button(w, "processRecoveryScan"));
                Check(!Button(w, "processRecoveryStop").IsEnabled && !Field<bool>(w, "processRecoveryFresh") && stops == 0,
                    "cancelled scan granted stop eligibility");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW failed/cancelled elevation scan cannot stop processes and leaves visible retry (inert service only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w); int stopped = 0; int writesBefore = f.Http.Writes.Count;
                Set(w, "processRecoveryScanOverride", new Func<CancellationToken, Task<RecoveryReport>>(c => Task.FromResult(RecoveryFixture())));
                Set(w, "processRecoveryStopOverride", new Func<RecoveryReport, IEnumerable<int>, CancellationToken, Task<RecoveryReport>>((scan, ids, c) => {
                    stopped++; throw new IOException("Fixture recovery result could not be read after stop request");
                }));
                Set(w, "processRecoveryConfirmOverride", new Func<string, bool>(message => true));
                Click(w, Button(w, "processRecoveryScan"));
                Tree(Field<StackPanel>(w, "processRecoveryRows")).OfType<CheckBox>().Single(x => ((RecoveryCandidate)x.Tag).Pid == 101).IsChecked = true;
                Click(w, Button(w, "processRecoveryStop"));
                Check(stopped == 1 && Field<LauncherBridge>(w, "bridge").Address == null && !Field<bool>(w, "connectionReady") &&
                    Field<TextBox>(w, "details").Text.Contains("could not be read") && Button(w, "retryConnection").IsEnabled &&
                    !Button(w, "processRecoveryStop").IsEnabled && f.Http.Writes.Count == writesBefore,
                    "uncertain stop result retained stale attachment, retried a stop or lost error/reconnect");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW uncertain stop result invalidates attachment and consumed selection without auto-reconnect (inert service only)");
            }
            foreach (bool running in new[] { true, false }) using (var f = new Fixture(root))
            {
                var w = f.Window;
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", running }, { "error", !running },
                    { "message", "Fixture attached launch awaiting Play" }, { "code", running ? null : (object)4 }, { "output", "Fixture attached launch failed before restart" } };
                Set(w, "status", new Dictionary<string, object>());
                Load(w);
                string feedback = Field<TextBlock>(w, "operationText").Text;
                Check(feedback.Contains(running ? "awaiting Play" : "failed before restart"), "startup discovery concealed existing backend job");
                if (!running) Check(Field<Expander>(w, "feedbackPanel").IsExpanded && Field<TextBox>(w, "details").Text.Contains("failed before restart"), "startup failed job lost its details");
                Refresh(w); Check(Field<TextBlock>(w, "operationText").Text == feedback, "unchanged startup job changed feedback repeatedly");
                Console.WriteLine("PASS WINDOW restart displays attached " + (running ? "running" : "failed") + " launch instead of discovery feedback (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowGameSettings = true;
                string launcher = Path.Combine(f.Store.Root, "chosen-game", "launcher.exe"); Directory.CreateDirectory(Path.GetDirectoryName(launcher)); File.WriteAllText(launcher, "Inert choice fixture; never execute.");
                Await(w, "ChooseLauncher", launcher, CancellationToken.None);
                Check(f.Http.GameSettingsWrites == 0 && new PackageStore(f.Store.Root).State.pendingLauncherPath == launcher, "disconnected native choice was applied or lost instead of queued durably");
                Connect(w);
                Check(f.Http.GameSettingsWrites == 1 && f.Store.State.pendingLauncherPath == null && new PackageStore(f.Store.Root).State.pendingLauncherPath == null, "confirmed native choice did not consume its pending marker");
                f.Http.Status["game"] = new Dictionary<string, object> { { "saved", true }, { "mode", "manual" }, { "launcher", "" }, { "problem", "" } };
                Json.Save(f.Http.SettingsFile, new { gameStart = "manual", gameLauncher = "" });
                Connect(w);
                Check(f.Http.GameSettingsWrites == 1 && Json.Text(Json.Child(f.Http.Status, "game"), "mode") == "manual" && Field<TextBlock>(w, "gameHint").Text == new Strings()["gameManualSaved"], "later developer manual choice was overwritten by historical native path");
                Check(Field<TextBlock>(w, "gamePath").Text == new Strings()["manualStart"], "manual helper mode still displayed stale native launcher path");
                Await(w, "ChooseLauncher", launcher, CancellationToken.None);
                Check(f.Http.GameSettingsWrites == 2 && f.Store.State.pendingLauncherPath == null, "a new explicit native choice could not replace manual mode once");
                Console.WriteLine("PASS WINDOW durable native path intent applies once, preserves later helper preferences and accepts another deliberate choice (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowGameSettings = true;
                string launcher = Path.Combine(f.Store.Root, "restart-game", "launcher.exe"); Directory.CreateDirectory(Path.GetDirectoryName(launcher)); File.WriteAllText(launcher, "Inert restart fixture.");
                f.Store.State.launcherPath = launcher; f.Store.State.pendingLauncherPath = launcher; f.Store.Save();
                var restarted = new PackageStore(f.Store.Root); Set(w, "store", restarted);
                Set(w, "discovery", null); Set(w, "status", new Dictionary<string, object>());
                Load(w);
                Check(f.Http.GameSettingsWrites == 1 && restarted.State.pendingLauncherPath == null && Json.Text(Json.Child(f.Http.Status, "game"), "launcher") == launcher, "restart lost or repeated pending native game choice");
                Connect(w); Check(f.Http.GameSettingsWrites == 1, "reconnection repeated already confirmed native choice");
                Console.WriteLine("PASS WINDOW a pending native launcher choice survives state reload and is consumed once (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowGameSettings = true; f.Http.RejectGameSettings = true;
                string launcher = Path.Combine(f.Store.Root, "rejected-game", "launcher.exe"); Directory.CreateDirectory(Path.GetDirectoryName(launcher)); File.WriteAllText(launcher, "Inert retry fixture.");
                f.Store.State.launcherPath = launcher; f.Store.State.pendingLauncherPath = launcher; f.Store.Save();
                Click(w, Button(w, "retryConnection"));
                Check(f.Http.GameSettingsWrites == 1 && new PackageStore(f.Store.Root).State.pendingLauncherPath == launcher && Json.Text(Json.Child(f.Http.Status, "game"), "mode") == "manual", "rejected path choice was lost or silently applied");
                Check(Field<TextBox>(w, "details").Text.Contains("Fixture game choice rejected"), "rejected path choice has no visible recovery reason");
                f.Http.RejectGameSettings = false; Click(w, Button(w, "retryConnection"));
                Check(f.Http.GameSettingsWrites == 2 && f.Store.State.pendingLauncherPath == null, "path retry failed to consume confirmed pending intent");
                Console.WriteLine("PASS WINDOW rejected native path stays pending across disk reload and succeeds only after explicit retry (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowGameSettings = true;
                string library = Path.Combine(f.Store.Root, "steam-library"), install = Path.Combine(library, "steamapps", "common", "Wuthering Waves");
                string launcher = Path.Combine(install, "Wuthering Waves.exe"), shipping = Path.Combine(install, "Client", "Binaries", "Win64", "Client-Win64-Shipping.exe");
                Directory.CreateDirectory(Path.GetDirectoryName(shipping));
                File.WriteAllText(launcher, "Inert Steam bootstrap; never execute."); File.WriteAllText(shipping, "Inert game; never execute.");
                File.WriteAllText(Path.Combine(library, "steamapps", "appmanifest_3513350.acf"), "\"AppState\" { \"appid\" \"3513350\" \"installdir\" \"Wuthering Waves\" }");
                string official = Path.Combine(f.Store.Root, "official-game", "launcher.exe"); Directory.CreateDirectory(Path.GetDirectoryName(official)); File.WriteAllText(official, "Inert official launcher.");
                Set(w, "gameChoicesOverride", new Func<CancellationToken, Task<GameDiscovery.Result>>(c => Task.FromResult(new GameDiscovery.Result {
                    Message = "gameMultiple", Choices = new List<GameDiscovery.Choice> {
                        new GameDiscovery.Choice { Mode = "launcher", Path = official }, new GameDiscovery.Choice { Mode = "steam", Path = launcher } } })));
                Click(w, Button(w, "browse"));
                Check(Field<StackPanel>(w, "gameLocationOptions").Visibility == Visibility.Visible && Field<ComboBox>(w, "gameLocations").Items.Count == 2, "Change did not expose both launch routes");
                Check(!Button(w, "useGameLocation").IsEnabled && f.Http.GameSettingsWrites == 0 && f.Store.State.pendingLauncherPath == null, "opening ambiguous choices applied a route");
                Field<ComboBox>(w, "gameLocations").SelectedIndex = 1;
                Click(w, Button(w, "useGameLocation"));
                Check(f.Http.GameSettingsWrites == 0 && new PackageStore(f.Store.Root).State.pendingLauncherPath == launcher, "disconnected Steam selection not retained durably");
                var restarted = new PackageStore(f.Store.Root); Set(w, "store", restarted);
                Set(w, "discovery", null); Set(w, "status", new Dictionary<string, object>()); Load(w);
                Check(f.Http.GameSettingsWrites == 1 && restarted.State.pendingLauncherPath == null && Json.Text(Json.Child(f.Http.Status, "game"), "mode") == "steam", "reloaded choice did not post Steam mode exactly once");
                Check(Json.Text(Json.Child(f.Http.Status, "game"), "launcher") == launcher && Field<TextBlock>(w, "gameHint").Text == new Strings()["gameSteamSaved"], "Steam path or explanation incorrect");
                Connect(w); Check(f.Http.GameSettingsWrites == 1, "Steam selection repeated on reconnect");
                Await(w, "ChooseLauncher", official, CancellationToken.None);
                Check(f.Http.GameSettingsWrites == 2 && Json.Text(Json.Child(f.Http.Status, "game"), "mode") == "launcher", "explicit switch back to official kept Steam mode");
                Check(f.Http.Launches == 0, "choosing installation launched the game");
                Console.WriteLine("PASS WINDOW explicit Steam/official picker, disconnected persistence and one-time reloaded Steam selection (inert files and fake HTTP only)");
            }
            foreach (bool freshInstall in new[] { false, true })
            using (var f = new Fixture(root, true, true))
            {
                var w = f.Window; f.Http.AllowGameSettings = true;
                f.A.release.created = "2026-10-04T00:00:00Z";
                var versions = Field<ComboBox>(w, "releases"); versions.Items.Clear();
                versions.Items.Add(f.B.release); versions.Items.Add(f.A.release); versions.SelectedItem = f.B.release;
                if (freshInstall)
                {
                    f.Store.State.installed.Clear(); f.Store.State.selected = null; f.Store.State.previous = null; f.Store.Save();
                    versions.SelectedItem = f.A.release; Invoke(w, "PopulateInstalled"); Invoke(w, "ShowStatus");
                }
                else Connect(w);
                string install = Path.Combine(f.Store.Root, "steamapps", "common", "Wuthering Waves");
                string bootstrap = Path.Combine(install, "Wuthering Waves.exe");
                string shipping = Path.Combine(install, "Client", "Binaries", "Win64", "Client-Win64-Shipping.exe");
                Directory.CreateDirectory(Path.GetDirectoryName(shipping)); File.WriteAllText(bootstrap, "Inert bootstrap."); File.WriteAllText(shipping, "Inert shipping.");
                File.WriteAllText(Path.Combine(f.Store.Root, "steamapps", "appmanifest_3513350.acf"), "\"AppState\" { \"appid\" \"3513350\" \"installdir\" \"Wuthering Waves\" }");
                f.Http.RejectGameSettings = true; // An older helper cannot understand Steam.
                Await(w, "ChooseLauncher", bootstrap, CancellationToken.None);
                Check(f.Http.GameSettingsWrites == 0 && f.Store.State.pendingLauncherPath == bootstrap, "Steam selection contacted old/no helper or lost pending intent");
                Check(versions.SelectedItem == f.A.release && Field<Expander>(w, "advancedPanel").IsExpanded, "pending Steam did not offer the newer package in step 02");
                Check(Field<TextBlock>(w, "gameHint").Text == new Strings()["gameSteamUpdateRequired"], "pending upgrade explanation missing");
                if (!freshInstall)
                {
                    Check(Field<bool>(w, "connectionReady") && Field<Border>(w, "connectionPanel").Visibility == Visibility.Collapsed, "valid old helper presented as disconnected");
                    Check(Json.Text(Json.Child(f.Http.Status, "game"), "mode") == "manual", "old helper settings were changed before update");
                    var restarted = new PackageStore(f.Store.Root); Set(w, "store", restarted);
                    typeof(HelperHttp).GetField("store", PrivateInstance).SetValue(f.Http, restarted);
                    Set(w, "discovery", null); Connect(w);
                    Check(f.Http.GameSettingsWrites == 0 && restarted.State.pendingLauncherPath == bootstrap && Field<bool>(w, "connectionReady"), "pending Steam restart treated old helper as failed connection");
                    versions.SelectedItem = f.B.release; Field<CheckBox>(w, "risk").IsChecked = true;
                    Check(!Field<Button>(w, "launchButton").IsEnabled, "unsupported old package could launch with pending Steam choice");
                    versions.SelectedItem = f.A.release;
                }
                Field<CheckBox>(w, "compatible").IsChecked = true;
                f.Http.RejectGameSettings = false;
                Click(w, Field<Button>(w, "launchButton"));
                var applied = Field<PackageStore>(w, "store");
                Check(applied.Selected.release.id == f.A.release.id && f.Http.GameSettingsWrites == 1 && Json.Text(Json.Child(f.Http.Status, "game"), "mode") == "steam", "new package did not apply Steam intent exactly once");
                Check(applied.State.pendingLauncherPath == null && new PackageStore(applied.Root).State.pendingLauncherPath == null, "confirmed Steam intent remained queued");
                Check(f.Http.Launches == 0 && f.Http.RiskAcknowledgements == 0, "installation launched or acknowledged launch automatically");
                Connect(w); Check(f.Http.GameSettingsWrites == 1, "post-update reconnect reapplied Steam choice");
                Console.WriteLine("PASS WINDOW " + (freshInstall ? "fresh Steam choose/install/launch separation" : "old helper stays connected while Steam waits for package update") + " (inert ZIP/paths and fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w); f.Http.Status["gameRunning"] = true;
                var capture = Json.Child(f.Http.Status, "recording"); var sources = Json.Child(capture, "sources");
                sources["steamvr"] = false; sources["simulator"] = true; Refresh(w);
                Field<ComboBox>(w, "source").SelectedIndex = 1;
                Check(!Button(w, "recordStart").IsEnabled && Field<TextBlock>(w, "recordingState").Text == new Strings()["recordUnavailable"], "missing selected SteamVR recorder accepted");
                Field<ComboBox>(w, "source").SelectedIndex = 2;
                Check(Button(w, "recordStart").IsEnabled && Field<TextBlock>(w, "recordingState").Text == new Strings()["recordReady"], "available simulator source ignored or live readiness overstated");
                sources["simulator"] = false; sources["steamvr"] = true; Refresh(w);
                Check(!Button(w, "recordStart").IsEnabled, "missing selected simulator recorder accepted");
                Field<ComboBox>(w, "source").SelectedIndex = 0; Check(Button(w, "recordStart").IsEnabled, "automatic source ignored available recorder");
                sources["steamvr"] = false; Refresh(w); Check(!Button(w, "recordStart").IsEnabled, "automatic source accepted no packaged recorders");
                Check(f.Http.RecordStarts == 0, "source discovery started actual capture");
                Console.WriteLine("PASS WINDOW recording source changes refresh capability gates and defer live-source confirmation (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; var releaseChoices = Field<ComboBox>(w, "releases");
                Check(releaseChoices.Items.Count > 1, "language fixture needs at least two catalog choices");
                releaseChoices.SelectedIndex = releaseChoices.Items.Count - 1;
                string selectedRelease = ((Release)releaseChoices.SelectedItem).id;
                Field<ComboBox>(w, "installed").SelectedItem = f.A;
                Field<ComboBox>(w, "source").SelectedIndex = 2; Field<ComboBox>(w, "fps").SelectedIndex = 2;
                Field<ComboBox>(w, "size").SelectedIndex = 2; Field<ComboBox>(w, "purpose").SelectedIndex = 1;
                Field<CheckBox>(w, "compatible").IsChecked = true; Field<CheckBox>(w, "risk").IsChecked = true;
                Field<Expander>(w, "advancedPanel").IsExpanded = true; Invoke(w, "ShowPage", true);
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "check" }, { "running", false }, { "error", true }, { "code", 1 }, { "output", "Fixture readiness failure retained across languages" } };
                Invoke(w, "ShowStatus");
                string evidence = Field<TextBox>(w, "details").Text;
                Check(evidence.Contains("Fixture readiness failure"), "fixture backend error was not presented");
                foreach (int language in new[] { 1, 0 })
                {
                    Field<ComboBox>(w, "languages").SelectedIndex = language; Drain();
                    Check(((Release)Field<ComboBox>(w, "releases").SelectedItem).id == selectedRelease, "language changed release selection");
                    Check(((Installed)Field<ComboBox>(w, "installed").SelectedItem).folder == f.A.folder, "language changed recovery row");
                    Check(Field<ComboBox>(w, "source").SelectedIndex == 2 && Field<ComboBox>(w, "fps").SelectedIndex == 2 && Field<ComboBox>(w, "size").SelectedIndex == 2 && Field<ComboBox>(w, "purpose").SelectedIndex == 1, "language changed recording options");
                    Check(Field<CheckBox>(w, "compatible").IsChecked == true && Field<CheckBox>(w, "risk").IsChecked == true, "language lost consent");
                    Check(Field<bool>(w, "troubleshootingVisible") && Field<Expander>(w, "advancedPanel").IsExpanded, "language changed page or update state");
                    Check(Field<TextBox>(w, "details").Text == evidence, "language lost backend error details");
                    string code = language == 1 ? "zh-Hans" : "en";
                    Check(new PackageStore(f.Store.Root).State.language == code, "language event did not save the selected language");
                    Check(Convert.ToString(Button(w, "repair").Content) == new Strings { Language = code }["repair"], "controls did not actually translate");
                    Offscreen(w);
                }
                Console.WriteLine("PASS WINDOW language events retain selections, capture options, consent, tab and error evidence (EN -> zh -> EN)");
            }
            using (var f = new Fixture(root))
            {
                Connect(f.Window);
                File.WriteAllText(Path.Combine(f.Store.Folder(f.A), "app/dev/wuwa_player.py"), "damaged old fixture");
                Field<ComboBox>(f.Window, "installed").SelectedItem = f.A;
                Click(f.Window, Button(f.Window, "repair"));
                Check(f.Store.Selected.release.id == f.A.release.id && f.Store.Selected.folder != f.A.folder, "repair did not rebuild the chosen inactive version");
                PackageStore.Verify(f.Store.Folder(f.Store.Selected), f.A.release, CancellationToken.None);
                Check(f.Store.State.previous == f.B.folder && Directory.Exists(f.Store.Folder(f.B)), "repair lost the previously active version");
                Click(f.Window, Button(f.Window, "useVersion"));
                Check(f.Store.State.previous == f.B.folder, "reconnecting the current version destroyed rollback");
                Check(f.Downloads.Requests == 0 && f.Http.Stops == 2, "repair/reconnect did not use only verified local files and fixture helper stops");
                Console.WriteLine("PASS WINDOW repair click rebuilds the chosen version; reconnect click preserves rollback (local cache and fake HTTP only)");
            }
            using (var f = new Fixture(root, false))
            {
                Check(!Field<Button>(f.Window, "launchButton").IsEnabled, "fresh setup enabled an unconfirmed install or launch");
                foreach (string key in new[] { "recordStart", "recordStop", "repair", "useVersion", "remove", "rollback" }) Check(!Button(f.Window, key).IsEnabled, "empty setup enabled " + key);
                Field<CheckBox>(f.Window, "compatible").IsChecked = true;
                Check(Field<Button>(f.Window, "launchButton").IsEnabled, "compatibility checkbox did not enable installation");
                Field<CheckBox>(f.Window, "risk").IsChecked = true;
                Check(Field<Button>(f.Window, "launchButton").IsEnabled && Convert.ToString(Field<Button>(f.Window, "launchButton").Content) == "Install VR", "primary action must offer install, not launch, on first use");
                Check(Field<Button>(f.Window, "cancelButton").Visibility == Visibility.Collapsed, "idle setup displays cancellation");
                foreach (int language in new[] { 1, 0 })
                {
                    Field<ComboBox>(f.Window, "languages").SelectedIndex = language; Drain();
                    var visual = (FrameworkElement)f.Window.Content;
                    visual.Measure(new Size(720, 641)); visual.Arrange(new Rect(0, 0, 720, 641)); visual.UpdateLayout();
                    var notice = Field<TextBlock>(f.Window, "riskNotice"); var consent = Field<CheckBox>(f.Window, "compatible");
                    var page = Field<ScrollViewer>(f.Window, "setupPage");
                    var group = notice.Parent as StackPanel;
                    Check(group != null && consent.Parent == group && group.Children.IndexOf(notice) < group.Children.IndexOf(consent),
                        "first-install risk text is not adjacent before its checkbox");
                    foreach (var element in new FrameworkElement[] { notice, consent })
                    {
                        var at = element.TransformToAncestor(page).Transform(new Point(0, 0));
                        Check(at.Y >= 0 && at.Y + element.ActualHeight <= page.ViewportHeight,
                            "minimum-size first-install risk or consent is below the fold in language " + language);
                    }
                }
                Offscreen(f.Window);
                Console.WriteLine("PASS WINDOW fresh setup gates actions and keeps adjacent risk/consent visible at 720x641 content in EN/zh");
            }
            using (var f = new Fixture(root))
            {
                // Reuse only the tiny inert ZIP in cache, but start without an installed selection.
                f.Store.State.installed.Clear(); f.Store.State.selected = null; f.Store.State.previous = null; f.Store.Save();
                var versions = Field<ComboBox>(f.Window, "releases"); versions.Items.Clear(); versions.Items.Add(f.A.release); versions.SelectedIndex = 0;
                Invoke(f.Window, "PopulateInstalled"); Invoke(f.Window, "ShowStatus");
                Field<CheckBox>(f.Window, "compatible").IsChecked = true;
                Click(f.Window, Field<Button>(f.Window, "launchButton"));
                Check(f.Store.Selected.release.id == f.A.release.id, "primary install did not install selected package");
                Check(Convert.ToString(Field<Button>(f.Window, "launchButton").Content) == "Launch in VR", "installed primary did not become launch");
                Check(Field<Button>(f.Window, "launchButton").IsEnabled && Field<CheckBox>(f.Window, "risk").IsChecked == true && f.Http.RiskAcknowledgements == 0,
                    "explicit combined install consent was lost or installation acknowledged/launched remotely");
                Check(f.Downloads.Requests == 0, "first install ignored verified local ZIP");
                Console.WriteLine("PASS WINDOW first-use primary installs a verified cached ZIP, then becomes Launch without starting a game");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w);
                f.Http.Status["openxr"] = new Dictionary<string, object> { { "name", "SteamVR" }, { "available", true }, { "canHeadset", true }, { "canSimulator", true }, { "manifest", "fixture-headset.json" } };
                Refresh(w);
                Check(!Button(w, "useHeadset").IsEnabled && Convert.ToString(Button(w, "useHeadset").Content).Contains("Current"), "headset current state offers a no-op");
                Check(Button(w, "useSimulator").IsEnabled, "available bundled simulator cannot be chosen");
                Set(w, "confirmationOverride", new Func<string, bool>(key => false));
                Click(w, Button(w, "useSimulator"));
                Check(f.Http.RuntimeChanges == 0 && Field<TextBlock>(w, "operationText").Text == "No changes made.", "declined runtime switch changed anything or hid cancellation");
                Set(w, "confirmationOverride", new Func<string, bool>(key => true));
                Click(w, Button(w, "useSimulator"));
                Check(f.Http.RuntimeChanges == 1 && !Button(w, "useSimulator").IsEnabled && Button(w, "useHeadset").IsEnabled, "simulator selection did not update both buttons");
                Check(Field<TextBlock>(w, "operationText").Text.Contains("Fixture runtime changed."), "runtime result hidden behind generic completion");
                Click(w, Button(w, "useHeadset"));
                Check(f.Http.RuntimeChanges == 2 && !Button(w, "useHeadset").IsEnabled, "headset restore did not update controls");
                Field<CheckBox>(w, "risk").IsChecked = true;
                Field<CheckBox>(w, "compatible").IsChecked = true;
                Check(Field<Button>(w, "launchButton").IsEnabled && Button(w, "repair").IsEnabled && Button(w, "useSimulator").IsEnabled,
                    "idle fixture did not make launch, recovery and runtime actions available before activity");
                foreach (string activity in new[] { "gameRunning", "injectorRunning" })
                {
                    f.Http.Status[activity] = true; Refresh(w);
                    Check(!Button(w, "useSimulator").IsEnabled && Field<TextBlock>(w, "runtimeHint").Text.Contains("Close WuWa"), "unsafe runtime choice is not visibly blocked for " + activity);
                    foreach (string key in new[] { "launch", "repair", "useVersion", "remove", "rollback", "restore", "reset" })
                        Check(!Button(w, key).IsEnabled, activity + " left " + key + " enabled");
                    f.Http.Status[activity] = false;
                }
                Json.Child(f.Http.Status, "openxr")["canSimulator"] = false; Refresh(w);
                Check(!Button(w, "useSimulator").IsEnabled && Field<TextBlock>(w, "runtimeHint").Text.Contains("OpenXR runtime"), "missing simulator capability silently enabled");
                Click(w, Button(w, "check"));
                Check(f.Http.ReadinessChecks == 1 && Field<TextBlock>(w, "operationText").Text.Contains("not applied yet") && Field<Expander>(w, "feedbackPanel").IsExpanded, "readiness result is hidden or conflated with headset success");
                f.Http.Status["openxr"] = new Dictionary<string, object> { { "name", "" }, { "available", false }, { "manifest", "" }, { "canHeadset", false }, { "canSimulator", true } }; Refresh(w);
                Check(Button(w, "useSimulator").IsEnabled && !Button(w, "useHeadset").IsEnabled &&
                    Field<TextBlock>(w, "runtime").Text == new Strings()["runtimeMissingSimulator"], "fresh PC lacks a supported simulator recovery action");
                Click(w, Button(w, "useSimulator"));
                var runtimeRequest = Json.Read<Dictionary<string, object>>(f.Http.RuntimeBody);
                Check(runtimeRequest.ContainsKey("expectedActive") && Json.Text(runtimeRequest, "expectedActive") == "" && f.Http.RuntimeChanges == 3,
                    "first runtime registration lost its explicit empty expectedActive guard");
                Json.Child(f.Http.Status, "openxr")["isBundledSimulator"] = false; Refresh(w);
                Check(Button(w, "useSimulator").IsEnabled && Field<TextBlock>(w, "runtime").Text.Contains(new Strings()["runtimeOtherSimulator"]), "foreign simulator conceals package switch");
                Console.WriteLine("PASS WINDOW headset/simulator roundtrip, decline, capability/activity gates and visible readiness result (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w);
                Check(!Field<Expander>(w, "advancedPanel").IsExpanded && !Field<Expander>(w, "recoveryPanel").IsExpanded && !Field<Expander>(w, "recordingOptions").IsExpanded, "expert controls clutter initial public flow");
                var publicTree = Tree((DependencyObject)w.Content).ToArray();
                Check(!publicTree.OfType<TabControl>().Any() && publicTree.Contains(Field<Expander>(w, "advancedPanel")) &&
                    !publicTree.Contains(Button(w, "recordStart")) && !Field<List<Button>>(w, "actions").Any(b => (string)b.Tag == "install"),
                    "public page retained tabs/recording or a duplicate install action, or misplaced updates");
                Check(Field<TextBlock>(w, "packageState").Text.Contains(f.B.release.id), "step02 omitted the actual installed version");
                Click(w, Button(w, "troubleshooting"));
                Check(Field<bool>(w, "troubleshootingVisible") && Tree((DependencyObject)w.Content).Contains(Field<Expander>(w, "controllerPanel")),
                    "footer did not open controller troubleshooting");
                Click(w, Button(w, "backToSetup"));
                Check(!Field<bool>(w, "troubleshootingVisible") && Field<Button>(w, "launchButton").Parent != null,
                    "Back did not restore setup or main action disappeared");
                Click(w, Button(w, "checkUpdates"));
                Check(Field<Expander>(w, "advancedPanel").IsExpanded && Field<TextBlock>(w, "catalogState").Text == new Strings()["catalogOffline"] &&
                    Field<Expander>(w, "feedbackPanel").IsExpanded, "failed update refresh was hidden");
                string opened = null; Set(w, "openOverride", new Action<string>(url => opened = url));
                Click(w, Button(w, "web"));
                Check(opened != null && new Uri(opened).AbsoluteUri == "http://127.0.0.1:34671/" && Field<TextBlock>(w, "operationText").Text.Contains("Developer tools opened"), "developer tools did not use connected package's web helper");
                Set(w, "confirmationOverride", new Func<string, bool>(key => false));
                Click(w, Button(w, "reset")); Check(f.Http.Resets == 0, "declining reset still sent it");
                Set(w, "confirmationOverride", new Func<string, bool>(key => true));
                Click(w, Button(w, "reset")); Check(f.Http.Resets == 1, "reset did not reach selected build once");
                Console.WriteLine("PASS WINDOW public setup has one CTA/no tabs; updates stay visible, troubleshooting returns, web uses same package, reset confirms (fake HTTP/browser)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.Status["riskAcknowledged"] = true; f.Http.AllowLaunch = true;
                Connect(w);
                Check(Field<CheckBox>(w, "risk").IsChecked == true && Field<Button>(w, "launchButton").IsEnabled &&
                    ((Release)Field<ComboBox>(w, "releases").SelectedItem).id == f.B.release.id,
                    "returning player lost prior consent or the active build");
                Field<CheckBox>(w, "risk").IsChecked = false; Connect(w);
                Check(Field<CheckBox>(w, "risk").IsChecked == false && !Field<Button>(w, "launchButton").IsEnabled,
                    "reconnect overrode a deliberate consent uncheck");
                Field<CheckBox>(w, "risk").IsChecked = true;
                Click(w, Field<Button>(w, "launchButton"));
                Check(f.Http.Launches == 1, "returning primary did not request launch exactly once");
                Console.WriteLine("PASS WINDOW returning saved consent enables one launch action while deliberate opt-out remains authoritative");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w);
                var choices = Field<ComboBox>(w, "releases"); choices.Items.Add(f.A.release); choices.SelectedItem = f.A.release;
                Check(Convert.ToString(Field<Button>(w, "launchButton").Content) == new Strings()["installSelected"] &&
                    !Field<Button>(w, "launchButton").IsEnabled && Field<TextBlock>(w, "packageState").Text.Contains(f.A.release.id) &&
                    Field<TextBlock>(w, "packageState").Text.Contains(f.B.release.id), "choosing a version did not distinguish installed/selected or require compatibility");
                Field<CheckBox>(w, "compatible").IsChecked = true;
                Click(w, Field<Button>(w, "launchButton"));
                Check(f.Store.Selected.release.id == f.A.release.id && Convert.ToString(Field<Button>(w, "launchButton").Content) == new Strings()["launch"] &&
                    f.Http.Launches == 0 && f.Downloads.Requests == 0, "one primary install-selected action launched the game or ignored local cache");
                Console.WriteLine("PASS WINDOW step02 version selection reuses the primary install action, preserves identity and returns to Launch");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window;
                var store = Field<PackageStore>(w, "store");
                var outerGuide = Path.Combine(store.Root, "PlayerGuide.html");
                File.WriteAllText(outerGuide, "<h1 id='en'>Player guide</h1><h1 id='zh'>玩家指南</h1>");
                string opened = null; Set(w, "openOverride", new Action<string>(url => opened = url));
                string selected = store.State.selected; store.State.selected = null;
                Click(w, Button(w, "guide"));
                Check(new Uri(opened).IsFile && Path.GetFullPath(new Uri(opened).LocalPath) == Path.GetFullPath(outerGuide) &&
                    new Uri(opened).Fragment == "#en", "fresh launcher guide was unavailable without a package");
                store.State.selected = selected;
                var guide = Path.Combine(store.Folder(store.Selected), "app/site/l/zh-Hans/guide.html");
                Directory.CreateDirectory(Path.GetDirectoryName(guide)); File.WriteAllText(guide, "<h1>操作指南</h1>");
                Field<ComboBox>(w, "languages").SelectedIndex = 1; Drain();
                Click(w, Button(w, "guide"));
                Check(Path.GetFullPath(new Uri(opened).LocalPath) == Path.GetFullPath(outerGuide) && new Uri(opened).Fragment == "#zh",
                    "launcher guide did not retain priority or Chinese section after installation");
                File.Delete(outerGuide);
                Click(w, Button(w, "guide"));
                Check(Path.GetFullPath(new Uri(opened).LocalPath) == Path.GetFullPath(guide), "missing outer guide broke installed offline fallback");
                Click(w, Button(w, "fullControls"));
                Check(new Uri(opened).IsFile && Path.GetFullPath(new Uri(opened).LocalPath) == Path.GetFullPath(guide) && new Uri(opened).Fragment == "#controls", "Chinese offline guide did not open with section anchor: " + opened + " expected " + guide);
                var candidateGuide = Path.Combine(Path.GetDirectoryName(guide), "quickstart.html");
                File.WriteAllText(candidateGuide, "<h1>候选版操作</h1>");
                Click(w, Button(w, "fullControls"));
                Check(Path.GetFullPath(new Uri(opened).LocalPath) == Path.GetFullPath(candidateGuide) && new Uri(opened).Fragment == "#controls", "candidate controls did not take priority over the older public guide");
                File.Delete(candidateGuide);
                File.Delete(guide); Click(w, Button(w, "fullControls"));
                Check(opened == "https://chronohaxx.github.io/wuwa-vr/guide.html#controls" && Field<TextBlock>(w, "operationText").Text.Contains("英文"), "missing translation did not explain English online fallback");
                // No helper connection is needed for retained package documentation.
                Check(Field<LauncherBridge>(w, "bridge").Address == null, "offline guide started a helper");
                Console.WriteLine("PASS WINDOW fresh EN/installed zh outer guide, portable controls and visible English fallback without a helper");
            }
            using (var f = new Fixture(root))
            {
                Connect(f.Window); f.Http.Status["gameRunning"] = true; f.Http.RejectRecordStart = true; Refresh(f.Window);
                Check(Button(f.Window, "recordStart").IsEnabled && !Button(f.Window, "recordStop").IsEnabled, "recording idle-state gates are wrong");
                Json.Child(f.Http.Status, "recording")["available"] = false; Refresh(f.Window);
                Check(!Button(f.Window, "recordStart").IsEnabled && Field<TextBlock>(f.Window, "recordingState").Text == new Strings()["recordUnavailable"], "unsupported recorder does not explain why Start is disabled");
                Json.Child(f.Http.Status, "recording")["available"] = true; Refresh(f.Window);
                Click(f.Window, Button(f.Window, "recordStart"));
                Check(f.Http.RecordStarts == 1 && Field<TextBox>(f.Window, "details").Text.Contains("Fixture recorder unavailable"), "recording error did not reach actual UI Details");
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "recording" }, { "running", true }, { "message", "Fixture recording" } };
                Json.Child(f.Http.Status, "recording")["running"] = true; Refresh(f.Window);
                Check(!Button(f.Window, "recordStart").IsEnabled && Button(f.Window, "recordStop").IsEnabled, "active recording gates are wrong");
                Check(Field<Button>(f.Window, "cancelButton").Visibility == Visibility.Collapsed, "recording incorrectly offers launch cancellation");
                Click(f.Window, Button(f.Window, "recordStop")); Check(f.Http.RecordStops == 1, "stop recording button used the wrong endpoint");
                Json.Child(f.Http.Status, "recording")["running"] = false;
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", true }, { "message", "Fixture launch waiting" } }; Refresh(f.Window);
                Check(!Button(f.Window, "recordStart").IsEnabled && !Button(f.Window, "recordStop").IsEnabled, "launch allowed conflicting recording actions");
                Check(Field<Button>(f.Window, "cancelButton").IsEnabled && Field<Button>(f.Window, "cancelButton").Visibility == Visibility.Visible, "launch lacks cancellation");
                Click(f.Window, Field<Button>(f.Window, "cancelButton")); Check(f.Http.Cancels == 1, "cancel button did not request cancellation once");
                Console.WriteLine("PASS WINDOW recording error, start/stop gates and launch-only cancellation use actual button events");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowLaunch = true; Connect(w);
                Check(!Field<Button>(w, "launchButton").IsEnabled, "launch bypassed unchecked account risk consent");
                Field<CheckBox>(w, "risk").IsChecked = true;
                // The installed selection, not a recovery-dropdown row, owns launch.
                Field<ComboBox>(w, "installed").SelectedItem = f.A;
                Click(w, Field<Button>(w, "launchButton"));
                Check(f.Http.RiskAcknowledgements == 1 && f.Http.Launches == 1 && Json.Text(Json.Read<Dictionary<string, object>>(f.Http.LaunchBody), "id") == f.B.release.buildId,
                    "launch did not acknowledge risk and request the active installed build exactly once");
                Check(Field<TextBlock>(w, "operationText").Text.Contains("Fixture launch waiting") && Field<ProgressBar>(w, "progress").Visibility == Visibility.Visible,
                    "accepted launch did not show waiting progress");
                Check(!Field<Button>(w, "launchButton").IsEnabled && Field<Button>(w, "cancelButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Visible,
                    "waiting launch allowed another launch or hid cancellation");
                Click(w, Field<Button>(w, "cancelButton"));
                Check(f.Http.Cancels == 1 && Field<TextBlock>(w, "operationText").Text.Contains("cancelled before the game started") &&
                    Field<Button>(w, "launchButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed,
                    "launch cancellation did not reach a visible terminal state ready for retry");
                Click(w, Field<Button>(w, "launchButton"));
                Check(f.Http.Launches == 2 && f.Http.RiskAcknowledgements == 2, "retry did not send the intended launch once");
                f.Http.Status["gameRunning"] = true; f.Http.Status["injectorRunning"] = true;
                Refresh(w);
                Check(Field<TextBlock>(w, "launchState").Text == new Strings()["awaitingStereo"], "running game was falsely reported as confirmed stereo");
                f.Http.Status["selected"] = f.B.release.buildId; f.Http.Status["selectionMatches"] = true;
                f.Http.Status["processes"] = new[] { new { name = "Custom_UEVR_Injector.exe", pid = 78 } };
                f.Http.Status["launch"] = new Dictionary<string, object> { { "phase", "finished" }, { "firstFrameSeen", true }, { "injectorRunning", true }, { "injectorPid", 78 }, { "buildId", f.B.release.buildId } };
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", false }, { "code", 0 }, { "output", "Fixture launch finished; stereo frame observed" } };
                Refresh(w);
                Check(Field<TextBlock>(w, "launchState").Text == new Strings()["stereoSeen"] && Field<TextBlock>(w, "operationText").Text.Contains("stereo frame observed"),
                    "matching current build/injector evidence did not reach launch status");
                Check(!Field<Button>(w, "launchButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed && Field<ProgressBar>(w, "progress").Visibility == Visibility.Collapsed,
                    "completed launch retained waiting controls or permitted duplicate launch");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW launch click acknowledges risk, targets active build, waits, cancels, retries and shows matching stereo evidence (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowLaunch = true; f.Http.RejectLaunch = true; Connect(w);
                Field<CheckBox>(w, "risk").IsChecked = true;
                Click(w, Field<Button>(w, "launchButton"));
                Check(f.Http.Launches == 1 && Field<TextBlock>(w, "operationText").Text.Contains("Fixture Windows permission was declined") &&
                    Field<TextBox>(w, "details").Text.Contains("Fixture Windows permission was declined") && Field<Expander>(w, "feedbackPanel").IsExpanded,
                    "launch HTTP rejection was not visibly explained");
                Check(Field<Button>(w, "launchButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed,
                    "rejected launch left the screen busy or prevented retry");
                f.Http.RejectLaunch = false;
                Click(w, Field<Button>(w, "launchButton"));
                f.Http.Status["launch"] = new Dictionary<string, object> { { "phase", "failed" } };
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", false }, { "error", true }, { "code", 4 }, { "output", "Fixture Windows permission was cancelled after request" } };
                Refresh(w);
                Check(Field<TextBlock>(w, "operationText").Text.Contains("cancelled after request") && Field<TextBox>(w, "details").Text.Contains("cancelled after request") &&
                    Field<Expander>(w, "feedbackPanel").IsExpanded && Field<Button>(w, "launchButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed,
                    "asynchronous launch failure hid its error or stranded retry");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW launch request rejection and asynchronous permission failure remain visible and permit retry (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w); Field<CheckBox>(w, "risk").IsChecked = true;
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "running", false }, { "code", 0 }, { "output", "Fixture worker dispatched" } };
                var worker = new Dictionary<string, object> { { "current", true }, { "running", true }, { "cancellable", true },
                    { "phase", "waiting-game" }, { "message", "Fixture Steam is opening; waiting for the selected game" }, { "attemptId", "current-attempt" }, { "ownerPid", 98765 } };
                f.Http.Status["launch"] = worker; Refresh(w);
                Check(Field<TextBlock>(w, "operationText").Text.Contains("Steam is opening") && Field<TextBlock>(w, "launchState").Text.Contains("Steam is opening"), "completed dispatch job hid external worker progress");
                Check(Field<Button>(w, "cancelButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Visible &&
                    !Field<Button>(w, "launchButton").IsEnabled && !Button(w, "useSimulator").IsEnabled && Field<ProgressBar>(w, "progress").IsIndeterminate,
                    "pending worker hid Stop waiting or allowed a conflicting launch/runtime action");
                var blockedClose = (Task<bool>)Invoke(w, "PrepareToClose"); PumpUntil(() => blockedClose.IsCompleted, "pending worker close guard");
                Check(!blockedClose.GetAwaiter().GetResult() && f.Http.Stops == 0 && Field<TextBox>(w, "details").Text.Contains("98765") &&
                    Field<TextBlock>(w, "operationText").Text.Contains(new Strings()["closeStartup"]), "closing abandoned the pending worker or hid its process identity");
                Click(w, Field<Button>(w, "cancelButton"));
                Check(f.Http.Cancels == 1 && Field<TextBlock>(w, "operationText").Text.Contains("cancelled before the game started") &&
                    Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed && Field<Button>(w, "launchButton").IsEnabled, "external worker Stop waiting failed");
                // The same completed job can outlive a worker failure: only phase/message change.
                worker["running"] = false; worker["cancellable"] = false; worker["phase"] = "failed"; worker["message"] = "Fixture Steam handoff failed after dispatch";
                f.Http.Status["launch"] = worker; Refresh(w);
                Check(Field<TextBlock>(w, "launchState").Text == new Strings()["launchFailed"] && Field<TextBox>(w, "details").Text.Contains("handoff failed after dispatch") &&
                    Field<Expander>(w, "feedbackPanel").IsExpanded && Field<Expander>(w, "feedbackPanel").Visibility == Visibility.Visible,
                    "terminal external worker failure stayed hidden behind successful dispatch");
                worker["current"] = false; worker["running"] = true; worker["cancellable"] = true; worker["message"] = "Stale worker must not block"; Refresh(w);
                Check(Field<Button>(w, "launchButton").IsEnabled && Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed &&
                    !Field<TextBlock>(w, "operationText").Text.Contains("Stale worker"), "historical worker affected current controls");
                f.Http.Status["gameRunning"] = true; f.Http.Status["injectorRunning"] = true; Refresh(w);
                var idleClose = (Task<bool>)Invoke(w, "PrepareToClose"); PumpUntil(() => idleClose.IsCompleted, "idle helper close");
                Check(idleClose.GetAwaiter().GetResult() && f.Http.Stops == 1 && Json.Flag(f.Http.Status, "gameRunning") && Json.Flag(f.Http.Status, "injectorRunning") && f.Http.Launches == 0,
                    "idle launcher exit failed to stop its helper exactly once or changed the running game");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW external worker remains visible/cancellable after dispatch, late failure expands, stale worker is ignored (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.AllowRecordStart = true; Connect(w);
                f.Http.Status["gameRunning"] = true; Refresh(w);
                Field<ComboBox>(w, "source").SelectedIndex = 1; Field<ComboBox>(w, "fps").SelectedIndex = 2;
                Field<ComboBox>(w, "size").SelectedIndex = 2; Field<ComboBox>(w, "purpose").SelectedIndex = 0;
                Click(w, Button(w, "recordStart"));
                var body = Json.Read<Dictionary<string, object>>(f.Http.RecordingBody);
                Check(f.Http.RecordStarts == 1 && Json.Flag(body, "videoOnly") && Json.Text(body, "source") == "steamvr" && Json.Text(body, "fps") == "60" && Json.Text(body, "eyeWidth") == "1280",
                    "recording controls sent different capture options");
                Check(Field<TextBlock>(w, "recordingState").Text == new Strings()["recordingActive"] && !Button(w, "recordStart").IsEnabled && Button(w, "recordStop").IsEnabled,
                    "recording start did not expose its active state and stop action");
                Check(Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed && !Field<ComboBox>(w, "source").IsEnabled, "recording offered launch cancellation or mutable capture options");
                Click(w, Button(w, "recordStop"));
                Check(f.Http.RecordStops == 1 && Json.Flag(Json.Child(f.Http.Status, "recording"), "stopping"), "stop did not request recorder finalization");
                Json.Child(f.Http.Status, "recording")["running"] = false; Json.Child(f.Http.Status, "recording")["stopping"] = false;
                f.Http.Status["job"] = new Dictionary<string, object> { { "kind", "recording" }, { "running", false }, { "code", 0 }, { "output", "Fixture recording saved successfully" } };
                Refresh(w);
                Check(f.Http.RecordStops == 1 && Field<TextBlock>(w, "recordingState").Text == new Strings()["recordReady"] &&
                    Button(w, "recordStart").IsEnabled && !Button(w, "recordStop").IsEnabled && Field<ComboBox>(w, "source").IsEnabled,
                    "recording stop did not return controls to ready state");
                Check(Field<TextBlock>(w, "operationText").Text.Contains("Fixture recording saved successfully") &&
                    Field<ProgressBar>(w, "progress").Visibility == Visibility.Collapsed && f.Http.Writes.All(path => new[] { "/api/language", "/api/record-start", "/api/record-stop" }.Contains(path)),
                    "successful recording output was hidden or another action was requested");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW recording click sends selected options, becomes active, stops and returns to ready with its saved result (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; var bridge = Field<LauncherBridge>(w, "bridge");
                f.Http.IdentityRoot = Path.Combine(f.Store.Root, "old-package", "app"); f.Http.IdentityPid = int.MaxValue;
                Json.Save(Path.Combine(bridge.Data, "launcher.json"), new { url = "http://127.0.0.1:34671/", pid = f.Http.IdentityPid, app = f.Http.IdentityRoot });
                Click(w, Button(w, "retryConnection"));
                var conflict = bridge.Conflict;
                Check(conflict != null && Field<Border>(w, "connectionPanel").Visibility == Visibility.Visible && !Field<Button>(w, "launchButton").IsEnabled,
                    "actual retry event did not show the conflicting-helper recovery state");
                string evidence = Field<TextBox>(w, "details").Text;
                foreach (int language in new[] { 1, 0 })
                {
                    Field<ComboBox>(w, "languages").SelectedIndex = language; Drain();
                    var translations = new Strings { Language = language == 1 ? "zh-Hans" : "en" };
                    Check(bridge.Conflict == conflict && bridge.Address == null && Field<Border>(w, "connectionPanel").Visibility == Visibility.Visible &&
                        Field<TextBlock>(w, "connectionMessage").Text.StartsWith(translations["launcherConflict"], StringComparison.Ordinal) &&
                        Field<TextBlock>(w, "connectionMessage").Text.Contains(conflict.AppRoot), "language change lost or failed to translate the conflict");
                    Check(Button(w, "openExisting").IsEnabled && Button(w, "switchLauncher").IsEnabled && Button(w, "retryConnection").IsEnabled &&
                        Convert.ToString(Button(w, "switchLauncher").Content) == translations["switchLauncher"] && !Field<Button>(w, "launchButton").IsEnabled &&
                        Field<TextBox>(w, "details").Text == evidence, "language rebuild lost recovery actions, guards or error evidence");
                    Offscreen(w);
                }
                Check(f.Http.Writes.Count == 0, "conflict/language flow wrote to the wrong helper");
                Console.WriteLine("PASS WINDOW conflict from an actual retry click survives EN/zh language events with recovery actions and error evidence intact");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; f.Http.OfflineFolder = f.B.folder;
                File.Delete(Path.Combine(f.Store.Folder(f.B), "python/pythonw.exe"));
                Click(w, Button(w, "retryConnection"));
                Check(!Field<bool>(w, "connectionReady") && Field<Border>(w, "connectionPanel").Visibility == Visibility.Visible,
                    "missing active helper did not expose recovery");
                foreach (string key in new[] { "repair", "useVersion", "rollback" }) Check(Button(w, key).IsEnabled, "broken active package disabled " + key);
                Click(w, Button(w, "repair"));
                Check(f.Store.Selected.release.id == f.B.release.id && f.Store.Selected.folder != f.B.folder && Field<bool>(w, "connectionReady"),
                    "repair could not replace a broken active package without starting it");
                PackageStore.Verify(f.Store.Folder(f.Store.Selected), f.B.release, CancellationToken.None);
                Check(f.Http.Stops == 0 && f.Downloads.Requests == 0 && f.Http.Launches == 0, "broken package recovery touched a process, download or game");
                Console.WriteLine("PASS WINDOW missing active helper stays repairable and is rebuilt from verified cache without executing the broken package");
            }
            foreach (string selectionAction in new[] { "useVersion", "rollback" }) using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w); Field<CheckBox>(w, "risk").IsChecked = true;
                Field<ComboBox>(w, "installed").SelectedItem = f.A;
                // Fails inside the fake HTTP token check, before any process-start fallback.
                f.Http.RejectedTokenFolder = f.A.folder;
                Click(w, Button(w, selectionAction));
                Check(f.Store.Selected.folder == f.A.folder && !Field<bool>(w, "connectionReady") &&
                    ((Release)Field<ComboBox>(w, "releases").SelectedItem).id == f.A.release.id &&
                    Convert.ToString(Field<Button>(w, "launchButton").Content) == new Strings()["launch"],
                    selectionAction + " committed selection but retained an obsolete install target after connection failure");
                f.Http.RejectedTokenFolder = null;
                Click(w, Button(w, "retryConnection"));
                Check(Field<bool>(w, "connectionReady") && Field<Button>(w, "launchButton").IsEnabled &&
                    Convert.ToString(Field<Button>(w, "launchButton").Content) == new Strings()["launch"] &&
                    ((Release)Field<ComboBox>(w, "releases").SelectedItem).id == f.A.release.id &&
                    f.Store.Selected.folder == f.A.folder && f.Http.Launches == 0 && f.Downloads.Requests == 0,
                    selectionAction + " retry did not preserve the committed version and return to Launch");
                Console.WriteLine("PASS WINDOW " + selectionAction + " connection failure/retry retains selected package and correct Launch action (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w); string copied = null;
                Set(w, "diagnosticsCopyOverride", new Action<string>(value => copied = value));
                Set(w, "lastNativeError", "IOException: Fixture native failure\n" + @"C:\Users\private-person\Native Log\failure.txt" +
                    "\nUserName: private-person\ntoken=native-secret\nAccount " + Environment.UserName);
                Field<TextBox>(w, "details").Text = "Fixture detail status\n" + @"\\private-server\share\private-person\detail.log" +
                    "\nserial: device-secret\n" + @"HID\VID_1234&PID_5678\unique-device";
                f.Http.DiagnosticsReply = "Selected build: window-b\nOpenXR: SteamVR\n" +
                    @"file:///C:/Users/private-person/backend.log" + "\nAuthorization: Bearer backend-secret\n" +
                    @"%USERPROFILE%\Private Folder\log.txt" + "\napi_key=service-secret";
                Click(w, Button(w, "copyDiagnostics"));
                Check(copied.Contains("IOException: Fixture native failure") && copied.Contains("Fixture detail status") &&
                    copied.Contains("Selected build: window-b") && copied.Contains("OpenXR: SteamVR") && copied.Contains("[path]") && copied.Contains("[device]"),
                    "diagnostic redaction lost useful native/backend status or skipped a source");
                foreach (var secret in new[] { "private-person", "private-server", "native-secret", "device-secret", "unique-device", "backend-secret", "service-secret", @"C:\Users", "%USERPROFILE%", "Account " + Environment.UserName })
                    Check(!copied.Contains(secret), "assembled diagnostics leaked " + secret);
                f.Http.DiagnosticsFailure = "password=exception-secret\n" + @"C:\Users\private-person\unavailable.log";
                Click(w, Button(w, "copyDiagnostics"));
                Check(copied.Contains(new Strings()["backendUnavailable"]) && !copied.Contains("exception-secret") &&
                    !copied.Contains("private-person") && Field<TextBlock>(w, "operationText").Text == new Strings()["diagnosticsLocalOnly"],
                    "backend diagnostic failure bypassed redaction or claimed a complete report");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW general copied diagnostics redact native details/backend/errors while preserving useful status (fake HTTP/clipboard)");
            }
            foreach (bool wrongBuild in new[] { false, true }) using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w);
                var release = Json.Read<Release>(Json.Write(f.A.release));
                // The archive really is intact: only the selected release identity
                // differs. Keep its exact bytes, size and checksum in the cache.
                if (wrongBuild) release.buildId += "-wrong"; else release.id += "-wrong";
                release.Validate();
                var choices = Field<ComboBox>(w, "releases"); choices.Items.Add(release); choices.SelectedItem = release;
                Field<CheckBox>(w, "compatible").IsChecked = true;
                var bridge = Field<LauncherBridge>(w, "bridge"); var address = bridge.Address;
                string priorState = Json.Write(f.Store.State);
                string priorDiskState = File.ReadAllText(Path.Combine(f.Store.Root, "manager.json"));
                int priorWrites = f.Http.Writes.Count;
                Click(w, Field<Button>(w, "launchButton"));
                Check(Field<TextBox>(w, "details").Text.Contains("Package identity does not match selected release") &&
                    Field<Expander>(w, "feedbackPanel").IsExpanded, "mismatched intact archive did not show its identity rejection");
                Check(f.Http.Stops == 0 && !f.Http.Writes.Skip(priorWrites).Any() && f.Http.Launches == 0 && f.Downloads.Requests == 0,
                    "identity rejection stopped the working helper, wrote to it, launched or downloaded");
                Check(f.Store.Selected.folder == f.B.folder && Json.Write(f.Store.State) == priorState &&
                    File.ReadAllText(Path.Combine(f.Store.Root, "manager.json")) == priorDiskState,
                    "identity rejection changed the selected package, rollback state or persisted settings");
                Check(Field<bool>(w, "connectionReady") && bridge.Address == address && address != null,
                    "identity rejection disconnected the existing helper");
                // Returning to the retained package must need no repair/reconnect.
                choices.SelectedItem = f.B.release;
                Check(Field<Button>(w, "launchButton").IsEnabled &&
                    Convert.ToString(Field<Button>(w, "launchButton").Content) == new Strings()["launch"],
                    "identity rejection stranded the existing package instead of retaining Launch");
                Console.WriteLine("PASS WINDOW intact cached archive with wrong " + (wrongBuild ? "build" : "release") +
                    " identity is rejected before helper stop and retains selected package/connection (fake HTTP only)");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window; Connect(w); Field<CheckBox>(w, "risk").IsChecked = true;
                // Reproduce an actual install promotion failure after the helper has stopped.
                var failingStore = (PackageStore)Activator.CreateInstance(typeof(PackageStore), BindingFlags.NonPublic | BindingFlags.Instance, null,
                    new object[] { f.Store.Root, new Action<string, string>((from, to) => { throw new IOException("Fixture permanent promotion failure"); }) }, null);
                Set(w, "store", failingStore);
                Click(w, Button(w, "repair"));
                Check(f.Http.Stops == 1 && !Field<bool>(w, "connectionReady") && Field<LauncherBridge>(w, "bridge").Address == null,
                    "failed package promotion left a falsely connected state");
                Check(Field<Border>(w, "connectionPanel").Visibility == Visibility.Visible && Button(w, "retryConnection").IsEnabled &&
                    !Field<Button>(w, "launchButton").IsEnabled && !Button(w, "useSimulator").IsEnabled,
                    "failed installation hid retry or left live actions enabled");
                Check(failingStore.Selected.folder == f.B.folder && Field<TextBox>(w, "details").Text.Contains("Fixture permanent promotion failure"),
                    "failed promotion changed active selection or lost error evidence");
                Click(w, Button(w, "retryConnection"));
                Check(Field<bool>(w, "connectionReady") && Field<Button>(w, "launchButton").IsEnabled && String.IsNullOrWhiteSpace(Field<TextBox>(w, "details").Text),
                    "retry could not reconnect the preserved active package cleanly");
                Console.WriteLine("PASS WINDOW failed promotion clears stale readiness, preserves installed selection and provides a working Retry");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window;
                var work = (Task)Invoke(w, "Run", new Func<CancellationToken, Task>(c => Task.Delay(Timeout.Infinite, c)));
                Click(w, Field<Button>(w, "cancelButton"));
                work.GetAwaiter().GetResult();
                Check(!Field<bool>(w, "connectionReady") && Field<Border>(w, "connectionPanel").Visibility == Visibility.Visible && Button(w, "retryConnection").IsEnabled,
                    "cancelling before the first connection hid Retry");
                Click(w, Button(w, "retryConnection"));
                Check(Field<bool>(w, "connectionReady") && f.Http.Stops == 0 && f.Http.Launches == 0, "cancel/retry could not recover without game side effects");
                Console.WriteLine("PASS WINDOW cancellation before first connection leaves a visible working Retry");
            }
            using (var f = new Fixture(root))
            {
                var w = f.Window;
                var work = (Task)Invoke(w, "Run", new Func<CancellationToken, Task>(c => { throw new TaskCanceledException("Fixture transport timeout, not user cancellation"); }));
                PumpUntil(() => work.IsCompleted, "transport timeout feedback"); work.GetAwaiter().GetResult();
                Check(Field<TextBlock>(w, "operationText").Text.Contains(new Strings()["failed"]) &&
                    Field<TextBox>(w, "details").Text == new Strings()["requestInterrupted"] && Field<Expander>(w, "feedbackPanel").IsExpanded,
                    "transport timeout was mislabeled as user Cancelled or hidden");
                Check(Field<Button>(w, "cancelButton").Visibility == Visibility.Collapsed && Field<ProgressBar>(w, "progress").Visibility == Visibility.Collapsed,
                    "transport timeout left an endless spinner");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW transport cancellation is actionable failure; explicit user cancellation remains separate");
            }
            using (var f = new Fixture(root, false))
            {
                var w = f.Window; var pending = new TaskCompletionSource<ControllerDiagnostics.Report>(); int reads = 0;
                Set(w, "controllerProbeOverride", new Func<CancellationToken, Task<ControllerDiagnostics.Report>>(c => { Interlocked.Increment(ref reads); return pending.Task; }));
                Invoke(w, "ShowPage", true); Field<Expander>(w, "controllerPanel").IsExpanded = true;
                Check(!Button(w, "controllerCopy").IsEnabled && Button(w, "controllerRefresh").IsEnabled, "controller check requires an installed helper or exposes empty copy");
                Button(w, "controllerRefresh").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
                Check(Field<bool>(w, "controllerChecking") && Field<TextBlock>(w, "controllerState").Text == new Strings()["controllerChecking"] &&
                    !Button(w, "controllerRefresh").IsEnabled && !Button(w, "controllerCopy").IsEnabled, "controller check lacks its local pending state");
                Check(Field<CancellationTokenSource>(w, "operation") == null && Field<ComboBox>(w, "languages").IsEnabled && Button(w, "browse").IsEnabled,
                    "controller probe blocked the normal launcher flow");
                pending.SetResult(ControllerSnapshot()); PumpUntil(() => !Field<bool>(w, "controllerChecking"), "controller snapshot");
                Check(reads == 1 && Field<TextBox>(w, "controllerDetails").Text.Contains("Slot 0: connected") &&
                    Field<TextBox>(w, "controllerDetails").Text.Contains("Slot 3: not connected") && Button(w, "controllerCopy").IsEnabled,
                    "controller snapshot omitted Windows slot results");
                Field<ComboBox>(w, "languages").SelectedIndex = 1; Drain();
                Check(Field<Expander>(w, "controllerPanel").IsExpanded && Field<TextBox>(w, "controllerDetails").Text.Contains("槽位 0：已连接") && reads == 1,
                    "language rebuild lost or re-ran controller results");
                string copied = null; Set(w, "controllerCopyOverride", new Action<string>(value => copied = value));
                Click(w, Button(w, "controllerCopy"));
                Check(copied != null && copied.Contains("Reality Runner") && copied.Contains("槽位 3") && !copied.Contains("private-person") &&
                    !copied.Contains("secret-serial") && !copied.Contains(@"C:\") && f.Http.Writes.Count == 0 && f.Http.Launches == 0,
                    "controller report leaked identifiers, used a helper or changed the game");
                Offscreen(w);
                Console.WriteLine("PASS WINDOW controller snapshot works without helper, stays nonblocking, survives EN/zh and copies only redacted labels (fake probe/clipboard)");
            }
            using (var f = new Fixture(root, false))
            {
                var w = f.Window; var late = new TaskCompletionSource<ControllerDiagnostics.Report>();
                Set(w, "controllerTimeout", TimeSpan.FromMilliseconds(20));
                Set(w, "controllerProbeOverride", new Func<CancellationToken, Task<ControllerDiagnostics.Report>>(c => late.Task));
                Button(w, "controllerRefresh").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
                PumpUntil(() => !Field<bool>(w, "controllerChecking"), "controller timeout");
                Check(Field<TextBlock>(w, "controllerState").Text == new Strings()["controllerTimedOut"] &&
                    Button(w, "controllerRefresh").IsEnabled && Button(w, "controllerCopy").IsEnabled, "timeout stranded controller retry/report");
                string timeoutReport = null; Set(w, "controllerCopyOverride", new Action<string>(value => timeoutReport = value));
                Click(w, Button(w, "controllerCopy"));
                Check(timeoutReport.Contains(new Strings()["controllerTimedOut"]), "timeout report was unavailable");
                var fresh = ControllerSnapshot(); fresh.XInputStatus = "partial";
                Set(w, "controllerTimeout", TimeSpan.FromSeconds(10));
                Set(w, "controllerProbeOverride", new Func<CancellationToken, Task<ControllerDiagnostics.Report>>(c => Task.FromResult(fresh)));
                Button(w, "controllerRefresh").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
                PumpUntil(() => !Field<bool>(w, "controllerChecking"), "controller retry");
                late.SetResult(ControllerSnapshot()); Drain();
                Check(Object.ReferenceEquals(Field<ControllerDiagnostics.Report>(w, "controllerReport"), fresh) &&
                    Field<TextBox>(w, "controllerDetails").Text.Contains("partial results"), "late timed-out result overwrote the newer controller check");
                Console.WriteLine("PASS WINDOW controller timeout keeps retry/copy usable and rejects a late stale result (fake probe only)");
            }
            using (var f = new Fixture(root, false))
            {
                var w = f.Window;
                Set(w, "controllerProbeOverride", new Func<CancellationToken, Task<ControllerDiagnostics.Report>>(c =>
                    Task.FromException<ControllerDiagnostics.Report>(new IOException(@"C:\Users\private-person\secret-serial failure"))));
                Button(w, "controllerRefresh").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));
                PumpUntil(() => !Field<bool>(w, "controllerChecking"), "controller failure");
                Check(Field<TextBlock>(w, "controllerState").Text == new Strings()["controllerFailed"] &&
                    !Field<TextBox>(w, "controllerDetails").Text.Contains("private-person"), "controller exception leaked its raw message");
                Set(w, "controllerCopyOverride", new Action<string>(value => { throw new InvalidOperationException("Fixture clipboard busy"); }));
                Click(w, Button(w, "controllerCopy"));
                Check(Field<TextBlock>(w, "controllerState").Text == new Strings()["controllerCopyFailed"] && Button(w, "controllerCopy").IsEnabled,
                    "clipboard failure stranded the report or hid retry guidance");
                Console.WriteLine("PASS WINDOW controller probe and clipboard failures stay localized, private and retryable (fake probe/clipboard)");
            }
            Console.WriteLine("29 offscreen WPF checks passed; no window was shown and no helper/game process was started.");
        }
        finally { SynchronizationContext.SetSynchronizationContext(previousContext); }
    }
}
