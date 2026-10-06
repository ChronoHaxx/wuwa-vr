using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using WuWaVR.Manager;

public static class UninstallTests
{
    static int count;
    static void Check(bool value, string label) { if (!value) throw new Exception("Uninstall: " + label); count++; Console.WriteLine("PASS UNINSTALL " + label); }
    sealed class Idle : IDisposable { public void Dispose() { } }
    sealed class Fixture
    {
        public string Manager, Data, Folder, Payload, Cache, Unknown;
        public PackageStore Store;
        public Installed Item;
        public LauncherUninstall.EnvironmentHooks Hooks;
        public int Stops;
        public Fixture(string root)
        {
            Manager = Path.Combine(root, Guid.NewGuid().ToString("N"), "manager"); Data = Path.Combine(Path.GetDirectoryName(Manager), "personal");
            Store = new PackageStore(Manager);
            var release = new Release { id = "candidate-uninstall-test", buildId = "fixture", gameVersion = "3.7", channel = "candidate",
                created = DateTime.UtcNow.ToString("o"), size = 1, sha256 = new string('a', 64) };
            Item = new Installed { release = release, folder = "candidate-uninstall-test-abc", installedUtc = DateTime.UtcNow.ToString("o") };
            Store.State.installed.Add(Item); Store.State.selected = Item.folder; Store.State.previous = Item.folder;
            Store.State.launcherPath = "preserve this saved installation"; Store.State.language = "zh-Hans"; Store.Save();
            Folder = Store.Folder(Item); Directory.CreateDirectory(Path.Combine(Folder, "app"));
            Payload = Path.Combine(Folder, "app", "owned.dll"); File.WriteAllText(Payload, "inert original package bytes");
            var portable = Path.Combine(Folder, "app", "portable.json");
            Json.Save(portable, new { packageId = "wuwa-vr-launcher-" + release.id, defaultBuild = "fixture" });
            Json.Save(Path.Combine(Folder, "manifest.json"), new { packageId = "wuwa-vr-launcher-" + release.id,
                files = new Dictionary<string,string> { { "app/owned.dll", RepoClient.Hash(Payload) }, { "app/portable.json", RepoClient.Hash(portable) } } });
            Directory.CreateDirectory(Store.Cache);
            string temp = Path.Combine(Store.Cache, "temp"); File.WriteAllText(temp, "inert complete ZIP");
            Cache = Path.Combine(Store.Cache, RepoClient.Hash(temp) + ".zip"); File.Move(temp, Cache);
            Directory.CreateDirectory(Path.Combine(Data, "recordings")); Directory.CreateDirectory(Path.Combine(Data, "backups"));
            File.WriteAllText(Path.Combine(Data, "recordings", "keep.mp4"), "personal recording");
            File.WriteAllText(Path.Combine(Data, "backups", "keep.txt"), "personal backup");
            File.WriteAllText(Path.Combine(Data, "settings.json"), "personal settings");
            Unknown = Path.Combine(Folder, "my-notes.txt");
            Hooks = new LauncherUninstall.EnvironmentHooks { StopHelper = (store, data, cancel) => { Stops++; },
                AcquireIdleGate = () => new Idle(), ActiveRuntimes = () => new string[0] };
        }
        public UninstallResult Run(CancellationToken token = default(CancellationToken))
        { return LauncherUninstall.Cleanup(Manager, Data, Hooks, token); }
        public bool PersonalKept() { return File.Exists(Path.Combine(Data, "recordings", "keep.mp4")) &&
            File.Exists(Path.Combine(Data, "backups", "keep.txt")) && File.Exists(Path.Combine(Data, "settings.json")); }
    }
    public static void Run(string testRoot)
    {
        count = 0; string root = Path.Combine(testRoot, "uninstall-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
        var f = new Fixture(root); var result = f.Run();
        var saved = new PackageStore(f.Manager).State;
        Check(result.Success && result.RemovedFiles == 4 && !Directory.Exists(f.Folder) && !File.Exists(f.Cache) && f.Stops == 1,
            "confirmed cleanup removes only known package bytes and verified download after cooperative stop");
        Check(f.PersonalKept() && saved.installed.Count == 0 && saved.selected == null && saved.previous == null &&
            saved.language == "zh-Hans" && saved.launcherPath == "preserve this saved installation", "recordings backups settings and manager preferences survive");
        Check(File.Exists(result.ReportPath) && File.ReadAllText(result.ReportPath).Contains("No game, injector") &&
            File.ReadAllText(result.ReportPath).Contains(f.Data), "readable result reports retained personal data and no forced process stop");
        f = new Fixture(root); File.WriteAllText(f.Unknown, "keep this user file"); result = f.Run();
        Check(File.Exists(f.Unknown) && !File.Exists(f.Payload) && !result.Success && result.RetainedReasons.Any(r => r.Contains("unlisted player")),
            "unknown files remain visible as retained rather than reporting full removal");
        f = new Fixture(root); f.Hooks.ActiveRuntimes = () => new[] { Path.Combine(f.Folder, "app", "openxr.json") }; result = f.Run();
        Check(File.Exists(f.Payload) && !File.Exists(f.Cache) && !result.Success && result.RetainedReasons.Any(r => r.Contains("active OpenXR")),
            "active runtime package remains while safe download cache is removed without registry changes");
        f = new Fixture(root); f.Hooks.ActiveRuntimes = () => { throw new UnauthorizedAccessException("registry denied"); }; result = f.Run();
        Check(File.Exists(f.Payload) && !File.Exists(f.Cache) && !result.Success && result.RetainedReasons.Any(r => r.Contains("registration could not be checked")),
            "unknown runtime registration retains all versions");
        f = new Fixture(root); f.Hooks.StopHelper = (s,d,c) => { throw new InvalidOperationException("recording or unknown worker active"); }; result = f.Run();
        Check(result.RemovedFiles == 0 && File.Exists(f.Payload) && File.Exists(f.Cache) && !result.Success && f.PersonalKept(),
            "busy recording or unverified helper never causes deletion or forced termination");
        f = new Fixture(root); f.Hooks.AcquireIdleGate = () => { throw new InvalidOperationException("startup mutex held"); }; result = f.Run();
        Check(result.RemovedFiles == 0 && !result.Success && File.Exists(f.Payload), "held startup profile lock refuses cleanup");
        f = new Fixture(root);
        var manifest = Json.Read<PackageManifest>(File.ReadAllText(Path.Combine(f.Folder,"manifest.json")));
        manifest.files["../escape.txt"] = new string('a',64); Json.Save(Path.Combine(f.Folder,"manifest.json"),manifest); result = f.Run();
        Check(File.Exists(f.Payload) && !result.Success && result.RetainedReasons.Any(r => r.Contains("Unsafe package path")),
            "all manifest paths are checked before any version file is deleted");
        f = new Fixture(root);
        Json.Save(Path.Combine(f.Folder,"app","portable.json"),new { packageId = "foreign", defaultBuild = "fixture" }); result=f.Run();
        Check(File.Exists(f.Payload) && !result.Success && result.RetainedReasons.Any(r => r.Contains("identity")), "foreign package receipt cannot authorize removal");
        f = new Fixture(root); File.AppendAllText(f.Payload," user modification"); result=f.Run();
        Check(File.Exists(f.Payload) && File.Exists(Path.Combine(f.Folder,"manifest.json")) && !result.Success &&
            result.RetainedReasons.Any(r => r.Contains("changed since")), "modified packaged bytes are retained");
        f = new Fixture(root);
        using (var locked = new FileStream(f.Payload, FileMode.Open, FileAccess.Read, FileShare.None)) { result=f.Run(); }
        Check(File.Exists(f.Payload) && File.Exists(Path.Combine(f.Folder,"app","portable.json")) && !result.Success,
            "locked package keeps its payload and identity metadata without force closing its owner");
        f = new Fixture(root);
        string lockedManifest = Path.Combine(f.Folder,"manifest.json");
        f.Hooks.DeleteFile=path => {
            if (path == lockedManifest)
                using (var locked = new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)) { File.Delete(path); }
            else File.Delete(path);
        };
        result=f.Run();
        Check(!result.Success && File.Exists(lockedManifest) && !File.Exists(f.Payload) &&
            !File.Exists(Path.Combine(f.Folder,"app","portable.json")), "metadata lock after payload deletion reports partial cleanup");
        f.Hooks.DeleteFile=File.Delete; result=f.Run();
        Check(result.Success && !Directory.Exists(f.Folder) && new PackageStore(f.Manager).State.installed.Count == 0,
            "matching manifest with every listed payload absent permits clean retry after metadata lock");
        f = new Fixture(root); File.Delete(Path.Combine(f.Folder,"app","portable.json")); result=f.Run();
        Check(!result.Success && File.Exists(f.Payload), "missing identity cannot authorize cleanup while any listed payload remains");
        f = new Fixture(root); File.WriteAllText(f.Cache,"changed download"); File.WriteAllText(Path.Combine(f.Store.Cache,"keep.txt"),"unknown");
        result=f.Run();
        Check(File.Exists(f.Cache) && File.Exists(Path.Combine(f.Store.Cache,"keep.txt")) && !result.Success,
            "modified cache and arbitrary cache files are preserved");
        f = new Fixture(root);
        using (var cancelled=new CancellationTokenSource()) { cancelled.Cancel(); result=f.Run(cancelled.Token); }
        Check(result.TimedOut && result.RemovedFiles == 0 && File.Exists(f.Payload) && !result.Success, "pre-cancelled cleanup has no destructive side effects");
        f = new Fixture(root);
        using (var cancelled=new CancellationTokenSource())
        {
            f.Hooks.DeleteFile=path=> { File.Delete(path); cancelled.Cancel(); };
            result=f.Run(cancelled.Token);
        }
        Check(result.TimedOut && !result.Success && File.Exists(Path.Combine(f.Folder,"manifest.json")) && f.PersonalKept(),
            "interrupted cleanup retains receipt and personal files and reports partial removal");
        bool refused=false; try { LauncherUninstall.SafeRoot(Path.GetPathRoot(root)); } catch(InvalidDataException) { refused=true; }
        Check(refused, "drive root cannot become an uninstall cleanup target");
        Console.WriteLine("PASS uninstall checks: "+count);
    }
}
