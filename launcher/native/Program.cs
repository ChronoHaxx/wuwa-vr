using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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
        // Velopack starts the app (and Windows starts it from the shortcut) with the install's current\ folder as
        // the working directory, and every process the app starts without its own directory inherits it. The
        // official game launcher stays in the tray, so one "Play without VR" kept current\ open and Windows refused
        // to move it: the 1.2.6 -> 1.2.7 update failed four times on 12 Nov 2026 ("being used by another process").
        // The app never needs a working directory (every path is absolute), so it leaves the install folder first.
        // A preview render resolves its argument against the directory it was given, so it keeps it.
        public static void LeaveInstallFolder()
        {
            try { Directory.SetCurrentDirectory(Environment.SystemDirectory); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }
        // The working directory for anything the app starts: a program's own folder (as Explorer would use),
        // otherwise System32. Never the install folder, whatever the app's own directory is.
        public static string StartDirectory(string target)
        {
            try
            {
                if (!String.IsNullOrEmpty(target) && Path.IsPathRooted(target) && File.Exists(target))
                {
                    string folder = Path.GetDirectoryName(Path.GetFullPath(target));
                    if (!String.IsNullOrEmpty(folder) && !InsideInstall(folder)) return folder;
                }
            }
            catch (ArgumentException) { } catch (NotSupportedException) { } catch (IOException) { }
            return Environment.SystemDirectory;
        }
        static bool InsideInstall(string folder)
        {
            string install = Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory).TrimEnd('\\') + "\\";
            return (Path.GetFullPath(folder).TrimEnd('\\') + "\\").StartsWith(install, StringComparison.OrdinalIgnoreCase);
        }
        [STAThread]
        public static int Main(string[] args)
        {
            ConfigureRuntime();
            if (!(args.Length >= 2 && args[0] == "--preview")) LeaveInstallFolder();
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
                    bool firstRun = !preview && !File.Exists(Path.Combine(root, "manager.json"));
                    var packages = new PackageStore(root);
                    if (firstRun)
                    {
                        // First start: follow Windows' display language until the player picks one.
                        packages.State.language = Strings.FromCulture(System.Globalization.CultureInfo.CurrentUICulture.Name);
                        packages.Save();
                    }
                    var window = new MainWindow(packages, data, preview, args.Length > 2 ? args[2] : null,
                        preview ? null : LauncherUpdateService.CreateInstalled());
                    if (preview)
                    {
                        File.AppendAllText(args[1] + ".render.log", "Layout constructed\n");
                        string fixturePath = Path.Combine(root, "preview.json"); double scale = 1;
                        if (File.Exists(fixturePath))
                        {
                            var fixture = Json.Read<Dictionary<string, object>>(File.ReadAllText(fixturePath));
                            window.ApplyPreviewFixture(fixture);
                            object requested; if (fixture.TryGetValue("scale", out requested)) scale = Math.Max(1, Math.Min(3, Convert.ToDouble(requested)));
                        }
                        window.SavePreview(args[1], scale); window.Close(); app.Shutdown();
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
                    if (Strings.Codes.Contains(language)) return language;
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is System.Security.SecurityException || e is ArgumentException || e is InvalidOperationException) { }
            return Strings.FromCulture(System.Globalization.CultureInfo.CurrentUICulture.Name);
        }
    }
}
