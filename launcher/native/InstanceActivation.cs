using System;
using System.IO;
using System.Security;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace WuWaVR.Manager
{
    // Windows named events signal only another manager using the same data root.
    // The callback is injected so IPC can be tested without creating a window.
    public sealed class InstanceActivation : IDisposable
    {
        readonly EventWaitHandle request, completed;
        readonly RegisteredWaitHandle registration;
        int disposed;
        public static string Key(string root)
        {
            string canonical = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar).ToUpperInvariant();
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(canonical))).Replace("-", "").Substring(0, 24);
        }
        static string Name(string root, string suffix) { return "Local\\WuWaVRManager-" + Key(root) + suffix; }
        public InstanceActivation(string root, Func<bool> activate)
        {
            if (activate == null) throw new ArgumentNullException(nameof(activate));
            request = new EventWaitHandle(false, EventResetMode.AutoReset, Name(root, "-activate"));
            completed = new EventWaitHandle(false, EventResetMode.AutoReset, Name(root, "-activated"));
            registration = ThreadPool.RegisterWaitForSingleObject(request, (state, timedOut) =>
            {
                if (Volatile.Read(ref disposed) != 0) return;
                try { if (activate() && Volatile.Read(ref disposed) == 0) completed.Set(); }
                catch (ObjectDisposedException) { }
                catch (InvalidOperationException) { } // Dispatcher/window closed during shutdown.
            }, null, Timeout.Infinite, false);
        }
        public static bool TryActivate(string root, int timeoutMilliseconds = 1500)
        {
            // The mutex can appear briefly before the first process registers IPC.
            for (int attempt = 0; attempt < 6; attempt++)
            {
                try
                {
                    using (var signal = EventWaitHandle.OpenExisting(Name(root, "-activate")))
                    using (var ack = EventWaitHandle.OpenExisting(Name(root, "-activated")))
                    { ack.Reset(); signal.Set(); return ack.WaitOne(timeoutMilliseconds); }
                }
                catch (WaitHandleCannotBeOpenedException) { if (attempt < 5) Thread.Sleep(50); }
                catch (UnauthorizedAccessException) { return false; }
                catch (SecurityException) { return false; }
            }
            return false;
        }
        public void Dispose()
        {
            if (Interlocked.Exchange(ref disposed, 1) != 0) return;
            registration.Unregister(null); request.Dispose(); completed.Dispose();
        }
    }
}
