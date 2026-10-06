using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace Custom_UEVR_Injector
{
    static class Program
    {
        private const string MutexName = "Custom_UEVR_Mutex";

        [STAThread]
        static int Main(string[] args)
        {
            InjectorArguments request;
            try { request = InjectorArguments.Parse(args); }
            catch (Exception error)
            { Console.Error.WriteLine("Injector arguments rejected: " + error.Message); return 2; }
            // Before mutex lookup, profiles, Forms or process enumeration. Backends
            // must check the passive EXE capability before invoking an old binary.
            if (request.CapabilitiesOnly) { Console.WriteLine(InjectorArguments.Capability); return 0; }
            TargetPathBinding.Initialize(request);
            bool createdNew;
            using (var mutex = new Mutex(true, MutexName, out createdNew))
            {
                if (!createdNew)
                {
                    if (request.IsPathBound)
                    { Console.Error.WriteLine("Another injector is active; close it normally before a path-bound launch."); return 3; }
                    FocusExistingInstance(); return 0;
                }
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new main__form());
            }
            return 0;
        }
        private static void FocusExistingInstance()
        {
            using (var me = Process.GetCurrentProcess())
            {
                var processes = Process.GetProcessesByName(me.ProcessName);
                try
                {
                    var other = processes.FirstOrDefault(p => p.Id != me.Id);
                    if (other == null) return;
                    IntPtr handle = other.MainWindowHandle;
                    if (handle == IntPtr.Zero) return;
                    ShowWindowAsync(handle, 9); SetForegroundWindow(handle);
                }
                finally { foreach (var process in processes) process.Dispose(); }
            }
        }
        [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr handle);
        [DllImport("user32.dll")] private static extern bool ShowWindowAsync(IntPtr handle, int command);
    }
}
