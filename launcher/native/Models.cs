using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;

namespace WuWaVR.Manager
{
    public sealed class Release
    {
        public string id { get; set; }
        public string gameVersion { get; set; }
        public string buildId { get; set; }
        public string channel { get; set; }
        public string published { get; set; }
        public string created { get; set; }
        public string url { get; set; }
        public string sha256 { get; set; }
        public long size { get; set; }
        public string notesUrl { get; set; }
        public override string ToString() { return id + "  ·  WuWa " + gameVersion; }
        public void Validate()
        {
            Paths.Id(id); Paths.Id(buildId);
            if (!Regex.IsMatch(gameVersion ?? "", @"^\d+\.\d+(\.\d+)?$") || (channel != "beta" && channel != "candidate")) throw new InvalidDataException("Unsupported release metadata.");
            RepoClient.ValidateSha256(sha256);
            if (size < 1 || size > 536870912) throw new InvalidDataException("Invalid download size.");
            if (channel == "candidate")
            {
                DateTimeOffset built;
                if (!id.StartsWith("candidate-", StringComparison.Ordinal) || !DateTimeOffset.TryParse(created, out built) ||
                    published != null || url != null || notesUrl != null)
                    throw new InvalidDataException("Local candidates require a creation date and no public release URLs.");
                return;
            }
            var expected = "https://github.com/ChronoHaxx/wuwa-vr/releases/download/" + id + "/WuWa-VR-Launcher.zip";
            if (!String.Equals(url, expected, StringComparison.Ordinal) || notesUrl != "https://github.com/ChronoHaxx/wuwa-vr/releases/tag/" + id)
                throw new InvalidDataException("Release must use this project's GitHub assets.");
            DateTimeOffset date;
            if (!DateTimeOffset.TryParse(published, out date)) throw new InvalidDataException("Invalid publication date.");
        }
    }
    public sealed class Catalog
    {
        public int schema { get; set; }
        public List<Release> releases { get; set; }
        public void Validate()
        {
            if (schema != 1 || releases == null || releases.Count == 0 || releases.Count > 100) throw new InvalidDataException("Unsupported catalog.");
            var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var release in releases) { release.Validate(); if (!ids.Add(release.id)) throw new InvalidDataException("Duplicate release."); }
        }
    }
    public sealed class Installed
    {
        public Release release { get; set; }
        public string folder { get; set; }
        public string installedUtc { get; set; }
        public override string ToString() { return release.id; }
    }
    public sealed class ManagerState
    {
        public string language { get; set; } = "en";
        public string selected { get; set; }
        public string previous { get; set; }
        public string launcherPath { get; set; }
        // Only an unapplied deliberate native choice may replace saved helper settings.
        public string pendingLauncherPath { get; set; }
        public List<Installed> installed { get; set; } = new List<Installed>();
    }
    public sealed class PackageManifest
    {
        public string packageId { get; set; }
        public Dictionary<string, string> files { get; set; }
    }
    public static class Json
    {
        public static T Read<T>(string text) { return new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 }.Deserialize<T>(text); }
        public static string Write(object value) { return new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024 }.Serialize(value); }
        public static void Save(string path, object value) { Save(path, value, (source, destination) => File.Replace(source, destination, null)); }
        internal static void Save(string path, object value, Action<string, string> replace)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            var temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            bool failed = false;
            try
            {
                using (var stream = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    var data = Encoding.UTF8.GetBytes(Write(value)); stream.Write(data, 0, data.Length); stream.Flush(true);
                }
                if (File.Exists(path))
                {
                    // Retry only replacement errors that retain both original names.
                    // 1176/1177 can move files partway; permissions/disk errors are not locks.
                    for (int attempt = 0; ; attempt++)
                    {
                        try { replace(temp, path); break; }
                        catch (IOException error) when (attempt < 4 && (
                            error.HResult == unchecked((int)0x80070020) || // ERROR_SHARING_VIOLATION
                            error.HResult == unchecked((int)0x80070021) || // ERROR_LOCK_VIOLATION
                            error.HResult == unchecked((int)0x80070497))) // ERROR_UNABLE_TO_REMOVE_REPLACED
                        { Thread.Sleep(50 << attempt); } // Five attempts; 750 ms total backoff.
                    }
                }
                else File.Move(temp, path);
            }
            catch { failed = true; throw; }
            finally
            {
                try { if (File.Exists(temp)) File.Delete(temp); }
                // A second lock during cleanup must not hide the original save error.
                catch (IOException) when (failed) { }
                catch (UnauthorizedAccessException) when (failed) { }
            }
        }
        public static Dictionary<string, object> Object(object value) { return value as Dictionary<string, object> ?? new Dictionary<string, object>(); }
        public static string Text(Dictionary<string, object> value, string key) { object v; return value.TryGetValue(key, out v) ? Convert.ToString(v) : ""; }
        public static bool Flag(Dictionary<string, object> value, string key) { object v; return value.TryGetValue(key, out v) && v is bool && (bool)v; }
        public static Dictionary<string, object> Child(Dictionary<string, object> value, string key) { object v; return value.TryGetValue(key, out v) ? Object(v) : Object(null); }
    }
    public static class Paths
    {
        public static void Id(string id) { if (!Regex.IsMatch(id ?? "", @"^[a-zA-Z0-9][a-zA-Z0-9_-]{0,99}$")) throw new InvalidDataException("Invalid identifier."); }
        public static string Inside(string root, string relative)
        {
            if (String.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative)) throw new InvalidDataException("Invalid package path.");
            var parts = relative.Replace('\\', '/').Split('/');
            foreach (var part in parts)
            {
                if (part.Length == 0 || part == "." || part == ".." || part.TrimEnd(' ', '.') != part || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
                    Regex.IsMatch(part, @"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$", RegexOptions.IgnoreCase))
                    throw new InvalidDataException("Unsafe package path: " + relative);
            }
            root = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            var full = Path.GetFullPath(Path.Combine(root, Path.Combine(parts)));
            if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Path escaped the install directory.");
            NoLinks(root.TrimEnd(Path.DirectorySeparatorChar));
            for (var p = full; p != null && p.Length >= root.Length; p = Path.GetDirectoryName(p)) NoLinks(p);
            return full;
        }
        public static void NoLinks(string path)
        {
            if ((File.Exists(path) || Directory.Exists(path)) && (File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
                throw new InvalidDataException("Linked install directories are not supported: " + path);
        }
        public static void DeleteOwnedTree(string root, string relative)
        {
            var path = Inside(root, relative);
            if (!Directory.Exists(path)) return;
            CheckTree(path); Directory.Delete(path, true);
        }
        static void CheckTree(string path)
        {
            NoLinks(path);
            foreach (var entry in Directory.EnumerateFileSystemEntries(path))
            { NoLinks(entry); if (Directory.Exists(entry)) CheckTree(entry); }
        }
    }
}
