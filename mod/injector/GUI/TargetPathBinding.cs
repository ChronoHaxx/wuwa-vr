using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace Custom_UEVR_Injector
{
    // Kept separate from Forms and profile initialization so argument/selection
    // tests never enumerate a real process or write a player profile.
    public sealed class InjectorArguments
    {
        public const string Capability = "WUWA_INJECTOR_TARGET_PATH_V1";
        public string GameExecutable { get; private set; }
        public string TargetPath { get; private set; }
        public bool CapabilitiesOnly { get; private set; }
        public bool IsPathBound { get { return TargetPath != null; } }

        public static InjectorArguments Parse(string[] args, Action<string> requireReadable = null)
        {
            if (args == null) throw new ArgumentException("Arguments are missing.");
            if (args.Length == 0) return new InjectorArguments();
            if (args.Length == 1 && args[0] == "--capabilities") return new InjectorArguments { CapabilitiesOnly = true };
            if (args.Length == 2 && args[0] == "--target-path")
            {
                string path = NormalizeTargetPath(args[1]);
                (requireReadable ?? RequireReadable)(path);
                return new InjectorArguments { GameExecutable = "Client-Win64-Shipping.exe", TargetPath = path };
            }
            // Existing official launcher invokes the bare shipping executable.
            if (args.Length == 1 && !String.IsNullOrWhiteSpace(args[0]) && !args[0].StartsWith("-", StringComparison.Ordinal) &&
                args[0].EndsWith("Win64-Shipping.exe", StringComparison.OrdinalIgnoreCase) &&
                args[0].IndexOfAny(new[] { '\\', '/', ':', '"', '\r', '\n', '\0' }) < 0)
                return new InjectorArguments { GameExecutable = args[0].Trim() };
            throw new ArgumentException("Use no arguments, a shipping executable name, or --target-path followed by one absolute Client-Win64-Shipping.exe path.");
        }
        public static string NormalizeTargetPath(string path)
        {
            if (String.IsNullOrWhiteSpace(path) || path != path.Trim() || path.Length > 32760 ||
                path.IndexOfAny(new[] { '"', '*', '?', '<', '>', '|', '\r', '\n', '\0' }) >= 0)
                throw new ArgumentException("Target path contains invalid characters.");
            path = path.Replace('/', '\\');
            bool drive = path.Length >= 3 && Char.IsLetter(path[0]) && path[1] == ':' && path[2] == '\\';
            bool unc = path.StartsWith(@"\\", StringComparison.Ordinal) && !path.StartsWith(@"\\.\", StringComparison.Ordinal);
            if ((!drive && !unc) || path.IndexOf(':', drive ? 2 : 0) >= 0)
                throw new ArgumentException("Target path must be a fully qualified Windows executable path.");
            var parts = path.Substring(drive ? 3 : 2).Split('\\');
            if (parts.Length < (drive ? 1 : 3)) throw new ArgumentException("Target path is incomplete.");
            foreach (var part in parts)
                if (String.IsNullOrEmpty(part) || part == "." || part == ".." || part.EndsWith(".", StringComparison.Ordinal) ||
                    part.EndsWith(" ", StringComparison.Ordinal)) throw new ArgumentException("Target path contains an ambiguous component.");
            string full = Path.GetFullPath(path);
            if (!String.Equals(Path.GetFileName(full), "Client-Win64-Shipping.exe", StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("Target path must name Client-Win64-Shipping.exe.");
            return full;
        }
        public static void RequireReadable(string path)
        {
            // Existence alone can conceal access failures. This never executes or
            // modifies the file and tolerates the running game's shared image.
            using (var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete)) { }
        }
    }

    public sealed class TargetProcessIdentity
    {
        public readonly int Pid;
        public readonly long CreatedFileTime;
        public readonly string ImagePath;
        public TargetProcessIdentity(int pid, long createdFileTime, string imagePath)
        { Pid = pid; CreatedFileTime = createdFileTime; ImagePath = imagePath; }
    }

    public static class TargetPathPolicy
    {
        public static bool Matches(string expected, TargetProcessIdentity process)
        {
            if (process == null || process.Pid <= 0 || process.CreatedFileTime <= 0 || String.IsNullOrWhiteSpace(process.ImagePath)) return false;
            try { return String.Equals(InjectorArguments.NormalizeTargetPath(process.ImagePath), expected, StringComparison.OrdinalIgnoreCase); }
            catch (ArgumentException) { return false; }
            catch (NotSupportedException) { return false; }
            catch (IOException) { return false; }
        }
        public static int Select(string expected, TargetProcessIdentity[] candidates, out string status)
        {
            status = "Waiting for the selected game path...";
            int selected = -1;
            foreach (var candidate in candidates)
                if (candidate == null || candidate.Pid <= 0 || candidate.CreatedFileTime <= 0 || String.IsNullOrEmpty(candidate.ImagePath))
                { status = "Cannot verify a same-name process path; injection is blocked."; return -1; }
            for (int i = 0; i < candidates.Length; ++i)
            {
                if (!Matches(expected, candidates[i])) continue;
                if (selected >= 0) { status = "Multiple instances match the selected game path; close duplicates normally."; return -1; }
                selected = i;
            }
            if (selected >= 0) status = "Selected game path verified.";
            else if (candidates.Length != 0) status = "Running same-name game is not the selected path; waiting for the selected game.";
            return selected;
        }
        public static bool SameInstance(string expected, TargetProcessIdentity selected, TargetProcessIdentity opened)
        {
            return Matches(expected, selected) && Matches(expected, opened) && selected.Pid == opened.Pid &&
                selected.CreatedFileTime == opened.CreatedFileTime;
        }
    }

    internal static class TargetPathBinding
    {
        internal static InjectorArguments Request { get; private set; }
        internal static string Status { get; private set; }
        private static readonly ConditionalWeakTable<Process, TargetProcessIdentity> Selected = new ConditionalWeakTable<Process, TargetProcessIdentity>();
        static TargetPathBinding()
        { Request = InjectorArguments.Parse(new string[0]); Status = "Waiting for the selected game path..."; }
        internal static void Initialize(InjectorArguments request) { Request = request; }
        internal static Process Select(Process[] processes)
        {
            Process selected = null;
            try
            {
                InjectorArguments.RequireReadable(Request.TargetPath);
                var identities = new TargetProcessIdentity[processes.Length];
                for (int i = 0; i < processes.Length; ++i)
                {
                    IntPtr handle = OpenProcess(0x1000, false, processes[i].Id); // QUERY_LIMITED_INFORMATION only.
                    if (handle == IntPtr.Zero) continue;
                    try { identities[i] = ReadIdentity(handle, processes[i].Id); }
                    catch (Win32Exception) { }
                    finally { CloseHandle(handle); }
                }
                string status; int index = TargetPathPolicy.Select(Request.TargetPath, identities, out status); Status = status;
                if (index >= 0) { selected = processes[index]; Selected.Add(selected, identities[index]); }
                return selected;
            }
            catch (Exception error)
            { Status = "Selected game path unavailable; injection is blocked: " + error.Message; return null; }
            finally { foreach (var process in processes) if (process != selected) process.Dispose(); }
        }
        internal static void AssertOpenedTarget(Process target, IntPtr handle)
        {
            if (!Request.IsPathBound) return;
            TargetProcessIdentity selected;
            InjectorArguments.RequireReadable(Request.TargetPath);
            if (!Selected.TryGetValue(target, out selected) ||
                !TargetPathPolicy.SameInstance(Request.TargetPath, selected, ReadIdentity(handle, target.Id)))
                throw new InvalidOperationException("Selected game process identity/path changed; no DLL was loaded.");
        }
        private static TargetProcessIdentity ReadIdentity(IntPtr handle, int pid)
        {
            var image = new StringBuilder(32768); int length = image.Capacity;
            long created, exited, kernel, user;
            if (!QueryFullProcessImageNameW(handle, 0, image, ref length) ||
                !GetProcessTimes(handle, out created, out exited, out kernel, out user))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot read the target image and creation time.");
            return new TargetProcessIdentity(pid, created, image.ToString());
        }
        [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern bool QueryFullProcessImageNameW(IntPtr handle, uint flags, StringBuilder image, ref int length);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetProcessTimes(IntPtr handle,
            out long creation, out long exit, out long kernel, out long user);
    }
}
