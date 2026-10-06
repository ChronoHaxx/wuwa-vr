using System;
using System.IO;
using System.Threading;
using System.Windows;

namespace WuWaVR.Manager
{
    public static class Program
    {
        public static void ConfigureRuntime()
        {
            // Package staging can exceed MAX_PATH, even when the selected
            // installation folder itself is short. No system policy is changed.
            AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling", false);
            AppContext.SetSwitch("Switch.System.IO.BlockLongPaths", false);
        }
        [STAThread]
        public static int Main(string[] args)
        {
            ConfigureRuntime();
            string root = Environment.GetEnvironmentVariable("WUWA_VR_MANAGER_DATA") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WuWa VR Manager");
            string data = Environment.GetEnvironmentVariable("WUWA_VR_DATA") ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WuWa VR Launcher");
            // Velopack's uninstall callback has a hard 30-second lifetime. Cleanup
            // has its own shorter budget and never displays a blocking prompt.
            Velopack.VelopackApp.Build().SetAutoApplyOnStartup(false)
                .OnBeforeUninstallFastCallback(version => LauncherUninstall.BeforeUninstall(root, data)).Run();
            if (LauncherProcessRecovery.TryRun(args)) return 0;
            bool preview = args.Length >= 2 && args[0] == "--preview";
            try
            {
                if (preview) root = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(args[1])), "preview-state");
                if (preview) File.WriteAllText(args[1] + ".render.log", "Starting preview\n");
                bool owned;
                using (var mutex = new Mutex(true, "Local\\WuWaVRManager-" + InstanceActivation.Key(preview ? args[1] : root), out owned))
                {
                    if (!owned)
                    {
                        if (preview) throw new IOException("This preview is already being rendered.");
                        if (InstanceActivation.TryActivate(root)) return 0;
                        var strings = new Strings { Language = SavedLanguage(root) };
                        MessageBox.Show(strings["alreadyOpen"], strings["title"]); return 2;
                    }
                    var app = new Application();
                    if (preview) File.AppendAllText(args[1] + ".render.log", "Application created\n");
                    var window = new MainWindow(new PackageStore(root), data, preview, args.Length > 2 ? args[2] : null,
                        preview ? null : LauncherUpdateService.CreateInstalled());
                    if (preview)
                    {
                        File.AppendAllText(args[1] + ".render.log", "Layout constructed\n");
                        window.SavePreview(args[1]); window.Close(); app.Shutdown();
                        File.AppendAllText(args[1] + ".render.log", "Preview saved\n"); return 0;
                    }
                    using (var activation = new InstanceActivation(root, () =>
                    {
                        if (app.Dispatcher.HasShutdownStarted || app.Dispatcher.HasShutdownFinished) return false;
                        return app.Dispatcher.Invoke(new Func<bool>(() =>
                        {
                            if (!window.IsLoaded) return false;
                            if (window.WindowState == WindowState.Minimized) window.WindowState = WindowState.Normal;
                            if (!window.IsVisible) window.Show();
                            return window.Activate();
                        }));
                    })) return app.Run(window);
                }
            }
            catch (Exception error)
            {
                if (preview) { File.WriteAllText(args[1] + ".error.txt", error.ToString()); return 1; }
                MessageBox.Show(error.Message, "WuWa VR", MessageBoxButton.OK, MessageBoxImage.Error); return 1;
            }
        }
        public static string SavedLanguage(string root)
        {
            try
            {
                string file = Path.Combine(root, "manager.json");
                if (File.Exists(file) && new FileInfo(file).Length <= 1024 * 1024)
                {
                    var saved = Json.Read<System.Collections.Generic.Dictionary<string, object>>(File.ReadAllText(file));
                    string language = saved == null ? "" : Json.Text(saved, "language");
                    if (language == "en" || language == "zh-Hans") return language;
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is System.Security.SecurityException || e is ArgumentException || e is InvalidOperationException) { }
            return System.Globalization.CultureInfo.CurrentUICulture.Name.StartsWith("zh", StringComparison.OrdinalIgnoreCase) ? "zh-Hans" : "en";
        }
    }
}
