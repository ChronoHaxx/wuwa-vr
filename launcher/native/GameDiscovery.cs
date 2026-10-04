using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security;
using System.Text.RegularExpressions;
using System.Threading;
using Microsoft.Win32;

namespace WuWaVR.Manager
{
    // Bootstrap the portable launcher's discovery policy before that package is
    // installed. This only reads saved settings, uninstall records and exact paths.
    public static class GameDiscovery
    {
        public sealed class Registration
        {
            public string Name, Key, Location, Icon;
        }
        public sealed class Result
        {
            public string Path, Message;
            public bool CanUse;
            public List<string> Candidates = new List<string>();
        }
        public static string ExistingLauncher(string value, Func<string, bool> exists)
        {
            if (String.IsNullOrWhiteSpace(value)) return null;
            try
            {
                var path = value.Trim().Trim('"');
                if (path.IndexOfAny(new[] { '\r', '\n', '"' }) >= 0 || !Regex.IsMatch(path, @"^(?:[a-zA-Z]:[\\/]|\\\\[^\\]+\\[^\\]+\\)") ||
                    !String.Equals(System.IO.Path.GetFileName(path), "launcher.exe", StringComparison.OrdinalIgnoreCase)) return null;
                path = System.IO.Path.GetFullPath(path);
                return exists(path) ? path : null;
            }
            catch (Exception e) when (e is ArgumentException || e is IOException || e is UnauthorizedAccessException || e is SecurityException || e is NotSupportedException) { return null; }
        }
        static string IconPath(string value)
        { return Regex.Replace((value ?? "").Trim(), @",\s*-?\d+$", "").Trim().Trim('"'); }
        // Cached paths and suggestions fill only an unsaved helper choice. A pending
        // deliberate native selection may replace it once; opening the manager cannot.
        public static string PathToApply(string managerPath, Result found, Dictionary<string, object> game, Func<string, bool> exists, bool pendingChoice = false)
        {
            if (Json.Flag(game, "saved") && !pendingChoice) return null;
            string path = ExistingLauncher(managerPath, exists);
            if (path == null && !String.IsNullOrWhiteSpace(managerPath)) return null;
            if (path == null && !Json.Flag(game, "saved") && found != null && found.CanUse)
                path = ExistingLauncher(found.Path, exists);
            return path != null && (Json.Text(game, "mode") != "launcher" ||
                !String.Equals(Json.Text(game, "launcher"), path, StringComparison.OrdinalIgnoreCase)) ? path : null;
        }
        public static Result Resolve(string managerPath, Dictionary<string, object> saved,
            IEnumerable<Registration> registrations, IEnumerable<string> defaults, Func<string, bool> exists, bool pendingChoice = false)
        {
            // Historical manager paths are a fallback, not a permanent override
            // of a later explicit choice made in the shared developer web tools.
            string mode = Json.Text(saved, "gameStart");
            if (!pendingChoice && (mode == "manual" || mode == "launcher")) managerPath = null;
            string explicitPath = !String.IsNullOrWhiteSpace(managerPath) ? managerPath :
                mode == "launcher" ? Json.Text(saved, "gameLauncher") : null;
            if (!String.IsNullOrWhiteSpace(managerPath) || mode == "launcher")
            {
                string valid = ExistingLauncher(explicitPath, exists);
                return new Result { Path = valid ?? explicitPath, CanUse = valid != null, Message = valid == null ? "gameSavedMissing" : "gameSavedFound" };
            }
            if (Json.Text(saved, "gameStart") == "manual") return new Result { Message = "gameManualSaved" };
            var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var entries = registrations.ToList();
            var excluded = entries.Where(e => (e.Key ?? "").StartsWith("Steam App ", StringComparison.OrdinalIgnoreCase) ||
                (e.Key ?? "").StartsWith("Epic ", StringComparison.OrdinalIgnoreCase)).Select(e => e.Location).Where(p => !String.IsNullOrWhiteSpace(p)).ToList();
            Action<string> add = value =>
            {
                var candidate = ExistingLauncher(value, exists); if (candidate == null) return;
                foreach (var directory in excluded)
                {
                    try { if (candidate.StartsWith(System.IO.Path.GetFullPath(directory).TrimEnd('\\', '/') + System.IO.Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) return; }
                    catch (Exception e) when (e is ArgumentException || e is IOException || e is NotSupportedException) { }
                }
                found.Add(candidate);
            };
            foreach (var entry in entries)
            {
                if ((entry.Name ?? "").IndexOf("Wuthering Waves", StringComparison.OrdinalIgnoreCase) < 0 ||
                    (entry.Key ?? "").StartsWith("Steam App ", StringComparison.OrdinalIgnoreCase) ||
                    (entry.Key ?? "").StartsWith("Epic ", StringComparison.OrdinalIgnoreCase)) continue;
                add(IconPath(entry.Icon));
                if (!String.IsNullOrWhiteSpace(entry.Location))
                {
                    try { add(System.IO.Path.Combine(entry.Location, "launcher.exe")); }
                    catch (ArgumentException) { }
                }
            }
            foreach (var candidate in defaults) add(candidate);
            var paths = found.OrderBy(p => p, StringComparer.OrdinalIgnoreCase).ToList();
            return new Result { Path = paths.Count == 1 ? paths[0] : null, CanUse = paths.Count == 1,
                Message = paths.Count == 1 ? "gameAutoFound" : paths.Count > 1 ? "gameMultiple" : "gameNotFound", Candidates = paths };
        }
        public static Result Discover(string managerPath, string backendData, CancellationToken cancel, string pendingPath = null)
        {
            cancel.ThrowIfCancellationRequested();
            bool pendingChoice = !String.IsNullOrWhiteSpace(pendingPath);
            if (pendingChoice) managerPath = pendingPath;
            var saved = new Dictionary<string, object>();
            string settings = System.IO.Path.Combine(backendData, "settings.json");
            try
            {
                if (File.Exists(settings))
                {
                    if (new FileInfo(settings).Length > 1024 * 1024) throw new InvalidDataException("Settings file too large.");
                    saved = Json.Read<Dictionary<string, object>>(File.ReadAllText(settings)) ?? saved;
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SecurityException || e is ArgumentException || e is InvalidOperationException)
            {
                if (String.IsNullOrWhiteSpace(managerPath)) return new Result { Message = "gameSettingsUnreadable" };
            }
            if (!String.IsNullOrWhiteSpace(managerPath) || Json.Text(saved, "gameStart") == "manual" || Json.Text(saved, "gameStart") == "launcher")
                return Resolve(managerPath, saved, new Registration[0], new string[0], File.Exists, pendingChoice);
            var registrations = new List<Registration>();
            foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                cancel.ThrowIfCancellationRequested();
                try
                {
                    using (var root = RegistryKey.OpenBaseKey(hive, view))
                    using (var uninstall = root.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"))
                    {
                        if (uninstall == null) continue;
                        foreach (var name in uninstall.GetSubKeyNames())
                        {
                            cancel.ThrowIfCancellationRequested();
                            try
                            {
                                using (var key = uninstall.OpenSubKey(name))
                                {
                                    if (key == null) continue;
                                    string display = Convert.ToString(key.GetValue("DisplayName"));
                                    if (display.IndexOf("Wuthering Waves", StringComparison.OrdinalIgnoreCase) < 0) continue;
                                    registrations.Add(new Registration { Name = display, Key = name, Location = Convert.ToString(key.GetValue("InstallLocation")), Icon = Convert.ToString(key.GetValue("DisplayIcon")) });
                                }
                            }
                            catch (Exception e) when (e is SecurityException || e is UnauthorizedAccessException || e is IOException) { }
                        }
                    }
                }
                catch (Exception e) when (e is SecurityException || e is UnauthorizedAccessException || e is IOException) { }
            }
            // Do not recursively scan disks or touch Steam/Epic launch paths.
            var defaults = new List<string>();
            foreach (var folder in new[] { Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) })
                if (!String.IsNullOrWhiteSpace(folder)) defaults.Add(System.IO.Path.Combine(folder, "Wuthering Waves", "launcher.exe"));
            foreach (var drive in DriveInfo.GetDrives())
            {
                cancel.ThrowIfCancellationRequested();
                try
                {
                    if (drive.DriveType != DriveType.Fixed) continue;
                    defaults.Add(System.IO.Path.Combine(drive.Name, "Wuthering Waves", "launcher.exe"));
                    defaults.Add(System.IO.Path.Combine(drive.Name, "Games", "Wuthering Waves", "launcher.exe"));
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SecurityException) { }
            }
            return Resolve(managerPath, saved, registrations, defaults, File.Exists);
        }
    }
}
