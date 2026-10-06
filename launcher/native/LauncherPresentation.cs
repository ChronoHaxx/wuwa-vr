using System;
using System.Collections.Generic;

namespace WuWaVR.Manager
{
    // Interpret observed backend state without treating installation or a runtime
    // registration as proof of headset output.
    public static class LauncherPresentation
    {
        public static string RuntimeSummary(bool connected, Dictionary<string, object> status, Func<string, string> text)
        {
            if (!connected) return text("runtimeHint");
            var runtime = Json.Child(status, "openxr");
            var name = Json.Text(runtime, "name");
            if (String.IsNullOrWhiteSpace(name)) return text(Json.Flag(runtime, "canSimulator") ? "runtimeMissingSimulator" : "runtimeMissing");
            if (Json.Flag(runtime, "isSimulator")) return name + " · " + text(Json.Flag(runtime, "isBundledSimulator") ? "runtimeBundledSimulator" : "runtimeOtherSimulator") +
                (Json.Flag(runtime, "available") ? "" : " · " + text("runtimeUnavailable"));
            return name + " · " + text(Json.Flag(runtime, "available") ? "runtimeDetected" : "runtimeUnavailable");
        }
        public static string PackageSummary(string activeId, string chosenId, bool chosenIsCandidate, Func<string, string> text)
        {
            if (String.IsNullOrEmpty(activeId)) return text(chosenIsCandidate ? "candidatePackage" : "none");
            return text(String.Equals(activeId, chosenId, StringComparison.OrdinalIgnoreCase) ? "installed" : "otherInstalled");
        }
        public static bool CanCancelBackend(Dictionary<string, object> job)
        {
            return Json.Flag(job, "running") && Json.Text(job, "kind") == "launch";
        }
    }
}
