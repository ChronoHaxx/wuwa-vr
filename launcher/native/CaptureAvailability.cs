using System.Collections.Generic;

namespace WuWaVR.Manager
{
    public static class CaptureAvailability
    {
        // This checks packaged capabilities only. The recorder checks the live
        // process/session; a registered runtime is not proof of capture support.
        public static bool HasSource(Dictionary<string, object> status, string source)
        {
            var recording = Json.Child(status, "recording");
            if (!Json.Flag(recording, "available")) return false;
            var sources = Json.Child(recording, "sources");
            if (source == "steamvr" || source == "simulator") return Json.Flag(sources, source);
            return source == "auto" && (Json.Flag(sources, "steamvr") || Json.Flag(sources, "simulator"));
        }
    }
}
