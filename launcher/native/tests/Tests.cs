using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
using WuWaVR.Manager;

class Tests
{
    static int passed;
    static string root;
    static void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
    static void Reject(Action action, string message)
    { try { action(); } catch (InvalidDataException) { return; } throw new Exception(message); }
    static void Test(string name, Action test) { test(); passed++; Console.WriteLine("PASS " + name); }
    static Release Release(string id, string file)
    { return new Release { id = id, buildId = "test-build", channel = "beta", gameVersion = "3.7", published = "2026-10-03T00:00:00Z", url = "https://github.com/ChronoHaxx/wuwa-vr/releases/download/" + id + "/WuWa-VR-Launcher.zip", notesUrl = "https://github.com/ChronoHaxx/wuwa-vr/releases/tag/" + id, size = new FileInfo(file).Length, sha256 = RepoClient.Hash(file) }; }
    static Release Candidate(string file)
    { var r = Release("candidate-test", file); r.channel = "candidate"; r.created = r.published; r.published = null; r.url = null; r.notesUrl = null; return r; }
    static string MakePackage(string id, string extra = null, bool badManifest = false,
        string manifestId = null, string portableId = null, string manifestBuild = "test-build",
        string portableBuild = "test-build", bool flat = false, string portableText = null)
    {
        var values = new Dictionary<string, byte[]> {
            { "python/pythonw.exe", Encoding.UTF8.GetBytes("test fixture; never execute") },
            { "app/dev/wuwa_player.py", Encoding.UTF8.GetBytes("# test fixture") },
            { "app/portable.json", Encoding.UTF8.GetBytes(portableText ?? Json.Write(new { packageId = portableId ?? "wuwa-vr-launcher-" + id, defaultBuild = portableBuild })) },
            { "app/dev/wuwa-builds.json", Encoding.UTF8.GetBytes("{}") },
            { "data.bin", Enumerable.Range(0, 24000).Select(x => (byte)(x * 13)).ToArray() }
        };
        var hashes = new Dictionary<string, string>();
        foreach (var item in values)
        {
            using (var sha = System.Security.Cryptography.SHA256.Create()) hashes[item.Key] = BitConverter.ToString(sha.ComputeHash(item.Value)).Replace("-", "").ToLowerInvariant();
        }
        if (badManifest) hashes["app/dev/wuwa_player.py"] = new string('0', 64);
        var file = Path.Combine(root, Guid.NewGuid().ToString("N") + ".zip");
        var prefix = flat ? "" : "WuWa VR/";
        using (var zip = ZipFile.Open(file, ZipArchiveMode.Create))
        {
            foreach (var item in values) using (var s = zip.CreateEntry(prefix + item.Key, CompressionLevel.NoCompression).Open()) s.Write(item.Value, 0, item.Value.Length);
            using (var s = new StreamWriter(zip.CreateEntry(prefix + "manifest.json").Open())) s.Write(Json.Write(new { files = hashes, packageId = manifestId ?? "wuwa-vr-launcher-" + id, defaultBuild = manifestBuild }));
            if (extra != null) using (var s = new StreamWriter(zip.CreateEntry(extra).Open())) s.Write("untrusted");
        }
        return file;
    }
    static void ArchiveIdentityTests(PackageStore store, string archive, Release release)
    {
        var before = Json.Write(store.State);
        var receipt = File.ReadAllBytes(Path.Combine(store.Root, "manager.json"));
        var inventory = Directory.GetFileSystemEntries(store.Root, "*", SearchOption.AllDirectories).OrderBy(x => x).ToArray();
        Action unchanged = () =>
        {
            AssertPromotionUnchanged(store, before);
            Assert(receipt.SequenceEqual(File.ReadAllBytes(Path.Combine(store.Root, "manager.json"))), "identity preflight rewrote the installed receipt");
            Assert(inventory.SequenceEqual(Directory.GetFileSystemEntries(store.Root, "*", SearchOption.AllDirectories).OrderBy(x => x)), "identity preflight created or removed installed files");
            Assert(Directory.GetDirectories(root, "identity-*").Length == 0, "identity preflight extracted metadata");
        };
        Test("identity preflight accepts both single-folder and flat portable archives without extraction", () =>
        {
            PackageStore.VerifyArchiveIdentity(archive, release, CancellationToken.None);
            var flat = MakePackage("test-flat", flat: true);
            PackageStore.VerifyArchiveIdentity(flat, Release("test-flat", flat), CancellationToken.None);
            unchanged();
        });
        Test("fully rehashed wrong package or build identities fail before any installed state changes", () =>
        {
            var wrong = new[] {
                MakePackage("test-v2", manifestId: "wuwa-vr-launcher-private-name"),
                MakePackage("test-v2", portableId: "wuwa-vr-launcher-private-name"),
                MakePackage("test-v2", manifestBuild: "another-build"),
                MakePackage("test-v2", portableBuild: "another-build")
            };
            foreach (var file in wrong)
            {
                var r = Release("test-v2", file); // Every catalog and per-file hash matches the bad identity's bytes.
                Reject(() => PackageStore.VerifyArchiveIdentity(file, r, CancellationToken.None), "valid hashes concealed an identity mismatch");
                unchanged();
                Reject(() => store.Install(file, r, CancellationToken.None), "full install accepted an identity mismatch");
                unchanged();
            }
        });
        Test("identity preflight rejects ambiguous roots and noncanonical or duplicate ZIP paths", () =>
        {
            foreach (var extra in new[] { "manifest.json", "other/manifest.json", "WuWa VR/APP/PORTABLE.JSON",
                "WuWa VR/app", "WuWa VR/../outside.txt", "WuWa VR\\duplicate.txt", "loose-file.txt" })
            {
                var file = MakePackage("test-v2", extra);
                Reject(() => PackageStore.VerifyArchiveIdentity(file, Release("test-v2", file), CancellationToken.None), "ambiguous package accepted: " + extra);
                unchanged();
            }
        });
        Test("identity preflight rejects malformed and oversized portable metadata", () =>
        {
            foreach (var text in new[] { "null", "{broken", new string(' ', 65537) })
            {
                var file = MakePackage("test-v2", portableText: text);
                Reject(() => PackageStore.VerifyArchiveIdentity(file, Release("test-v2", file), CancellationToken.None), "invalid identity metadata accepted");
                unchanged();
            }
        });
        Test("identity preflight validates catalog and archive integrity before opening metadata", () =>
        {
            var wrongSize = Release(release.id, archive); wrongSize.size++;
            Reject(() => PackageStore.VerifyArchiveIdentity(archive, wrongSize, CancellationToken.None), "wrong archive size accepted");
            var wrongHash = Release(release.id, archive); wrongHash.sha256 = new string('0', 64);
            Reject(() => PackageStore.VerifyArchiveIdentity(archive, wrongHash, CancellationToken.None), "wrong archive hash accepted");
            var wrongRelease = Release(release.id, archive); wrongRelease.id = "../invalid";
            Reject(() => PackageStore.VerifyArchiveIdentity(archive, wrongRelease, CancellationToken.None), "invalid release accepted");
            unchanged();
        });
        Test("cancelled identity preflight does not alter the current package", () =>
        {
            using (var cancel = new CancellationTokenSource())
            {
                cancel.Cancel();
                try { PackageStore.VerifyArchiveIdentity(archive, release, cancel.Token); throw new Exception("preflight cancellation ignored"); }
                catch (OperationCanceledException) { }
            }
            unchanged();
        });
    }
    sealed class FixtureHttp : HttpMessageHandler
    {
        readonly byte[] bytes;
        readonly bool ignoreRange, truncate, badRange;
        public long RequestedOffset;
        public int Requests;
        public FixtureHttp(byte[] bytes, bool ignoreRange = false, bool truncate = false, bool badRange = false)
        { this.bytes = bytes; this.ignoreRange = ignoreRange; this.truncate = truncate; this.badRange = badRange; }
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested(); Requests++; RequestedOffset = request.Headers.Range == null ? 0 : request.Headers.Range.Ranges.First().From.Value;
            long offset = ignoreRange ? 0 : RequestedOffset;
            var body = bytes.Skip((int)offset).Take(truncate ? 5000 : bytes.Length).ToArray();
            var response = new HttpResponseMessage(offset == 0 ? HttpStatusCode.OK : HttpStatusCode.PartialContent) { Content = new ByteArrayContent(body), RequestMessage = request };
            if (offset > 0) response.Content.Headers.ContentRange = new ContentRangeHeaderValue(offset + (badRange ? 1 : 0), bytes.Length - 1, bytes.Length);
            return Task.FromResult(response);
        }
    }
    static void DownloadTest(string archive, bool ignoreRange)
    {
        var r = Release("test-v1", archive); var cache = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(cache);
        var bytes = File.ReadAllBytes(archive); File.WriteAllBytes(Path.Combine(cache, r.sha256 + ".zip.partial"), bytes.Take(1000).ToArray());
        var handler = new FixtureHttp(bytes, ignoreRange);
        using (var client = new RepoClient(handler))
        {
            var result = client.Download(r, cache, null, CancellationToken.None).GetAwaiter().GetResult();
            Assert(handler.RequestedOffset == 1000, "resume did not request saved offset"); Assert(RepoClient.Hash(result) == r.sha256, "download bytes changed");
        }
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    static PackageStore PromotionStore(string folder, Action<string, string> move)
    {
        var constructor = typeof(PackageStore).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null,
            new[] { typeof(string), typeof(Action<string, string>) }, null);
        Assert(constructor != null, "promotion fixture seam missing");
        return (PackageStore)constructor.Invoke(new object[] { folder, move });
    }
    static void AssertPromotionUnchanged(PackageStore store, string before)
    {
        Assert(Json.Write(store.State) == before && Json.Write(new PackageStore(store.Root).State) == before,
            "failed promotion changed selected/previous version or its persisted receipt");
        Assert(Directory.GetDirectories(store.Root, "stage-*").Length == 0, "failed promotion left a staging directory");
        Assert(Directory.GetDirectories(Path.Combine(store.Root, "versions")).Length == store.State.installed.Count,
            "failed promotion left an unregistered version");
    }
    static void SaveWithReplace(string path, object value, Action<string, string> replace)
    {
        var save = typeof(Json).GetMethod("Save", BindingFlags.Static | BindingFlags.NonPublic, null,
            new[] { typeof(string), typeof(object), typeof(Action<string, string>) }, null);
        Assert(save != null, "JSON replacement fixture seam missing");
        try { save.Invoke(null, new object[] { path, value, replace }); }
        catch (TargetInvocationException error)
        { System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(error.InnerException).Throw(); throw; }
    }
    static void JsonSaveTests()
    {
        var folder = Path.Combine(root, "json-save-retry"); Directory.CreateDirectory(folder);
        var path = Path.Combine(folder, "manager.json");
        var oldValue = new { selected = "old", previous = "older", language = "zh-Hans" };
        var newValue = new { selected = "new", previous = "old", language = "en" };
        var oldText = Json.Write(oldValue); var newText = Json.Write(newValue);
        Test("JSON save retries a real file lock then atomically replaces the old state", () =>
        {
            Json.Save(path, oldValue); int attempts = 0;
            using (var held = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            using (var blocked = new ManualResetEventSlim())
            {
                var release = Task.Run(() => { blocked.Wait(); Thread.Sleep(75); held.Dispose(); });
                try
                {
                    SaveWithReplace(path, newValue, (source, destination) =>
                    {
                        attempts++;
                        try { File.Replace(source, destination, null); }
                        catch (IOException)
                        {
                            Assert(File.ReadAllText(path) == oldText, "locked replacement altered old state");
                            Assert(File.ReadAllText(source) == newText, "replacement was not fully written before retry");
                            blocked.Set(); throw;
                        }
                    });
                }
                finally { blocked.Set(); release.GetAwaiter().GetResult(); }
            }
            Assert(attempts >= 2 && attempts <= 5 && File.ReadAllText(path) == newText, "released file lock did not recover");
            Assert(Directory.GetFiles(folder, "*.tmp").Length == 0, "successful save left temporary JSON");
        });
        Test("persistent JSON file lock stays bounded and preserves the complete old state", () =>
        {
            Json.Save(path, oldValue); int attempts = 0; IOException failure = null;
            var elapsed = System.Diagnostics.Stopwatch.StartNew();
            using (var held = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            {
                try { SaveWithReplace(path, newValue, (source, destination) => { attempts++; File.Replace(source, destination, null); }); }
                catch (IOException error) { failure = error; }
                Assert(File.ReadAllText(path) == oldText, "persistent lock changed old JSON");
            }
            Assert(failure != null && attempts == 5, "persistent lock did not propagate after five attempts");
            Assert(elapsed.ElapsedMilliseconds >= 700 && elapsed.ElapsedMilliseconds < 3000, "JSON replacement backoff exceeded its small bound");
            Assert(Directory.GetFiles(folder, "*.tmp").Length == 0, "failed save left temporary JSON");
        });
        Test("JSON replacement recovers only from sharing, lock and unchanged-name removal errors", () =>
        {
            foreach (int code in new[] { 32, 33, 1175 })
            {
                Json.Save(path, oldValue); int attempts = 0; string firstTemp = null;
                SaveWithReplace(path, newValue, (source, destination) =>
                {
                    attempts++;
                    if (attempts == 1) { firstTemp = source; throw new IOException("fixture transient replacement", unchecked((int)0x80070000) | code); }
                    Assert(source == firstTemp && File.ReadAllText(destination) == oldText && File.ReadAllText(source) == newText,
                        "retry rewrote the temp file or altered the old state");
                    File.Replace(source, destination, null);
                });
                Assert(attempts == 2 && File.ReadAllText(path) == newText, "transient replacement code did not recover: " + code);
            }
        });
        Test("JSON save immediately propagates disk, permission and partial-move errors", () =>
        {
            foreach (Exception expected in new Exception[] {
                new UnauthorizedAccessException("fixture permission denied"),
                new IOException("fixture access denied", unchecked((int)0x80070005)),
                new IOException("fixture disk full", unchecked((int)0x80070070)),
                new IOException("fixture CRC failure", unchecked((int)0x80070017)),
                new IOException("fixture replacement move failure", unchecked((int)0x80070498)),
                new IOException("fixture partial replacement move", unchecked((int)0x80070499)) })
            {
                Json.Save(path, oldValue); int attempts = 0; Exception failure = null;
                try { SaveWithReplace(path, newValue, (source, destination) => { attempts++; throw expected; }); }
                catch (Exception error) { failure = error; }
                Assert(Object.ReferenceEquals(failure, expected) && attempts == 1, "permanent save failure retried or lost its original error");
                Assert(File.ReadAllText(path) == oldText && Directory.GetFiles(folder, "*.tmp").Length == 0,
                    "failed replacement deleted the old state or left temporary JSON");
            }
        });
        Test("temporary-file cleanup cannot conceal the original JSON save error", () =>
        {
            Json.Save(path, oldValue); FileStream held = null; string temp = null; Exception failure = null;
            var expected = new IOException("fixture original disk error", unchecked((int)0x80070070));
            try
            {
                try { SaveWithReplace(path, newValue, (source, destination) => { temp = source; held = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.None); throw expected; }); }
                catch (Exception error) { failure = error; }
                Assert(Object.ReferenceEquals(failure, expected) && File.ReadAllText(path) == oldText, "cleanup hid the original error or altered old JSON");
            }
            finally { if (held != null) held.Dispose(); if (temp != null) File.Delete(temp); }
        });
    }
    static void PromotionTests(string archive, Release release)
    {
        var folder = Path.Combine(root, "promotion-retry");
        var initial = new PackageStore(folder); initial.Install(archive, release, CancellationToken.None);
        Test("promotion recovers from a real directory lock and transient Win32 sharing/lock errors", () =>
        {
            int attempts = 0;
            var store = PromotionStore(folder, (source, destination) =>
            {
                if (++attempts == 1)
                {
                    // GENERIC_READ participates in Windows sharing checks; a zero-access
                    // metadata handle does not. Omit FILE_SHARE_DELETE to block the rename.
                    using (var held = CreateFile(source, 0x80000000, 3, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero))
                    {
                        Assert(!held.IsInvalid, "cannot lock fixture directory: " + Marshal.GetLastWin32Error());
                        Directory.Move(source, destination);
                    }
                    // If this filesystem unexpectedly permits the rename, restore the
                    // staging path after closing the handle so Install can clean it up.
                    Directory.Move(destination, source);
                    throw new Exception("directory lock did not block promotion");
                }
                Directory.Move(source, destination);
            });
            var installed = store.Install(archive, release, CancellationToken.None);
            Assert(attempts >= 2 && attempts <= 5, "locked promotion was not retried within its bound");
            Assert(new PackageStore(folder).Selected.folder == installed.folder, "recovered promotion was not persisted");
            PackageStore.Verify(store.Folder(installed), release, CancellationToken.None);
            foreach (var failure in new Exception[] {
                new IOException("fixture sharing violation", unchecked((int)0x80070020)),
                new IOException("fixture lock violation", unchecked((int)0x80070021)),
                new UnauthorizedAccessException("fixture transient access denial") })
            {
                attempts = 0;
                store = PromotionStore(folder, (source, destination) => { if (++attempts == 1) throw failure; Directory.Move(source, destination); });
                installed = store.Install(archive, release, CancellationToken.None);
                Assert(attempts >= 2 && attempts <= 5 && store.Selected.folder == installed.folder, "transient promotion did not recover");
                PackageStore.Verify(store.Folder(installed), release, CancellationToken.None);
            }
        });
        Test("persistent promotion access denial exhausts a small bound and preserves the installed version", () =>
        {
            int attempts = 0;
            var denied = new IOException("fixture persistent access denial", unchecked((int)0x80070005));
            var store = PromotionStore(folder, (source, destination) => { attempts++; throw denied; });
            string before = Json.Write(store.State);
            try { store.Install(archive, release, CancellationToken.None); throw new Exception("persistent access denial was concealed"); }
            catch (IOException error) { Assert(Object.ReferenceEquals(error, denied), "promotion lost the original failure"); }
            Assert(attempts == 5, "promotion retries were not bounded");
            AssertPromotionUnchanged(store, before);
        });
        Test("cancellation interrupts promotion backoff without activating the staged package", () =>
        {
            int attempts = 0;
            using (var cancel = new CancellationTokenSource())
            {
                var store = PromotionStore(folder, (source, destination) =>
                {
                    attempts++; if (attempts == 1) cancel.CancelAfter(20);
                    throw new IOException("fixture sharing violation", unchecked((int)0x80070020));
                });
                string before = Json.Write(store.State);
                try { store.Install(archive, release, cancel.Token); throw new Exception("promotion ignored cancellation"); }
                catch (OperationCanceledException error) { Assert(error.CancellationToken == cancel.Token, "wrong cancellation token"); }
                Assert(attempts < 5, "cancellation waited for retry exhaustion");
                AssertPromotionUnchanged(store, before);
            }
        });
        Test("permanent IO failures are immediate and corrupt packages never reach promotion", () =>
        {
            int attempts = 0;
            var missing = new IOException("fixture path not found", unchecked((int)0x80070003));
            var store = PromotionStore(folder, (source, destination) => { attempts++; throw missing; });
            string before = Json.Write(store.State);
            try { store.Install(archive, release, CancellationToken.None); throw new Exception("permanent error was concealed"); }
            catch (IOException error) { Assert(Object.ReferenceEquals(error, missing), "permanent failure was replaced"); }
            Assert(attempts == 1, "non-transient IO error was retried");
            AssertPromotionUnchanged(store, before);
            attempts = 0;
            var corrupt = MakePackage("promotion-corrupt", badManifest: true);
            Reject(() => store.Install(corrupt, Release("promotion-corrupt", corrupt), CancellationToken.None), "corrupt package was accepted");
            Assert(attempts == 0, "corrupt data reached promotion");
            AssertPromotionUnchanged(store, before);
        });
    }
    sealed class LauncherHttp : HttpMessageHandler
    {
        public string AppRoot;
        public int Pid = 123456;
        public int Posts;
        public readonly List<string> Requests = new List<string>();
        public Dictionary<string, object> Status = new Dictionary<string, object>();
        public Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> Intercept;
        public static HttpResponseMessage Reply(object body, HttpStatusCode code = HttpStatusCode.OK)
        { return new HttpResponseMessage(code) { Content = new StringContent(Json.Write(body), Encoding.UTF8, "application/json") }; }
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        {
            token.ThrowIfCancellationRequested();
            Requests.Add(request.RequestUri.AbsolutePath);
            if (request.Method == HttpMethod.Post) Posts++;
            if (Intercept != null) return Intercept(request, token);
            return DefaultReply(request);
        }
        public Task<HttpResponseMessage> DefaultReply(HttpRequestMessage request)
        {
            switch (request.RequestUri.AbsolutePath)
            {
                case "/api/identity": return Task.FromResult(Reply(new { app = "wuwa-vr-player-launcher", root = AppRoot, pid = Pid }));
                case "/": return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StringContent("<meta name=\"wuwa-token\" content=\"fixture_token\">") });
                case "/api/status": return Task.FromResult(Reply(Status));
                default: throw new Exception("Unexpected backend request: " + request.RequestUri);
            }
        }
    }
    static void WithBridge(Action<LauncherBridge, LauncherHttp> test)
    {
        string folder = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
        Json.Save(Path.Combine(folder, "launcher.json"), new { url = "http://127.0.0.1:34567/", pid = 123456 });
        var handler = new LauncherHttp { AppRoot = Path.Combine(folder, "package", "app") };
        using (var bridge = new LauncherBridge(folder, null, handler))
        { bridge.Connect(Path.Combine(folder, "package"), CancellationToken.None).GetAwaiter().GetResult(); test(bridge, handler); }
    }
    sealed class StalledBody : Stream
    {
        readonly TaskCompletionSource<int> pending = new TaskCompletionSource<int>(TaskCreationOptions.RunContinuationsAsynchronously);
        public bool ReadStarted, Disposed;
        public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken token)
        { ReadStarted = true; return pending.Task; } // Deliberately ignores cancellation, like a wedged body read.
        protected override void Dispose(bool disposing) { Disposed = true; pending.TrySetException(new ObjectDisposedException("fixture body")); base.Dispose(disposing); }
        public override bool CanRead { get { return true; } }
        public override bool CanSeek { get { return false; } }
        public override bool CanWrite { get { return false; } }
        public override long Length { get { throw new NotSupportedException(); } }
        public override long Position { get { throw new NotSupportedException(); } set { throw new NotSupportedException(); } }
        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
        public override long Seek(long offset, SeekOrigin origin) { throw new NotSupportedException(); }
        public override void SetLength(long value) { throw new NotSupportedException(); }
        public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
    }
    static LauncherBridge TimedBridge(string folder, LauncherHttp handler, int requestMs, int connectMs,
        Func<System.Diagnostics.ProcessStartInfo, System.Diagnostics.Process> start)
    {
        return (LauncherBridge)Activator.CreateInstance(typeof(LauncherBridge), BindingFlags.NonPublic | BindingFlags.Instance, null,
            new object[] { folder, null, handler, new Func<string[]>(() => new string[0]), new Func<int, bool?>(pid => false),
                TimeSpan.FromMilliseconds(requestMs), TimeSpan.FromMilliseconds(connectMs), start }, null);
    }
    static void BridgeDeadlineTests()
    {
        Test("complete helper requests bound stalled identity/token/status/POST bodies without a live helper", () =>
        {
            foreach (string path in new[] { "/api/identity", "/", "/api/status", "/api/launch" })
            {
                string folder = Path.Combine(root, "deadline-" + Guid.NewGuid().ToString("N")), package = Path.Combine(folder, "package");
                Directory.CreateDirectory(folder);
                Json.Save(Path.Combine(folder, "launcher.json"), new { url = "http://127.0.0.1:34567/", pid = 123456 });
                var handler = new LauncherHttp { AppRoot = Path.Combine(package, "app") };
                int starts = 0; var bodies = new List<StalledBody>();
                using (var bridge = TimedBridge(folder, handler, 40, 150, info => {
                    Assert(info.EnvironmentVariables["WUWA_VR_NO_DIALOG"] == "1", "hidden helper startup could open a blocking error dialog");
                    starts++; return System.Diagnostics.Process.GetCurrentProcess(); }))
                {
                    if (path == "/api/status" || path == "/api/launch") bridge.Connect(package, CancellationToken.None).GetAwaiter().GetResult();
                    handler.Intercept = (request, token) => {
                        if (request.RequestUri.AbsolutePath != path) return handler.DefaultReply(request);
                        var body = new StalledBody(); bodies.Add(body);
                        return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new StreamContent(body) });
                    };
                    var watch = System.Diagnostics.Stopwatch.StartNew();
                    try
                    {
                        (path == "/api/status" ? (Task)bridge.Status(CancellationToken.None) : path == "/api/launch" ?
                            bridge.Post(path, new { }, CancellationToken.None) : bridge.Connect(package, CancellationToken.None)).GetAwaiter().GetResult();
                        throw new Exception("stalled body was accepted");
                    }
                    catch (TimeoutException e) { Assert(e.Message.Contains("Retry connection") || e.Message.Contains("may still be running"), "timeout lacks a next step"); }
                    Assert(watch.ElapsedMilliseconds < 3000 && bodies.Count > 0 && bodies.All(b => b.ReadStarted && b.Disposed), "body deadline/disposal failed");
                    Assert(starts <= 1 && (path != "/api/launch" || handler.Posts == 1), "timeout retried a mutation or spawned multiple helpers");
                }
            }
        });
        Test("connect bounds stalled headers and repeated receipts under one elapsed deadline", () =>
        {
            foreach (bool stalledHeaders in new[] { false, true })
            {
                string folder = Path.Combine(root, "connect-deadline-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
                string package = Path.Combine(folder, "package");
                Json.Save(Path.Combine(folder, "launcher.json"), new { url = "http://127.0.0.1:34567/", pid = 123456 });
                var handler = new LauncherHttp { AppRoot = Path.Combine(package, "app") }; int starts = 0;
                var pending = new List<TaskCompletionSource<HttpResponseMessage>>();
                handler.Intercept = (request, token) => {
                    if (!stalledHeaders) return Task.FromResult(LauncherHttp.Reply(new { app = "unrelated-server", pid = 123456 }));
                    var item = new TaskCompletionSource<HttpResponseMessage>(TaskCreationOptions.RunContinuationsAsynchronously); pending.Add(item); return item.Task;
                };
                using (var bridge = TimedBridge(folder, handler, 40, 150, info => {
                    Assert(info.EnvironmentVariables["WUWA_VR_NO_DIALOG"] == "1", "hidden helper startup could open a blocking error dialog");
                    starts++; return System.Diagnostics.Process.GetCurrentProcess(); }))
                {
                    var watch = System.Diagnostics.Stopwatch.StartNew();
                    try { bridge.Connect(package, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("connect never timed out"); }
                    catch (TimeoutException e) { Assert(e.Message.Contains("did not become ready"), "overall startup timeout lost stage"); }
                    Assert(watch.ElapsedMilliseconds < 3000 && starts == 1 && handler.Requests.Count > 1 && handler.Posts == 0, "receipt loop escaped deadline or mutated backend");
                    foreach (var item in pending) item.TrySetResult(LauncherHttp.Reply(new { }));
                }
            }
        });
    }
    static Dictionary<string, object> IdleStatus()
    {
        return Json.Read<Dictionary<string, object>>(Json.Write(new { gameRunning = false, injectorRunning = false,
            job = new { running = false }, recording = new { running = false } }));
    }
    static void WithConflict(Action<LauncherBridge, LauncherHttp> test)
    {
        string folder = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
        var handler = new LauncherHttp { AppRoot = Path.Combine(folder, "old-package", "app"), Pid = int.MaxValue, Status = IdleStatus() };
        Json.Save(Path.Combine(folder, "launcher.json"), new { url = "http://127.0.0.1:34567/", pid = handler.Pid, app = handler.AppRoot });
        using (var bridge = new LauncherBridge(folder, null, handler))
        {
            try { bridge.Connect(Path.Combine(folder, "intended-package"), CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("conflicting package attached"); }
            catch (InvalidOperationException e) { Assert(e.Message.Contains("Another WuWa launcher"), "conflict explanation missing"); }
            Assert(bridge.Conflict != null && bridge.Address == null && bridge.AppRoot == null, "conflict became the normal connection");
            test(bridge, handler);
        }
    }
    static void ConflictTests()
    {
        Test("conflicting launcher stays separate and its open address is freshly verified", () => WithConflict((bridge, http) =>
        {
            var conflict = bridge.Conflict;
            Assert(conflict.Address == "http://127.0.0.1:34567" && conflict.AppRoot == http.AppRoot && conflict.Pid == http.Pid, "conflict identity lost");
            Assert(bridge.OpenConflictAddress(CancellationToken.None).GetAwaiter().GetResult() == conflict.Address, "wrong existing launcher URL");
            Assert(http.Requests.SequenceEqual(new[] { "/api/identity", "/api/identity" }) && http.Posts == 0, "opening recovery mutated or started a helper");
            Assert(bridge.Address == null && bridge.AppRoot == null && bridge.Conflict == conflict, "opening silently attached the wrong package");
        }));
        Test("idle conflict recovery authenticates only stop and permits explicit intended-package reconnect", () => WithConflict((bridge, http) =>
        {
            var conflict = bridge.Conflict;
            int identities = 0, statuses = 0;
            http.Intercept = (request, token) =>
            {
                string path = request.RequestUri.AbsolutePath;
                if (path == "/api/identity") identities++;
                if (path == "/api/status") statuses++;
                if (request.Method == HttpMethod.Post)
                {
                    Assert(path == "/api/stop" && identities == 2 && statuses == 1, "stop lacked fresh identity/activity guards");
                    Assert(request.Headers.GetValues("Origin").Single() == conflict.Address && request.Headers.GetValues("X-WuWa-Token").Single() == "fixture_token", "stop was not authenticated to the conflicting helper");
                    return Task.FromResult(LauncherHttp.Reply(new { ok = true }));
                }
                return http.DefaultReply(request);
            };
            bridge.StopConflict(CancellationToken.None).GetAwaiter().GetResult();
            Assert(http.Posts == 1 && bridge.Conflict == null && bridge.Address == null && bridge.AppRoot == null, "stop did not leave a disconnected, resolved state");
            // Model the next package's already-started helper; no fixture executable runs.
            http.Intercept = null; http.AppRoot = Path.Combine(bridge.Data, "intended-package", "app");
            Json.Save(Path.Combine(bridge.Data, "launcher.json"), new { url = conflict.Address + "/", pid = http.Pid, app = http.AppRoot });
            bridge.Connect(Path.GetDirectoryName(http.AppRoot), CancellationToken.None).GetAwaiter().GetResult();
            Assert(bridge.AppRoot == http.AppRoot && bridge.Conflict == null && http.Posts == 1, "intended reconnect failed or wrote settings");
        }));
        Test("conflict recovery refuses game, injector, job, recording and unknown activity", () =>
        {
            foreach (string key in new[] { "gameRunning", "injectorRunning", "job", "recording" })
                foreach (string condition in new[] { "busy", "missing", "null", "wrong-type" }) WithConflict((bridge, http) =>
                {
                    var target = key == "job" || key == "recording" ? Json.Child(http.Status, key) : http.Status;
                    string field = key == "job" || key == "recording" ? "running" : key;
                    if (condition == "missing") target.Remove(field);
                    else target[field] = condition == "busy" ? (object)true : condition == "null" ? null : (object)"false";
                    var conflict = bridge.Conflict;
                    try { bridge.StopConflict(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("busy or unknown activity was stopped"); }
                    catch (InvalidOperationException error) { Assert(error.Message.Contains(condition == "busy" ? "Close WuWa" : "Cannot confirm"), "wrong activity explanation"); }
                    Assert(http.Posts == 0 && bridge.Conflict == conflict && bridge.Address == null, "activity guard lost conflict or sent a mutation");
                });
            WithConflict((bridge, http) =>
            {
                http.Status = null;
                try { bridge.StopConflict(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("null status was considered idle"); }
                catch (InvalidOperationException error) { Assert(error.Message.Contains("Cannot confirm"), "null status did not fail closed"); }
                Assert(http.Posts == 0 && bridge.Conflict != null, "null status permitted a mutation");
            });
        });
        Test("conflict recovery rejects receipt and identity changes before opening or stopping", () =>
        {
            foreach (string field in new[] { "receipt-url", "receipt-pid", "receipt-root", "identity-app", "identity-pid", "identity-root" })
                foreach (bool late in new[] { false, true }) WithConflict((bridge, http) =>
                {
                    var conflict = bridge.Conflict; bool changed = !late;
                    Action changeReceipt = () =>
                    {
                        Json.Save(Path.Combine(bridge.Data, "launcher.json"), new {
                            url = field == "receipt-url" ? "http://127.0.0.1:34568/" : conflict.Address + "/",
                            pid = field == "receipt-pid" ? 123452 : conflict.Pid,
                            app = field == "receipt-root" ? Path.Combine(bridge.Data, "replacement", "app") : conflict.AppRoot });
                    };
                    if (changed && field.StartsWith("receipt-", StringComparison.Ordinal)) changeReceipt();
                    http.Intercept = (request, token) =>
                    {
                        if (request.RequestUri.AbsolutePath == "/api/status")
                        {
                            changed = true; if (field.StartsWith("receipt-", StringComparison.Ordinal)) changeReceipt();
                        }
                        if (changed && request.RequestUri.AbsolutePath == "/api/identity" && field.StartsWith("identity-", StringComparison.Ordinal))
                            return Task.FromResult(LauncherHttp.Reply(new {
                                app = field == "identity-app" ? "different-server" : "wuwa-vr-player-launcher",
                                root = field == "identity-root" ? Path.Combine(bridge.Data, "replacement", "app") : conflict.AppRoot,
                                pid = field == "identity-pid" ? 123452 : conflict.Pid }));
                        return http.DefaultReply(request);
                    };
                    if (!late)
                    {
                        try { bridge.OpenConflictAddress(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("changed helper URL was offered"); }
                        catch (InvalidOperationException error) { Assert(error.Message.Contains("changed"), "wrong changed-helper explanation"); }
                    }
                    try { bridge.StopConflict(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("changed helper was stopped"); }
                    catch (InvalidOperationException error) { Assert(error.Message.Contains("changed"), "wrong changed-helper explanation"); }
                    Assert(http.Posts == 0 && bridge.Conflict == conflict && bridge.Address == null, "identity race sent a stop or discarded conflict");
                });
        });
        Test("conflict recovery cancellation preserves the conflict without sending stop", () => WithConflict((bridge, http) =>
        {
            var conflict = bridge.Conflict;
            http.Intercept = async (request, token) => { await Task.Delay(Timeout.Infinite, token); return LauncherHttp.Reply(new { }); };
            using (var cancel = new CancellationTokenSource())
            {
                var pending = bridge.StopConflict(cancel.Token); cancel.Cancel();
                try { pending.GetAwaiter().GetResult(); throw new Exception("conflict recovery ignored cancellation"); } catch (OperationCanceledException) { }
            }
            Assert(http.Posts == 0 && bridge.Conflict == conflict && bridge.Address == null, "cancelled recovery mutated or discarded conflict");
        }));
        Test("a rejected stop retains the conflict instead of claiming recovery", () => WithConflict((bridge, http) =>
        {
            var conflict = bridge.Conflict;
            http.Intercept = (request, token) => request.RequestUri.AbsolutePath == "/api/stop"
                ? Task.FromResult(LauncherHttp.Reply(new { error = "An operation just started" }, HttpStatusCode.BadRequest)) : http.DefaultReply(request);
            try { bridge.StopConflict(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("rejected stop was concealed"); }
            catch (InvalidOperationException error) { Assert(error.Message == "An operation just started", "stop rejection lost"); }
            Assert(http.Posts == 1 && bridge.Conflict == conflict && bridge.Address == null, "rejected stop was reported as resolved");
        }));
    }
    sealed class GuardProbe
    {
        public string[] Names = new string[0];
        public bool? Alive = false;
        public int ProcessChecks, PidChecks;
        public string[] ReadNames() { ProcessChecks++; return Names; }
        public bool? ReadPid(int pid) { PidChecks++; return Alive; }
    }
    static void WithPackageGuard(Action<LauncherBridge, LauncherHttp, string, GuardProbe> test)
    {
        string folder = Path.Combine(root, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(folder);
        string package = Path.Combine(folder, "broken-package");
        var probe = new GuardProbe();
        var http = new LauncherHttp { AppRoot = Path.Combine(package, "app"), Pid = int.MaxValue, Status = IdleStatus() };
        Json.Save(Path.Combine(folder, "launcher.json"), new { url = "http://127.0.0.1:34567/", pid = http.Pid, app = http.AppRoot });
        var constructor = typeof(LauncherBridge).GetConstructor(BindingFlags.Instance | BindingFlags.NonPublic, null,
            new[] { typeof(string), typeof(string), typeof(HttpMessageHandler), typeof(Func<string[]>), typeof(Func<int, bool?>) }, null);
        Assert(constructor != null, "native activity fixture seam missing");
        using (var bridge = (LauncherBridge)constructor.Invoke(new object[] { folder, null, http, new Func<string[]>(probe.ReadNames), new Func<int, bool?>(probe.ReadPid) }))
            test(bridge, http, package, probe);
    }
    static void PackageGuardTests()
    {
        Test("package repair permits a dead cached helper and missing executable without starting it", () => WithPackageGuard((bridge, http, package, probe) =>
        {
            Assert(!File.Exists(Path.Combine(package, "python", "pythonw.exe")), "broken fixture unexpectedly has an executable");
            bridge.Connect(package, CancellationToken.None).GetAwaiter().GetResult();
            http.Intercept = (request, token) => { throw new HttpRequestException("fixture helper exited"); };
            bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult();
            Assert(probe.PidChecks == 1 && probe.ProcessChecks >= 2 && http.Posts == 0 && bridge.Address == null && bridge.AppRoot == null,
                "dead helper repair restarted/stopped a helper or retained stale connection state");
            File.Delete(Path.Combine(bridge.Data, "launcher.json"));
            int requests = http.Requests.Count, pids = probe.PidChecks;
            bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult();
            Assert(http.Requests.Count == requests && probe.PidChecks == pids, "missing receipt attempted to start or contact a helper");
            Assert(!Directory.Exists(package), "repair guard created package files");
        }));
        Test("package repair independently refuses all game and injector image names", () =>
        {
            foreach (string name in new[] { "Client-Win64-Shipping.exe", "Wuthering Waves.exe", "Custom_UEVR_Injector.exe", "UEVRInjector.exe", "UEVRInjector" })
                WithPackageGuard((bridge, http, package, probe) =>
                {
                    probe.Names = new[] { "explorer", name };
                    try { bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("active native game/injector permitted package change"); }
                    catch (InvalidOperationException error) { Assert(error.Message.Contains("Close WuWa"), "native activity guard explanation missing"); }
                    Assert(http.Requests.Count == 0 && probe.PidChecks == 0, "active game guard contacted or changed a helper");
                });
        });
        Test("unreachable package helper requires a valid demonstrably absent PID", () =>
        {
            foreach (string condition in new[] { "missing", "zero", "negative", "invalid", "live", "unknown" }) WithPackageGuard((bridge, http, package, probe) =>
            {
                var receipt = new Dictionary<string, object> { { "url", "http://127.0.0.1:34567/" }, { "app", http.AppRoot } };
                if (condition != "missing") receipt["pid"] = condition == "zero" ? (object)0 : condition == "negative" ? -1 : condition == "invalid" ? (object)"not-a-pid" : http.Pid;
                Json.Save(Path.Combine(bridge.Data, "launcher.json"), receipt);
                probe.Alive = condition == "unknown" ? (bool?)null : true;
                http.Intercept = (request, token) => { throw new HttpRequestException("fixture helper unavailable"); };
                try { bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("unverified helper PID permitted package change"); }
                catch (InvalidOperationException error) { Assert(error.Message.Contains("process"), "unverified PID explanation missing"); }
                Assert(http.Posts == 0, "unverified process received a stop");
            });
        });
        Test("package repair refuses a reachable busy or incomplete helper status", () =>
        {
            foreach (string activity in new[] { "gameRunning", "injectorRunning", "job", "recording" })
                foreach (bool missing in new[] { false, true }) WithPackageGuard((bridge, http, package, probe) =>
                {
                    var value = activity == "job" || activity == "recording" ? Json.Child(http.Status, activity) : http.Status;
                    string key = activity == "job" || activity == "recording" ? "running" : activity;
                    if (missing) value.Remove(key); else value[key] = true;
                    try { bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("busy or unknown helper permitted package change"); }
                    catch (InvalidOperationException error) { Assert(error.Message.Contains(missing ? "Cannot confirm" : "Close WuWa"), "wrong helper activity explanation"); }
                    Assert(http.Posts == 0 && bridge.Address != null, "failed activity guard stopped or discarded the live helper");
                });
        });
        Test("package repair exposes a foreign helper for explicit recovery without stopping it", () => WithPackageGuard((bridge, http, package, probe) =>
        {
            http.AppRoot = Path.Combine(bridge.Data, "foreign-package", "app");
            Json.Save(Path.Combine(bridge.Data, "launcher.json"), new { url = "http://127.0.0.1:34567/", pid = http.Pid, app = http.AppRoot });
            try { bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("foreign helper silently replaced"); }
            catch (InvalidOperationException error) { Assert(error.Message.Contains("Another WuWa launcher"), "foreign helper explanation missing"); }
            Assert(bridge.Conflict != null && bridge.Conflict.AppRoot == http.AppRoot && bridge.Address == null && http.Posts == 0,
                "foreign helper was attached, stopped or hidden from recovery");
        }));
        Test("package repair gracefully stops only the freshly verified idle helper", () => WithPackageGuard((bridge, http, package, probe) =>
        {
            probe.Alive = true;
            http.Intercept = (request, token) =>
            {
                if (request.Method == HttpMethod.Post)
                {
                    Assert(request.RequestUri.AbsolutePath == "/api/stop" && http.Requests.Count(p => p == "/api/identity") == 2 && http.Requests.Contains("/api/status"),
                        "package guard wrote settings or stopped without identity and status checks");
                    Assert(request.Headers.GetValues("Origin").Single() == "http://127.0.0.1:34567" && request.Headers.GetValues("X-WuWa-Token").Single() == "fixture_token", "package guard stop was unauthenticated");
                    probe.Alive = false; return Task.FromResult(LauncherHttp.Reply(new { ok = true }));
                }
                return http.DefaultReply(request);
            };
            bridge.PreparePackageChange(package, CancellationToken.None).GetAwaiter().GetResult();
            Assert(http.Posts == 1 && probe.PidChecks == 1 && probe.ProcessChecks >= 2 && bridge.Address == null && bridge.AppRoot == null && bridge.Conflict == null,
                "idle helper stop did not wait for exit and clear state");
            Assert(!Directory.Exists(package), "package guard started or created its missing helper");
        }));
        Test("package repair refuses in-flight process/receipt changes and honours cancellation", () =>
        {
            foreach (string condition in new[] { "process", "receipt", "cancel" }) WithPackageGuard((bridge, http, package, probe) =>
            {
                using (var cancel = new CancellationTokenSource())
                {
                    http.Intercept = (request, token) =>
                    {
                        if (request.RequestUri.AbsolutePath == "/api/status")
                        {
                            if (condition == "process") probe.Names = new[] { "UEVRInjector.exe" };
                            if (condition == "receipt") Json.Save(Path.Combine(bridge.Data, "launcher.json"), new { url = "http://127.0.0.1:34568/", pid = http.Pid, app = http.AppRoot });
                            if (condition == "cancel") cancel.Cancel();
                        }
                        return http.DefaultReply(request);
                    };
                    try { bridge.PreparePackageChange(package, cancel.Token).GetAwaiter().GetResult(); throw new Exception("changed/cancelled preparation permitted a package change"); }
                    catch (OperationCanceledException) { Assert(condition == "cancel", "unexpected preparation cancellation"); }
                    catch (InvalidOperationException error) { Assert(condition != "cancel" && error.Message.Contains(condition == "process" ? "Close WuWa" : "changed"), "wrong changed-state explanation"); }
                    Assert(http.Posts == 0 && bridge.Address != null, "preparation race stopped or discarded the helper");
                }
            });
        });
    }
    static async Task Smoke(string folder, string suppliedArchive = null)
    {
        var store = new PackageStore(Path.Combine(folder, "store"));
        var release = RepoClient.BundledCatalog().releases[0];
        if (suppliedArchive != null)
        {
            var supplied = Path.GetFullPath(suppliedArchive);
            if (new FileInfo(supplied).Length != release.size || RepoClient.Hash(supplied) != release.sha256)
                throw new InvalidDataException("Smoke archive does not match the selected bundled catalog release.");
            Directory.CreateDirectory(store.Cache);
            File.Copy(supplied, Path.Combine(store.Cache, release.sha256 + ".zip"), true);
        }
        using (var client = new RepoClient())
        {
            var archive = await client.Download(release, store.Cache, null, CancellationToken.None);
            Console.WriteLine("DOWNLOADED " + release.size + " bytes SHA256 " + RepoClient.Hash(archive));
            var item = store.Install(archive, release, CancellationToken.None);
            Console.WriteLine("VERIFIED " + item.release.id + " at " + store.Folder(item));
            using (var bridge = new LauncherBridge(Path.Combine(folder, "backend-data"), Path.Combine(folder, "test-roaming")))
            {
                try
                {
                    await bridge.Connect(store.Folder(item), CancellationToken.None);
                    var status = await bridge.Status(CancellationToken.None);
                    Assert(Json.Text(Json.Child(status, "package"), "id") == "wuwa-vr-launcher-" + release.id, "backend identity wrong");
                    await bridge.Post("/api/language", new { language = "zh-Hans" }, CancellationToken.None);
                    Assert(File.ReadAllText(Path.Combine(folder, "backend-data", "language.txt")).Trim() == "zh-Hans", "language not saved");
                    Console.WriteLine("BRIDGE PASS identity, status, authenticated language write; no injection or runtime change");
                }
                finally { if (bridge.Address != null) await bridge.Stop(CancellationToken.None); }
            }
        }
    }
    static void DiscoveryTests()
    {
        string official = @"C:\Program Files\Wuthering Waves\launcher.exe", custom = @"D:\Games\Wuthering Waves\launcher.exe";
        var empty = new Dictionary<string, object>();
        var files = new HashSet<string>(new[] { official, custom }, StringComparer.OrdinalIgnoreCase);
        Func<string, bool> exists = files.Contains;
        var none = new GameDiscovery.Registration[0];
        Test("default game discovered without installed VR helper and deduplicated", () =>
        {
            var found = GameDiscovery.Resolve(null, empty, none, new[] { official, official.ToUpperInvariant() }, exists);
            Assert(found.CanUse && found.Path == official && found.Candidates.Count == 1, "default launcher not selected uniquely");
        });
        Test("quoted DisplayIcon-only registration recognized", () =>
        {
            var entry = new GameDiscovery.Registration { Name = "Wuthering Waves", Key = "KRInstall Wuthering Waves Overseas", Icon = "\"" + custom + "\",0" };
            var found = GameDiscovery.Resolve(null, empty, new[] { entry }, new string[0], exists);
            Assert(found.CanUse && found.Path == custom, "DisplayIcon without InstallLocation ignored");
        });
        Test("saved custom path wins and stale saved path is not silently replaced", () =>
        {
            var saved = Json.Read<Dictionary<string, object>>(Json.Write(new { gameStart = "launcher", gameLauncher = custom }));
            var found = GameDiscovery.Resolve(null, saved, none, new[] { official }, exists);
            Assert(found.CanUse && found.Path == custom, "saved custom choice replaced");
            saved["gameLauncher"] = @"Z:\Missing\launcher.exe";
            found = GameDiscovery.Resolve(null, saved, none, new[] { official }, exists);
            Assert(!found.CanUse && found.Message == "gameSavedMissing" && found.Path == (string)saved["gameLauncher"], "stale choice silently changed");
        });
        Test("manual startup remains manual until user explicitly chooses a launcher", () =>
        {
            var saved = Json.Read<Dictionary<string, object>>("{\"gameStart\":\"manual\"}");
            var found = GameDiscovery.Resolve(null, saved, none, new[] { official }, p => { throw new Exception("manual route must not probe candidates"); });
            Assert(!found.CanUse && found.Path == null && found.Message == "gameManualSaved", "manual choice changed");
            Assert(GameDiscovery.Resolve(custom, saved, none, new[] { official }, exists, true).Path == custom, "deliberate Browse ignored");
        });
        Test("Steam installation never becomes official default", () =>
        {
            var steam = new GameDiscovery.Registration { Name = "Wuthering Waves", Key = "Steam App 3513350", Location = Path.GetDirectoryName(custom), Icon = custom };
            var found = GameDiscovery.Resolve(null, empty, new[] { steam }, new[] { custom, official }, exists);
            Assert(found.CanUse && found.Path == official && found.Candidates.Count == 1, "Steam route promoted to official");
        });
        Test("several installations require a choice", () =>
        {
            var found = GameDiscovery.Resolve(null, empty, none, new[] { official, custom }, exists);
            Assert(!found.CanUse && found.Path == null && found.Candidates.Count == 2 && found.Message == "gameMultiple", "arbitrary installation selected");
            Assert(GameDiscovery.PathToApply(null, found, empty, exists) == null, "ambiguous suggestion applied");
        });
        Test("relative paths and launch arguments are rejected before probing", () =>
        {
            foreach (string path in new[] { "launcher.exe", "C:launcher.exe", @"\Program Files\Wuthering Waves\launcher.exe", official + " --launch", official.Replace("Wuthering", "Wuther\ning"), @"C:\Game\Client.exe" })
            {
                Assert(GameDiscovery.ExistingLauncher(path, p => { throw new Exception("invalid path probed: " + path); }) == null, "unsafe launcher accepted");
            }
        });
        Test("automatic discovery cannot override saved helper startup choices", () =>
        {
            var found = GameDiscovery.Resolve(null, empty, none, new[] { official }, exists);
            foreach (string mode in new[] { "manual", "launcher" })
            {
                var game = Json.Read<Dictionary<string, object>>(Json.Write(new { mode, launcher = custom, saved = true }));
                Assert(GameDiscovery.PathToApply(null, found, game, exists) == null, "auto suggestion overwrote saved choice");
                Assert(GameDiscovery.PathToApply(official, found, game, exists, true) == official, "explicit user selection not applied");
            }
            Assert(GameDiscovery.PathToApply(null, found, empty, exists) == official, "unsaved choice not populated");
            var manual = Json.Read<Dictionary<string, object>>("{\"gameStart\":\"manual\"}");
            Assert(GameDiscovery.Resolve(official, manual, none, new[] { official }, exists).Message == "gameManualSaved", "historical manager path replaced later manual choice");
            var customSaved = Json.Read<Dictionary<string, object>>(Json.Write(new { gameStart = "launcher", gameLauncher = custom }));
            Assert(GameDiscovery.Resolve(official, customSaved, none, new[] { official }, exists).Path == custom, "historical manager path replaced later helper launcher");
            var savedGame = Json.Read<Dictionary<string, object>>(Json.Write(new { mode = "manual", saved = true }));
            Assert(GameDiscovery.PathToApply(official, found, savedGame, exists) == null, "historical native choice remained a permanent override");
        });
        Test("discovery honors isolated data and leaves saved settings untouched", () =>
        {
            string folder = Path.Combine(root, "discovery-data"); Directory.CreateDirectory(folder);
            string file = Path.Combine(folder, "settings.json"), body = "{\"gameStart\":\"manual\"}"; File.WriteAllText(file, body);
            Assert(GameDiscovery.Discover(null, folder, CancellationToken.None).Message == "gameManualSaved", "different data root read");
            Assert(File.ReadAllText(file) == body, "settings changed during discovery");
            File.WriteAllText(file, "{broken");
            Assert(GameDiscovery.Discover(null, folder, CancellationToken.None).Message == "gameSettingsUnreadable", "unreadable settings replaced");
            Assert(File.ReadAllText(file) == "{broken", "unreadable settings overwritten");
        });
        string steamRoot = @"D:\SteamLibrary", steamPath = steamRoot + @"\steamapps\common\Wuthering Waves\Wuthering Waves.exe";
        string shipping = steamRoot + @"\steamapps\common\Wuthering Waves\Client\Binaries\Win64\Client-Win64-Shipping.exe";
        string manifest = steamRoot + @"\steamapps\appmanifest_3513350.acf";
        var steamFiles = new HashSet<string>(new[] { official, steamPath, shipping }, StringComparer.OrdinalIgnoreCase);
        var metadata = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            { manifest, "\"AppState\" { \"appid\" \"3513350\" \"installdir\" \"Wuthering Waves\" }" },
            { @"C:\Steam\steamapps\libraryfolders.vdf", "\"libraryfolders\" { \"1\" { \"path\" \"D:\\\\SteamLibrary\" \"apps\" { \"3513350\" \"100\" } } }" }
        };
        Func<string, string> readSteam = p => metadata.ContainsKey(p) ? metadata[p] : null;
        Test("Steam library metadata discovers validated bootstrap with independent launch mode", () =>
        {
            var paths = GameDiscovery.SteamCandidates(new[] { @"C:\Steam", steamRoot }, steamFiles.Contains, readSteam);
            Assert(paths.Count == 1 && paths[0] == steamPath, "secondary Steam library was missed or duplicated");
            var found = GameDiscovery.Resolve(null, empty, none, new string[0], steamFiles.Contains, false, paths, readSteam);
            Assert(found.CanUse && found.Mode == "steam" && found.Path == steamPath && found.Message == "gameSteamFound", "Steam treated as official or not discovered");
            Assert(found.Choices.Count == 1 && found.Choices[0].Mode == "steam", "picker lost Steam identity");
        });
        Test("official and Steam coexist without silently choosing either", () =>
        {
            var found = GameDiscovery.Resolve(null, empty, none, new[] { official }, steamFiles.Contains, false, new[] { steamPath }, readSteam);
            Assert(!found.CanUse && found.Message == "gameMultiple" && found.Choices.Count == 2, "multi-install default was silently selected");
            Assert(GameDiscovery.PathToApply(null, found, empty, steamFiles.Contains, false, readSteam) == null, "ambiguous install applied");
            foreach (string mode in new[] { "launcher", "manual", "steam" })
            {
                var saved = Json.Read<Dictionary<string, object>>(Json.Write(new { gameStart = mode, gameLauncher = mode == "steam" ? steamPath : official }));
                var selected = GameDiscovery.Resolve(steamPath, saved, none, new[] { official }, steamFiles.Contains, false, new[] { steamPath }, readSteam);
                Assert(mode == "manual" ? selected.Message == "gameManualSaved" : selected.Mode == mode, "saved route replaced: " + mode);
                var game = Json.Read<Dictionary<string, object>>(Json.Write(new { mode, launcher = mode == "steam" ? steamPath : official, saved = true }));
                Assert(GameDiscovery.PathToApply(steamPath, found, game, steamFiles.Contains, false, readSteam) == null, "cached Steam path overrode saved route");
                Assert(GameDiscovery.PathToApply(steamPath, found, game, steamFiles.Contains, true, readSteam) == (mode == "steam" ? null : steamPath), "explicit Steam selection did not apply exactly once");
            }
        });
        Test("Steam selection rejects malformed or mismatched install identity", () =>
        {
            string original = metadata[manifest];
            foreach (string bad in new[] { "", original.Replace("3513350", "999999"), original.Replace("Wuthering Waves", "Other"), original + " \"appid\" \"3513350\"", new string('x', 1024 * 1024 + 1) })
            {
                metadata[manifest] = bad;
                Assert(GameDiscovery.ExistingChoice(steamPath, steamFiles.Contains, readSteam) == null, "invalid manifest accepted");
            }
            metadata[manifest] = original;
            steamFiles.Remove(shipping);
            Assert(GameDiscovery.ExistingChoice(steamPath, steamFiles.Contains, readSteam) == null, "missing real game executable accepted");
            steamFiles.Add(shipping);
            foreach (string bad in new[] { "Wuthering Waves.exe", @"C:Wuthering Waves.exe", steamPath + " -foo", @"C:\Other\Wuthering Waves.exe", steamPath.Replace("Wuthering Waves.exe", "Client-Win64-Shipping.exe") })
                Assert(GameDiscovery.ExistingChoice(bad, p => true, readSteam) == null, "non-bootstrap/argument path accepted");
            metadata[manifest] = original.Replace("Wuthering Waves", "..\\..\\Elsewhere");
            Assert(GameDiscovery.SteamCandidates(new[] { steamRoot }, p => true, readSteam).Count == 0, "manifest path traversal accepted");
            metadata[manifest] = original;
        });
        Test("saved Steam identity remains saved when missing and cannot silently become official", () =>
        {
            var saved = Json.Read<Dictionary<string, object>>(Json.Write(new { gameStart = "steam", gameLauncher = steamPath }));
            var found = GameDiscovery.Resolve(null, saved, none, new[] { official }, steamFiles.Contains, false, null, readSteam);
            Assert(found.CanUse && found.Mode == "steam" && found.Message == "gameSteamSaved", "saved Steam selection not restored");
            steamFiles.Remove(steamPath);
            found = GameDiscovery.Resolve(null, saved, none, new[] { official }, steamFiles.Contains, false, null, readSteam);
            Assert(!found.CanUse && found.Path == steamPath && found.Message == "gameSavedMissing", "missing Steam silently replaced");
            steamFiles.Add(steamPath);
            saved["gameLauncher"] = official;
            Assert(!GameDiscovery.Resolve(null, saved, none, new[] { official }, steamFiles.Contains, false, null, readSteam).CanUse, "Steam mode accepted official executable");
        });
    }
    [STAThread]
    static int Main(string[] args)
    {
        Program.ConfigureRuntime();
        try
        {
            if (args.Length == 2 && args[0] == "--activation-probe") return InstanceActivation.TryActivate(Path.GetFullPath(args[1])) ? 0 : 2;
            if (args.Length == 2 && args[0] == "--discover")
            { Console.WriteLine(Json.Write(GameDiscovery.Discover(null, Path.GetFullPath(args[1]), CancellationToken.None))); return 0; }
            if (args.Length == 3 && args[0] == "--e2e") return EndToEndTests.Run(Path.GetFullPath(args[1]), Path.GetFullPath(args[2]));
            if ((args.Length == 2 || args.Length == 3) && args[0] == "--smoke") { Smoke(Path.GetFullPath(args[1]), args.Length == 3 ? args[2] : null).GetAwaiter().GetResult(); return 0; }
            root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../../work", Guid.NewGuid().ToString("N")));
            Directory.CreateDirectory(root);
            JsonSaveTests();
            var zip = MakePackage("test-v1"); var release = Release("test-v1", zip);
            Test("localization coverage and embedded catalog", () => { new Strings(); RepoClient.BundledCatalog().Validate(); });
            Test("deep installation paths support verification and removal beyond MAX_PATH", () =>
            {
                var deepRoot = Path.Combine(root, new string('x', 95), new string('y', 95));
                var deepStore = new PackageStore(deepRoot);
                var deepItem = deepStore.Install(zip, release, CancellationToken.None);
                Assert(Path.Combine(deepStore.Folder(deepItem), "app/dev/wuwa_player.py").Length > 260, "fixture did not exceed Windows legacy path limit");
                PackageStore.Verify(deepStore.Folder(deepItem), release, CancellationToken.None);
                deepStore.Remove(deepItem);
                Assert(deepStore.Selected == null, "deep-path removal failed");
            });
            DiscoveryTests();
            Test("unconnected runtime never claims an earlier runtime is ready", () =>
            {
                var state = Json.Read<Dictionary<string, object>>("{\"openxr\":{\"name\":\"SteamVR\",\"available\":true}}");
                Assert(LauncherPresentation.RuntimeSummary(false, state, k => k) == "runtimeHint", "stale runtime shown as current");
            });
            Test("connected helper reports absent runtime instead of install hint", () =>
            {
                var state = Json.Read<Dictionary<string, object>>("{\"openxr\":{\"available\":false}}");
                Assert(LauncherPresentation.RuntimeSummary(true, state, k => k) == "runtimeMissing", "missing runtime concealed");
            });
            Test("named broken and available runtimes stay distinct", () =>
            {
                foreach (bool available in new[] { false, true })
                {
                    var state = Json.Read<Dictionary<string, object>>(Json.Write(new { openxr = new { name = "SteamVR", available } }));
                    Assert(LauncherPresentation.RuntimeSummary(true, state, k => k) == "SteamVR · " + (available ? "runtimeDetected" : "runtimeUnavailable"), "runtime state conflated");
                }
            });
            Test("runtime summary distinguishes foreign simulator and first-time setup recovery", () =>
            {
                var s = Json.Read<Dictionary<string, object>>(Json.Write(new { openxr = new { name = "", available = false, canSimulator = true } }));
                Assert(LauncherPresentation.RuntimeSummary(true, s, k => k) == "runtimeMissingSimulator", "fresh PC hides valid simulator recovery");
                s["openxr"] = new Dictionary<string, object> { { "name", "OpenXR Simulator" }, { "available", true }, { "isSimulator", true }, { "isBundledSimulator", false } };
                Assert(LauncherPresentation.RuntimeSummary(true, s, k => k).Contains("runtimeOtherSimulator"), "another package's simulator mislabeled current");
                Json.Child(s, "openxr")["isBundledSimulator"] = true;
                Assert(LauncherPresentation.RuntimeSummary(true, s, k => k).Contains("runtimeBundledSimulator"), "selected bundled simulator not distinguished");
            });
            Test("choosing another release never labels it installed", () =>
            {
                Assert(LauncherPresentation.PackageSummary("old", "candidate", true, k => k) == "otherInstalled", "wrong selected release labelled installed");
                Assert(LauncherPresentation.PackageSummary("candidate", "candidate", true, k => k) == "installed", "same release not recognized");
                Assert(LauncherPresentation.PackageSummary(null, "candidate", true, k => k) == "candidatePackage", "fresh candidate state wrong");
            });
            Test("backend cancel only applies to an active launch", () =>
            {
                foreach (var kind in new[] { "launch", "recording", "check", "restore", "unknown", "" })
                foreach (bool running in new[] { false, true })
                {
                    var job = Json.Read<Dictionary<string, object>>(Json.Write(new { kind, running }));
                    Assert(LauncherPresentation.CanCancelBackend(job) == (running && kind == "launch"), "unsupported job cancellation exposed");
                }
            });
            Test("publishable catalog passes online policy and retains private bundled candidate", () =>
            {
                var body = File.ReadAllBytes(Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "../../catalog.public.json")));
                using (var client = new RepoClient(new FixtureHttp(body)))
                {
                    var online = client.FetchCatalog(CancellationToken.None).GetAwaiter().GetResult();
                    Assert(online.releases.All(r => r.channel == "beta" && !String.IsNullOrEmpty(r.url)), "private release in public feed");
                    var combined = RepoClient.KeepBundledCandidates(online, new Catalog { schema = 1, releases = new List<Release> { Candidate(zip) } });
                    Assert(combined.releases.Any(r => r.channel == "candidate") && combined.releases.Any(r => r.channel == "beta"), "refresh loses available release type");
                }
            });
            Test("reject traversal, absolute paths and Windows device names", () =>
            {
                foreach (var path in new[] { "../escape", "a/../../escape", "C:/escape", "a:stream", "a./file", "a /file", "CON.txt", "a/../b", "a//b", "\\\\host\\share" })
                    Reject(() => Paths.Inside(root, path), "unsafe path accepted: " + path);
            });
            Test("non-English path supported by package storage", () => { Assert(Paths.Inside(root, "鸣潮/日本語.txt").StartsWith(root), "Unicode failed"); });
            Test("release rejects foreign assets and malformed hash", () =>
            {
                var bad = Release("test-v1", zip); bad.url = "https://example.com/test.zip"; Reject(bad.Validate, "foreign URL accepted");
                bad = Release("test-v1", zip); bad.sha256 = "missing"; Reject(bad.Validate, "missing checksum accepted");
            });
            Test("resume interrupted download", () => DownloadTest(zip, false));
            Test("complete bundle installs without a network request", () =>
            {
                var bundled = Path.Combine(root, "bundled");
                var payload = Paths.Inside(bundled, release.id + "/WuWa-VR-Launcher.zip");
                Directory.CreateDirectory(Path.GetDirectoryName(payload)); File.Copy(zip, payload);
                var handler = new FixtureHttp(File.ReadAllBytes(zip));
                using (var client = new RepoClient(handler))
                {
                    var archive = client.Download(release, Path.Combine(root, "unused-cache"), null, CancellationToken.None, bundled).GetAwaiter().GetResult();
                    Assert(archive == payload && handler.Requests == 0, "bundle unexpectedly downloaded");
                    var local = new PackageStore(Path.Combine(root, "offline-install"));
                    var item = local.Install(archive, release, CancellationToken.None);
                    PackageStore.Verify(local.Folder(item), release, CancellationToken.None);
                }
            });
            Test("corrupt bundle is rejected without altering it or falling back silently", () =>
            {
                var bundled = Path.Combine(root, "bad-bundle");
                var payload = Paths.Inside(bundled, release.id + "/WuWa-VR-Launcher.zip");
                Directory.CreateDirectory(Path.GetDirectoryName(payload));
                var bytes = File.ReadAllBytes(zip); bytes[0] ^= 1; File.WriteAllBytes(payload, bytes);
                var handler = new FixtureHttp(File.ReadAllBytes(zip));
                using (var client = new RepoClient(handler))
                    Reject(() => client.Download(release, Path.Combine(root, "bad-bundle-cache"), null, CancellationToken.None, bundled).GetAwaiter().GetResult(), "corrupt bundle accepted");
                Assert(handler.Requests == 0 && File.ReadAllBytes(payload).SequenceEqual(bytes), "corrupt local source was changed or ignored");
            });
            Test("missing selected bundle uses the normal download path", () =>
            {
                var handler = new FixtureHttp(File.ReadAllBytes(zip));
                using (var client = new RepoClient(handler))
                {
                    var archive = client.Download(release, Path.Combine(root, "bundle-fallback"), null, CancellationToken.None, Path.Combine(root, "no-bundle")).GetAwaiter().GetResult();
                    Assert(handler.Requests == 1 && RepoClient.Hash(archive) == release.sha256, "missing bundle did not download");
                }
            });
            Test("cancelled bundled acquisition never installs or requests a download", () =>
            {
                var handler = new FixtureHttp(File.ReadAllBytes(zip));
                using (var client = new RepoClient(handler)) using (var cancel = new CancellationTokenSource())
                {
                    cancel.Cancel();
                    try { client.Download(release, Path.Combine(root, "cancelled-bundle"), null, cancel.Token, Path.Combine(root, "bundled")).GetAwaiter().GetResult(); throw new Exception("cancel ignored"); }
                    catch (OperationCanceledException) { Assert(handler.Requests == 0, "cancelled request sent"); }
                }
            });
            Test("restart when server ignores Range", () => DownloadTest(zip, true));
            Test("truncated response retained for retry", () =>
            {
                string cache = Path.Combine(root, "truncated");
                using (var client = new RepoClient(new FixtureHttp(File.ReadAllBytes(zip), truncate: true)))
                {
                    try { client.Download(release, cache, null, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("truncated response accepted"); }
                    catch (IOException) { Assert(new FileInfo(Path.Combine(cache, release.sha256 + ".zip.partial")).Length == 5000, "partial not kept"); }
                }
            });
            Test("reject incorrect resume offset", () =>
            {
                string cache = Path.Combine(root, "bad-range"); Directory.CreateDirectory(cache); File.WriteAllBytes(Path.Combine(cache, release.sha256 + ".zip.partial"), File.ReadAllBytes(zip).Take(1000).ToArray());
                using (var client = new RepoClient(new FixtureHttp(File.ReadAllBytes(zip), badRange: true))) Reject(() => client.Download(release, cache, null, CancellationToken.None).GetAwaiter().GetResult(), "bad resume accepted");
            });
            Test("checksum failure never promoted to cache", () =>
            {
                string cache = Path.Combine(root, "bad-sha"); var bad = Release("test-v1", zip); bad.sha256 = new string('0', 64);
                using (var client = new RepoClient(new FixtureHttp(File.ReadAllBytes(zip)))) Reject(() => client.Download(bad, cache, null, CancellationToken.None).GetAwaiter().GetResult(), "bad hash accepted");
                Assert(!Directory.GetFiles(cache).Any(), "bad download retained as installable");
            });
            Test("local candidate installs its verified bundle without HTTP", () =>
            {
                var archive = MakePackage("candidate-test"); var r = Candidate(archive);
                string bundled = Path.Combine(root, "candidate-bundle"), payload = Paths.Inside(bundled, r.id + "/WuWa-VR-Launcher.zip");
                Directory.CreateDirectory(Path.GetDirectoryName(payload)); File.Copy(archive, payload);
                var handler = new FixtureHttp(new byte[0]);
                using (var client = new RepoClient(handler))
                {
                    var found = client.Download(r, Path.Combine(root, "candidate-cache"), null, CancellationToken.None, bundled).GetAwaiter().GetResult();
                    var local = new PackageStore(Path.Combine(root, "candidate-installed"));
                    local.Install(found, r, CancellationToken.None);
                    Assert(local.Selected.release.channel == "candidate" && handler.Requests == 0, "candidate was published or downloaded");
                }
            });
            Test("missing local candidate never attempts a public download", () =>
            {
                var handler = new FixtureHttp(new byte[0]);
                using (var client = new RepoClient(handler))
                    Reject(() => client.Download(Candidate(zip), Path.Combine(root, "missing-candidate"), null, CancellationToken.None).GetAwaiter().GetResult(), "candidate downloaded");
                Assert(handler.Requests == 0, "missing candidate caused network request");
            });
            Test("candidate cannot advertise a public release URL", () =>
            {
                var r = Candidate(zip); r.url = release.url;
                Reject(r.Validate, "candidate pretended to be public");
            });
            Test("remote catalog cannot introduce unpublished candidates", () =>
            {
                var catalog = new Catalog { schema = 1, releases = new List<Release> { Candidate(zip) } };
                using (var client = new RepoClient(new FixtureHttp(Encoding.UTF8.GetBytes(Json.Write(catalog)))))
                    Reject(() => client.FetchCatalog(CancellationToken.None).GetAwaiter().GetResult(), "remote candidate accepted");
            });
            Test("update refresh preserves the bundled candidate beside public beta", () =>
            {
                var local = new Catalog { schema = 1, releases = new List<Release> { Candidate(zip) } };
                var online = new Catalog { schema = 1, releases = new List<Release> { release } };
                var combined = RepoClient.KeepBundledCandidates(online, local);
                Assert(combined.releases.Count == 2 && combined.releases[0].channel == "candidate" && combined.releases[1].id == release.id, "refresh lost local candidate");
            });
            var store = new PackageStore(Path.Combine(root, "installed")); Installed first = null, second = null;
            Test("fresh install with per-file verification", () => { first = store.Install(zip, release, CancellationToken.None); Assert(store.Selected.folder == first.folder, "selection failed"); PackageStore.Verify(store.Folder(first), release, CancellationToken.None); });
            ArchiveIdentityTests(store, zip, release);
            PromotionTests(zip, release);
            Test("failed update leaves current selection intact", () =>
            {
                var badZip = MakePackage("test-v2", badManifest: true); Reject(() => store.Install(badZip, Release("test-v2", badZip), CancellationToken.None), "bad file hash accepted");
                Assert(new PackageStore(store.Root).Selected.folder == first.folder, "failed install changed selection");
            });
            Test("zip slip rejected without outside writes", () =>
            {
                var badZip = MakePackage("test-v2", "../../escaped.txt"); Reject(() => store.Install(badZip, Release("test-v2", badZip), CancellationToken.None), "zip slip accepted");
                Assert(!File.Exists(Path.Combine(root, "escaped.txt")), "outside write");
            });
            Test("unmanifested files rejected", () =>
            { var badZip = MakePackage("test-v2", "WuWa VR/extra.exe"); Reject(() => store.Install(badZip, Release("test-v2", badZip), CancellationToken.None), "extra executable accepted"); });
            Test("cancelled install does not activate", () =>
            {
                var cancel = new CancellationTokenSource(); cancel.Cancel();
                try { store.Install(zip, release, cancel.Token); throw new Exception("cancel ignored"); } catch (OperationCanceledException) { }
                Assert(store.Selected.folder == first.folder, "cancel changed selection");
            });
            Test("upgrade retains old package and external settings", () =>
            {
                string settings = Path.Combine(root, "player-settings.json"); File.WriteAllText(settings, "KEEP"); var secondZip = MakePackage("test-v2");
                second = store.Install(secondZip, Release("test-v2", secondZip), CancellationToken.None);
                Assert(store.State.previous == first.folder && Directory.Exists(store.Folder(first)), "previous release lost"); Assert(File.ReadAllText(settings) == "KEEP", "settings overwritten");
            });
            Test("reselecting the active version preserves rollback across restart", () =>
            {
                store.Select(second);
                var reopened = new PackageStore(store.Root);
                Assert(reopened.Selected.folder == second.folder && reopened.State.previous == first.folder, "reselection replaced rollback target");
                reopened.Select(reopened.State.installed.First(i => i.folder == reopened.State.previous));
                Assert(reopened.Selected.folder == first.folder, "retained version could not be restored");
            });
            Test("rollback survives restarting installer", () => { store.Select(first); Assert(new PackageStore(store.Root).Selected.folder == first.folder, "rollback not persisted"); });
            Test("locked uninstall restores already moved files and keeps selection", () =>
            {
                using (var held = new FileStream(Path.Combine(store.Folder(second), "app/dev/wuwa_player.py"), FileMode.Open, FileAccess.Read, FileShare.None))
                {
                    try { store.Remove(second); throw new Exception("locked uninstall accepted"); } catch (IOException) { }
                }
                PackageStore.Verify(store.Folder(second), second.release, CancellationToken.None);
                Assert(new PackageStore(store.Root).State.installed.Count == 2, "failed removal lost its receipt");
            });
            Test("removal preserves unrecognized files and other versions", () =>
            {
                var note = Path.Combine(store.Folder(second), "my-note.txt"); File.WriteAllText(note, "KEEP"); var remaining = store.Remove(second);
                Assert(remaining == 1 && File.ReadAllText(note) == "KEEP", "user file removed"); Assert(File.Exists(Path.Combine(store.Folder(first), "app/portable.json")), "other version removed");
            });
            Test("removal requires a current restoration, including old helpers with stale timestamps", () =>
            {
                var s = new Dictionary<string, object>();
                Assert(!LauncherBridge.RemovalNeedsRestore(s), "unused package requires a nonexistent backup");
                s["originalBackup"] = true;
                Assert(LauncherBridge.RemovalNeedsRestore(s), "unrestored profile permitted removal");
                s["restoredOriginalAt"] = "2026-10-03T12:00:00Z"; s["selected"] = "";
                Assert(!LauncherBridge.RemovalNeedsRestore(s), "restored profile blocked removal");
                s["selected"] = "test-build";
                Assert(LauncherBridge.RemovalNeedsRestore(s), "old restoration timestamp concealed a reapplied build");
                s["selected"] = ""; s["originalBackup"] = false;
                s["openxr"] = new Dictionary<string, object> { { "isSimulator", true } };
                Assert(LauncherBridge.RemovalNeedsRestore(s), "active bundled runtime permitted removal");
            });
            Test("running game is not reported as confirmed stereo", () =>
            {
                var s = new Dictionary<string, object> { { "gameRunning", true } }; Assert(LauncherBridge.LaunchSummary(s, x => x) == "awaitingStereo", "false success");
                s["launch"] = new Dictionary<string, object> { { "current", true }, { "phase", "failed" }, { "firstFrameSeen", true }, { "injectorRunning", true } };
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "launchFailed", "failure concealed");
                s["gameRunning"] = false; Json.Child(s, "launch")["phase"] = "finished";
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "gameNotRunning", "old frames imply running game");
            });
            Test("stereo evidence must match the current injector and selected build", () =>
            {
                var s = Json.Read<Dictionary<string, object>>(Json.Write(new {
                    gameRunning = true, injectorRunning = true, selectionMatches = true, selected = "test-build",
                    launch = new { phase = "finished", firstFrameSeen = true, injectorRunning = true, injectorPid = 77, buildId = "test-build" },
                    processes = new[] { new { name = "Custom_UEVR_Injector.exe", pid = 78 } }
                }));
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "awaitingStereo", "stale injector confirmed");
                Json.Child(s, "launch")["injectorPid"] = 78;
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "stereoSeen", "matching evidence not reported");
                s["selected"] = "another-build"; Assert(LauncherBridge.LaunchSummary(s, x => x) == "awaitingStereo", "wrong build confirmed");
                s["selected"] = "test-build"; s["job"] = new Dictionary<string, object> { { "kind", "launch" }, { "error", true } };
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "launchFailed", "failed checks concealed by finished phase");
                s.Remove("job"); Json.Child(s, "launch")["phase"] = "cancelled"; Json.Child(s, "launch")["current"] = true;
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "launchCancelled", "cancellation concealed");
            });
            Test("external launch progress and cancellation require current correlated worker state", () =>
            {
                var s = Json.Read<Dictionary<string, object>>(Json.Write(new { gameRunning = false,
                    job = new { kind = "launch", running = false, code = 0 },
                    launch = new { current = true, running = true, cancellable = true, phase = "waiting-game", message = "Waiting for Steam" } }));
                Assert(LauncherBridge.CanCancelLaunch(s) && LauncherBridge.LaunchWorkerRunning(s) && LauncherBridge.LaunchSummary(s, x => x) == "Waiting for Steam", "worker lost after dispatch job completed");
                Json.Child(s, "launch")["current"] = false;
                Assert(!LauncherBridge.CanCancelLaunch(s) && !LauncherBridge.LaunchWorkerRunning(s), "stale worker exposed cancel");
                Json.Child(s, "launch")["phase"] = "failed";
                Assert(LauncherBridge.LaunchSummary(s, x => x) == "gameNotRunning", "stale failure overwrote new state");
            });
            BridgeDeadlineTests();
            Test("backend launch errors reach the UI with authenticated request", () => WithBridge((bridge, http) =>
            {
                http.Intercept = async (request, token) =>
                {
                    Assert(request.RequestUri.AbsolutePath == "/api/launch", "wrong endpoint");
                    Assert(request.Headers.GetValues("Origin").Single() == "http://127.0.0.1:34567", "origin missing");
                    Assert(request.Headers.GetValues("X-WuWa-Token").Single() == "fixture_token", "token missing");
                    Assert(Json.Text(Json.Read<Dictionary<string, object>>(await request.Content.ReadAsStringAsync()), "id") == "test-build", "wrong build requested");
                    return LauncherHttp.Reply(new { error = "Windows permission was cancelled" }, HttpStatusCode.BadRequest);
                };
                try { bridge.Post("/api/launch", new { id = "test-build" }, CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("error concealed"); }
                catch (InvalidOperationException e) { Assert(e.Message == "Windows permission was cancelled", "backend explanation lost"); }
            }));
            Test("cancel command reaches backend once without launching anything", () => WithBridge((bridge, http) =>
            {
                http.Intercept = (request, token) =>
                {
                    Assert(request.Method == HttpMethod.Post && request.RequestUri.AbsolutePath == "/api/cancel", "cancel issued other action");
                    return Task.FromResult(LauncherHttp.Reply(new { ok = true, message = "Cancelled before launch" }));
                };
                bridge.Post("/api/cancel", new { }, CancellationToken.None).GetAwaiter().GetResult(); Assert(http.Posts == 1, "duplicate cancellation");
            }));
            Test("package switching refuses an active game or recording", () => WithBridge((bridge, http) =>
            {
                foreach (var active in new[] { new { gameRunning = true, recording = new { running = false } }, new { gameRunning = false, recording = new { running = true } } })
                {
                    http.Status = Json.Read<Dictionary<string, object>>(Json.Write(active));
                    try { bridge.Stop(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("active backend stopped"); } catch (InvalidOperationException) { }
                }
                Assert(http.Posts == 0, "stop requested despite live activity");
            }));
            Test("live backend from another package is not silently replaced", () => WithBridge((bridge, http) =>
            {
                string originalRoot = bridge.AppRoot;
                try { bridge.Connect(Path.Combine(root, "different-package"), CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("wrong package accepted"); }
                catch (InvalidOperationException e) { Assert(e.Message.Contains("Another WuWa launcher"), "wrong conflict message"); }
                Assert(http.Posts == 0 && bridge.Conflict != null && bridge.AppRoot == originalRoot, "existing helper modified or conflict lost");
                int identities = http.Requests.Count(x => x == "/api/identity");
                bridge.Connect(Path.GetDirectoryName(originalRoot), CancellationToken.None).GetAwaiter().GetResult();
                Assert(bridge.Conflict == null && http.Requests.Count(x => x == "/api/identity") == identities + 1,
                    "intended reconnect did not revalidate identity and clear the conflict");
            }));
            ConflictTests();
            PackageGuardTests();
            Test("cached transport failure or timeout reattaches with fresh address and credentials", () =>
            {
                foreach (bool timeout in new[] { false, true }) WithBridge((bridge, http) =>
                {
                    string original = bridge.Address, package = Path.GetDirectoryName(bridge.AppRoot);
                    const string recovered = "http://127.0.0.1:34568";
                    Json.Save(Path.Combine(bridge.Data, "launcher.json"), new { url = recovered + "/", pid = 123456 });
                    int staleRequests = 0;
                    http.Intercept = (request, token) =>
                    {
                        if (request.RequestUri.GetLeftPart(UriPartial.Authority) == original)
                        {
                            Assert(request.RequestUri.AbsolutePath == "/api/status", "old helper received a mutation"); staleRequests++;
                            if (timeout) throw new TaskCanceledException("fixture transport timeout");
                            throw new HttpRequestException("fixture helper has stopped");
                        }
                        Assert(request.RequestUri.GetLeftPart(UriPartial.Authority) == recovered, "unexpected recovery address");
                        if (request.RequestUri.AbsolutePath == "/") return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) {
                            Content = new StringContent("<meta name=\"wuwa-token\" content=\"recovered_token\">") });
                        if (request.RequestUri.AbsolutePath == "/api/settings")
                        {
                            Assert(request.Headers.GetValues("Origin").Single() == recovered, "old origin reused");
                            Assert(request.Headers.GetValues("X-WuWa-Token").Single() == "recovered_token", "old token reused");
                            return Task.FromResult(LauncherHttp.Reply(new { ok = true }));
                        }
                        return http.DefaultReply(request);
                    };
                    bridge.Connect(package, CancellationToken.None).GetAwaiter().GetResult();
                    bridge.Status(CancellationToken.None).GetAwaiter().GetResult();
                    bridge.Post("/api/settings", new { }, CancellationToken.None).GetAwaiter().GetResult();
                    Assert(staleRequests == 1 && bridge.Address == recovered, "dead cached connection was not recovered");
                    Assert(http.Requests.Count(x => x == "/api/identity") == 2, "recovered identity was not verified");
                });
            });
            Test("a live helper HTTP error does not trigger reattachment or restart", () => WithBridge((bridge, http) =>
            {
                string address = bridge.Address, package = Path.GetDirectoryName(bridge.AppRoot);
                http.Intercept = (request, token) => Task.FromResult(LauncherHttp.Reply(new { error = "fixture failure" }, HttpStatusCode.InternalServerError));
                foreach (bool stop in new[] { false, true })
                {
                    try { (stop ? bridge.Stop(CancellationToken.None) : bridge.Connect(package, CancellationToken.None)).GetAwaiter().GetResult(); throw new Exception("HTTP error concealed"); }
                    catch (InvalidOperationException e) { Assert(e.Message.Contains("HTTP 500"), "HTTP failure explanation lost"); }
                }
                Assert(http.Requests.Count(x => x == "/api/identity") == 1 && http.Posts == 0 && bridge.Address == address, "live HTTP failure caused recovery or mutation");
            }));
            Test("helper recovery cancellation never reattaches, restarts or stops", () =>
            {
                foreach (bool stop in new[] { false, true }) WithBridge((bridge, http) =>
                {
                    string address = bridge.Address, package = Path.GetDirectoryName(bridge.AppRoot);
                    http.Intercept = async (request, token) => { await Task.Delay(Timeout.Infinite, token); return LauncherHttp.Reply(new { }); };
                    using (var cancel = new CancellationTokenSource())
                    {
                        var pending = stop ? bridge.Stop(cancel.Token) : bridge.Connect(package, cancel.Token); cancel.Cancel();
                        try { pending.GetAwaiter().GetResult(); throw new Exception("recovery ignored cancellation"); } catch (OperationCanceledException) { }
                    }
                    Assert(http.Requests.Count(x => x == "/api/identity") == 1 && http.Posts == 0 && bridge.Address == address, "cancellation caused recovery or mutation");
                });
            });
            Test("stop after a transport failure still refuses fresh game, injector, job and recording activity", () =>
            {
                foreach (string active in new[] { "gameRunning", "injectorRunning", "job", "recording" }) WithBridge((bridge, http) =>
                {
                    http.Status[active] = active == "job" || active == "recording" ? (object)new Dictionary<string, object> { { "running", true } } : true;
                    int statusRequests = 0;
                    http.Intercept = (request, token) =>
                    {
                        if (request.RequestUri.AbsolutePath == "/api/status" && ++statusRequests == 1) throw new HttpRequestException("fixture old helper unavailable");
                        return http.DefaultReply(request);
                    };
                    try { bridge.Stop(CancellationToken.None).GetAwaiter().GetResult(); throw new Exception("recovered busy helper stopped"); }
                    catch (InvalidOperationException e) { Assert(e.Message.Contains("Close WuWa"), "activity guard did not run after recovery"); }
                    Assert(statusRequests == 2 && http.Requests.Count(x => x == "/api/identity") == 2 && http.Posts == 0, "recovery bypassed a fresh identity/activity check");
                });
            });
            Test("recovery refuses a conflicting live helper without clearing the stop guard", () =>
            {
                foreach (bool stop in new[] { false, true }) WithBridge((bridge, http) =>
                {
                    string address = bridge.Address, appRoot = bridge.AppRoot, package = Path.GetDirectoryName(appRoot);
                    http.AppRoot = Path.Combine(root, "conflicting-package", "app");
                    http.Intercept = (request, token) =>
                    {
                        if (request.RequestUri.AbsolutePath == "/api/status") throw new HttpRequestException("fixture old helper unavailable");
                        return http.DefaultReply(request);
                    };
                    try { (stop ? bridge.Stop(CancellationToken.None) : bridge.Connect(package, CancellationToken.None)).GetAwaiter().GetResult(); throw new Exception("recovery replaced conflicting helper"); }
                    catch (InvalidOperationException e) { Assert(e.Message.Contains("Another WuWa launcher"), "conflicting identity was not preserved"); }
                    Assert(http.Posts == 0 && bridge.Address == address && bridge.AppRoot == appRoot, "failed recovery discarded safety state or modified another helper");
                });
            });
            Test("cancellation interrupts an outstanding backend status request", () => WithBridge((bridge, http) =>
            {
                http.Intercept = async (request, token) => { await Task.Delay(Timeout.Infinite, token); return LauncherHttp.Reply(new { }); };
                using (var cancel = new CancellationTokenSource())
                {
                    var pending = bridge.Status(cancel.Token); cancel.Cancel();
                    try { pending.GetAwaiter().GetResult(); throw new Exception("request ignored cancellation"); } catch (OperationCanceledException) { }
                }
            }));
            Test("duplicate manager activation signals only the canonical same data root without creating windows", () =>
            {
                string activationRoot = Path.Combine(root, "activation"); int callbacks = 0;
                Assert(InstanceActivation.Key(activationRoot) == InstanceActivation.Key(activationRoot.ToUpperInvariant() + Path.DirectorySeparatorChar), "equivalent manager roots produce different IPC keys");
                using (var activation = new InstanceActivation(activationRoot, () => { Interlocked.Increment(ref callbacks); return true; }))
                {
                    Assert(!InstanceActivation.TryActivate(activationRoot + "-other", 50) && callbacks == 0, "another data root received activation");
                    Assert(InstanceActivation.TryActivate(activationRoot + Path.DirectorySeparatorChar) && callbacks == 1, "first duplicate activation was lost");
                    Assert(InstanceActivation.TryActivate(activationRoot) && callbacks == 2, "subsequent duplicate activation was lost");
                    var start = new System.Diagnostics.ProcessStartInfo(Assembly.GetExecutingAssembly().Location, "--activation-probe \"" + activationRoot + "\"")
                    { UseShellExecute = false, CreateNoWindow = true, WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden };
                    using (var child = System.Diagnostics.Process.Start(start))
                    {
                        try { child.PriorityClass = System.Diagnostics.ProcessPriorityClass.BelowNormal; } catch (InvalidOperationException) { }
                        Assert(child.WaitForExit(5000), "background activation probe timed out");
                        Assert(child.ExitCode == 0 && callbacks == 3, "separate background process did not activate the same-root callback");
                    }
                }
                Assert(!InstanceActivation.TryActivate(activationRoot, 50), "disposed first instance still accepted activation");
                using (var denied = new InstanceActivation(activationRoot, () => false))
                    Assert(!InstanceActivation.TryActivate(activationRoot, 50), "failed activation did not request fallback");
            });
            Test("duplicate manager fallback uses the persisted UI language without loading packages", () =>
            {
                string folder = Path.Combine(root, "activation-language");
                foreach (string language in new[] { "en", "zh-Hans" })
                {
                    Json.Save(Path.Combine(folder, "manager.json"), new { language, installed = new[] { "deliberately not parsed as packages" } });
                    Assert(Program.SavedLanguage(folder) == language, "duplicate fallback ignored saved language " + language);
                    Assert(new Strings { Language = language }["alreadyOpen"] != "alreadyOpen", "duplicate fallback lacks translation");
                }
            });
            Console.WriteLine(passed + " tests passed. Evidence: " + root);
            ControllerDiagnosticsTests.Run();
            ProcessRecoveryTests.Run(root);
            LauncherUpdateTests.Run(root);
            WindowTests.Run(root); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
