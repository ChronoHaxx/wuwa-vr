using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace Custom_UEVR_Injector
{
    public static class InjectionManager
    {
        // PID alone is reusable, and a persisted last_pid is not proof that
        // this game instance contains the requested DLLs.
        private static readonly Dictionary<string, bool> Attempts = new Dictionary<string, bool>();
        private static bool busy;
        private static string lastTargetStatus;
        private static string Identity(Process process) => process.Id + ":" + process.StartTime.ToUniversalTime().Ticks;

        public static Process GetTargetProcess()
        {
            string executable = TargetPathBinding.Request.IsPathBound ? TargetPathBinding.Request.GameExecutable : Functions.game_executable;
            if (string.IsNullOrWhiteSpace(executable)) return null;
            var all = Process.GetProcessesByName(Path.GetFileNameWithoutExtension(executable));
            if (TargetPathBinding.Request.IsPathBound) return TargetPathBinding.Select(all);
            // Do not choose an arbitrary game instance if there are several.
            if (all.Length == 1) return all[0];
            foreach (var process in all) process.Dispose();
            return null;
        }

        public static bool IsProcessRunning()
        {
            using (var process = GetTargetProcess()) return process != null;
        }

        public static bool IsAlreadyInjected()
        {
            using (var process = GetTargetProcess())
                try { return process != null && Attempts.TryGetValue(Identity(process), out var loaded) && loaded; }
                catch (InvalidOperationException) { return false; }
        }

        public static bool CanAutoInject()
        {
            if (busy) return false;
            using (var process = GetTargetProcess())
                try { return process != null && !Attempts.ContainsKey(Identity(process)); }
                catch (Exception) { return false; }
        }

        public static void MonitorProcess(main__form form)
        {
            if (form.IsDisposed || !Functions.ProfileExists() || busy) return;
            using (var process = GetTargetProcess())
            {
                if (TargetPathBinding.Request.IsPathBound && lastTargetStatus != TargetPathBinding.Status)
                {
                    lastTargetStatus = TargetPathBinding.Status;
                    form.listResults.AppendText("[Target] " + lastTargetStatus + Environment.NewLine);
                }
                string key = null;
                try { if (process != null && !process.HasExited) key = Identity(process); }
                catch (Exception) { }
                bool attempted = key != null && Attempts.TryGetValue(key, out _);
                bool loaded = attempted && Attempts[key];
                form.button_inject.Text = key == null ? TargetPathBinding.Request.IsPathBound ? TargetPathBinding.Status : "Waiting for one game instance..."
                    : loaded ? "DLLs loaded - waiting for VR in game"
                    : attempted ? "Load failed - close game before retrying" : "[Ready] Inject VR now?";
                form.button_inject.Enabled = key != null && !attempted;
                form.button_game_folder.Enabled = !TargetPathBinding.Request.IsPathBound && (key == null || !attempted);
                form.button_uevr_folder.Enabled = key == null || !attempted;
            }
        }

        public static async void InjectAll(main__form form) => await InjectAllAsync(form);

        internal static async Task<bool> InjectAllAsync(main__form form)
        {
            if (busy) return false;
            busy = true;
            string key = null;
            Action<string> log = message => { if (!form.IsDisposed) form.listResults.AppendText(message + Environment.NewLine); };
            try
            {
                if (string.IsNullOrWhiteSpace(Functions.uevr_folder) || !Directory.Exists(Functions.uevr_folder))
                    throw new InvalidOperationException("Choose an existing UEVR folder.");
                if (Functions.DLL_files == null || !Functions.DLL_files.Any(x => !string.IsNullOrWhiteSpace(x)))
                    throw new InvalidOperationException("No DLL configured for injection.");
                var paths = Functions.DLL_files.Where(x => !string.IsNullOrWhiteSpace(x))
                    .Select(x => Path.GetFullPath(Path.Combine(Functions.uevr_folder, x))).ToArray();
                // Refuse the entire batch before loading anything if a file is missing.
                foreach (var path in paths)
                    if (!File.Exists(path)) throw new FileNotFoundException("Required DLL is missing.", path);
                using (var process = GetTargetProcess())
                {
                    if (process == null) throw new InvalidOperationException(TargetPathBinding.Request.IsPathBound ? TargetPathBinding.Status : "Expected one running game process. Close duplicate instances.");
                    key = Identity(process);
                    if (Attempts.TryGetValue(key, out var previous))
                    {
                        log(previous ? "This instance's DLLs were already verified." : "A load already failed in this instance. Close the game normally before retrying.");
                        return previous;
                    }
                    // One attempt per process instance, including timeout or partial
                    // success. The auto-loop must never repeatedly inject it.
                    Attempts[key] = false;
                    Functions.injector_config_data["custom_var_last_pid"] = "0";
                    Functions.injector_config_update = true;
                    form.button_inject.Enabled = false;
                    form.button_game_folder.Enabled = false;
                    form.button_uevr_folder.Enabled = false;
                    foreach (var path in paths)
                    {
                        form.button_inject.Text = "Loading " + Path.GetFileName(path) + "...";
                        log("[InjectDll] PID=" + process.Id + " DLL=" + Path.GetFileName(path));
                        var messages = new List<string>();
                        bool loaded = await Task.Run(() => RemoteLibraryLoader.Load(process, path, messages.Add));
                        foreach (var message in messages) log(message);
                        if (!loaded)
                        {
                            log("ERROR: Injection did not complete. Keep this log; close the game normally before retrying.");
                            return false;
                        }
                    }
                    // Recheck the full set: a DLL could have disappeared while a later
                    // dependency loaded. Module loading alone is not first-frame proof.
                    foreach (var path in paths)
                        if (!RemoteLibraryLoader.IsLoaded(process, path))
                            throw new InvalidOperationException("A requested DLL is no longer loaded: " + Path.GetFileName(path));
                    Attempts[key] = true;
                    Functions.injector_config_data["custom_var_last_pid"] = process.Id.ToString();
                    Functions.injector_config_update = true;
                    log("All requested modules are loaded. The launcher still needs to observe UEVR and a stereo frame.");
                    if ($"{Functions.injector_config_data["custom_var_auto_focus"]}" == "1")
                        Functions.FocusOnGame(Functions.game_executable);
                    if ($"{Functions.injector_config_data["custom_var_auto_close"]}" == "1")
                        Functions.CloseApplicationDelayed();
                    return true;
                }
            }
            catch (Exception error)
            {
                log("[InjectAll ERROR] " + error.GetType().Name + ": " + error.Message);
                // Stop repeated preflight attempts too, until a new game process.
                try { using (var p = GetTargetProcess()) if (p != null) Attempts[Identity(p)] = false; }
                catch (Exception) { }
                return false;
            }
            finally
            {
                busy = false;
                MonitorProcess(form);
                if (!form.IsDisposed) LogSyncManager.SyncLog(form.listResults);
            }
        }
    }
}
