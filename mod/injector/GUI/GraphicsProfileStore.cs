using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;

namespace Custom_UEVR_Injector
{
    // A policy marker means the launcher owns migration. Opening the injector must
    // never recreate quality defaults, even when a damaged marker cannot be parsed.
    public sealed class GraphicsProfileStore
    {
        private readonly string profile;
        public static readonly string[] ScriptKeys = {
            "sg.ResolutionQuality", "sg.ViewDistanceQuality", "sg.AntiAliasingQuality",
            "sg.PostProcessQuality", "sg.ShadowQuality", "sg.TextureQuality",
            "sg.EffectsQuality", "sg.FoliageQuality", "sg.ShadingQuality",
            "sg.ReflectionQuality", "r.VSync", "r.VolumetricCloud",
            "sg.GlobalIlluminationQuality", "r.ReflectionMethod"
        };

        private GraphicsProfileStore(string profile) { this.profile = Path.GetFullPath(profile); }

        public static GraphicsProfileStore Open(string profile)
        {
            if (string.IsNullOrWhiteSpace(profile)) return null;
            return File.Exists(Path.Combine(profile, "wuwa-graphics-policy.json"))
                ? new GraphicsProfileStore(profile) : null;
        }

        public Dictionary<string, object> ReadOverrides(string filename)
        {
            bool standard = filename == "cvars_standard.txt";
            if (!standard && filename != "user_script.txt") throw new ArgumentException("Unsupported graphics file.");
            var values = new Dictionary<string, object>();
            string path = Path.Combine(profile, filename);
            if (!File.Exists(path)) return values;
            if (new FileInfo(path).Length > 1024 * 1024) throw new InvalidDataException("Graphics file exceeds 1 MiB.");
            string[] keys = standard ? new[] { "Core_r.ScreenPercentage" } : ScriptKeys;
            foreach (string line in File.ReadAllLines(path))
                foreach (string key in keys)
                {
                    var match = Regex.Match(line, "^[ \\t]*" + Regex.Escape(key)
                        + (standard ? "[ \\t]*=[ \\t]*(.*)$" : "[ \\t]+(.*)$"),
                        standard ? RegexOptions.None : RegexOptions.IgnoreCase);
                    if (match.Success) values[key] = match.Groups[1].Value.Trim();
                }
            return values;
        }

        // Called only for a deliberate slider edit, never while loading controls.
        // Patch the current file, not a parsed dictionary: comments, unknown commands,
        // BOM and line endings survive. Duplicate instances of this key all change.
        public void SetOverride(string filename, string key, string value)
        {
            bool standard = filename == "cvars_standard.txt" && key == "Core_r.ScreenPercentage";
            bool script = filename == "user_script.txt" && Array.IndexOf(ScriptKeys, key) >= 0;
            double number;
            if ((!standard && !script) || !double.TryParse(value, NumberStyles.Float,
                    CultureInfo.InvariantCulture, out number) || double.IsNaN(number) || double.IsInfinity(number))
                throw new ArgumentException("Unsupported graphics override.");

            string path = Path.Combine(profile, filename);
            if (File.Exists(path) && new FileInfo(path).Length > 1024 * 1024)
                throw new InvalidDataException("Graphics file exceeds 1 MiB.");
            byte[] bytes = File.Exists(path) ? File.ReadAllBytes(path) : new byte[0];
            if (bytes.Length > 1024 * 1024) throw new InvalidDataException("Graphics file exceeds 1 MiB.");
            Encoding encoding = new UTF8Encoding(false, true);
            int prefix = 0;
            if (bytes.Length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) prefix = 3;
            else if (bytes.Length >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) { encoding = new UnicodeEncoding(false, false, true); prefix = 2; }
            else if (bytes.Length >= 2 && bytes[0] == 0xfe && bytes[1] == 0xff) { encoding = new UnicodeEncoding(true, false, true); prefix = 2; }
            string text = encoding.GetString(bytes, prefix, bytes.Length - prefix);
            string newline = text.Contains("\r\n") ? "\r\n" : text.Contains("\n") ? "\n" : Environment.NewLine;
            string[] parts = Regex.Split(text, "(\\r\\n|\\n|\\r)");
            var match = new Regex("^[ \\t]*" + Regex.Escape(key) + (standard ? "[ \\t]*=" : "[ \\t]+"),
                standard ? RegexOptions.None : RegexOptions.IgnoreCase);
            string replacement = key + (standard ? "=" : " ") + value;
            bool found = false;
            for (int i = 0; i < parts.Length; i += 2)
                if (match.IsMatch(parts[i])) { parts[i] = replacement; found = true; }
            string changed = string.Concat(parts);
            if (!found) changed += (changed.Length > 0 && !changed.EndsWith("\n") && !changed.EndsWith("\r") ? newline : "") + replacement + newline;
            byte[] encoded = encoding.GetBytes(changed);
            byte[] output = new byte[prefix + encoded.Length];
            Array.Copy(bytes, output, prefix);
            Array.Copy(encoded, 0, output, prefix, encoded.Length);
            File.WriteAllBytes(path, output);
        }
    }
}
