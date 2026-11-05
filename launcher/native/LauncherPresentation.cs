using System;
using System.Collections.Generic;

namespace WuWaVR.Manager
{
    // Interpret observed backend state without treating installation or a runtime
    // registration as proof of headset output.
    public static class LauncherPresentation
    {
        public static string RuntimeLaunchBlock(bool connected, Dictionary<string, object> status)
        {
            if (!connected) return "connectionRequired";
            var runtime = Json.Child(status, "openxr");
            if (!Json.Flag(runtime, "available")) return "runtimeUnavailableHint";
            if (Json.Flag(runtime, "isSimulator") && !Json.Flag(runtime, "isBundledSimulator")) return "runtimeSelectCurrentSimulator";
            if (runtime.ContainsKey("launchReady") && !Json.Flag(runtime, "launchReady")) return "runtimeUnavailableHint";
            return null;
        }
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
        // Plain words for the startup worker's phases (WuWaLaunchLifecycle.ps1, sim-run.ps1). An
        // unknown phase keeps the worker's own message; its raw text always goes to Details.
        // 133 recorded launches (Oct 2026): VR came up a median 26 s after UEVR's log started, at
        // most 70 s, and most showed a recoverable "needs attention" line on the way.
        public static string LaunchPhaseKey(string phase, bool steam, bool slow)
        {
            switch (phase)
            {
                case "permission-pending": return "phasePermission";
                case "preflight": return "phasePreparing";
                case "waiting-for-play": return steam ? "phaseSteam" : "phasePressPlay";
                case "game-starting": return "phaseGameStarting";
                case "injected": return slow ? "phaseSlow" : "phaseStartingVr";
                case "settling": return "phaseInVr";
                default: return null;
            }
        }
        // Which of the four launch chips is lit: permission, Play, starting VR, in VR.
        public static int LaunchStep(string phase)
        {
            switch (phase)
            {
                case "": case "permission-pending": case "preflight": case "handoff-unconfirmed": return 0;
                case "waiting-for-play": case "waiting-game": case "game-starting": return 1;
                case "injected": return 2;
                case "settling": return 3;
                default: return -1;
            }
        }
        // The chip where a failed or cancelled launch stopped. Workers since 1.2.7 record the last
        // progress phase (reachedPhase, WuWaLaunchLifecycle.ps1); older ones leave their evidence.
        public static int StoppedStep(Dictionary<string, object> launch)
        {
            switch (Json.Text(launch, "outcome"))
            {
                case "PreflightFailed": case "ElevationCancelled": case "ElevationFailed": case "ChildExited": return 0;
                case "LauncherClosed": return 1;
            }
            int step = LaunchStep(Json.Text(launch, "reachedPhase"));
            if (step >= 0 && Json.Text(launch, "reachedPhase") != "") return step;
            if (Json.Flag(launch, "firstFrameSeen") || Json.Flag(launch, "backendProjectionSeen")) return 3;
            if (Json.Flag(launch, "backendLogStarted")) return 2;
            return Json.Flag(launch, "elevated") ? 1 : 0;
        }
        // A plain reason for a failed or cancelled launch, by the worker's outcome. The worker's
        // own message (log names, exit codes) stays in Details.
        public static string FailureKey(Dictionary<string, object> launch, bool cancelled)
        {
            string outcome = Json.Text(launch, "outcome");
            if (cancelled) return outcome == "ElevationCancelled" ? "failPermissionDeclined" : "failCancelled";
            switch (outcome)
            {
                case "PreflightFailed": return "failPreflight";
                case "ElevationFailed": case "ChildExited": return "failBlocked";
                case "LauncherClosed": return "failLauncherClosed";
                case "TimedOut": return "failTimedOut";
                case "GameExited": return "failGameClosed";
                case "InjectorExited": case "NotInjected": case "BackendStopped": return "failNoAttach";
                case "TargetUnverified": return "failLostGame";
                default: return "failUnknown";
            }
        }
        // VR is up and the worker only finishes its 30 s check (sim-run.ps1 SettleSeconds).
        public static bool InVr(Dictionary<string, object> status)
        {
            var launch = Json.Child(status, "launch");
            return LauncherBridge.LaunchWorkerRunning(status) && Json.Text(launch, "phase") == "settling" &&
                !Json.Flag(launch, "stalled") && !Json.Flag(launch, "activityUnknown");
        }
        public static bool CanCancelBackend(Dictionary<string, object> job)
        {
            return Json.Flag(job, "running") && Json.Text(job, "kind") == "launch";
        }
    }
}
