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
            public string Path, Message, Mode;
            public bool CanUse;
            public List<string> Candidates = new List<string>();
            public List<Choice> Choices = new List<Choice>();
        }
        public sealed class Choice { public string Path, Mode; }
        const string SteamApp = "3513350";
        static string ReadMetadata(string path)
        {
            if (!File.Exists(path) || new FileInfo(path).Length > 1024 * 1024) return null;
            return File.ReadAllText(path);
        }
        static string VdfValue(string body, string key)
        {
            if (body == null || body.Length > 1024 * 1024) return null;
            var values = Regex.Matches(body, "\\\"" + Regex.Escape(key) + "\\\"\\s*\\\"([^\\\"\\r\\n]*)\\\"", RegexOptions.IgnoreCase, TimeSpan.FromMilliseconds(100));
            return values.Count == 1 ? values[0].Groups[1].Value.Replace("\\\\", "\\") : null;
        }
        public static Choice ExistingChoice(string value, Func<string, bool> exists, Func<string, string> readText = null)
        {
            string official = ExistingLauncher(value, exists);
            if (official != null) return new Choice { Path = official, Mode = "launcher" };
            if (String.IsNullOrWhiteSpace(value)) return null;
            try
            {
                string path = value.Trim().Trim('"');
                if (!Regex.IsMatch(path, @"^[a-zA-Z]:[\\/]") || path.IndexOfAny(new[] { '\r', '\n', '"' }) >= 0 ||
                    !String.Equals(System.IO.Path.GetFileName(path), "Wuthering Waves.exe", StringComparison.OrdinalIgnoreCase)) return null;
                path = System.IO.Path.GetFullPath(path);
                string install = System.IO.Path.GetDirectoryName(path), common = System.IO.Path.GetDirectoryName(install), apps = System.IO.Path.GetDirectoryName(common);
                if (!String.Equals(System.IO.Path.GetFileName(common), "common", StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(System.IO.Path.GetFileName(apps), "steamapps", StringComparison.OrdinalIgnoreCase) || !exists(path) ||
                    !exists(System.IO.Path.Combine(install, "Client", "Binaries", "Win64", "Client-Win64-Shipping.exe"))) return null;
                string manifest = (readText ?? ReadMetadata)(System.IO.Path.Combine(apps, "appmanifest_" + SteamApp + ".acf"));
                if (VdfValue(manifest, "appid") != SteamApp || !String.Equals(VdfValue(manifest, "installdir"), System.IO.Path.GetFileName(install), StringComparison.OrdinalIgnoreCase)) return null;
                return new Choice { Path = path, Mode = "steam" };
            }
            catch (Exception e) when (e is ArgumentException || e is IOException || e is UnauthorizedAccessException || e is SecurityException || e is NotSupportedException || e is RegexMatchTimeoutException) { return null; }
        }
        // Steam's small metadata files give exact library roots; never scan a disk.
        public static List<string> SteamCandidates(IEnumerable<string> roots, Func<string, bool> exists, Func<string, string> readText = null)
        {
            var libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            var read = readText ?? ReadMetadata;
            Action<string> add = value =>
            {
                if (libraries.Count >= 64 || String.IsNullOrWhiteSpace(value) || !Regex.IsMatch(value, @"^[a-zA-Z]:[\\/]")) return;
                try { libraries.Add(System.IO.Path.GetFullPath(value)); } catch (Exception e) when (e is ArgumentException || e is NotSupportedException || e is IOException) { }
            };
            foreach (string root in roots.Take(64)) add(root);
            foreach (string root in libraries.ToArray())
            {
                try
                {
                    string body = read(System.IO.Path.Combine(root, "steamapps", "libraryfolders.vdf"));
                    if (body == null || body.Length > 1024 * 1024) continue;
                    foreach (Match m in Regex.Matches(body, "\\\"(?:path|[0-9]+)\\\"\\s*\\\"([^\\\"\\r\\n]*)\\\"", RegexOptions.IgnoreCase, TimeSpan.FromMilliseconds(100))) add(m.Groups[1].Value.Replace("\\\\", "\\"));
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SecurityException || e is ArgumentException || e is RegexMatchTimeoutException) { }
            }
            var result = new List<string>();
            foreach (string library in libraries)
            {
                try
                {
                    string body = read(System.IO.Path.Combine(library, "steamapps", "appmanifest_" + SteamApp + ".acf"));
                    string dir = VdfValue(body, "installdir");
                    if (VdfValue(body, "appid") != SteamApp || String.IsNullOrWhiteSpace(dir) || dir == "." || dir == ".." || dir.IndexOfAny(new[] { '\\', '/', ':', '"' }) >= 0) continue;
                    var choice = ExistingChoice(System.IO.Path.Combine(library, "steamapps", "common", dir, "Wuthering Waves.exe"), exists, read);
                    if (choice != null && choice.Mode == "steam") result.Add(choice.Path);
                }
                catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SecurityException || e is ArgumentException || e is RegexMatchTimeoutException) { }
            }
            return result.Distinct(StringComparer.OrdinalIgnoreCase).ToList();
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
        public static string PathToApply(string managerPath, Result found, Dictionary<string, object> game, Func<string, bool> exists, bool pendingChoice = false, Func<string, string> readText = null)
        {
            if (Json.Flag(game, "saved") && !pendingChoice) return null;
            Choice choice = ExistingChoice(managerPath, exists, readText);
            if (choice == null && !String.IsNullOrWhiteSpace(managerPath)) return null;
            if (choice == null && !Json.Flag(game, "saved") && found != null && found.CanUse)
                choice = ExistingChoice(found.Path, exists, readText);
            return choice != null && (Json.Text(game, "mode") != choice.Mode ||
                !String.Equals(Json.Text(game, "launcher"), choice.Path, StringComparison.OrdinalIgnoreCase)) ? choice.Path : null;
        }
        public static Result Resolve(string managerPath, Dictionary<string, object> saved,
            IEnumerable<Registration> registrations, IEnumerable<string> defaults, Func<string, bool> exists, bool pendingChoice = false,
            IEnumerable<string> steamPaths = null, Func<string, string> readText = null)
        {
            // Historical manager paths are a fallback, not a permanent override
            // of a later explicit choice made in the shared developer web tools.
            string mode = Json.Text(saved, "gameStart");
            if (!pendingChoice && (mode == "manual" || mode == "launcher" || mode == "steam")) managerPath = null;
            string explicitPath = !String.IsNullOrWhiteSpace(managerPath) ? managerPath :
                mode == "launcher" || mode == "steam" ? Json.Text(saved, "gameLauncher") : null;
            if (!String.IsNullOrWhiteSpace(managerPath) || mode == "launcher" || mode == "steam")
            {
                var valid = ExistingChoice(explicitPath, exists, readText);
                if (String.IsNullOrWhiteSpace(managerPath) && valid != null && valid.Mode != mode) valid = null;
                return new Result { Path = valid?.Path ?? explicitPath, Mode = valid?.Mode ?? mode, CanUse = valid != null,
                    Message = valid == null ? "gameSavedMissing" : valid.Mode == "steam" ? "gameSteamSaved" : "gameSavedFound" };
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
            var choices = found.Select(p => new Choice { Path = p, Mode = "launcher" }).ToList();
            foreach (var candidate in steamPaths ?? new string[0])
            {
                var choice = ExistingChoice(candidate, exists, readText);
                if (choice != null && choice.Mode == "steam" && found.Add(choice.Path)) choices.Add(choice);
            }
            var paths = found.OrderBy(p => p, StringComparer.OrdinalIgnoreCase).ToList();
            return new Result { Path = paths.Count == 1 ? paths[0] : null, CanUse = paths.Count == 1,
                Mode = choices.Count == 1 ? choices[0].Mode : null,
                Message = paths.Count == 1 ? choices[0].Mode == "steam" ? "gameSteamFound" : "gameAutoFound" : paths.Count > 1 ? "gameMultiple" : "gameNotFound",
                Candidates = paths, Choices = choices.OrderBy(p => p.Mode).ThenBy(p => p.Path, StringComparer.OrdinalIgnoreCase).ToList() };
        }
        public static Result Discover(string managerPath, string backendData, CancellationToken cancel, string pendingPath = null, bool choicesOnly = false)
        {
            cancel.ThrowIfCancellationRequested();
            bool pendingChoice = !String.IsNullOrWhiteSpace(pendingPath);
            if (pendingChoice) managerPath = pendingPath;
            var saved = new Dictionary<string, object>();
            string settings = System.IO.Path.Combine(backendData, "settings.json");
            try
            {
                if (!choicesOnly && File.Exists(settings))
                {
                    if (new FileInfo(settings).Length > 1024 * 1024) throw new InvalidDataException("Settings file too large.");
                    saved = Json.Read<Dictionary<string, object>>(File.ReadAllText(settings)) ?? saved;
                }
            }
            catch (Exception e) when (e is IOException || e is UnauthorizedAccessException || e is SecurityException || e is ArgumentException || e is InvalidOperationException)
            {
                if (String.IsNullOrWhiteSpace(managerPath)) return new Result { Message = "gameSettingsUnreadable" };
            }
            if (choicesOnly) { managerPath = null; pendingChoice = false; }
            if (!String.IsNullOrWhiteSpace(managerPath) || Json.Text(saved, "gameStart") == "manual" || Json.Text(saved, "gameStart") == "launcher" || Json.Text(saved, "gameStart") == "steam")
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
            var steamRoots = new List<string>();
            foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                cancel.ThrowIfCancellationRequested();
                try
                {
                    using (var root = RegistryKey.OpenBaseKey(hive, view))
                    using (var key = root.OpenSubKey(@"SOFTWARE\Valve\Steam"))
                    {
                        if (key != null) { steamRoots.Add(Convert.ToString(key.GetValue("SteamPath"))); steamRoots.Add(Convert.ToString(key.GetValue("InstallPath"))); }
                    }
                }
                catch (Exception e) when (e is SecurityException || e is UnauthorizedAccessException || e is IOException) { }
            }
            var programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
            if (!String.IsNullOrWhiteSpace(programFiles)) steamRoots.Add(System.IO.Path.Combine(programFiles, "Steam"));
            // No recursive disk scan. Official defaults and Steam metadata only.
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
            var steam = SteamCandidates(steamRoots, File.Exists);
            // Uninstall records can identify a library omitted from the current Steam account.
            foreach (var entry in registrations.Where(r => String.Equals(r.Key, "Steam App " + SteamApp, StringComparison.OrdinalIgnoreCase)))
                if (!String.IsNullOrWhiteSpace(entry.Location))
                    try { steam.Add(System.IO.Path.Combine(entry.Location, "Wuthering Waves.exe")); } catch (ArgumentException) { }
            return Resolve(managerPath, saved, registrations, defaults, File.Exists, false, steam);
        }
    }
}
