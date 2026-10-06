using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Custom_UEVR_Injector
{
    // A started remote thread is not proof that LoadLibrary succeeded. Use
    // Unicode paths, wait for completion, then verify the exact loaded module.
    internal static class RemoteLibraryLoader
    {
        internal static string Canonical(string path) => Path.GetFullPath(path);

        internal static bool IsLoaded(Process target, string path)
        {
            path = Canonical(path);
            target.Refresh();
            foreach (ProcessModule module in target.Modules)
                if (string.Equals(Canonical(module.FileName), path, StringComparison.OrdinalIgnoreCase))
                    return true;
            return false;
        }

        private static string Error(string operation) => operation + ": " +
            new Win32Exception(Marshal.GetLastWin32Error()).Message;

        private static IntPtr LoaderAddress(Process target)
        {
            var local = GetProcAddress(GetModuleHandleW("kernel32.dll"), "LoadLibraryW");
            if (local == IntPtr.Zero || !GetModuleHandleExW(6, local, out var owner))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot resolve LoadLibraryW");
            var path = new StringBuilder(32768);
            if (GetModuleFileNameW(owner, path, path.Capacity) == 0)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            var name = Path.GetFileName(path.ToString());
            // GetProcAddress may forward from kernel32 into KernelBase. Resolve
            // against the actual owner in the target, not this process's VA.
            if (!name.Equals("kernel32.dll", StringComparison.OrdinalIgnoreCase) &&
                !name.Equals("kernelbase.dll", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("LoadLibraryW has an unexpected module owner.");
            var offset = local.ToInt64() - owner.ToInt64();
            target.Refresh();
            foreach (ProcessModule module in target.Modules)
                if (module.ModuleName.Equals(name, StringComparison.OrdinalIgnoreCase) &&
                    offset >= 0 && offset < module.ModuleMemorySize)
                    return new IntPtr(module.BaseAddress.ToInt64() + offset);
            throw new InvalidOperationException("The target's Windows loader module could not be resolved.");
        }

        internal static bool Load(Process target, string path, Action<string> log, uint timeoutMs = 15000)
        {
            IntPtr process = IntPtr.Zero, memory = IntPtr.Zero, thread = IntPtr.Zero;
            bool threadStarted = false, threadFinished = false;
            try
            {
                path = Canonical(path);
                if (!File.Exists(path)) { log("Missing DLL: " + path); return false; }
                if (IntPtr.Size != 8) { log("Use the 64-bit injector for this game."); return false; }
                // CREATE_THREAD | QUERY_INFORMATION | VM_OPERATION | VM_READ | VM_WRITE.
                process = OpenProcess(0x43a, false, target.Id);
                if (process == IntPtr.Zero) { log(Error("OpenProcess")); return false; }
                // Recheck the actual handle used for remote writes, not only the
                // discovery PID. A reused PID or unreadable image fails closed.
                TargetPathBinding.AssertOpenedTarget(target, process);
                if (!IsWow64Process(process, out var wow64)) { log(Error("IsWow64Process")); return false; }
                if (wow64) { log("The target is 32-bit; this injector expects a 64-bit game."); return false; }
                if (IsLoaded(target, path)) { log("Already loaded: " + Path.GetFileName(path)); return true; }
                foreach (ProcessModule module in target.Modules)
                    if (module.ModuleName.Equals(Path.GetFileName(path), StringComparison.OrdinalIgnoreCase))
                    {
                        log("A different copy of " + Path.GetFileName(path) + " is already loaded. Close the game normally before switching builds.");
                        return false;
                    }
                var loader = LoaderAddress(target);
                var bytes = Encoding.Unicode.GetBytes(path + "\0");
                memory = VirtualAllocEx(process, IntPtr.Zero, (UIntPtr)bytes.Length, 0x3000, 4);
                if (memory == IntPtr.Zero) { log(Error("VirtualAllocEx")); return false; }
                if (!WriteProcessMemory(process, memory, bytes, (UIntPtr)bytes.Length, out var written) ||
                    written.ToUInt64() != (ulong)bytes.Length)
                { log(Error("WriteProcessMemory")); return false; }
                thread = CreateRemoteThread(process, IntPtr.Zero, UIntPtr.Zero, loader, memory, 0, IntPtr.Zero);
                if (thread == IntPtr.Zero) { log(Error("CreateRemoteThread")); return false; }
                threadStarted = true;
                var wait = WaitForSingleObject(thread, timeoutMs);
                if (wait != 0)
                {
                    log(wait == 0x102 ? "DLL load timed out; completion is unknown. Close the game normally before retrying."
                                     : Error("Waiting for DLL load"));
                    // Never release a path still being read or terminate the
                    // loader thread. The target releases it on process exit.
                    return false;
                }
                threadFinished = true;
                bool loaded = IsLoaded(target, path);
                if (!loaded)
                {
                    // Thread exit is a DWORD; it is not a 64-bit HMODULE and
                    // cannot establish successful loading. Keep it diagnostic.
                    string code = GetExitCodeThread(thread, out var exitCode) ? " (thread result 0x" + exitCode.ToString("X8") + ")" : "";
                    log("The requested DLL is not loaded" + code + ": " + Path.GetFileName(path) +
                        ". Check dependencies, security software and the injector log. No successful injection is recorded.");
                    return false;
                }
                log("Verified loaded module: " + Path.GetFileName(path));
                return true;
            }
            catch (Exception error)
            {
                log(error.GetType().Name + ": " + error.Message);
                return false;
            }
            finally
            {
                if (memory != IntPtr.Zero && (!threadStarted || threadFinished))
                    VirtualFreeEx(process, memory, UIntPtr.Zero, 0x8000);
                if (thread != IntPtr.Zero) CloseHandle(thread);
                if (process != IntPtr.Zero) CloseHandle(process);
            }
        }

        [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool IsWow64Process(IntPtr process, out bool wow64);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true, SetLastError = true)] private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll", ExactSpelling = true, SetLastError = true)] private static extern bool GetModuleHandleExW(uint flags, IntPtr address, out IntPtr module);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)] private static extern uint GetModuleFileNameW(IntPtr module, StringBuilder path, int length);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint type, uint protect);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint type);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] bytes, UIntPtr size, out UIntPtr written);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attributes, UIntPtr stack, IntPtr start, IntPtr parameter, uint flags, IntPtr id);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetExitCodeThread(IntPtr thread, out uint exitCode);
        [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    }
}
