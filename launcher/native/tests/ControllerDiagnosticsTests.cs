using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using WuWaVR.Manager;

// Pure fake probes: no XInput call, registry query, device access, process
// enumeration, HidHide invocation, desktop input or configuration changes.
public static class ControllerDiagnosticsTests
{
    static void Check(bool value, string message) { if (!value) throw new Exception("Controller diagnostics: " + message); }
    static ControllerDiagnostics.Sources Fake(Dictionary<string, string> output = null)
    {
        output = output ?? new Dictionary<string, string> {
            { "--cloak-state", "--cloak-off\r\n" }, { "--inv-state", "--inv-off\r\n" },
            { "--dev-list", "" }, { "--app-list", "" } };
        return new ControllerDiagnostics.Sources {
            XInput = () => Task.FromResult(new uint?[] { 1167, 0, 1167, 1167 }),
            Locate = () => new ControllerDiagnostics.Location { Installed = true, Path = @"C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe" },
            Command = (path, command, cancel) => new ControllerDiagnostics.CommandResult { ExitCode = 0, Output = output[command] },
            Processes = () => new[] { "steam", "UEVRInjector", "RealityRunner", "UnrelatedSecretName" }, TimeoutMs = 1000 };
    }
    static ControllerDiagnostics.Report Run(ControllerDiagnostics.Sources sources)
    { return ControllerDiagnostics.RunAsync(sources, CancellationToken.None).GetAwaiter().GetResult(); }
    public static int Run()
    {
        int passed = 0;
        Action<string, Action> test = (name, body) => { body(); passed++; Console.WriteLine("PASS controller " + name); };
        test("checks every XInput slot without assuming slot zero", () => {
            var result = Run(Fake());
            Check(result.XInputStatus == "available" && result.Slots.Count == 4, "incomplete XInput report");
            Check(result.Slots.Select(s => s.Index).SequenceEqual(new[] { 0, 1, 2, 3 }), "slot numbers changed");
            Check(result.Slots[0].Status == "disconnected" && result.Slots[1].Status == "connected", "connected secondary slot lost");
            Check(!result.Findings.Contains("no_xinput_connected"), "slot zero treated as all slots");
        });
        test("API absence and API errors remain distinct from disconnected pads", () => {
            var sources = Fake(); sources.XInput = () => Task.FromResult<uint?[]>(null);
            var missing = Run(sources);
            Check(missing.XInputStatus == "unavailable" && missing.Slots.All(s => s.Status == "unavailable"), "missing API became disconnected");
            sources.XInput = () => Task.FromResult(new uint?[] { 1167, 1167, 1167, 1167 });
            Check(Run(sources).Findings.Contains("no_xinput_connected"), "all disconnected evidence lost");
            sources.XInput = () => Task.FromResult(new uint?[] { 5, null, 0, 1167 });
            var partial = Run(sources);
            Check(partial.XInputStatus == "partial" && partial.Slots[0].ErrorCode == 5 && partial.Slots[2].Status == "connected", "partial API error hid valid slot");
        });
        test("saved hiding rules are counted and redacted without physical topology claims", () => {
            var sources = Fake(new Dictionary<string, string> {
                { "--cloak-state", "--cloak-on\n" }, { "--inv-state", "--inv-off\n" },
                { "--dev-list", "--dev-hide \"HID\\VID_045E&PID_028E\\SECRET_SERIAL\"\n--dev-hide \"BTHENUM\\VID_045E\\PRIVATE_BT_ADDRESS\"\n" },
                { "--app-list", "--app-reg \"C:\\Users\\PrivatePerson\\Apps\\RealityRunner-Win64-Shipping.exe\"\n--app-reg \"C:\\Users\\PrivatePerson\\PersonalSecret.exe\"\n" } });
            var result = Run(sources); var json = Json.Write(result);
            Check(result.HidHide.Status == "available" && result.HidHide.Cloaking == true && result.HidHide.Inverse == false, "known state not parsed");
            Check(result.HidHide.HiddenDeviceCount == 2 && result.HidHide.XboxCompatibleHiddenCount == 2 && result.HidHide.BluetoothHiddenCount == 1, "rule classification changed");
            Check(result.HidHide.ExplicitVirtualHiddenCount == 0, "Xbox VID incorrectly proved virtual topology");
            Check(result.HidHide.ApplicationRules.Contains("Reality Runner") && result.HidHide.ApplicationRules.Contains("Other application"), "safe application categories missing");
            Check(result.Findings.Contains("hidhide_game_not_listed"), "normal-mode potential restriction missing");
            foreach (var secret in new[] { "PrivatePerson", "SECRET_SERIAL", "PRIVATE_BT_ADDRESS", "PersonalSecret", "C:\\", "HID\\", "VID_045E" })
                Check(!json.Contains(secret), "public report leaked " + secret);
        });
        test("inverse mode does not reuse normal allow-list diagnosis", () => {
            var sources = Fake(new Dictionary<string, string> { { "--cloak-state", "--cloak-on" }, { "--inv-state", "--inv-on" },
                { "--dev-list", "--dev-hide \"ROOT\\VIGEM\\PRIVATE\"" }, { "--app-list", "--app-reg \"D:\\Game\\Client-Win64-Shipping.exe\"" } });
            var result = Run(sources);
            Check(result.HidHide.GameRulePresent == true && result.HidHide.ExplicitVirtualHiddenCount == 1, "known rule not recognized");
            Check(result.Findings.Contains("hidhide_inverse") && !result.Findings.Contains("hidhide_game_not_listed"), "inverse semantics inverted");
        });
        test("malformed, excessive and timed-out output stays unknown", () => {
            var sources = Fake(); sources.Command = (path, command, cancel) => command == "--cloak-state" ?
                new ControllerDiagnostics.CommandResult { ExitCode = 0, Output = "--cloak-on\n--cloak-off" } :
                command == "--inv-state" ? new ControllerDiagnostics.CommandResult { ExitCode = 0, Output = "--inv-off" } :
                command == "--dev-list" ? new ControllerDiagnostics.CommandResult { ExitCode = 0, Output = new string('X', 32769) } :
                new ControllerDiagnostics.CommandResult { TimedOut = true };
            var result = Run(sources);
            Check(result.HidHide.Status == "partial" && result.HidHide.Cloaking == null && result.HidHide.HiddenDeviceCount == null && result.HidHide.GameRulePresent == null, "failed read inferred a negative result");
            Check(result.Findings.Contains("hidhide_incomplete") && !result.Findings.Contains("hidhide_game_not_listed"), "incomplete output overdiagnosed");
            sources.Command = (path, command, cancel) => new ControllerDiagnostics.CommandResult { ExitCode = 0, TimedOut = true, Output = "--cloak-on" };
            Check(Run(sources).HidHide.Status == "timeout", "CLI timeout was concealed");
        });
        test("unknown installation and genuinely empty lists are different", () => {
            var sources = Fake(); var empty = Run(sources);
            Check(empty.HidHide.HiddenDeviceCount == 0 && empty.HidHide.ApplicationRuleCount == 0 && empty.HidHide.Status == "available", "empty successful lists became unavailable");
            sources.Locate = () => new ControllerDiagnostics.Location { Installed = true };
            Check(Run(sources).HidHide.Status == "partial", "driver without CLI declared absent");
            sources.Locate = () => new ControllerDiagnostics.Location { Installed = null };
            Check(Run(sources).HidHide.Installed == null, "registry access failure declared absent");
            sources.Locate = () => new ControllerDiagnostics.Location { Installed = false };
            Check(Run(sources).HidHide.Status == "not_found", "confirmed missing installation lost");
        });
        test("helper names are fixed labels and exceptions reveal no private data", () => {
            var sources = Fake(); var result = Run(sources);
            Check(result.Helpers.SequenceEqual(new[] { "Reality Runner", "Steam", "UEVR injector" }), "helper names escaped categorization");
            sources.Processes = () => { throw new Exception(@"C:\Users\Secret\Private.exe"); };
            sources.Command = (path, command, cancel) => { throw new Exception("DEVICE_SERIAL_SECRET"); };
            result = Run(sources);
            Check(result.HelpersStatus == "error" && result.Findings.Contains("helpers_incomplete"), "failed inventory claimed no processes");
            Check(!Json.Write(result).Contains("Secret") && !Json.Write(result).Contains("DEVICE_SERIAL"), "exception text leaked");
        });
        test("XInput deadline preserves independent results and returns four unknown slots", () => {
            var sources = Fake(); sources.TimeoutMs = 40; sources.XInput = () => new TaskCompletionSource<uint?[]>().Task;
            var watch = Stopwatch.StartNew(); var result = Run(sources);
            Check(watch.ElapsedMilliseconds < 2000 && result.XInputStatus == "timeout", "XInput probe did not time out");
            Check(result.Slots.Count == 4 && result.Slots.All(s => s.Status == "timeout") && result.HidHide.Status == "available", "timeout discarded other evidence");
        });
        test("cancellation exits a waiting read without claiming disconnection", () => {
            var sources = Fake(); sources.XInput = () => new TaskCompletionSource<uint?[]>().Task;
            using (var cancel = new CancellationTokenSource())
            {
                var pending = ControllerDiagnostics.RunAsync(sources, cancel.Token);
                cancel.Cancel(); bool stopped = false;
                try { pending.GetAwaiter().GetResult(); }
                catch (OperationCanceledException) { stopped = true; }
                Check(stopped, "cancelled check returned a report");
            }
        });
        test("CLI execution admits only fixed read commands and keeps paths out of shell text", () => {
            const string path = @"C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe";
            var start = ControllerDiagnostics.CommandStartInfo(path, "--app-list");
            Check(start.FileName == path && start.Arguments == "--app-list" && !start.UseShellExecute && start.CreateNoWindow && start.RedirectStandardInput, "unsafe process invocation");
            foreach (var command in new[] { "--cloak-off", "--dev-unhide", "--app-list --cloak-off", "--app-list\n--cloak-off" })
            {
                bool rejected = false;
                try { ControllerDiagnostics.CommandStartInfo(path, command); } catch (ArgumentException) { rejected = true; }
                Check(rejected, "mutation command accepted");
            }
            foreach (var badPath in new[] { "HidHideCLI.exe", @"C:\Temp\wrong.exe", "C:\\bad\"name\\HidHideCLI.exe" })
            {
                bool rejected = false;
                try { ControllerDiagnostics.CommandStartInfo(badPath, "--dev-list"); } catch (ArgumentException) { rejected = true; }
                Check(rejected, "unsafe CLI path accepted");
            }
        });
        Console.WriteLine("Controller diagnostics: " + passed + " groups passed.");
        return passed;
    }
}
