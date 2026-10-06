using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using WuWaVR.Manager;

public static class ProcessRecoveryTests
{
    static int count;
    static void Check(bool value, string label) { if (!value) throw new Exception("Recovery: " + label); count++; Console.WriteLine("PASS RECOVERY " + label); }
    static void Refuse(Action action, string label)
    { bool refused = false; try { action(); } catch (InvalidOperationException) { refused = true; } Check(refused, label); }
    static RecoveryCandidate Copy(RecoveryCandidate value) { return Json.Read<RecoveryCandidate>(Json.Write(value)); }
    public static void Run(string testRoot)
    {
        count = 0;
        string root = Path.Combine(testRoot, "process-recovery-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(root, "app/dev")); Directory.CreateDirectory(Path.Combine(root, "python"));
        var hashes = new Dictionary<string, string>();
        foreach (var name in new[] { "app/dev/start-wuwa-build.ps1", "app/dev/sim-run.ps1", "app/dev/wuwa-runtime.ps1", "app/dev/wuwa_player.py", "python/pythonw.exe" })
        { File.WriteAllText(Path.Combine(root, name), "inert fixture; never execute"); hashes[name] = RepoClient.Hash(Path.Combine(root, name)); }
        var roots = new List<LauncherProcessRecovery.Root> { new LauncherProcessRecovery.Root { Path = root, Files = hashes } };
        string ps = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32/WindowsPowerShell/v1.0/powershell.exe");
        string script = Path.Combine(root, "app/dev/start-wuwa-build.ps1");
        var row = new RecoveryCandidate { Pid = 12345, ParentPid = 99, Executable = ps, StartedUtc = DateTime.UtcNow.ToString("o"), UserSid = "S-1-5-21-1", SessionId = 1,
            CommandLine = "\"" + ps + "\" -NoProfile -ExecutionPolicy Bypass -File \"" + script + "\" -Id test -Elevated" };
        Check(LauncherProcessRecovery.Classify(row, roots) && row.Kind == "Startup worker", "legacy exact startup script recognized without a launch receipt");
        var altered = Copy(row); altered.CommandLine = "powershell.exe -EncodedCommand abc -File \"" + script + "\"";
        Check(!LauncherProcessRecovery.Classify(altered, roots), "encoded-command lookalike rejected");
        altered = Copy(row); altered.Executable = Path.Combine(root, "powershell.exe");
        Check(!LauncherProcessRecovery.Classify(altered, roots), "unrelated executable named powershell rejected");
        altered = Copy(row); altered.CommandLine = "powershell.exe -File \"" + Path.Combine(root, "elsewhere/start-wuwa-build.ps1") + "\"";
        Check(!LauncherProcessRecovery.Classify(altered, roots), "same script filename outside exact package location rejected");
        altered = Copy(row); altered.CommandLine = "powershell.exe -File \"" + Path.Combine(root, "app/dev/sim-run.ps1") + "\" -Activate";
        Check(LauncherProcessRecovery.Classify(altered, roots) && LauncherProcessRecovery.ActivityBlocker(altered, new List<LauncherProcessRecovery.ProcessEntry>()) != null, "runtime-mutation arguments are never stoppable");
        altered.CommandLine = altered.CommandLine.Replace("-Activate", "-Act:$true");
        Check(LauncherProcessRecovery.Classify(altered, roots) && altered.Kind == "Runtime/profile worker", "abbreviated PowerShell runtime switch is protected too");
        altered = Copy(row); altered.CommandLine = "powershell.exe -File \"" + Path.Combine(root, "app/dev/wuwa-runtime.ps1") + "\" -Mode simulator";
        Check(LauncherProcessRecovery.Classify(altered, roots) && altered.Kind == "Runtime/profile worker", "runtime worker visible but protected");
        var helper = new RecoveryCandidate { Executable = Path.Combine(root, "python/pythonw.exe"), CommandLine = "pythonw.exe -I -B -X utf8 \"" + Path.Combine(root, "app/dev/wuwa_player.py") + "\" --no-open" };
        Check(LauncherProcessRecovery.Classify(helper, roots) && helper.Kind == "Launcher helper", "exact bundled helper identified");
        helper.HelperUnresponsive = true;
        LauncherProcessRecovery.AssertForceStopSafe(helper);
        helper.HelperUnresponsive = false;
        Refuse(() => LauncherProcessRecovery.AssertForceStopSafe(helper), "helper becoming responsive again cannot bypass its normal voice/recording stop checks");
        helper.CommandLine = "pythonw.exe -cignored \"" + Path.Combine(root, "app/dev/wuwa_player.py") + "\"";
        Check(!LauncherProcessRecovery.Classify(helper, roots), "inline-python lookalike rejected");
        // A prior portable copy outside manager records is trusted only for the
        // exact saved owner, and only when its script bytes match verified code.
        // Production additionally pins actual public Oct4/Oct6 ZIP digests; those
        // were independently checked against the immutable ZIP manifests.
        string legacyRoot = Path.Combine(root, "legacy-copy");
        string legacyScript = Path.Combine(legacyRoot, "app/dev/start-wuwa-build.ps1");
        Directory.CreateDirectory(Path.GetDirectoryName(legacyScript)); File.Copy(script, legacyScript);
        var legacyFiles = new Dictionary<string, string> { { "app/dev/start-wuwa-build.ps1", RepoClient.Hash(legacyScript) } };
        string legacyId = "wuwa-vr-launcher-candidate-legacy-fixture";
        Json.Save(Path.Combine(legacyRoot, "manifest.json"), new { packageId = legacyId, files = legacyFiles });
        Json.Save(Path.Combine(legacyRoot, "app/portable.json"), new { packageId = legacyId });
        var legacy = Copy(row); legacy.CommandLine = row.CommandLine.Replace(script, legacyScript);
        var recorded = new RecoveryCandidate { Pid = row.Pid, StartedUtc = row.StartedUtc };
        Check(!LauncherProcessRecovery.Classify(Copy(legacy), roots) && LauncherProcessRecovery.ClassifyRecordedOwner(legacy, recorded, row.UserSid, row.SessionId, roots), "exact recorded owner can recover an unregistered copy of verified packaged startup code");
        var wrongOwner = Copy(recorded); wrongOwner.StartedUtc = DateTime.UtcNow.AddDays(-1).ToString("o");
        Check(!LauncherProcessRecovery.ClassifyRecordedOwner(Copy(legacy), wrongOwner, row.UserSid, row.SessionId, roots), "legacy package discovery refuses recycled PID creation time");
        Check(!LauncherProcessRecovery.ClassifyRecordedOwner(Copy(legacy), recorded, "S-1-0-0", row.SessionId, roots) && !LauncherProcessRecovery.ClassifyRecordedOwner(Copy(legacy), recorded, row.UserSid, row.SessionId + 1, roots), "legacy package discovery refuses another SID or session");
        var other = Copy(legacy); other.Pid++;
        Check(roots.Count == 1 && !LauncherProcessRecovery.ClassifyRecordedOwner(other, recorded, row.UserSid, row.SessionId, roots), "discovered legacy root never becomes an arbitrary process allowlist");
        File.AppendAllText(legacyScript, " different untrusted script");
        legacyFiles["app/dev/start-wuwa-build.ps1"] = RepoClient.Hash(legacyScript);
        Json.Save(Path.Combine(legacyRoot, "manifest.json"), new { packageId = legacyId, files = legacyFiles });
        Check(!LauncherProcessRecovery.ClassifyRecordedOwner(Copy(legacy), recorded, row.UserSid, row.SessionId, roots), "self-consistent arbitrary manifest cannot authorize unknown startup code");
        var diagnosticOnly = LauncherProcessRecovery.UnverifiedRecordedOwner(Copy(legacy));
        Check(!diagnosticOnly.Eligible && diagnosticOnly.Pid == legacy.Pid && diagnosticOnly.Script == Path.GetFullPath(legacyScript) && diagnosticOnly.Kind == "Recorded launcher owner", "unverifiable recorded owner stays visible with exact path but cannot be stopped");
        string legacyData = Path.Combine(root, "legacy-data"), marker = Path.Combine(legacyData, "runs/fixture/cancel.request");
        Directory.CreateDirectory(Path.GetDirectoryName(marker));
        Json.Save(Path.Combine(legacyData, "launch-state.json"), new { pid = row.Pid, started = row.StartedUtc, cancelPath = marker });
        LauncherProcessRecovery.TryCancelOwnedLaunch(legacyData, row, () => false);
        Check(File.Exists(marker), "matching legacy receipt receives graceful cancellation before force recovery");
        File.Delete(marker);
        var reused = Copy(row); reused.StartedUtc = DateTime.UtcNow.AddDays(-1).ToString("o");
        LauncherProcessRecovery.TryCancelOwnedLaunch(legacyData, reused, () => false);
        Check(!File.Exists(marker), "legacy cancellation refuses reused PID identity");
        File.AppendAllText(script, "modified");
        Check(!LauncherProcessRecovery.Classify(Copy(row), roots), "changed package script hash rejected");
        Check(LauncherProcessRecovery.ActivityBlocker(row, new List<LauncherProcessRecovery.ProcessEntry> { new LauncherProcessRecovery.ProcessEntry { Pid = 12, Parent = 2, Name = "Client-Win64-Shipping.exe" } }) != null, "running game blocks recovery without targeting it");
        Check(LauncherProcessRecovery.ActivityBlocker(row, new List<LauncherProcessRecovery.ProcessEntry> { new LauncherProcessRecovery.ProcessEntry { Pid = 12, Parent = row.Pid, Name = "unknown-recorder.exe" } }) != null, "unknown active child blocks parent stop");
        Check(LauncherProcessRecovery.ActivityBlocker(row, new List<LauncherProcessRecovery.ProcessEntry>()) == null, "verified childless startup can recover");
        Refuse(() => LauncherProcessRecovery.StopAsync(new RecoveryReport { Candidates = new List<RecoveryCandidate> { row } }, new[] { row.Pid }, CancellationToken.None), "fabricated report cannot request elevation/stop");
        bool actionSent = false, cancellationObserved = false;
        try { LauncherProcessRecovery.ThrowIfCancelled(() => true); actionSent = true; } catch (OperationCanceledException) { cancellationObserved = true; }
        Check(cancellationObserved && !actionSent, "last-moment cancellation refuses dispatch");
        Check(LauncherProcessRecovery.SplitArguments("pythonw.exe -I \"C:\\folder with spaces\\wuwa_player.py\"")[2] == @"C:\folder with spaces\wuwa_player.py", "Windows quoted token parsing preserves exact script path");
        var self = LauncherProcessRecovery.InspectProcess(Process.GetCurrentProcess().Id);
        Check(self.Pid == Process.GetCurrentProcess().Id && !String.IsNullOrWhiteSpace(self.CommandLine) && !String.IsNullOrWhiteSpace(self.UserSid), "real read-only handle identity and command-line query work");
        // Only this newly spawned inert child is ever terminated. No UAC, game,
        // installed helper, real user state or production runtime is touched.
        string wait = Path.Combine(root, "own-wait-fixture.ps1"); File.WriteAllText(wait, "Start-Sleep -Seconds 30");
        var info = new ProcessStartInfo(ps, "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"" + wait + "\"") { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden };
        using (var child = Process.Start(info))
        {
            try
            {
                var actual = LauncherProcessRecovery.InspectProcess(child.Id);
                var missing = LauncherProcessRecovery.MissingCandidateResult(Copy(actual), new List<LauncherProcessRecovery.ProcessEntry> { new LauncherProcessRecovery.ProcessEntry { Pid = child.Id, Name = "powershell.exe" } });
                Check(missing.Outcome == "Refused" && !child.HasExited, "live unclassifiable PID is not falsely reported exited");
                using (var handle = LauncherProcessRecovery.Open(child.Id, true))
                {
                    var wrong = Copy(actual); wrong.StartedUtc = DateTime.UtcNow.AddDays(-1).ToString("o");
                    Refuse(() => LauncherProcessRecovery.TerminateVerified(handle, wrong), "reused-PID/creation-time mismatch refuses termination");
                    Check(!child.HasExited, "identity refusal leaves own inert child alive");
                    wrong = Copy(actual); wrong.CommandLine += " changed";
                    Refuse(() => LauncherProcessRecovery.TerminateVerified(handle, wrong), "changed command line refuses termination on same handle");
                    wrong = Copy(actual); wrong.UserSid = "S-1-0-0";
                    Refuse(() => LauncherProcessRecovery.TerminateVerified(handle, wrong), "different owner refuses termination");
                    LauncherProcessRecovery.TerminateVerified(handle, actual);
                    Check(child.WaitForExit(3000), "production final handle check stops only own selected inert child");
                    missing = LauncherProcessRecovery.MissingCandidateResult(Copy(actual), new List<LauncherProcessRecovery.ProcessEntry>());
                    Check(missing.Outcome == "Exited", "absent completed process receives the exited result");
                }
            }
            finally { if (!child.HasExited) { child.Kill(); child.WaitForExit(3000); } }
        }
        Console.WriteLine("PASS recovery checks: " + count + "; fixtures: " + root);
    }
    // Optional read-only artifact check run against an extracted *public* package,
    // not a process. Exercises historical pins independently of installed roots.
    public static void CheckShippedLegacyPackage(string packageRoot)
    {
        string script = Path.Combine(packageRoot, "app/dev/start-wuwa-build.ps1");
        string ps = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "System32/WindowsPowerShell/v1.0/powershell.exe");
        var row = new RecoveryCandidate { Pid = 12345, StartedUtc = "2026-10-04T14:54:54.0000000Z", Executable = ps, UserSid = "S-1-5-21-1", SessionId = 1,
            CommandLine = "\"" + ps + "\" -NoProfile -ExecutionPolicy Bypass -File \"" + script + "\" -Id lightfix2-20261001 -Elevated -NoDialog" };
        if (!LauncherProcessRecovery.ClassifyRecordedOwner(row, Copy(row), row.UserSid, row.SessionId, new List<LauncherProcessRecovery.Root>()))
            throw new Exception("Exact shipped legacy package failed its historical code pin: " + packageRoot);
        Console.WriteLine("PASS public legacy package pin: " + packageRoot);
    }
}
