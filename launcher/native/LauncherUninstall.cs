using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32;

namespace WuWaVR.Manager
{
    public sealed class UninstallResult
    {
        public string Message { get; set; }
        public string ReportPath { get; set; }
        public int RemovedFiles { get; set; }
        public List<string> RetainedReasons { get; set; } = new List<string>();
        public bool TimedOut { get; set; }
        public bool Success { get { return !TimedOut && RetainedReasons.Count == 0; } }
    }

    // Velopack's uninstall callback has a hard 30-second limit. It must never
    // display a question, request UAC, launch a helper or forcibly stop anything.
    public static class LauncherUninstall
    {
        internal sealed class EnvironmentHooks
        {
            internal Action<PackageStore, string, CancellationToken> StopHelper;
            internal Func<IDisposable> AcquireIdleGate;
            internal Func<string[]> ActiveRuntimes;
            internal Action<string> DeleteFile = File.Delete;
        }

        public static Task<UninstallResult> PrepareAsync(string managerRoot, string helperData, CancellationToken cancel)
        { return PrepareAsync(managerRoot, helperData, Production(), cancel); }

        internal static Task<UninstallResult> PrepareAsync(string managerRoot, string helperData, EnvironmentHooks hooks, CancellationToken cancel)
        {
            return Task.Run(() => {
                using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancel))
                { deadline.CancelAfter(TimeSpan.FromSeconds(20)); return Cleanup(managerRoot, helperData, hooks, deadline.Token); }
            });
        }

        public static void BeforeUninstall(string managerRoot, string helperData)
        {
            UninstallResult result;
            Mutex native = null; bool nativeOwned = false;
            try
            {
                // The Windows uninstall callback must not race another native
                // window changing PackageStore. In-app cleanup runs within that
                // window's own operation guard and does not take this probe.
                native = new Mutex(false, "Local\\WuWaVRManager-" + InstanceActivation.Key(managerRoot));
                try { nativeOwned = native.WaitOne(0); } catch (AbandonedMutexException) { nativeOwned = true; }
                if (!nativeOwned) throw new InvalidOperationException("The WuWa VR window is still open. Downloaded files were kept. Close it and run cleanup before uninstalling.");
                var task = PrepareAsync(managerRoot, helperData, CancellationToken.None);
                if (task.Wait(TimeSpan.FromSeconds(22))) result = task.GetAwaiter().GetResult();
                else
                {
                    result = new UninstallResult { TimedOut = true };
                    result.RetainedReasons.Add("Cleanup did not finish within its time limit. Some downloaded files may remain; no process was forcibly stopped.");
                    Finish(managerRoot, helperData, result);
                }
            }
            catch (Exception error)
            {
                result = new UninstallResult(); result.RetainedReasons.Add("Cleanup could not complete: " + error.GetBaseException().Message);
                Finish(managerRoot, helperData, result);
            }
            finally { if (nativeOwned) native.ReleaseMutex(); native?.Dispose(); }
            // Keep a visible explanation after Velopack exits this callback.
            if (!result.Success && File.Exists(result.ReportPath))
                try { Process.Start(new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32/notepad.exe"),
                    "\"" + result.ReportPath + "\"") { UseShellExecute = false }); } catch { }
        }

        static EnvironmentHooks Production()
        {
            return new EnvironmentHooks {
                StopHelper = (store, data, cancel) => {
                    string package = store.State.installed.Select(store.Folder).FirstOrDefault();
                    string receiptPath = Paths.Inside(data, "launcher.json");
                    if (File.Exists(receiptPath))
                    {
                        if (new FileInfo(receiptPath).Length > 1024 * 1024) throw new InvalidDataException("The helper receipt is too large.");
                        var receipt = Json.Read<Dictionary<string, object>>(File.ReadAllText(receiptPath));
                        string app = Json.Text(receipt, "app");
                        if (!String.IsNullOrWhiteSpace(app))
                        {
                            string expected = Path.GetFullPath(app);
                            package = store.State.installed.Select(store.Folder).FirstOrDefault(p =>
                                String.Equals(Path.Combine(p, "app"), expected, StringComparison.OrdinalIgnoreCase));
                            if (package == null) throw new InvalidOperationException("The saved helper belongs to another package. Use confirmed process recovery before removing downloaded files.");
                        }
                    }
                    if (package == null)
                    {
                        if (File.Exists(receiptPath)) throw new InvalidOperationException("A helper receipt remains without a known installed package. Use confirmed process recovery first.");
                        return;
                    }
                    using (var bridge = new LauncherBridge(data))
                        bridge.PreparePackageChange(package, cancel).GetAwaiter().GetResult();
                },
                AcquireIdleGate = () => new IdleGate(),
                ActiveRuntimes = ReadActiveRuntimes
            };
        }

        internal static UninstallResult Cleanup(string managerRoot, string helperData, EnvironmentHooks hooks, CancellationToken cancel)
        {
            var result = new UninstallResult();
            try
            {
                managerRoot = SafeRoot(managerRoot); helperData = SafeRoot(helperData);
                cancel.ThrowIfCancellationRequested();
                if (!Directory.Exists(managerRoot)) { Finish(managerRoot, helperData, result); return result; }
                var store = new PackageStore(managerRoot);
                hooks.StopHelper(store, helperData, cancel);
                cancel.ThrowIfCancellationRequested();
                // Hold existing startup/profile locks on this same worker thread.
                using (hooks.AcquireIdleGate())
                {
                    string[] active = null;
                    try { active = hooks.ActiveRuntimes(); }
                    catch (Exception error) { result.RetainedReasons.Add("Versions retained because OpenXR registration could not be checked: " + error.Message); }
                    foreach (var item in store.State.installed.ToArray())
                    {
                        cancel.ThrowIfCancellationRequested();
                        string folder = store.Folder(item);
                        if (active == null) continue;
                        if (active.Any(path => IsWithin(folder, path)))
                        { result.RetainedReasons.Add(folder + ": retained because the active OpenXR runtime references this package. Select a headset runtime before removing it."); continue; }
                        try { RemoveVersion(store, item, hooks, cancel, result); }
                        catch (OperationCanceledException) { throw; }
                        catch (Exception error) { result.RetainedReasons.Add(folder + ": " + error.Message); }
                    }
                    RemoveCache(store.Cache, hooks, cancel, result);
                }
            }
            catch (OperationCanceledException)
            { result.TimedOut = true; result.RetainedReasons.Add("Cleanup was cancelled or reached its 20-second limit. Remaining files were retained."); }
            catch (Exception error)
            { result.RetainedReasons.Add("Downloaded files retained: " + error.Message + " Use Troubleshooting > Stuck launcher processes to inspect and confirm recovery, then retry cleanup."); }
            Finish(managerRoot, helperData, result);
            return result;
        }

        static void RemoveVersion(PackageStore store, Installed item, EnvironmentHooks hooks, CancellationToken cancel, UninstallResult result)
        {
            string folder = store.Folder(item);
            if (!Directory.Exists(folder)) { ForgetVersion(store, item); return; }
            string manifestPath = Paths.Inside(folder, "manifest.json"), portablePath = Paths.Inside(folder, "app/portable.json");
            if (new FileInfo(manifestPath).Length > 8 * 1024 * 1024)
                throw new InvalidDataException("Package identity metadata is too large.");
            var manifest = Json.Read<PackageManifest>(File.ReadAllText(manifestPath));
            if (manifest == null || manifest.files == null || manifest.files.Count == 0 || manifest.files.Count > 20000 ||
                manifest.packageId != "wuwa-vr-launcher-" + item.release.id)
                throw new InvalidDataException("Package identity does not match the installed record; files kept.");
            // Validate every path before deleting any. Changed/unknown user files
            // survive; only bytes still matching the package receipt are removed.
            var known = manifest.files.Select(pair => new KeyValuePair<string, string>(Paths.Inside(folder, pair.Key), pair.Value)).ToArray();
            foreach (var file in known) RepoClient.ValidateSha256(file.Value);
            if (File.Exists(portablePath))
            {
                if (new FileInfo(portablePath).Length > 1024 * 1024) throw new InvalidDataException("Package identity metadata is too large.");
                var portable = Json.Read<Dictionary<string, object>>(File.ReadAllText(portablePath));
                if (Json.Text(portable, "packageId") != manifest.packageId || Json.Text(portable, "defaultBuild") != item.release.buildId)
                    throw new InvalidDataException("Package identity does not match the installed record; files kept.");
            }
            else if (known.Any(file => File.Exists(file.Key)))
                throw new InvalidDataException("Package identity is missing while payload files remain; files kept.");
            // A previous cleanup may have removed portable.json and every
            // payload file before a metadata lock interrupted it. The matching
            // installed manifest permits finishing only that empty remainder.
            bool changed = false;
            foreach (var file in known)
            {
                cancel.ThrowIfCancellationRequested(); Paths.Inside(folder, file.Key.Substring(folder.Length + 1));
                if (!File.Exists(file.Key)) continue;
                if (!String.Equals(Hash(file.Key, cancel), file.Value, StringComparison.OrdinalIgnoreCase))
                { result.RetainedReasons.Add(file.Key + ": changed since installation; retained."); changed = true; continue; }
            }
            if (changed) return;
            foreach (var file in known.OrderBy(p => p.Key.Equals(portablePath, StringComparison.OrdinalIgnoreCase) ? 1 : 0))
            {
                cancel.ThrowIfCancellationRequested(); Paths.Inside(folder, file.Key.Substring(folder.Length + 1));
                if (!File.Exists(file.Key)) continue;
                hooks.DeleteFile(file.Key); result.RemovedFiles++;
            }
            cancel.ThrowIfCancellationRequested();
            foreach (string path in new[] { Paths.Inside(folder, "SHA256SUMS.txt"), manifestPath })
                if (File.Exists(path)) { hooks.DeleteFile(path); result.RemovedFiles++; }
            ForgetVersion(store, item);
            var parents = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { folder };
            foreach (var file in known)
                for (string parent = Path.GetDirectoryName(file.Key); IsWithin(folder, parent); parent = Path.GetDirectoryName(parent))
                { cancel.ThrowIfCancellationRequested(); parents.Add(parent); }
            foreach (var parent in parents.OrderByDescending(p => p.Length)) RemoveEmptyDirectory(parent, cancel);
            if (Directory.Exists(folder)) result.RetainedReasons.Add(folder + ": unlisted player files retained.");
        }

        static void ForgetVersion(PackageStore store, Installed item)
        {
            store.State.installed.Remove(item);
            if (store.State.selected == item.folder) store.State.selected = null;
            if (store.State.previous == item.folder) store.State.previous = null;
            store.Save();
        }

        static void RemoveCache(string cache, EnvironmentHooks hooks, CancellationToken cancel, UninstallResult result)
        {
            if (!Directory.Exists(cache)) return;
            Paths.NoLinks(cache);
            foreach (string file in Directory.EnumerateFiles(cache))
            {
                cancel.ThrowIfCancellationRequested();
                string name = Path.GetFileName(file);
                if (!System.Text.RegularExpressions.Regex.IsMatch(name, "^[a-fA-F0-9]{64}\\.zip$"))
                { result.RetainedReasons.Add(file + ": not a verified complete download; retained."); continue; }
                Paths.Inside(cache, name);
                if (!String.Equals(Hash(file, cancel), name.Substring(0, 64), StringComparison.OrdinalIgnoreCase))
                { result.RetainedReasons.Add(file + ": download digest changed; retained."); continue; }
                Paths.Inside(cache, name); hooks.DeleteFile(file); result.RemovedFiles++;
            }
            foreach (var directory in Directory.EnumerateDirectories(cache))
            { cancel.ThrowIfCancellationRequested(); result.RetainedReasons.Add(directory + ": unlisted cache directory retained."); }
            RemoveEmptyDirectory(cache, cancel);
        }

        static string Hash(string path, CancellationToken cancel)
        {
            using (var sha = SHA256.Create()) using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                byte[] buffer = new byte[81920]; int count;
                while ((count = stream.Read(buffer, 0, buffer.Length)) != 0)
                { cancel.ThrowIfCancellationRequested(); sha.TransformBlock(buffer, 0, count, null, 0); }
                sha.TransformFinalBlock(new byte[0], 0, 0);
                return BitConverter.ToString(sha.Hash).Replace("-", "").ToLowerInvariant();
            }
        }

        static void RemoveEmptyDirectory(string path, CancellationToken cancel)
        {
            if (!Directory.Exists(path)) return;
            Paths.NoLinks(path); cancel.ThrowIfCancellationRequested();
            if (!Directory.EnumerateFileSystemEntries(path).Any()) Directory.Delete(path, false);
        }

        internal static string SafeRoot(string path)
        {
            string full = Path.GetFullPath(path).TrimEnd(Path.DirectorySeparatorChar);
            if (String.Equals(full, Path.GetPathRoot(full).TrimEnd(Path.DirectorySeparatorChar), StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("A drive root is not a launcher data folder.");
            for (string parent = full; !String.IsNullOrEmpty(parent); parent = Path.GetDirectoryName(parent)) Paths.NoLinks(parent);
            return full;
        }
        static bool IsWithin(string root, string path)
        { return !String.IsNullOrWhiteSpace(path) && Path.GetFullPath(path).StartsWith(root.TrimEnd('\\', '/') + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase); }

        static string[] ReadActiveRuntimes()
        {
            var paths = new List<string>();
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
                using (var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                using (var key = hklm.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1"))
                { var value = key?.GetValue("ActiveRuntime") as string; if (!String.IsNullOrWhiteSpace(value)) paths.Add(value); }
            var inherited = Environment.GetEnvironmentVariable("XR_RUNTIME_JSON");
            if (!String.IsNullOrWhiteSpace(inherited)) paths.Add(inherited);
            return paths.ToArray();
        }

        sealed class IdleGate : IDisposable
        {
            readonly List<Mutex> locks = new List<Mutex>();
            public IdleGate()
            {
                try
                {
                    foreach (string name in new[] { "Local\\WuWaVRRenderingTestElevationRequest", "Local\\WuWaVRRenderingTestLaunch", "Local\\WuWaVRBuildProfileSwitch" })
                    {
                        var mutex = new Mutex(false, name); bool owned = false;
                        try { try { owned = mutex.WaitOne(0); } catch (AbandonedMutexException) { owned = true; }
                            if (!owned) throw new InvalidOperationException("A startup or runtime/profile worker still holds its lock. Confirm process recovery first.");
                            locks.Add(mutex); } finally { if (!owned) mutex.Dispose(); }
                    }
                    var processes = Process.GetProcesses();
                    try {
                        foreach (var process in processes)
                            if (new[] { "client-win64-shipping", "wuthering waves", "custom_uevr_injector", "uevrinjector", "wuwa-recorder", "wuwa-simulator-recorder" }
                                .Contains(process.ProcessName.ToLowerInvariant()))
                                throw new InvalidOperationException("The game, injector or recorder is running. Downloaded files were retained; nothing was terminated.");
                    } finally { foreach (var process in processes) process.Dispose(); }
                } catch { Dispose(); throw; }
            }
            public void Dispose() { foreach (var mutex in locks.AsEnumerable().Reverse()) { try { mutex.ReleaseMutex(); } finally { mutex.Dispose(); } } locks.Clear(); }
        }

        static void Finish(string managerRoot, string helperData, UninstallResult result)
        {
            result.Message = "Removed " + result.RemovedFiles + " downloaded package/cache files. Recordings, logs, settings, backups and unknown files were kept."
                + (result.Success ? " Cleanup finished. You can uninstall WuWa VR in Windows Settings > Apps." : " Some files were retained; see the report for their paths and reasons.");
            try
            {
                string root = SafeRoot(managerRoot); Directory.CreateDirectory(root);
                result.ReportPath = Paths.Inside(root, "uninstall-result.txt");
                var lines = new[] { "WuWa VR uninstall cleanup", DateTime.UtcNow.ToString("o"), "", result.Message,
                    "Package data: " + root, "Preserved personal data: " + Path.GetFullPath(helperData), "",
                    "No game, injector, headset runtime or stuck process was forcibly stopped. Shared OpenXR registration was not changed.", "" }
                    .Concat(result.RetainedReasons);
                File.WriteAllLines(result.ReportPath, lines, new UTF8Encoding(false));
            }
            catch { /* Uninstall must still return before Velopack's deadline. */ }
        }
    }
}
