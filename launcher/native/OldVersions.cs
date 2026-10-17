using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;

namespace WuWaVR.Manager
{
    // Troubleshooting > Recovery > Remove old versions. Every install, update and repair extracts a new
    // folder of about 115 MB and keeps the old ones for rollback, and nothing pruned them (a test machine
    // held 83 version folders, 9.3 GB, on 17 Oct 2026). This removes every version except the one in use,
    // the previous one (rollback) and any the active OpenXR runtime points at, through the receipt-only
    // PackageStore.Remove; then the generated files and empty folders a removal leaves, unrecorded folders
    // holding nothing else, and downloads no kept version needs. Unknown (player) files are always kept.
    public static class OldVersions
    {
        public sealed class Plan
        {
            public readonly List<Installed> Versions = new List<Installed>();
            public readonly List<Installed> Kept = new List<Installed>();
            public readonly List<string> Leftovers = new List<string>();
            public readonly List<string> Downloads = new List<string>();
            public long Bytes;
            public bool Empty { get { return Versions.Count == 0 && Leftovers.Count == 0 && Downloads.Count == 0; } }
        }
        public sealed class Result
        {
            public int Removed;
            public long Freed;
            public readonly List<string> Retained = new List<string>();
        }

        // activeRuntimes: the OpenXR runtime manifests in use; null when they could not be read (keeps every version).
        public static Plan Find(PackageStore store, string[] activeRuntimes)
        {
            var plan = new Plan();
            var keep = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (store.State.selected != null) keep.Add(store.State.selected);
            if (store.State.previous != null) keep.Add(store.State.previous);
            // Nothing selected: the newest install stays, so there is always one to launch.
            if (store.Selected == null && store.State.installed.Count > 0)
                keep.Add(store.State.installed.OrderBy(x => x.installedUtc ?? "", StringComparer.Ordinal).Last().folder);
            foreach (var item in store.State.installed)
            {
                string folder = store.Folder(item);
                if (keep.Contains(item.folder) || activeRuntimes == null || activeRuntimes.Any(path => Within(folder, path)))
                { plan.Kept.Add(item); continue; }
                plan.Versions.Add(item); plan.Bytes += Size(folder);
            }
            string versions = Path.Combine(store.Root, "versions");
            if (Directory.Exists(versions))
            {
                var recorded = new HashSet<string>(store.State.installed.Select(x => x.folder), StringComparer.OrdinalIgnoreCase);
                foreach (string directory in Directory.EnumerateDirectories(versions))
                {
                    string name = Path.GetFileName(directory);
                    if (recorded.Contains(name) || !Regex.IsMatch(name, @"^[a-zA-Z0-9][a-zA-Z0-9_-]{0,99}$")) continue;
                    string folder = Paths.Inside(store.Root, "versions/" + name);
                    if (!OnlyGenerated(folder)) continue;
                    plan.Leftovers.Add(folder); plan.Bytes += Size(folder);
                }
            }
            if (Directory.Exists(store.Cache))
            {
                var needed = new HashSet<string>(plan.Kept.Select(x => x.release.sha256), StringComparer.OrdinalIgnoreCase);
                foreach (string file in Directory.EnumerateFiles(store.Cache, "*.zip"))
                {
                    string name = Path.GetFileName(file);
                    if (!Regex.IsMatch(name, "^[a-fA-F0-9]{64}\\.zip$") || needed.Contains(name.Substring(0, 64))) continue;
                    string zip = Paths.Inside(store.Cache, name);
                    plan.Downloads.Add(zip); plan.Bytes += new FileInfo(zip).Length;
                }
            }
            return plan;
        }

        public static Result Remove(PackageStore store, Plan plan, CancellationToken cancel)
        {
            var result = new Result();
            foreach (var item in plan.Versions)
            {
                cancel.ThrowIfCancellationRequested();
                string folder = store.Folder(item); long before = Size(folder);
                try { store.Remove(item); ClearGenerated(folder); result.Removed++; }
                catch (Exception error) when (!(error is OperationCanceledException))
                { result.Retained.Add(item.release.DisplayLabel + ": " + error.Message); }
                result.Freed += before - Size(folder);
            }
            foreach (string folder in plan.Leftovers)
            {
                cancel.ThrowIfCancellationRequested(); long before = Size(folder);
                try { if (OnlyGenerated(folder)) ClearGenerated(folder); }
                catch (Exception error) when (!(error is OperationCanceledException)) { result.Retained.Add(folder + ": " + error.Message); }
                result.Freed += before - Size(folder);
            }
            foreach (string zip in plan.Downloads)
            {
                cancel.ThrowIfCancellationRequested();
                try { Paths.NoLinks(zip); long size = new FileInfo(zip).Length; File.Delete(zip); result.Freed += size; }
                catch (Exception error) when (!(error is OperationCanceledException)) { result.Retained.Add(zip + ": " + error.Message); }
            }
            return result;
        }

        // The injector writes Custom_UEVR_Injector.txt into a version's runtime folder; it is not in the
        // package receipt (PackageStore.Verify exempts it), so a removal leaves it behind.
        static bool Generated(string folder, string file)
        {
            string relative = file.Substring(folder.TrimEnd('\\', '/').Length + 1).Replace('\\', '/');
            return relative.StartsWith("app/runtime/", StringComparison.OrdinalIgnoreCase) && relative.IndexOf('/', "app/runtime/".Length) > 0 &&
                relative.EndsWith("/Custom_UEVR_Injector.txt", StringComparison.OrdinalIgnoreCase) &&
                relative.Count(c => c == '/') == 3;
        }
        static bool OnlyGenerated(string folder)
        {
            if (!Directory.Exists(folder) || Linked(folder)) return false;
            foreach (string entry in Directory.EnumerateFileSystemEntries(folder, "*", SearchOption.AllDirectories))
            {
                if (Linked(entry)) return false;
                if (File.Exists(entry) && !Generated(folder, entry)) return false;
            }
            return true;
        }
        // Generated files, then every directory left empty, deepest first; anything else stays.
        static void ClearGenerated(string folder)
        {
            if (!Directory.Exists(folder) || Linked(folder)) return;
            foreach (string file in Directory.EnumerateFiles(folder, "Custom_UEVR_Injector.txt", SearchOption.AllDirectories).ToList())
                if (!Linked(file) && Generated(folder, file)) File.Delete(file);
            foreach (string directory in Directory.EnumerateDirectories(folder, "*", SearchOption.AllDirectories).OrderByDescending(d => d.Length).ToList())
                if (!Linked(directory) && !Directory.EnumerateFileSystemEntries(directory).Any()) Directory.Delete(directory, false);
            if (!Directory.EnumerateFileSystemEntries(folder).Any()) Directory.Delete(folder, false);
        }
        static bool Linked(string path) { return (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0; }
        static long Size(string folder)
        {
            if (!Directory.Exists(folder) || Linked(folder)) return 0;
            long total = 0;
            foreach (string file in Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories))
                try { total += new FileInfo(file).Length; } catch (IOException) { }
            return total;
        }
        static bool Within(string root, string path)
        {
            return !String.IsNullOrWhiteSpace(path) && Path.GetFullPath(path).StartsWith(root.TrimEnd('\\', '/') + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
        }
    }
}
