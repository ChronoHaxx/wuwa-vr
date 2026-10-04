using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using WuWaVR.Manager;

// Real portable Python/PowerShell helper and real WPF events, fully isolated data.
// No fake server. No window shown. No launch/apply/runtime/recording request is sent.
public static class EndToEndTests
{
    const BindingFlags Fields = BindingFlags.NonPublic | BindingFlags.Instance;
    static T Get<T>(object owner, string name) { return (T)owner.GetType().GetField(name, Fields).GetValue(owner); }
    static void Set(object owner, string name, object value) { owner.GetType().GetField(name, Fields).SetValue(owner, value); }
    static void Check(bool value, string message) { if (!value) throw new Exception("E2E: " + message); }
    static void Pump(Func<bool> done)
    {
        if (done()) return;
        var frame = new DispatcherFrame(); var started = Stopwatch.StartNew();
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(20) };
        timer.Tick += (s, e) => { if (done() || started.Elapsed.TotalSeconds > 45) frame.Continue = false; };
        timer.Start(); try { Dispatcher.PushFrame(frame); } finally { timer.Stop(); }
        Check(done(), "operation exceeded 45 seconds");
    }
    static void Await(Task task) { Pump(() => task.IsCompleted); task.GetAwaiter().GetResult(); }
    static T Await<T>(Task<T> task) { Await((Task)task); return task.GetAwaiter().GetResult(); }
    static void Idle(MainWindow window)
    {
        Pump(() => Get<CancellationTokenSource>(window, "operation") == null && !Get<bool>(window, "polling"));
        Get<DispatcherTimer>(window, "poll").Stop();
        Check(!window.IsVisible && PresentationSource.FromVisual(window) == null, "window acquired a desktop surface");
    }
    static Button Button(MainWindow w, string key) { return Get<List<Button>>(w, "actions").Single(b => (string)b.Tag == key); }
    static void Click(MainWindow w, string key)
    {
        var button = Button(w, key); Check(button.IsEnabled, "disabled action " + key);
        button.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent)); Idle(w);
    }
    static MainWindow Window(PackageStore store, string data, string roaming)
    {
        var w = new MainWindow(store, data, false, "en");
        Get<LauncherBridge>(w, "bridge").Dispose();
        Set(w, "bridge", new LauncherBridge(data, roaming));
        return w;
    }
    static void Load(MainWindow w) { w.RaiseEvent(new RoutedEventArgs(FrameworkElement.LoadedEvent)); Idle(w); }
    public static int Run(string folder, string archive)
    {
        var previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DispatcherSynchronizationContext());
        Directory.CreateDirectory(folder);
        var store = new PackageStore(Path.Combine(folder, "manager"));
        string data = Path.Combine(folder, "backend"), roaming = Path.Combine(folder, "roaming");
        MainWindow window = null;
        LauncherBridge old = null;
        try
        {
            var release = RepoClient.BundledCatalog().releases[0];
            Directory.CreateDirectory(store.Cache);
            File.Copy(archive, Path.Combine(store.Cache, release.sha256.ToLowerInvariant() + ".zip"));
            var a = store.Install(archive, release, CancellationToken.None);
            var b = store.Install(archive, release, CancellationToken.None);
            var marker = new { gameStart = "manual", gameLauncher = "", retainedTestChoice = "preserve-me" };
            Json.Save(Path.Combine(data, "settings.json"), marker);
            string settingsBefore = File.ReadAllText(Path.Combine(data, "settings.json"));
            old = new LauncherBridge(data, roaming);
            Await(old.Connect(store.Folder(a), CancellationToken.None));
            var initial = Await(old.Status(CancellationToken.None));
            Check(!Json.Flag(initial, "gameRunning") && !Json.Flag(initial, "injectorRunning"), "a real game/injector is active; no helper will be stopped");
            string oldAddress = old.Address;
            window = Window(store, data, roaming); Load(window);
            var bridge = Get<LauncherBridge>(window, "bridge");
            Check(bridge.Conflict != null && bridge.Address == null, "second package did not reproduce real helper conflict");
            Check(Get<Border>(window, "connectionPanel").Visibility == Visibility.Visible, "conflict recovery is not visible");
            Check(!Button(window, "launch").IsEnabled, "launch enabled with wrong helper");
            window.SavePreview(Path.Combine(folder, "01-conflict.png"));
            Get<ComboBox>(window, "languages").SelectedIndex = 1;
            window.SavePreview(Path.Combine(folder, "01-conflict-zh.png"));
            Get<ComboBox>(window, "languages").SelectedIndex = 0;
            Console.WriteLine("PASS E2E actual helper A blocks B with visible recovery; no misleading install or launch option");

            string opened = null; Set(window, "openOverride", new Action<string>(url => opened = url));
            Click(window, "openExisting"); Check(opened == oldAddress, "existing launcher opens wrong URL");
            Click(window, "switchLauncher");
            Check(bridge.Conflict == null && Get<bool>(window, "connectionReady"), "real stop/reconnect button did not recover: " + Get<TextBlock>(window, "operationText").Text);
            Check(bridge.AppRoot == Path.GetFullPath(Path.Combine(store.Folder(b), "app")), "connected to wrong installed package");
            Check(Get<Border>(window, "connectionPanel").Visibility == Visibility.Collapsed, "resolved conflict still shown");
            Check(String.IsNullOrWhiteSpace(Get<TextBox>(window, "details").Text) && !Get<Expander>(window, "feedbackPanel").IsExpanded, "resolved connection error is still displayed");
            Check(!String.IsNullOrEmpty(Get<string>(window, "lastNativeError")), "recovery discarded diagnostic evidence");
            Check(File.ReadAllText(Path.Combine(data, "settings.json")) == settingsBefore, "handover rewrote saved game choice");
            Check(!File.Exists(Path.Combine(data, "launch-state.json")), "handover dispatched a launch");
            Get<CheckBox>(window, "risk").IsChecked = true;
            Check(Button(window, "launch").IsEnabled, "actual recovered helper still cannot reach launch action");
            window.SavePreview(Path.Combine(folder, "02-recovered.png"));
            Console.WriteLine("PASS E2E actual WPF recovery stops A gracefully, starts B and preserves settings; Launch becomes available");

            Click(window, "check");
            // Wait for real PowerShell readiness, without applying the profile.
            var deadline = DateTime.UtcNow.AddSeconds(30);
            while (DateTime.UtcNow < deadline)
            {
                Await((Task)window.GetType().GetMethod("RefreshStatus", Fields).Invoke(window, null));
                if (!Json.Flag(Json.Child(Get<Dictionary<string, object>>(window, "status"), "job"), "running")) break;
                Await(Task.Delay(150));
            }
            var job = Json.Child(Get<Dictionary<string, object>>(window, "status"), "job");
            Check(!Json.Flag(job, "running") && !Json.Flag(job, "error"), "real readiness failed: " + Json.Text(job, "output"));
            Check(Get<Expander>(window, "feedbackPanel").IsExpanded && !String.IsNullOrWhiteSpace(Get<TextBox>(window, "details").Text), "readiness result not presented");
            Console.WriteLine("PASS E2E real PowerShell readiness returns its actual result in the native UI: " + Json.Text(job, "output"));

            string bAddress = bridge.Address;
            window.Close(); window = Window(new PackageStore(store.Root), data, roaming); Load(window);
            bridge = Get<LauncherBridge>(window, "bridge");
            Check(Get<bool>(window, "connectionReady") && bridge.Address == bAddress, "closing/reopening did not reuse matching helper");
            Console.WriteLine("PASS E2E close/reopen reconnects matching real helper without a phantom conflict");

            // Exercise shipped Python/modules. A fixture Blocked result never
            // masquerades as a real headset PASS, even in an exported report.
            var startBody = new { event_id = "isolated-e2e-session", build_id = release.buildId, language = "zh-Hans" };
            var playtest = Await(bridge.Post("/api/playtest/start", startBody, CancellationToken.None));
            var session = Json.Child(playtest, "session");
            string playtestId = Json.Text(session, "session_id");
            Check(playtestId.Length == 32 && Json.Text(Json.Child(session, "build"), "id") == release.buildId,
                "packaged playtest did not pin this release");
            var resultBody = new { session_id = playtestId, item_id = "stereo", status = "blocked",
                note = "后台隔离测试；未运行游戏或头显。", event_id = "isolated-e2e-result" };
            Await(bridge.Post("/api/playtest/result", resultBody, CancellationToken.None));
            playtest = Await(bridge.Post("/api/playtest/result", resultBody, CancellationToken.None));
            session = Json.Child(playtest, "session");
            var events = ((System.Collections.IEnumerable)session["events"]).Cast<object>().Select(Json.Object).ToList();
            Check(events.Count(e => Json.Text(e, "event_id") == "isolated-e2e-result") == 1,
                "packaged retry duplicated the saved result");
            playtest = Await(bridge.Post("/api/playtest/finish", new { session_id = playtestId, event_id = "isolated-e2e-finish" }, CancellationToken.None));
            session = Json.Child(playtest, "session");
            var checks = ((System.Collections.IEnumerable)session["checks"]).Cast<object>().Select(Json.Object).ToList();
            Check(Json.Text(session, "state") == "finished" && checks.Count == 6 &&
                checks.Where(c => Json.Text(c, "id") != "stereo").All(c => Json.Text(c, "status") == "not_tested") &&
                checks.Single(c => Json.Text(c, "id") == "stereo")["status"].ToString() == "blocked",
                "packaged session changed untested checks or could not finish");
            Check(!Directory.EnumerateFiles(folder, "*.wav", SearchOption.AllDirectories).Any() &&
                !Directory.Exists(Path.Combine(roaming, "UnrealVRMod", "Client-Win64-Shipping", "playtest-control")),
                "idle packaged playtest produced audio or game-profile bridge files");
            Console.WriteLine("PASS E2E shipped Python creates a build-bound Chinese session, saves one idempotent Blocked result and finishes with other checks Not tested; no audio/game profile");

            using (var external = new LauncherBridge(data, roaming))
            {
                Await(external.Connect(store.Folder(b), CancellationToken.None));
                Await(external.Stop(CancellationToken.None));
            }
            Await((Task)window.GetType().GetMethod("RefreshStatus", Fields).Invoke(window, null));
            Check(!Get<bool>(window, "connectionReady") && Get<Border>(window, "connectionPanel").Visibility == Visibility.Visible, "stopped helper left stale healthy state");
            Check(!Button(window, "launch").IsEnabled, "stopped helper still offers launch");
            Click(window, "retryConnection");
            Check(Get<bool>(window, "connectionReady") && bridge.Conflict == null, "retry failed to restart stopped helper");
            Check(String.IsNullOrWhiteSpace(Get<TextBox>(window, "details").Text) && !Get<Expander>(window, "feedbackPanel").IsExpanded, "retry left stale failure details visible");
            Check(File.ReadAllText(Path.Combine(data, "settings.json")) == settingsBefore, "retry changed saved settings");
            Console.WriteLine("PASS E2E externally stopped helper is visibly disconnected; Retry starts correct version with saved choices intact");
            playtest = Await(bridge.Post("/api/playtest/start", startBody, CancellationToken.None));
            Check(Json.Text(Json.Child(playtest, "session"), "session_id") == playtestId &&
                Json.Text(Json.Child(playtest, "session"), "state") == "finished", "packaged playtest was not durable across helper restart");
            Console.WriteLine("PASS E2E completed playtest survives actual helper restart and idempotent start retry");
            Await(bridge.Stop(CancellationToken.None));
            window.Close(); window = null;
            var executable = Path.Combine(store.Folder(b), "python", "pythonw.exe");
            File.Move(executable, executable + ".disabled");
            try
            {
                window = Window(new PackageStore(store.Root), data, roaming); Load(window);
                Check(!Get<bool>(window, "connectionReady") && Button(window, "repair").IsEnabled, "real broken active package cannot be repaired");
                Click(window, "repair");
                var repairedStore = Get<PackageStore>(window, "store");
                bridge = Get<LauncherBridge>(window, "bridge");
                Check(Get<bool>(window, "connectionReady") && repairedStore.Selected.folder != b.folder && repairedStore.State.previous == b.folder,
                    "actual active package repair failed or lost previous selection");
                Check(File.ReadAllText(Path.Combine(data, "settings.json")) == settingsBefore && !File.Exists(Path.Combine(data, "launch-state.json")), "repair changed saved game setup or launched the game");
                Check(String.IsNullOrWhiteSpace(Get<TextBox>(window, "details").Text), "successful repair still shows missing-file failure");
                window.SavePreview(Path.Combine(folder, "03-repaired.png"));
                Console.WriteLine("PASS E2E actual missing-python active package repaired from verified local ZIP; fresh helper ready, saved settings and rollback retained");
                Await(bridge.Stop(CancellationToken.None));
            }
            finally { File.Move(executable + ".disabled", executable); }
            Console.WriteLine("PASS E2E cleanup: real isolated helper stopped. No foreground window, game launch, injection, runtime switch or profile apply.");
            return 0;
        }
        catch (Exception e) { Console.WriteLine(e); return 1; }
        finally
        {
            // Only attempt graceful cleanup of known isolated helpers. Never kill a process.
            if (window != null)
            {
                var current = Get<LauncherBridge>(window, "bridge");
                try
                {
                    if (current.Address != null) Await(current.Stop(CancellationToken.None));
                    else if (current.Conflict != null) Await(current.StopConflict(CancellationToken.None));
                }
                catch (Exception e) { Console.WriteLine("Cleanup refused: " + e.Message); }
                window.Close();
            }
            if (old != null) old.Dispose();
            SynchronizationContext.SetSynchronizationContext(previousContext);
        }
    }
}
