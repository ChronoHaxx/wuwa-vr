using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;

namespace WuWaVR.Manager
{
    public sealed class PackageStore
    {
        public readonly string Root;
        readonly string statePath;
        readonly Action<string, string> moveDirectory;
        public ManagerState State { get; private set; }
        public PackageStore(string root) : this(root, Directory.Move) { }
        internal PackageStore(string root, Action<string, string> moveDirectory)
        {
            this.moveDirectory = moveDirectory ?? throw new ArgumentNullException(nameof(moveDirectory));
            Root = Path.GetFullPath(root); Paths.NoLinks(Root); Directory.CreateDirectory(Root);
            statePath = Paths.Inside(Root, "manager.json");
            State = File.Exists(statePath) ? Json.Read<ManagerState>(File.ReadAllText(statePath)) : new ManagerState();
            if (State == null || State.installed == null) throw new InvalidDataException("Installer state is unreadable; keep it for recovery.");
            foreach (var item in State.installed) { item.release.Validate(); Folder(item); }
        }
        public string Cache { get { return Paths.Inside(Root, "cache"); } }
        public Installed Selected { get { return State.installed.FirstOrDefault(x => x.folder == State.selected); } }
        public string Folder(Installed item) { Paths.Id(item.folder); return Paths.Inside(Root, "versions/" + item.folder); }
        public void Save() { Json.Save(statePath, State); }
        public void Select(Installed item)
        {
            if (!State.installed.Any(x => x.folder == item.folder)) throw new InvalidOperationException("Unknown installed release.");
            if (!Directory.Exists(Folder(item))) throw new IOException("Installed folder is missing. Repair this version.");
            if (State.selected == item.folder) return; // Reconnecting the active version must preserve rollback.
            var old = State.selected; var oldPrevious = State.previous;
            State.previous = old; State.selected = item.folder;
            try { Save(); } catch { State.selected = old; State.previous = oldPrevious; throw; }
        }
        // Cheap read-only preflight before stopping the working helper. Full
        // extraction, file verification and promotion still run during Install.
        public static void VerifyArchiveIdentity(string archive, Release release, CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            release.Validate();
            if (new FileInfo(archive).Length != release.size || RepoClient.Hash(archive) != release.sha256.ToLowerInvariant())
                throw new InvalidDataException("Archive integrity check failed.");
            cancel.ThrowIfCancellationRequested();
            using (var zip = ZipFile.OpenRead(archive))
            {
                if (zip.Entries.Count > 20000) throw new InvalidDataException("Too many package entries.");
                var entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.OrdinalIgnoreCase);
                var directories = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                // Reuse the installer's Windows path policy without creating a
                // directory or resolving paths against any installed package.
                var validationRoot = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(archive)), "identity-" + Guid.NewGuid().ToString("N"));
                foreach (var entry in zip.Entries)
                {
                    cancel.ThrowIfCancellationRequested();
                    if (entry.FullName.Contains("\\")) throw new InvalidDataException("Noncanonical package path.");
                    var name = entry.FullName.EndsWith("/") ? entry.FullName.Substring(0, entry.FullName.Length - 1) : entry.FullName;
                    Paths.Inside(validationRoot, name);
                    if ((entry.ExternalAttributes >> 16 & 0xF000) == 0xA000) throw new InvalidDataException("Package contains a symbolic link.");
                    if (entries.ContainsKey(name)) throw new InvalidDataException("Duplicate package entry.");
                    entries.Add(name, entry);
                    if (entry.FullName.EndsWith("/")) directories.Add(name);
                }
                foreach (var name in entries.Keys)
                {
                    var parent = name;
                    while (parent.LastIndexOf('/') >= 0)
                    {
                        parent = parent.Substring(0, parent.LastIndexOf('/'));
                        if (entries.ContainsKey(parent) && !directories.Contains(parent))
                            throw new InvalidDataException("Package file conflicts with a directory.");
                    }
                }
                var manifests = entries.Keys.Where(name => !directories.Contains(name) &&
                    (name.Equals("manifest.json", StringComparison.OrdinalIgnoreCase) ||
                     (name.Count(ch => ch == '/') == 1 && name.EndsWith("/manifest.json", StringComparison.OrdinalIgnoreCase)))).ToArray();
                if (manifests.Length != 1) throw new InvalidDataException("Ambiguous portable package root.");
                var prefix = manifests[0].Substring(0, manifests[0].Length - "manifest.json".Length);
                if (prefix.Length != 0 && entries.Keys.Any(name => !name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) &&
                    !(directories.Contains(name) && name.Equals(prefix.TrimEnd('/'), StringComparison.OrdinalIgnoreCase))))
                    throw new InvalidDataException("Not a single-root portable package.");
                ZipArchiveEntry portable;
                if (!entries.TryGetValue(prefix + "app/portable.json", out portable) || directories.Contains(prefix + "app/portable.json"))
                    throw new InvalidDataException("Portable package identity is missing.");
                RequireIdentity(ReadIdentity(entries[manifests[0]], 8 * 1024 * 1024, cancel),
                    ReadIdentity(portable, 64 * 1024, cancel), release);
            }
        }
        static Dictionary<string, object> ReadIdentity(ZipArchiveEntry entry, int limit, CancellationToken cancel)
        {
            if (entry.Length < 1 || entry.Length > limit) throw new InvalidDataException("Package identity metadata is too large or empty.");
            using (var source = entry.Open())
            using (var buffer = new MemoryStream())
            {
                var bytes = new byte[8192]; int count;
                while ((count = source.Read(bytes, 0, bytes.Length)) > 0)
                {
                    cancel.ThrowIfCancellationRequested();
                    if (buffer.Length + count > limit || buffer.Length + count > entry.Length)
                        throw new InvalidDataException("Package identity metadata exceeds its declared size.");
                    buffer.Write(bytes, 0, count);
                }
                if (buffer.Length != entry.Length) throw new InvalidDataException("Truncated package identity metadata.");
                try { return Json.Read<Dictionary<string, object>>(new UTF8Encoding(false, true).GetString(buffer.ToArray()).TrimStart('\uFEFF')); }
                catch (Exception error) when (error is ArgumentException || error is InvalidOperationException)
                { throw new InvalidDataException("Invalid package identity metadata.", error); }
            }
        }
        static void RequireIdentity(Dictionary<string, object> manifest, Dictionary<string, object> portable, Release release)
        {
            var expected = "wuwa-vr-launcher-" + release.id;
            if (manifest == null || portable == null ||
                Json.Text(manifest, "packageId") != expected || Json.Text(portable, "packageId") != expected ||
                Json.Text(manifest, "defaultBuild") != release.buildId || Json.Text(portable, "defaultBuild") != release.buildId)
                throw new InvalidDataException("Package identity does not match selected release.");
        }
        public Installed Install(string archive, Release release, CancellationToken cancel)
        {
            release.Validate();
            if (new FileInfo(archive).Length != release.size || RepoClient.Hash(archive) != release.sha256.ToLowerInvariant())
                throw new InvalidDataException("Archive integrity check failed.");
            string stagingId = "stage-" + Guid.NewGuid().ToString("N");
            string staging = Paths.Inside(Root, stagingId); Directory.CreateDirectory(staging);
            string moved = null;
            try
            {
                var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long total = 0;
                using (var zip = ZipFile.OpenRead(archive))
                {
                    if (zip.Entries.Count > 20000) throw new InvalidDataException("Too many package entries.");
                    foreach (var entry in zip.Entries)
                    {
                        cancel.ThrowIfCancellationRequested();
                        var relative = entry.FullName.TrimEnd('/');
                        var target = Paths.Inside(staging, relative);
                        if ((entry.ExternalAttributes >> 16 & 0xF000) == 0xA000) throw new InvalidDataException("Package contains a symbolic link.");
                        if (entry.FullName.EndsWith("/")) { Directory.CreateDirectory(target); continue; }
                        if (!seen.Add(relative.Replace('\\', '/'))) throw new InvalidDataException("Duplicate package entry.");
                        total = checked(total + entry.Length);
                        if (total > 2147483648L) throw new InvalidDataException("Expanded package exceeds 2 GiB.");
                        Directory.CreateDirectory(Path.GetDirectoryName(target));
                        using (var source = entry.Open()) using (var dest = new FileStream(target, FileMode.CreateNew))
                        {
                            byte[] bytes = new byte[81920]; int count; long written = 0;
                            while ((count = source.Read(bytes, 0, bytes.Length)) > 0)
                            { cancel.ThrowIfCancellationRequested(); written += count; if (written > entry.Length) throw new InvalidDataException("Invalid ZIP length."); dest.Write(bytes, 0, count); }
                            if (written != entry.Length) throw new InvalidDataException("Truncated ZIP entry.");
                        }
                    }
                }
                string package = staging;
                if (!File.Exists(Path.Combine(package, "manifest.json")))
                {
                    var dirs = Directory.GetDirectories(staging);
                    if (dirs.Length != 1 || Directory.GetFiles(staging).Length != 0) throw new InvalidDataException("Not a complete portable package.");
                    package = dirs[0];
                }
                Verify(package, release, cancel);
                var folderId = release.id + "-" + Guid.NewGuid().ToString("N").Substring(0, 12);
                var destination = Paths.Inside(Root, "versions/" + folderId);
                Directory.CreateDirectory(Path.GetDirectoryName(destination)); cancel.ThrowIfCancellationRequested();
                Promote(package, destination, cancel); moved = "versions/" + folderId;
                var installed = new Installed { release = release, folder = folderId, installedUtc = DateTime.UtcNow.ToString("o") };
                var previous = State.selected; var previousPrior = State.previous;
                State.installed.Add(installed); State.previous = previous; State.selected = folderId;
                try { Save(); }
                catch { State.installed.Remove(installed); State.selected = previous; State.previous = previousPrior; throw; }
                moved = null; return installed;
            }
            finally
            {
                Paths.DeleteOwnedTree(Root, stagingId);
                if (moved != null) Paths.DeleteOwnedTree(Root, moved);
            }
        }
        void Promote(string package, string destination, CancellationToken cancel)
        {
            // Scanners can briefly hold the newly verified directory. Retry only
            // its atomic rename; never repeat extraction, verification or state writes.
            for (int attempt = 0; ; attempt++)
            {
                cancel.ThrowIfCancellationRequested();
                try { moveDirectory(package, destination); return; }
                catch (IOException error) when (attempt < 4 && PromotionMayBeLocked(error)) { }
                catch (UnauthorizedAccessException error) when (attempt < 4 && PromotionMayBeLocked(error)) { }
                // Five attempts, with at most 1.5 seconds of interruptible backoff.
                if (cancel.WaitHandle.WaitOne(100 << attempt)) cancel.ThrowIfCancellationRequested();
            }
        }
        static bool PromotionMayBeLocked(Exception error)
        {
            return error.HResult == unchecked((int)0x80070005) || // ERROR_ACCESS_DENIED
                error.HResult == unchecked((int)0x80070020) || // ERROR_SHARING_VIOLATION
                error.HResult == unchecked((int)0x80070021); // ERROR_LOCK_VIOLATION
        }
        public static void Verify(string package, Release release, CancellationToken cancel)
        {
            var manifestText = File.ReadAllText(Paths.Inside(package, "manifest.json"));
            var manifest = Json.Read<PackageManifest>(manifestText);
            if (manifest == null || manifest.files == null || manifest.files.Count == 0 || manifest.files.Count > 20000)
                throw new InvalidDataException("Invalid package manifest.");
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var pair in manifest.files)
            {
                cancel.ThrowIfCancellationRequested(); RepoClient.ValidateSha256(pair.Value);
                if (!names.Add(pair.Key.Replace('\\', '/'))) throw new InvalidDataException("Duplicate manifest path.");
                var path = Paths.Inside(package, pair.Key);
                if (!File.Exists(path) || RepoClient.Hash(path) != pair.Value.ToLowerInvariant())
                    throw new InvalidDataException("Package file failed verification: " + pair.Key);
            }
            foreach (var required in new[] { "python/pythonw.exe", "app/dev/wuwa_player.py", "app/portable.json", "app/dev/wuwa-builds.json" })
                if (!names.Contains(required)) throw new InvalidDataException("Required package file is not covered by the manifest: " + required);
            var info = Json.Read<Dictionary<string, object>>(File.ReadAllText(Paths.Inside(package, "app/portable.json")));
            RequireIdentity(Json.Read<Dictionary<string, object>>(manifestText), info, release);
            // Refuse unmanifested executable payloads; only generated receipts are exempt in an already-used package.
            foreach (var path in Directory.EnumerateFiles(package, "*", SearchOption.AllDirectories))
            {
                var relative = path.Substring(package.TrimEnd('\\', '/').Length + 1).Replace('\\', '/');
                if (names.Contains(relative) || relative.Equals("manifest.json", StringComparison.OrdinalIgnoreCase) || relative.Equals("SHA256SUMS.txt", StringComparison.OrdinalIgnoreCase)) continue;
                if (relative.StartsWith("app/runtime/") && relative.EndsWith("/Custom_UEVR_Injector.txt")) continue;
                throw new InvalidDataException("Unlisted package file: " + relative);
            }
        }
        // Remove only files recorded in the verified package. Unknown user files are retained.
        // Callers must first restore the profile/runtime and stop the backend/game.
        public int Remove(Installed item)
        {
            if (!State.installed.Contains(item)) throw new InvalidOperationException("Unknown installed release.");
            string folder = Folder(item);
            var manifest = Json.Read<PackageManifest>(File.ReadAllText(Paths.Inside(folder, "manifest.json")));
            if (manifest == null || manifest.files == null) throw new InvalidDataException("Cannot remove a package without its file receipt.");
            var files = manifest.files.Keys.Concat(new[] { "manifest.json", "SHA256SUMS.txt" }).Select(p => Paths.Inside(folder, p)).ToList();
            string removalId = "remove-" + Guid.NewGuid().ToString("N");
            string removal = Paths.Inside(Root, removalId); Directory.CreateDirectory(removal);
            var moved = new List<KeyValuePair<string, string>>();
            var selected = State.selected; var previous = State.previous; int index = State.installed.IndexOf(item);
            try
            {
                foreach (var path in files)
                {
                    if (!File.Exists(path)) continue;
                    string retained = Paths.Inside(removal, moved.Count.ToString());
                    File.Move(path, retained); moved.Add(new KeyValuePair<string, string>(path, retained));
                }
                State.installed.Remove(item);
                if (State.selected == item.folder) State.selected = null;
                if (State.previous == item.folder) State.previous = null;
                Save();
            }
            catch
            {
                if (!State.installed.Contains(item)) State.installed.Insert(index, item);
                State.selected = selected; State.previous = previous;
                foreach (var pair in moved.AsEnumerable().Reverse()) File.Move(pair.Value, pair.Key);
                Paths.DeleteOwnedTree(Root, removalId); throw;
            }
            // Only after the state commit may the owned files be deleted. Unknown
            // player files remain in the old package folder, outside this staging tree.
            Paths.DeleteOwnedTree(Root, removalId);
            return Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories).Count();
        }
    }
}
