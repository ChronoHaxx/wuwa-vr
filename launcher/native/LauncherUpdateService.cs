using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Velopack;
using Velopack.Sources;

namespace WuWaVR.Manager
{
    // App updates replace only Velopack's app directory. Mod versions, helper
    // data and user settings retain their existing, separate LocalAppData roots.
    public sealed class LauncherUpdate
    {
        public readonly string Version;
        internal readonly object Native;
        public LauncherUpdate(string version) : this(version, null) { }
        internal LauncherUpdate(string version, object native) { Version = version; Native = native; }
    }

    public interface ILauncherUpdateAdapter
    {
        bool Installed { get; }
        string CurrentVersion { get; }
        Task<LauncherUpdate> CheckAsync(CancellationToken cancel);
        Task DownloadAsync(LauncherUpdate update, Action<int> progress, CancellationToken cancel);
        void ApplyAndRestart(LauncherUpdate update);
    }

    public sealed class LauncherUpdateService : IDisposable
    {
        public const string Version = "1.0.3";
        public const string AppId = "ChronoHaxx.WuWaVR";
        public const string Channel = "win-beta";
        public const string Repository = "https://github.com/ChronoHaxx/wuwa-vr";
        public const string FeedUrl = "https://chronohaxx.github.io/wuwa-vr/launcher-updates/";
        readonly ILauncherUpdateAdapter adapter;
        readonly SemaphoreSlim serial = new SemaphoreSlim(1, 1);
        readonly CancellationTokenSource lifetime = new CancellationTokenSource();
        readonly object sync = new object();
        LauncherUpdate available;
        bool ready, checkFailed, checkedOnce, busy;
        public bool Installed { get { return adapter != null && adapter.Installed; } }
        public string CurrentVersion { get { return adapter == null ? Version : adapter.CurrentVersion; } }
        public string AvailableVersion { get { lock (sync) return available?.Version; } }
        public bool ReadyToRestart { get { lock (sync) return ready; } }
        public bool CheckFailed { get { lock (sync) return checkFailed; } }
        public bool CheckedOnce { get { lock (sync) return checkedOnce; } }
        public bool Busy { get { lock (sync) return busy; } }

        // An omitted adapter is deliberately inert: previews and offscreen UI
        // tests never acquire a network client. Program opts into production.
        public LauncherUpdateService(ILauncherUpdateAdapter adapter = null) { this.adapter = adapter; }
        public static LauncherUpdateService CreateInstalled()
        { return new LauncherUpdateService(new VelopackLauncherUpdateAdapter()); }

        public async Task CheckAsync(CancellationToken cancel)
        {
            if (!Installed) return;
            using (var linked = CancellationTokenSource.CreateLinkedTokenSource(cancel, lifetime.Token))
            {
                await serial.WaitAsync(linked.Token).ConfigureAwait(false);
                try
                {
                    lock (sync) { busy = true; if (ready) return; }
                    var found = await adapter.CheckAsync(linked.Token).ConfigureAwait(false);
                    linked.Token.ThrowIfCancellationRequested();
                    lock (sync) { available = found; checkedOnce = true; checkFailed = false; }
                }
                catch (OperationCanceledException) { throw; }
                catch { lock (sync) checkFailed = true; throw; }
                finally { lock (sync) busy = false; serial.Release(); }
            }
        }
        public async Task DownloadAsync(Action<int> progress, CancellationToken cancel)
        {
            using (var linked = CancellationTokenSource.CreateLinkedTokenSource(cancel, lifetime.Token))
            {
                await serial.WaitAsync(linked.Token).ConfigureAwait(false);
                try
                {
                    LauncherUpdate target;
                    lock (sync) { busy = true; target = available; ready = false; }
                    if (!Installed || target == null) throw new InvalidOperationException("Check for a launcher update first.");
                    await adapter.DownloadAsync(target, progress, linked.Token).ConfigureAwait(false);
                    linked.Token.ThrowIfCancellationRequested();
                    // The adapter returns only after Velopack has verified the package.
                    lock (sync) ready = true;
                }
                finally { lock (sync) busy = false; serial.Release(); }
            }
        }
        public async Task RestartAsync(Func<CancellationToken, Task> requireIdle, Action saveState, CancellationToken cancel)
        {
            if (requireIdle == null || saveState == null) throw new ArgumentNullException();
            using (var linked = CancellationTokenSource.CreateLinkedTokenSource(cancel, lifetime.Token))
            {
                await serial.WaitAsync(linked.Token).ConfigureAwait(false);
                try
                {
                    LauncherUpdate target;
                    lock (sync) { busy = true; target = ready ? available : null; }
                    if (!Installed || target == null) throw new InvalidOperationException("Download and verify the launcher update first.");
                    await requireIdle(linked.Token).ConfigureAwait(false);
                    linked.Token.ThrowIfCancellationRequested();
                    saveState();
                    // Saving may take time. Re-check activity/identity immediately
                    // before the explicit app restart, regardless of selected mod.
                    await requireIdle(linked.Token).ConfigureAwait(false);
                    linked.Token.ThrowIfCancellationRequested();
                    adapter.ApplyAndRestart(target);
                }
                finally { lock (sync) busy = false; serial.Release(); }
            }
        }
        public void Dispose() { lifetime.Cancel(); }
    }

    internal sealed class VelopackLauncherUpdateAdapter : ILauncherUpdateAdapter
    {
        readonly UpdateManager manager;
        readonly object sync = new object();
        Task<UpdateInfo> pendingCheck;
        internal static UpdateOptions Options()
        { return new UpdateOptions { ExplicitChannel = LauncherUpdateService.Channel, AllowVersionDowngrade = false }; }
        internal static SimpleWebSource Source(IFileDownloader downloader = null)
        { return new SimpleWebSource(LauncherUpdateService.FeedUrl, downloader ?? new MetadataBoundedDownloader()); }
        public VelopackLauncherUpdateAdapter() : this(new UpdateManager(Source(), Options())) { }
        internal VelopackLauncherUpdateAdapter(UpdateManager manager) { this.manager = manager; }
        public bool Installed { get { return manager.IsInstalled && manager.AppId == LauncherUpdateService.AppId; } }
        public string CurrentVersion { get { return manager.CurrentVersion?.ToString() ?? LauncherUpdateService.Version; } }
        public async Task<LauncherUpdate> CheckAsync(CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            if (!Installed) return null;
            Task<UpdateInfo> check;
            lock (sync)
            {
                // SDK 1.2.161 has no cancellation argument for this method.
                // Reuse a still-running request after timeout/cancellation;
                // never publish its result without a new, active caller.
                if (pendingCheck == null || pendingCheck.IsCompleted)
                {
                    pendingCheck = manager.CheckForUpdatesAsync();
                    pendingCheck.ContinueWith(t => { var observed = t.Exception; }, CancellationToken.None,
                        TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
                }
                check = pendingCheck;
            }
            using (var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancel))
            {
                var delay = Task.Delay(TimeSpan.FromSeconds(20), timeout.Token);
                if (await Task.WhenAny(check, delay).ConfigureAwait(false) != check)
                { cancel.ThrowIfCancellationRequested(); throw new TimeoutException("The launcher update check timed out. Try again later."); }
                timeout.Cancel();
            }
            cancel.ThrowIfCancellationRequested();
            var update = await check.ConfigureAwait(false);
            if (update == null) return null;
            Validate(update);
            return new LauncherUpdate(update.TargetFullRelease.Version.ToString(), update);
        }
        void Validate(UpdateInfo update)
        {
            var asset = update?.TargetFullRelease;
            if (!Installed || asset == null || asset.PackageId != LauncherUpdateService.AppId || update.IsDowngrade ||
                asset.Version == null || asset.Version.CompareTo(manager.CurrentVersion) <= 0)
                throw new InvalidDataException("The update feed returned an unexpected launcher package.");
        }
        UpdateInfo Native(LauncherUpdate update)
        {
            var native = update?.Native as UpdateInfo;
            Validate(native); return native;
        }
        public Task DownloadAsync(LauncherUpdate update, Action<int> progress, CancellationToken cancel)
        { return manager.DownloadUpdatesAsync(Native(update), progress, cancel); }
        public void ApplyAndRestart(LauncherUpdate update)
        { manager.ApplyUpdatesAndRestart(Native(update).TargetFullRelease); }

        // The SDK check lacks a cancellation token. Bound the actual metadata
        // request as well as our caller's wait, without shortening large package
        // downloads (which have their own cancellation token).
        sealed class MetadataBoundedDownloader : IFileDownloader
        {
            readonly HttpClientFileDownloader inner = new HttpClientFileDownloader();
            public Task<string> DownloadString(string url, IDictionary<string, string> headers, double timeout)
            { return inner.DownloadString(url, headers, Math.Min(timeout, 0.25)); }
            public Task<byte[]> DownloadBytes(string url, IDictionary<string, string> headers, double timeout)
            { return inner.DownloadBytes(url, headers, Math.Min(timeout, 0.25)); }
            public Task DownloadFile(string url, string target, Action<int> progress, IDictionary<string, string> headers, double timeout, CancellationToken cancel)
            { return inner.DownloadFile(url, target, progress, headers, timeout, cancel); }
        }
    }
}
