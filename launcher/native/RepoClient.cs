// SHA-256/TLS download pattern adapted from CircuitLord's RepoClient.cs (MIT).
// Streaming resume, bounded downloads and catalog policy are WuWa-specific.
using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Linq;
using System.Security.Cryptography;
using System.Threading;
using System.Threading.Tasks;

namespace WuWaVR.Manager
{
    public sealed class RepoClient : IDisposable
    {
        public const string CatalogUrl = "https://raw.githubusercontent.com/ChronoHaxx/wuwa-vr/main/launcher/native/catalog.public.json";
        readonly HttpClient http;
        public RepoClient(HttpMessageHandler handler = null)
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            http = handler == null ? new HttpClient() : new HttpClient(handler);
            http.Timeout = TimeSpan.FromMinutes(10);
            http.DefaultRequestHeaders.UserAgent.ParseAdd("WuWaVR-Manager/0.1");
        }
        public static Catalog BundledCatalog()
        {
            using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("catalog.json"))
            using (var reader = new StreamReader(stream)) { var c = Json.Read<Catalog>(reader.ReadToEnd()); c.Validate(); return c; }
        }
        public async Task<Catalog> FetchCatalog(CancellationToken cancel)
        {
            using (var response = await http.GetAsync(CatalogUrl, HttpCompletionOption.ResponseHeadersRead, cancel))
            {
                response.EnsureSuccessStatusCode();
                var text = await BoundedText(response, 512 * 1024, cancel);
                var result = Json.Read<Catalog>(text); result.Validate();
                if (result.releases.Any(r => r.channel != "beta")) throw new InvalidDataException("Online catalog contains an unpublished candidate.");
                result.releases = result.releases.OrderByDescending(r => DateTimeOffset.Parse(r.published)).ToList(); return result;
            }
        }
        public static Catalog KeepBundledCandidates(Catalog online, Catalog bundled)
        {
            online.Validate(); bundled.Validate();
            var local = bundled.releases.Where(r => r.channel == "candidate").ToList();
            var result = new Catalog { schema = 1, releases = local.Concat(online.releases.Where(r => !local.Any(l => l.id == r.id))).ToList() };
            result.Validate(); return result;
        }
        public static async Task<string> BoundedText(HttpResponseMessage response, int limit, CancellationToken cancel)
        {
            using (var stream = await response.Content.ReadAsStreamAsync())
            using (var buffer = new MemoryStream())
            {
                var bytes = new byte[8192]; int count;
                while ((count = await stream.ReadAsync(bytes, 0, bytes.Length, cancel)) > 0)
                { if (buffer.Length + count > limit) throw new InvalidDataException("Response too large."); buffer.Write(bytes, 0, count); }
                return System.Text.Encoding.UTF8.GetString(buffer.ToArray());
            }
        }
        public static string BundledArchive(Release release, string directory, CancellationToken cancel)
        {
            release.Validate(); cancel.ThrowIfCancellationRequested();
            var path = Paths.Inside(directory, release.id + "/WuWa-VR-Launcher.zip");
            if (!File.Exists(path)) return null;
            if (new FileInfo(path).Length != release.size || Hash(path) != release.sha256.ToLowerInvariant())
                throw new InvalidDataException("The bundled package failed its checksum. Download a fresh complete bundle.");
            cancel.ThrowIfCancellationRequested();
            return path;
        }
        public async Task<string> Download(Release release, string cache, IProgress<double> progress, CancellationToken cancel, string bundledDirectory = null)
        {
            release.Validate(); cancel.ThrowIfCancellationRequested();
            if (!String.IsNullOrEmpty(bundledDirectory))
            {
                var bundled = await Task.Run(() => BundledArchive(release, bundledDirectory, cancel), cancel);
                if (bundled != null) return bundled;
            }
            Directory.CreateDirectory(cache); Paths.NoLinks(cache);
            var final = Paths.Inside(cache, release.sha256.ToLowerInvariant() + ".zip");
            var partial = final + ".partial"; Paths.NoLinks(partial);
            if (File.Exists(final))
            {
                if (new FileInfo(final).Length == release.size && Hash(final) == release.sha256.ToLowerInvariant()) return final;
                File.Delete(final);
            }
            if (release.channel == "candidate")
                throw new InvalidDataException("This local test package is missing. Extract the complete candidate bundle, keeping its packages folder beside the app.");
            if (File.Exists(partial) && new FileInfo(partial).Length > release.size) File.Delete(partial);
            long offset = File.Exists(partial) ? new FileInfo(partial).Length : 0;
            if (offset < release.size)
            {
                using (var request = new HttpRequestMessage(HttpMethod.Get, release.url))
                {
                    if (offset > 0) request.Headers.Range = new RangeHeaderValue(offset, null);
                    using (var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, cancel))
                    {
                        response.EnsureSuccessStatusCode();
                        if (response.RequestMessage != null && response.RequestMessage.RequestUri.Scheme != "https") throw new InvalidDataException("Download redirected away from HTTPS.");
                        if (response.StatusCode == HttpStatusCode.PartialContent)
                        {
                            var range = response.Content.Headers.ContentRange;
                            if (range == null || range.From != offset || range.Length != release.size || range.To != release.size - 1)
                                throw new InvalidDataException("Invalid download resume response.");
                        }
                        else if (response.StatusCode == HttpStatusCode.OK) offset = 0; // server ignored Range: restart safely
                        else throw new InvalidDataException("Unexpected download response.");
                        using (var input = await response.Content.ReadAsStreamAsync())
                        using (var output = new FileStream(partial, offset == 0 ? FileMode.Create : FileMode.Append, FileAccess.Write, FileShare.None, 81920, true))
                        {
                            var bytes = new byte[81920]; int count; long done = offset;
                            while ((count = await input.ReadAsync(bytes, 0, bytes.Length, cancel)) > 0)
                            {
                                if (done + count > release.size) throw new InvalidDataException("Download exceeds declared size.");
                                await output.WriteAsync(bytes, 0, count, cancel); done += count;
                                if (progress != null) progress.Report((double)done / release.size);
                            }
                            output.Flush(true);
                        }
                    }
                }
            }
            cancel.ThrowIfCancellationRequested();
            if (new FileInfo(partial).Length != release.size) throw new IOException("Download interrupted; retry to resume.");
            if (Hash(partial) != release.sha256.ToLowerInvariant()) { File.Delete(partial); throw new InvalidDataException("Download checksum did not match. Retry the download."); }
            File.Move(partial, final); return final;
        }
        public static string Hash(string path)
        {
            using (var sha = SHA256.Create()) using (var stream = File.OpenRead(path))
                return BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
        }
        public static void ValidateSha256(string value)
        {
            if (value == null || value.Length != 64) throw new InvalidDataException("A SHA-256 checksum is required.");
            foreach (char c in value) if (!Uri.IsHexDigit(c)) throw new InvalidDataException("Invalid SHA-256 checksum.");
        }
        public void Dispose() { http.Dispose(); }
    }
}
