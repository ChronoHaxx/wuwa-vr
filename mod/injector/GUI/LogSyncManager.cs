using System;
using System.IO;
using System.Windows.Forms;

namespace Custom_UEVR_Injector
{
    public static class LogSyncManager
    {
        private static readonly string _logFilePath = Path.Combine(
            Path.GetDirectoryName(Application.ExecutablePath),
            "Custom_UEVR_Injector.txt"
        );
        private static string _lastText;
        private static bool _writeWarningShown;

        public static void SyncLog(TextBox listResults)
        {
            var current = listResults.Text;
            if (current != _lastText)
            {
                try
                {
                    File.WriteAllText(_logFilePath, current);
                    _lastText = current;
                    _writeWarningShown = false;
                }
                catch (Exception error) when (error is IOException || error is UnauthorizedAccessException)
                {
                    if (!_writeWarningShown)
                    {
                        _writeWarningShown = true;
                        listResults.AppendText(Environment.NewLine + "Could not save the injector log: " + error.Message +
                            Environment.NewLine + "Copy the visible log before closing this window." + Environment.NewLine);
                    }
                }
            }
        }
    }
}
