using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Velopack;
using Velopack.Locators;
using Velopack.Logging;
using Velopack.Sources;
using WuWaVR.Manager;

// No updater process, public network, installed-app mutation or game is used.
public static class LauncherUpdateTests
{
    static void Check(bool value, string message) { if (!value) throw new Exception("Launcher update: " + message); }
    static void Wait(Task task) { task.GetAwaiter().GetResult(); }
    static void Reject<T>(Action action) where T : Exception
    { try { action(); } catch (T) { return; } throw new Exception("Expected " + typeof(T).Name); }
    internal sealed class Adapter : ILauncherUpdateAdapter
    {
        public bool Installed { get; set; } = true;
        public string CurrentVersion { get { return "1.0.0"; } }
        public int Checks, Downloads, Applies;
        public LauncherUpdate Next = new LauncherUpdate("1.1.0");
        public Func<CancellationToken, Task<LauncherUpdate>> OnCheck;
        public Func<CancellationToken, Task> OnDownload;
        public Action OnApply;
        public Task<LauncherUpdate> CheckAsync(CancellationToken c)
        { Checks++; return OnCheck == null ? Task.FromResult(Next) : OnCheck(c); }
        public Task DownloadAsync(LauncherUpdate update, Action<int> progress, CancellationToken c)
        { Downloads++; c.ThrowIfCancellationRequested(); progress?.Invoke(100); return OnDownload == null ? Task.CompletedTask : OnDownload(c); }
        public void ApplyAndRestart(LauncherUpdate update) { Applies++; OnApply?.Invoke(); }
    }
    static LauncherUpdateService Ready(Adapter adapter)
    {
        var service = new LauncherUpdateService(adapter);
        Wait(service.CheckAsync(CancellationToken.None)); Wait(service.DownloadAsync(null, CancellationToken.None)); return service;
    }
    static Task Idle(CancellationToken c) { c.ThrowIfCancellationRequested(); return Task.CompletedTask; }
    sealed class Helper : HttpMessageHandler
    {
        public string Root;
        public bool Offline, Busy, Unknown;
        public int Gets, Posts;
        public Action<string> OnRequest;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested(); Gets++;
            if (request.Method != HttpMethod.Get) { Posts++; throw new Exception("Read-only gate wrote to helper."); }
            OnRequest?.Invoke(request.RequestUri.AbsolutePath);
            if (Offline) throw new HttpRequestException("Fixture offline");
            object reply = request.RequestUri.AbsolutePath == "/api/identity" ?
                (object)new { app = "wuwa-vr-player-launcher", root = Root, pid = 123456 } :
                Unknown ? (object)new { gameRunning = false } : new { gameRunning = Busy, injectorRunning = false, job = new { running = false }, recording = new { running = false } };
            return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent(Json.Write(reply)) });
        }
    }
    static void WithGate(string root, Action<LauncherBridge, Helper, string> action, Func<string[]> names = null, Func<int, bool?> alive = null)
    {
        string data = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(data);
        var helper = new Helper { Root = Path.Combine(data, "unselected-package", "app") };
        string receipt = Path.Combine(data, "launcher.json");
        Json.Save(receipt, new { url = "http://127.0.0.1:34722/", app = helper.Root, pid = 123456 });
        using (var bridge = new LauncherBridge(data, null, helper, names ?? (() => new string[0]), alive ?? (pid => false)))
            action(bridge, helper, receipt);
    }

    sealed class LocalSource : IUpdateSource
    {
        public VelopackAsset Asset;
        public byte[] Package;
        public string Channel, AppId;
        public int Downloads;
        public Task<VelopackAssetFeed> GetReleaseFeed(IVelopackLogger log, string appId, string channel, Guid? stagingId, VelopackAsset latestLocalRelease)
        {
            AppId = appId; Channel = channel;
            return Task.FromResult(new VelopackAssetFeed { Assets = new[] { Asset } });
        }
        public Task DownloadReleaseEntry(IVelopackLogger log, VelopackAsset entry, string target, Action<int> progress, CancellationToken cancel)
        { cancel.ThrowIfCancellationRequested(); Downloads++; File.WriteAllBytes(target, Package); progress?.Invoke(100); return Task.CompletedTask; }
    }
    static LocalSource Feed()
    {
        byte[] bytes;
        using (var memory = new MemoryStream())
        {
            using (var zip = new ZipArchive(memory, ZipArchiveMode.Create, true))
            using (var writer = new StreamWriter(zip.CreateEntry("ChronoHaxx.WuWaVR.nuspec").Open()))
                writer.Write("<?xml version=\"1.0\"?><package><metadata><id>ChronoHaxx.WuWaVR</id><version>1.1.0</version><authors>Test</authors><description>Offline update fixture</description></metadata></package>");
            bytes = memory.ToArray();
        }
        using (var sha = SHA256.Create())
        using (var sha1 = SHA1.Create())
            return new LocalSource { Package = bytes, Asset = new VelopackAsset {
                PackageId = LauncherUpdateService.AppId, Version = SemanticVersion.Parse("1.1.0"), Type = VelopackAssetType.Full,
                FileName = "ChronoHaxx.WuWaVR-1.1.0-full.nupkg", Size = bytes.Length,
                SHA256 = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", ""),
                SHA1 = BitConverter.ToString(sha1.ComputeHash(bytes)).Replace("-", "") } };
    }
    static VelopackLauncherUpdateAdapter RealAdapter(string root, LocalSource source)
    {
        string packages = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(packages);
        var locator = new TestVelopackLocator(LauncherUpdateService.AppId, "1.0.0", packages);
        return new VelopackLauncherUpdateAdapter(new UpdateManager(source, VelopackLauncherUpdateAdapter.Options(), locator));
    }
    sealed class WebFixture : IFileDownloader
    {
        public LocalSource Feed = LauncherUpdateTests.Feed();
        public string FeedRequest, DownloadRequest;
        public const string PackageUrl = "https://github.com/ChronoHaxx/wuwa-vr/releases/download/launcher-v1.1.0/ChronoHaxx.WuWaVR-1.1.0-full.nupkg";
        public Task<string> DownloadString(string url, IDictionary<string, string> headers, double timeout)
        {
            FeedRequest = url; var a = Feed.Asset;
            return Task.FromResult(Json.Write(new { Assets = new[] { new {
                a.PackageId, Version = "1.1.0", Type = "Full", FileName = PackageUrl, a.SHA1, a.SHA256, a.Size } } }));
        }
        public Task<byte[]> DownloadBytes(string url, IDictionary<string, string> headers, double timeout)
        { throw new Exception("Unexpected fixture bytes request"); }
        public Task DownloadFile(string url, string target, Action<int> progress, IDictionary<string, string> headers, double timeout, CancellationToken cancel)
        { cancel.ThrowIfCancellationRequested(); DownloadRequest = url; File.WriteAllBytes(target, Feed.Package); return Task.CompletedTask; }
    }

    public static void Run(string root)
    {
        int passed = 0;
        Action<string, Action> test = (name, action) => { action(); passed++; Console.WriteLine("PASS UPDATE " + name); };
        test("inert and unpackaged launchers never check or download", () => {
            using (var inert = new LauncherUpdateService()) { Wait(inert.CheckAsync(CancellationToken.None)); Check(!inert.Installed && !inert.CheckedOnce, "inert service checked"); }
            var fake = new Adapter { Installed = false };
            using (var service = new LauncherUpdateService(fake)) { Wait(service.CheckAsync(CancellationToken.None)); Check(fake.Checks == 0, "uninstalled checked"); }
        });
        test("offline check preserves available update and downloaded update never auto-applies", () => {
            var fake = new Adapter(); using (var service = new LauncherUpdateService(fake)) {
                Wait(service.CheckAsync(CancellationToken.None));
                fake.OnCheck = c => { throw new IOException("offline"); };
                Reject<IOException>(() => Wait(service.CheckAsync(CancellationToken.None)));
                Check(service.AvailableVersion == "1.1.0" && service.CheckFailed, "outage erased candidate");
                Wait(service.DownloadAsync(null, CancellationToken.None));
                Wait(service.CheckAsync(CancellationToken.None));
                Check(service.ReadyToRestart && fake.Applies == 0 && fake.Checks == 2, "download/check applied or replaced candidate");
            }
            Check(fake.Applies == 0, "dispose auto-applied");
        });
        test("cancelled or failed download cannot become ready; retry works", () => {
            var fake = new Adapter(); using (var service = new LauncherUpdateService(fake)) {
                Wait(service.CheckAsync(CancellationToken.None));
                fake.OnDownload = c => { throw new InvalidDataException("bad checksum"); };
                Reject<InvalidDataException>(() => Wait(service.DownloadAsync(null, CancellationToken.None)));
                Check(!service.ReadyToRestart, "bad download ready");
                using (var cancel = new CancellationTokenSource()) {
                    fake.OnDownload = c => { cancel.Cancel(); return Task.CompletedTask; };
                    Reject<OperationCanceledException>(() => Wait(service.DownloadAsync(null, cancel.Token)));
                }
                Check(!service.ReadyToRestart && fake.Applies == 0, "cancelled download ready");
                fake.OnDownload = null; Wait(service.DownloadAsync(null, CancellationToken.None)); Check(service.ReadyToRestart, "retry failed");
            }
        });
        test("cancelled late check cannot publish state", () => {
            var pending = new TaskCompletionSource<LauncherUpdate>();
            var fake = new Adapter { OnCheck = c => pending.Task };
            using (var service = new LauncherUpdateService(fake)) using (var cancel = new CancellationTokenSource()) {
                var task = service.CheckAsync(cancel.Token); cancel.Cancel(); pending.SetResult(new LauncherUpdate("9.9.9"));
                Reject<OperationCanceledException>(() => Wait(task)); Check(service.AvailableVersion == null && !service.Busy, "late result published");
            }
        });
        test("busy fresh gate blocks restart without discarding verified download", () => {
            var fake = new Adapter(); using (var service = Ready(fake)) {
                int saves = 0;
                Reject<InvalidOperationException>(() => Wait(service.RestartAsync(c => { throw new InvalidOperationException("busy"); }, () => saves++, CancellationToken.None)));
                Check(saves == 0 && fake.Applies == 0 && service.ReadyToRestart, "busy path mutated/applied/discarded");
            }
        });
        test("state flush failure and new activity after save block apply", () => {
            var fake = new Adapter(); using (var service = Ready(fake)) {
                Reject<IOException>(() => Wait(service.RestartAsync(Idle, () => { throw new IOException("state locked"); }, CancellationToken.None)));
                int gates = 0;
                Reject<InvalidOperationException>(() => Wait(service.RestartAsync(c => { if (++gates == 2) throw new InvalidOperationException("game started"); return Task.CompletedTask; }, () => { }, CancellationToken.None)));
                Check(gates == 2 && fake.Applies == 0 && service.ReadyToRestart, "fresh post-save gate skipped");
            }
        });
        test("cancellation after the fresh idle check prevents apply", () => {
            var fake = new Adapter(); using (var service = Ready(fake)) using (var cancel = new CancellationTokenSource()) {
                int gates = 0;
                Reject<OperationCanceledException>(() => Wait(service.RestartAsync(c => {
                    if (++gates == 2) cancel.Cancel(); return Task.CompletedTask;
                }, () => { }, cancel.Token)));
                Check(gates == 2 && fake.Applies == 0 && service.ReadyToRestart, "cancelled restart applied");
            }
        });
        test("explicit restart saves once, rechecks idle and preserves unrelated settings bytes", () => {
            string dir = Path.Combine(root, "update-preserve"); Directory.CreateDirectory(dir);
            var store = new PackageStore(Path.Combine(dir, "manager")); store.State.language = "zh-Hans"; store.State.launcherPath = "saved-game-path"; store.Save();
            string backend = Path.Combine(dir, "settings.json"), profile = Path.Combine(dir, "UEVR-user-settings.ini");
            File.WriteAllText(backend, "{\"personal\":true}"); File.WriteAllText(profile, "UserSetting=Keep\r\n");
            byte[] before = File.ReadAllBytes(backend), beforeProfile = File.ReadAllBytes(profile), managerBefore = File.ReadAllBytes(Path.Combine(store.Root, "manager.json"));
            var fake = new Adapter(); using (var service = Ready(fake)) {
                int saves = 0, gates = 0;
                fake.OnApply = () => Check(gates == 2 && saves == 1, "apply before flush and fresh gate");
                Wait(service.RestartAsync(c => { gates++; return Task.CompletedTask; }, () => { store.Save(); saves++; }, CancellationToken.None));
                Check(fake.Applies == 1 && File.ReadAllBytes(backend).SequenceEqual(before) && File.ReadAllBytes(profile).SequenceEqual(beforeProfile) &&
                    File.ReadAllBytes(Path.Combine(store.Root, "manager.json")).SequenceEqual(managerBefore), "update changed settings");
            }
        });
        test("gate checks native game without a selected mod or helper receipt", () => {
            foreach (string name in new[] { "Client-Win64-Shipping", "Wuthering Waves", "custom_uevr_injector", "uevrinjector.exe" })
                WithGate(root, (bridge, helper, receipt) => { File.Delete(receipt); Reject<InvalidOperationException>(() => Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None))); Check(helper.Gets == 0, "queried missing helper"); }, () => new[] { name });
        });
        test("gate accepts idle unselected helper read-only and refuses busy or unknown", () => WithGate(root, (bridge, helper, receipt) => {
            Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None)); Check(helper.Posts == 0 && bridge.Address == null, "gate attached or stopped helper");
            helper.Busy = true; Reject<InvalidOperationException>(() => Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None)));
            helper.Busy = false; helper.Unknown = true; Reject<InvalidOperationException>(() => Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None)));
        }));
        test("gate refuses receipt race and live or unknown unreachable helper", () => {
            WithGate(root, (bridge, helper, receipt) => {
                helper.OnRequest = path => { if (path == "/api/status") Json.Save(receipt, new { pid = 777 }); };
                Reject<InvalidOperationException>(() => Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None)));
            });
            foreach (bool? alive in new bool?[] { true, null })
                WithGate(root, (bridge, helper, receipt) => { helper.Offline = true; Reject<InvalidOperationException>(() => Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None))); }, alive: pid => alive);
            WithGate(root, (bridge, helper, receipt) => { helper.Offline = true; Wait(bridge.RequireLauncherUpdateIdle(CancellationToken.None)); });
        });
        test("real SDK adapter uses explicit beta channel and verifies local package", () => {
            var feed = Feed(); var adapter = RealAdapter(root, feed); using (var service = new LauncherUpdateService(adapter)) {
                Wait(service.CheckAsync(CancellationToken.None));
                Check(service.Installed && service.AvailableVersion == "1.1.0" && feed.AppId == LauncherUpdateService.AppId && feed.Channel == "win-beta", "SDK source/channel mismatch");
                Wait(service.DownloadAsync(null, CancellationToken.None)); Check(service.ReadyToRestart && feed.Downloads == 1, "SDK download not verified");
            }
            Check(VelopackLauncherUpdateAdapter.Source().BaseUri.AbsoluteUri == LauncherUpdateService.FeedUrl, "stable public feed changed");
        });
        test("real SimpleWebSource follows absolute GitHub package URL and keeps cache inside package directory", () => {
            var web = new WebFixture(); string packages = Path.Combine(root, "web-source-packages"); Directory.CreateDirectory(packages);
            var locator = new TestVelopackLocator(LauncherUpdateService.AppId, "1.0.0", packages);
            var adapter = new VelopackLauncherUpdateAdapter(new UpdateManager(VelopackLauncherUpdateAdapter.Source(web), VelopackLauncherUpdateAdapter.Options(), locator));
            using (var service = new LauncherUpdateService(adapter)) {
                Wait(service.CheckAsync(CancellationToken.None)); Wait(service.DownloadAsync(null, CancellationToken.None));
                Check(new Uri(web.FeedRequest).AbsolutePath == "/wuwa-vr/launcher-updates/releases.win-beta.json", "wrong channel feed URL");
                Check(web.DownloadRequest == WebFixture.PackageUrl && service.ReadyToRestart, "absolute GitHub URL not downloaded and verified");
                Check(Directory.GetFiles(packages, "*.nupkg").Length == 1, "package escaped or missed local cache");
            }
        });
        test("real SDK checksum failure never exposes restart", () => {
            var feed = Feed(); feed.Package = Encoding.UTF8.GetBytes("corrupt fixture package");
            using (var service = new LauncherUpdateService(RealAdapter(root, feed))) {
                Wait(service.CheckAsync(CancellationToken.None)); bool failed = false;
                try { Wait(service.DownloadAsync(null, CancellationToken.None)); } catch (Exception) { failed = true; }
                Check(failed && !service.ReadyToRestart, "SDK accepted corrupt package");
            }
        });
        test("real SDK adapter rejects wrong application and ignores older versions", () => {
            var feed = Feed(); feed.Asset.PackageId = "Unrelated.App";
            using (var service = new LauncherUpdateService(RealAdapter(root, feed))) {
                bool rejected = false; try { Wait(service.CheckAsync(CancellationToken.None)); } catch (InvalidDataException) { rejected = true; }
                Check(rejected || service.AvailableVersion == null, "wrong app accepted");
            }
            feed = Feed(); feed.Asset.Version = SemanticVersion.Parse("0.9.0");
            using (var service = new LauncherUpdateService(RealAdapter(root, feed))) { Wait(service.CheckAsync(CancellationToken.None)); Check(service.AvailableVersion == null, "downgrade offered"); }
        });
        WindowChecks(root, test);
        Console.WriteLine(passed + " launcher update groups passed (mocked and local SDK; no installed app apply).");
    }

    const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    static T Field<T>(MainWindow w, string name) { return (T)typeof(MainWindow).GetField(name, Private).GetValue(w); }
    static void Set(MainWindow w, string name, object value) { typeof(MainWindow).GetField(name, Private).SetValue(w, value); }
    static void Pump(Task task)
    {
        if (!task.IsCompleted) {
            var frame = new DispatcherFrame(); var started = DateTime.UtcNow;
            var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
            timer.Tick += (s, e) => { if (task.IsCompleted || DateTime.UtcNow - started > TimeSpan.FromSeconds(5)) frame.Continue = false; };
            timer.Start(); try { Dispatcher.PushFrame(frame); } finally { timer.Stop(); }
        }
        Check(task.IsCompleted, "offscreen operation timed out"); Wait(task);
    }
    static Task Call(MainWindow w, string method, params object[] args)
    {
        return (Task)typeof(MainWindow).GetMethod(method, Private | BindingFlags.DeclaredOnly, null,
            args.Select(a => a.GetType()).ToArray(), null).Invoke(w, args);
    }
    static void WindowChecks(string root, Action<string, Action> test)
    {
        test("offscreen update UI retains language and requires explicit restart consent", () => {
            var fake = new Adapter(); var service = new LauncherUpdateService(fake);
            string dir = Path.Combine(root, "update-window");
            var w = new MainWindow(new PackageStore(dir), Path.Combine(dir, "backend"), false, "en", service);
            try {
                Pump(Call(w, "CheckLauncherUpdates", CancellationToken.None, true));
                Check(Field<TextBlock>(w, "launcherVersion").Text.Contains("1.1.0") && Field<Button>(w, "launcherUpdateButton").Visibility == Visibility.Visible, "available update hidden");
                Pump(Call(w, "UpdateLauncher", CancellationToken.None));
                Set(w, "confirmationOverride", new Func<string, bool>(key => false));
                Pump(Call(w, "UpdateLauncher", CancellationToken.None)); Check(fake.Applies == 0 && service.ReadyToRestart, "declined restart applied");
                Field<Strings>(w, "text").Language = "zh-Hans";
                typeof(MainWindow).GetMethod("Render", Private | BindingFlags.DeclaredOnly, null, Type.EmptyTypes, null).Invoke(w, null);
                Check(Field<Button>(w, "launcherUpdateButton").Content.ToString() == new Strings { Language = "zh-Hans" }["restartLauncher"], "language change lost ready state");
                Check(!w.IsVisible && PresentationSource.FromVisual(w) == null, "offscreen test became visible");
            } finally { w.Close(); }
        });
        test("preview never checks even when supplied an installed adapter", () => {
            var fake = new Adapter(); var service = new LauncherUpdateService(fake);
            string dir = Path.Combine(root, "update-preview");
            var w = new MainWindow(new PackageStore(dir), Path.Combine(dir, "backend"), true, "en", service);
            try { Pump(Call(w, "CheckLauncherUpdates", CancellationToken.None, false)); Check(fake.Checks == 0, "preview network adapter invoked"); }
            finally { w.Close(); service.Dispose(); }
        });
    }
}
