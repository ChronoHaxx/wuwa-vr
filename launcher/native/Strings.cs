using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

namespace WuWaVR.Manager
{
    public sealed class Strings
    {
        // Picker order: the two original languages first, then by code. Names are
        // written in their own language so a reader can always find theirs.
        public static readonly string[] Codes = { "en", "zh-Hans", "ar", "de", "es", "fr", "ja", "ko", "pt-BR", "ru" };
        public static readonly string[] Names = { "English", "简体中文", "العربية", "Deutsch", "Español", "Français", "日本語", "한국어", "Português (Brasil)", "Русский" };
        public static bool RightToLeft(string code) { return code == "ar"; }
        public static string Supported(string code) { return Codes.Contains(code) ? code : "en"; }
        // Maps a Windows UI culture (zh-CN, pt-BR, de-AT…) to a launcher language.
        public static string FromCulture(string culture)
        {
            culture = culture ?? "";
            if (culture.StartsWith("zh", StringComparison.OrdinalIgnoreCase)) return "zh-Hans";
            if (culture.StartsWith("pt", StringComparison.OrdinalIgnoreCase)) return "pt-BR";
            string language = culture.Split('-')[0].ToLowerInvariant();
            return Codes.Contains(language) ? language : "en";
        }
        readonly Dictionary<string, Dictionary<string, string>> values = new Dictionary<string, Dictionary<string, string>>();
        public string Language = "en";
        public Strings()
        {
            var assembly = Assembly.GetExecutingAssembly();
            foreach (var resource in assembly.GetManifestResourceNames().Where(x => x.Contains(".locales.") && x.EndsWith(".json")))
            using (var reader = new StreamReader(assembly.GetManifestResourceStream(resource)))
            { var v = Json.Read<Dictionary<string, string>>(reader.ReadToEnd()); values.Add(v["code"], v); }
            if (Codes.Any(code => !values.ContainsKey(code))) throw new InvalidDataException("Launcher translations are missing.");
            foreach (var language in values.Values)
                if (values["en"].Keys.Except(language.Keys).Any()) throw new InvalidDataException("Incomplete launcher translation.");
        }
        public string this[string key]
        {
            get { string result; var language = values.ContainsKey(Language) ? values[Language] : values["en"]; return language.TryGetValue(key, out result) ? result : key; }
        }
        public string ControllerReport(ControllerDiagnostics.Report report)
        {
            var output = new StringBuilder();
            output.AppendLine(this["controllerReportTitle"]);
            output.AppendLine(String.Format(this["controllerCheckedAt"], report.CheckedAtUtc.ToUniversalTime().ToString("yyyy-MM-dd HH:mm:ss 'UTC'")));
            output.AppendLine(String.Format(this["controllerXInput"], ControllerStatus(report.XInputStatus)));
            for (int index = 0; index < 4; ++index)
            {
                var slot = report.Slots?.FirstOrDefault(s => s != null && s.Index == index);
                output.AppendLine(String.Format(this["controllerSlot"], index, ControllerStatus(slot?.Status)) +
                    (slot?.ErrorCode.HasValue == true ? " (" + slot.ErrorCode.Value + ")" : ""));
            }
            output.AppendLine(this["controllerSlotsHint"]);
            output.AppendLine();
            var hidden = report.HidHide;
            output.AppendLine("HidHide: " + ControllerStatus(hidden?.Status));
            output.AppendLine(String.Format(this["controllerHidHideState"], ControllerBoolean(hidden?.Installed),
                ControllerBoolean(hidden?.Cloaking), ControllerBoolean(hidden?.Inverse)));
            output.AppendLine(String.Format(this["controllerHiddenRules"], ControllerCount(hidden?.HiddenDeviceCount),
                ControllerCount(hidden?.ApplicationRuleCount)));
            bool hiddenRulesRead = hidden?.HiddenDeviceCount.HasValue == true;
            output.AppendLine(String.Format(this["controllerRuleKinds"], ControllerCount(hiddenRulesRead ? (int?)hidden.XboxCompatibleHiddenCount : null),
                ControllerCount(hiddenRulesRead ? (int?)hidden.BluetoothHiddenCount : null), ControllerCount(hiddenRulesRead ? (int?)hidden.ExplicitVirtualHiddenCount : null)));
            output.AppendLine(String.Format(this["controllerGameRule"], ControllerBoolean(hidden?.GameRulePresent)));
            output.AppendLine(String.Format(this["controllerAppRules"], hidden?.ApplicationRuleCount.HasValue == true ?
                ControllerLabels(hidden.ApplicationRules) : this["controllerStatus_unknown"]));
            output.AppendLine(this["controllerRulesHint"]);
            output.AppendLine();
            output.AppendLine(String.Format(this["controllerHelperStatus"], ControllerStatus(report.HelpersStatus)));
            output.AppendLine(String.Format(this["controllerHelpers"], report.HelpersStatus == "available" ?
                ControllerLabels(report.Helpers) : this["controllerStatus_unknown"]));
            foreach (string finding in (report.Findings ?? new string[0]).Distinct())
            {
                string key;
                if (ControllerFindingKeys.TryGetValue(finding ?? "", out key)) output.AppendLine("• " + this[key]);
            }
            output.AppendLine(); output.Append(this["controllerPrivacy"]);
            return output.ToString();
        }
        string ControllerStatus(string status)
        {
            return this["controllerStatus_" + (new[] { "available", "partial", "connected", "disconnected", "error", "unavailable", "timeout", "not_found" }
                .Contains(status) ? status : "unknown")];
        }
        string ControllerBoolean(bool? value) { return this[value.HasValue ? value.Value ? "controllerYes" : "controllerNo" : "controllerStatus_unknown"]; }
        string ControllerCount(int? value) { return value.HasValue && value.Value >= 0 ? value.Value.ToString() : this["controllerStatus_unknown"]; }
        string ControllerLabels(string[] labels)
        {
            // Never echo an unexpected process name, application path or ID from
            // a provider. These are the only labels the probe contract permits.
            var allowed = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
                { "Reality Runner", "Reality Runner" }, { "Steam", "Steam" }, { "UEVR injector", this["controllerInjector"] },
                { "WuWa game", this["controllerGame"] }, { "WuWa launcher", this["controllerLauncher"] },
                { "Other application", this["controllerOtherApp"] } };
            var safe = (labels ?? new string[0]).Select(label => {
                string value; return label != null && allowed.TryGetValue(label, out value) ? value : this["controllerOtherApp"];
            }).Distinct().ToArray();
            return safe.Length == 0 ? this["controllerNoneSeen"] : String.Join(", ", safe);
        }
        static readonly Dictionary<string, string> ControllerFindingKeys = new Dictionary<string, string> {
            { "xinput_unavailable", "controllerFindingXInputUnavailable" }, { "no_xinput_connected", "controllerFindingNone" },
            { "xinput_probe_error", "controllerFindingProbe" }, { "hidhide_active", "controllerFindingHiding" },
            { "hidhide_game_not_listed", "controllerFindingGameRule" }, { "hidhide_inverse", "controllerFindingInverse" },
            { "hidhide_incomplete", "controllerFindingIncomplete" }, { "helpers_incomplete", "controllerFindingHelpersIncomplete" },
            { "shared_xinput_visibility", "controllerFindingVisibility" } };
    }
}
