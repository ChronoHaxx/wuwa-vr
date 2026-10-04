using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32;

[assembly: InternalsVisibleTo("Manager.Tests")]

namespace WuWaVR.Manager
{
    // Independent of the portable helper, game and backend. No device state,
    // driver configuration, application rules or input is changed by this probe.
    public static class ControllerDiagnostics
    {
        public sealed class Slot
        {
            public int Index;
            public string Status = "unavailable";
            public uint? ErrorCode;
        }
        public sealed class HidHideReport
        {
            public string Status = "not_found";
            public bool? Installed, Cloaking, Inverse, GameRulePresent;
            public int? HiddenDeviceCount, ApplicationRuleCount;
            // Counts describe saved rules, not present devices. A Microsoft VID
            // cannot distinguish a physical Xbox pad from a virtual Xbox pad.
            public int XboxCompatibleHiddenCount, BluetoothHiddenCount, ExplicitVirtualHiddenCount;
            public string[] ApplicationRules = new string[0];
        }
        public sealed class Report
        {
            public DateTime CheckedAtUtc = DateTime.UtcNow;
            public string XInputStatus = "unavailable";
            public string HelpersStatus = "unavailable";
            public List<Slot> Slots = new List<Slot>();
            public HidHideReport HidHide = new HidHideReport();
            public string[] Helpers = new string[0], Findings = new string[0];
        }

        internal sealed class Location { internal string Path; internal bool? Installed; }
        internal sealed class CommandResult
        {
            internal string Output = "";
            internal int ExitCode = -1;
            internal bool TimedOut, Truncated;
        }
        internal sealed class Sources
        {
            internal Func<Task<uint?[]>> XInput;
            internal Func<Location> Locate;
            internal Func<string, string, CancellationToken, CommandResult> Command;
            internal Func<string[]> Processes;
            internal int TimeoutMs = 1800;
        }
        static readonly string[] ReadCommands = { "--cloak-state", "--inv-state", "--dev-list", "--app-list" };
        const int CommandTimeoutMs = 1500, MaxOutputChars = 32768, MaxRules = 256;
        static readonly object XInputLock = new object();
        static Task<uint?[]> activeXInput;

        public static Task<Report> RunAsync(CancellationToken cancel)
        {
            return RunAsync(new Sources { XInput = SystemXInputAsync, Locate = LocateHidHide,
                Command = ReadCommand, Processes = ReadProcessNames }, cancel);
        }

        // This seam runs entirely on fake sources in the component tests.
        internal static async Task<Report> RunAsync(Sources sources, CancellationToken cancel)
        {
            cancel.ThrowIfCancellationRequested();
            var result = new Report();
            var xinput = ReadXInputAsync(sources, result, cancel);
            var hidhide = Task.Run(() => ReadHidHide(sources, cancel), cancel);
            var helpers = Task.Run(sources.Processes, cancel);
            // Four individually bounded commands plus discovery. Broken fake or
            // third-party probes cannot keep the user-facing check open forever.
            var hidFinished = FinishBefore(hidhide, sources.TimeoutMs * 4, cancel);
            var helpersFinished = FinishBefore(helpers, sources.TimeoutMs, cancel);
            await Task.WhenAll(xinput, hidFinished, helpersFinished).ConfigureAwait(false);
            var hidDone = await hidFinished.ConfigureAwait(false);
            if (hidDone)
            {
                try { result.HidHide = await hidhide.ConfigureAwait(false); }
                catch (OperationCanceledException) { cancel.ThrowIfCancellationRequested(); result.HidHide.Status = "error"; }
                catch { result.HidHide.Status = "error"; }
            }
            else result.HidHide.Status = "timeout";
            if (await helpersFinished.ConfigureAwait(false))
            {
                try { result.Helpers = SafeHelperNames(await helpers.ConfigureAwait(false)); result.HelpersStatus = "available"; }
                catch { result.HelpersStatus = "error"; }
            }
            else result.HelpersStatus = "timeout";
            var findings = new List<string> { "shared_xinput_visibility" };
            if (result.XInputStatus == "unavailable") findings.Add("xinput_unavailable");
            if (result.XInputStatus == "available" && !result.Slots.Any(s => s.Status == "connected")) findings.Add("no_xinput_connected");
            if (result.XInputStatus == "error" || result.XInputStatus == "partial" || result.XInputStatus == "timeout") findings.Add("xinput_probe_error");
            if (result.HidHide.Cloaking == true) findings.Add("hidhide_active");
            if (result.HidHide.Cloaking == true && result.HidHide.Inverse == false &&
                result.HidHide.HiddenDeviceCount > 0 && result.HidHide.GameRulePresent == false) findings.Add("hidhide_game_not_listed");
            if (result.HidHide.Inverse == true) findings.Add("hidhide_inverse");
            if (result.HidHide.Status != "available" && result.HidHide.Status != "not_found") findings.Add("hidhide_incomplete");
            if (result.HelpersStatus != "available") findings.Add("helpers_incomplete");
            result.Findings = findings.ToArray();
            cancel.ThrowIfCancellationRequested();
            return result;
        }

        static async Task<bool> FinishBefore(Task task, int milliseconds, CancellationToken cancel)
        {
            var finished = await Task.WhenAny(task, Task.Delay(milliseconds, cancel)).ConfigureAwait(false);
            cancel.ThrowIfCancellationRequested();
            if (finished == task) return true;
            // Observe eventual faults of a timed-out background read.
            _ = task.ContinueWith(t => { var ignored = t.Exception; }, CancellationToken.None,
                TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
            return false;
        }

        static async Task ReadXInputAsync(Sources sources, Report result, CancellationToken cancel)
        {
            uint?[] codes = null;
            try
            {
                var read = sources.XInput();
                if (!await FinishBefore(read, sources.TimeoutMs, cancel).ConfigureAwait(false)) result.XInputStatus = "timeout";
                else { codes = await read.ConfigureAwait(false); result.XInputStatus = codes == null ? "unavailable" : "available"; }
            }
            catch (OperationCanceledException) { cancel.ThrowIfCancellationRequested(); result.XInputStatus = "error"; }
            catch { result.XInputStatus = "error"; }
            if (codes != null && codes.Length != 4) { codes = null; result.XInputStatus = "error"; }
            for (int index = 0; index < 4; index++)
            {
                var slot = new Slot { Index = index, Status = result.XInputStatus };
                if (codes != null)
                {
                    slot.ErrorCode = codes[index];
                    slot.Status = codes[index] == 0 ? "connected" : codes[index] == 1167 ? "disconnected" : "error";
                    if (slot.Status == "error") result.XInputStatus = "partial";
                }
                result.Slots.Add(slot);
            }
        }

        static Task<uint?[]> SystemXInputAsync()
        {
            lock (XInputLock)
            {
                // A stuck native driver read is reused, never multiplied on Retry.
                if (activeXInput == null || activeXInput.IsCompleted) activeXInput = Task.Run(ReadSystemXInput);
                return activeXInput;
            }
        }
        [StructLayout(LayoutKind.Sequential)]
        struct XInputState
        {
            public uint Packet;
            public ushort Buttons;
            public byte LeftTrigger, RightTrigger;
            public short LeftX, LeftY, RightX, RightY;
        }
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        delegate uint GetState(uint index, out XInputState state);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll")]
        static extern bool FreeLibrary(IntPtr module);
        static uint?[] ReadSystemXInput()
        {
            foreach (var name in new[] { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" })
            {
                // Absolute System32 path and restricted DLL search prevent loading
                // an adjacent game/proxy DLL or looking in the current directory.
                var module = LoadLibraryEx(Path.Combine(Environment.SystemDirectory, name), IntPtr.Zero, 0x00000800);
                if (module == IntPtr.Zero) continue;
                try
                {
                    var address = GetProcAddress(module, "XInputGetState");
                    if (address == IntPtr.Zero) continue;
                    var get = (GetState)Marshal.GetDelegateForFunctionPointer(address, typeof(GetState));
                    var codes = new uint?[4];
                    for (uint index = 0; index < 4; index++) { XInputState state; codes[index] = get(index, out state); }
                    return codes;
                }
                finally { FreeLibrary(module); }
            }
            return null;
        }

        static Location LocateHidHide()
        {
            var result = new Location { Installed = false };
            // No PATH/current-directory search and no arbitrary uninstall command.
            foreach (var root in new[] { Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86) }.Distinct())
            {
                if (String.IsNullOrEmpty(root)) continue;
                var candidate = Path.Combine(root, "Nefarius Software Solutions", "HidHide", "x64", "HidHideCLI.exe");
                if (File.Exists(candidate)) { result.Path = candidate; result.Installed = true; break; }
            }
            if (result.Path == null)
            {
                try { using (var service = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Services\HidHide", false)) result.Installed = service != null; }
                catch { result.Installed = null; }
            }
            return result;
        }

        static HidHideReport ReadHidHide(Sources sources, CancellationToken cancel)
        {
            var location = sources.Locate();
            var report = new HidHideReport { Installed = location.Installed };
            if (String.IsNullOrEmpty(location.Path)) { report.Status = location.Installed == false ? "not_found" : "partial"; return report; }
            int successful = 0; bool timeout = false;
            foreach (var command in ReadCommands)
            {
                cancel.ThrowIfCancellationRequested();
                CommandResult output;
                try { output = sources.Command(location.Path, command, cancel); }
                catch (OperationCanceledException) { throw; }
                catch { continue; }
                if (output == null) continue;
                timeout |= output.TimedOut;
                if (output.ExitCode != 0 || output.TimedOut || output.Truncated || output.Output == null || output.Output.Length > MaxOutputChars) continue;
                if (command == "--cloak-state") { report.Cloaking = ParseState(output.Output, "cloak"); if (report.Cloaking.HasValue) successful++; }
                else if (command == "--inv-state") { report.Inverse = ParseState(output.Output, "inv"); if (report.Inverse.HasValue) successful++; }
                else
                {
                    var rules = ParseRules(output.Output, command == "--dev-list" ? "dev-hide" : "app-reg");
                    if (rules == null) continue;
                    successful++;
                    if (command == "--dev-list")
                    {
                        report.HiddenDeviceCount = rules.Count;
                        report.XboxCompatibleHiddenCount = rules.Count(s => s.IndexOf("VID_045E", StringComparison.OrdinalIgnoreCase) >= 0);
                        report.BluetoothHiddenCount = rules.Count(s => s.StartsWith("BTH", StringComparison.OrdinalIgnoreCase));
                        report.ExplicitVirtualHiddenCount = rules.Count(s => s.IndexOf("VIGEM", StringComparison.OrdinalIgnoreCase) >= 0 || s.IndexOf("VJOY", StringComparison.OrdinalIgnoreCase) >= 0);
                    }
                    else
                    {
                        report.ApplicationRuleCount = rules.Count;
                        report.ApplicationRules = rules.Select(SafeApplicationName).Distinct().OrderBy(s => s, StringComparer.Ordinal).ToArray();
                        report.GameRulePresent = rules.Any(s => String.Equals(LeafName(s), "Client-Win64-Shipping.exe", StringComparison.OrdinalIgnoreCase));
                    }
                }
            }
            report.Status = successful == 4 ? "available" : successful > 0 ? "partial" : timeout ? "timeout" : "error";
            return report;
        }
        // Official CLI emits --cloak-on/off, --inv-on/off, and quoted --dev-hide /
        // --app-reg entries: github.com/nefarius/HidHide/blob/master/HidHideCLI/src/Commands.cpp.
        // The emitted commands are parsed as data and NEVER replayed.
        internal static bool? ParseState(string output, string kind)
        {
            var value = (output ?? "").Trim();
            if (value == "--" + kind + "-on") return true;
            if (value == "--" + kind + "-off") return false;
            return null;
        }
        static List<string> ParseRules(string output, string verb)
        {
            var rules = new List<string>();
            foreach (var row in output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (String.IsNullOrWhiteSpace(row)) continue;
                var match = Regex.Match(row.Trim(), "^--" + verb + " \"([^\"\\r\\n]+)\"$");
                if (!match.Success || match.Groups[1].Value.Any(Char.IsControl) || rules.Count >= MaxRules) return null;
                rules.Add(match.Groups[1].Value);
            }
            return rules;
        }
        static string LeafName(string value) { return (value ?? "").Replace('/', '\\').Split('\\').LastOrDefault() ?? ""; }
        static string SafeApplicationName(string value)
        {
            var leaf = LeafName(value).ToLowerInvariant();
            if (leaf == "realityrunner.exe" || leaf == "realityrunner-win64-shipping.exe") return "Reality Runner";
            if (leaf == "steam.exe") return "Steam";
            if (leaf == "uevrinjector.exe") return "UEVR injector";
            if (leaf == "client-win64-shipping.exe") return "WuWa game";
            if (leaf == "wuwa vr.exe" || leaf == "wuwa vr launcher.exe") return "WuWa launcher";
            return "Other application";
        }
        static string[] SafeHelperNames(string[] names)
        {
            return (names ?? new string[0]).Take(4096).Select(n => SafeApplicationName((n ?? "") + ".exe"))
                .Where(n => n == "Reality Runner" || n == "Steam" || n == "UEVR injector").Distinct().OrderBy(n => n, StringComparer.Ordinal).ToArray();
        }
        static string[] ReadProcessNames()
        {
            var names = new List<string>(); var processes = Process.GetProcesses();
            foreach (var process in processes)
            {
                using (process) { try { names.Add(process.ProcessName); } catch { } }
            }
            return names.ToArray();
        }

        internal static ProcessStartInfo CommandStartInfo(string path, string command)
        {
            if (!ReadCommands.Contains(command)) throw new ArgumentException("Only HidHide read commands are supported.");
            if (String.IsNullOrEmpty(path) || !Path.IsPathRooted(path) || path.IndexOfAny(new[] { '"', '\r', '\n' }) >= 0 ||
                !String.Equals(Path.GetFileName(path), "HidHideCLI.exe", StringComparison.OrdinalIgnoreCase)) throw new ArgumentException("Invalid HidHide CLI path.");
            // FileName is a separate API field (spaces need no shell quoting);
            // Arguments contains exactly one fixed, argument-free read command.
            return new ProcessStartInfo { FileName = Path.GetFullPath(path), Arguments = command, UseShellExecute = false,
                CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardOutput = true,
                RedirectStandardError = true, RedirectStandardInput = true };
        }
        static CommandResult ReadCommand(string path, string command, CancellationToken cancel)
        {
            var result = new CommandResult();
            using (var process = new Process { StartInfo = CommandStartInfo(path, command) })
            {
                if (!process.Start()) return result;
                process.StandardInput.Close();
                var output = new StringBuilder(); var error = new StringBuilder();
                var stdout = Drain(process.StandardOutput, output, result);
                var stderr = Drain(process.StandardError, error, result);
                var watch = Stopwatch.StartNew();
                try
                {
                    while (!process.WaitForExit(25))
                    {
                        cancel.ThrowIfCancellationRequested();
                        if (watch.ElapsedMilliseconds >= CommandTimeoutMs) { result.TimedOut = true; break; }
                    }
                    if (!result.TimedOut && process.HasExited) result.ExitCode = process.ExitCode;
                }
                finally
                {
                    if (!process.HasExited) { try { process.Kill(); } catch { } }
                    // Never block indefinitely waiting for inherited pipe handles.
                    try { if (!Task.WaitAll(new[] { stdout, stderr }, 150)) result.Truncated = true; } catch { result.Truncated = true; }
                }
                if (stdout.IsCompleted && !stdout.IsFaulted) result.Output = output.ToString();
                else result.Truncated = true;
            }
            return result;
        }
        static async Task Drain(StreamReader reader, StringBuilder retained, CommandResult result)
        {
            var buffer = new char[1024]; int read;
            while ((read = await reader.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false)) > 0)
            {
                var keep = Math.Min(read, Math.Max(0, MaxOutputChars - retained.Length));
                if (keep > 0) retained.Append(buffer, 0, keep);
                if (keep < read) result.Truncated = true;
            }
        }
    }
}
