# Steam identity and desktop-shell dispatch; no work is done when dot-sourced.
Set-StrictMode -Version 2

function Get-WuWaSteamVdfValue {
    param([string]$Text, [string]$Name)
    if ($Text.Length -gt 1048576) { return $null }
    $matches = [regex]::Matches($Text, '"' + [regex]::Escape($Name) + '"\s*"([^"\r\n]*)"', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($matches.Count -ne 1) { return $null }
    return $matches[0].Groups[1].Value.Replace('\\','\')
}

function Get-WuWaSteamGame {
    param([Parameter(Mandatory)][string]$Bootstrap)
    if ($Bootstrap -notmatch '^[A-Za-z]:[\\/]' -or $Bootstrap -match '["\r\n]') { throw 'Choose the full local Steam Wuthering Waves.exe path.' }
    $path = [IO.Path]::GetFullPath($Bootstrap)
    $install = Split-Path -Parent $path
    $common = Split-Path -Parent $install
    $apps = Split-Path -Parent $common
    $shipping = Join-Path $install 'Client\Binaries\Win64\Client-Win64-Shipping.exe'
    $manifest = Join-Path $apps 'appmanifest_3513350.acf'
    if ([IO.Path]::GetFileName($path) -ine 'Wuthering Waves.exe' -or
        (Split-Path -Leaf $common) -ine 'common' -or (Split-Path -Leaf $apps) -ine 'steamapps' -or
        -not (Test-Path -LiteralPath $path -PathType Leaf) -or -not (Test-Path -LiteralPath $shipping -PathType Leaf) -or
        -not (Test-Path -LiteralPath $manifest -PathType Leaf) -or (Get-Item -LiteralPath $manifest).Length -gt 1048576) {
        throw 'The selected Steam game files or app 3513350 manifest are missing. Choose the installation again.'
    }
    $text = [IO.File]::ReadAllText($manifest)
    if ((Get-WuWaSteamVdfValue $text 'appid') -cne '3513350' -or
        (Get-WuWaSteamVdfValue $text 'installdir') -ine (Split-Path -Leaf $install)) { throw 'The Steam manifest does not match the selected Wuthering Waves installation.' }
    return [pscustomobject]@{ Bootstrap=$path; Shipping=$shipping; AppId='3513350'; Uri='steam://rungameid/3513350' }
}

function Get-WuWaSteamClient {
    $candidates = @()
    try { $candidates += [string](Get-ItemPropertyValue -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamExe -ErrorAction Stop) } catch { }
    try { $candidates += Join-Path ([string](Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -Name InstallPath -ErrorAction Stop)) 'steam.exe' } catch { }
    if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe' }
    foreach ($path in $candidates) {
        if ($path -match '^[A-Za-z]:[\\/]' -and [IO.Path]::GetFileName($path) -ieq 'steam.exe' -and (Test-Path -LiteralPath $path -PathType Leaf)) { return [IO.Path]::GetFullPath($path) }
    }
    throw 'Steam is not installed for this Windows user. Install/sign in to Steam, or choose manual start.'
}

function Assert-WuWaSteamInjector {
    param([Parameter(Mandatory)][string]$Injector)
    # Old injectors ignore unknown arguments and open their GUI. Never execute
    # one to probe capabilities; this marker is inside the package-hashed EXE.
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Injector)
    if ($info.Comments -cne 'WUWA_INJECTOR_TARGET_PATH_V1') { throw 'This package needs the updated Steam-aware injector. Update WuWa VR before using Steam launch.' }
}

function Get-WuWaNormalDesktopShell {
    # Reuse the existing interactive Explorer shell. Starting Steam from this
    # elevated worker directly could elevate it; never fall back to that route.
    if (-not ('WuWa.SteamShellToken' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
namespace WuWa {
    public static class SteamShellToken {
        [DllImport("user32.dll")] static extern IntPtr GetShellWindow();
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
        [DllImport("advapi32.dll")] static extern bool OpenProcessToken(IntPtr process, uint access, out IntPtr token);
        [DllImport("advapi32.dll")] static extern bool GetTokenInformation(IntPtr token, int info, out int value, int length, out int needed);
        public static long NormalDesktopWindow() {
            IntPtr hwnd = GetShellWindow(); uint pid;
            if (hwnd == IntPtr.Zero || GetWindowThreadProcessId(hwnd, out pid) == 0) throw new InvalidOperationException("No interactive desktop shell.");
            using (var process = Process.GetProcessById((int)pid)) {
                string expected = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe");
                if (!String.Equals(process.MainModule.FileName, expected, StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("The desktop shell is not Windows Explorer.");
            }
            IntPtr handle = OpenProcess(0x1000, false, pid), token = IntPtr.Zero;
            try {
                int elevated, needed;
                if (handle == IntPtr.Zero || !OpenProcessToken(handle, 8, out token) || !GetTokenInformation(token, 20, out elevated, 4, out needed) || elevated != 0)
                    throw new InvalidOperationException("The desktop shell is elevated or its token cannot be verified.");
            } finally { if (token != IntPtr.Zero) CloseHandle(token); if (handle != IntPtr.Zero) CloseHandle(handle); }
            return hwnd.ToInt64();
        }
    }
}
'@
    }
    try {
        $expectedWindow = [WuWa.SteamShellToken]::NormalDesktopWindow()
        $shell = New-Object -ComObject Shell.Application
        $windows = $shell.Windows()
        $desktopWindow = 0
        $desktop = $windows.FindWindowSW(0, 0, 8, [ref]$desktopWindow, 1)
        if (-not $desktop -or (([long]$desktopWindow -band 0xffffffffL) -ne ($expectedWindow -band 0xffffffffL))) { throw 'Desktop shell identity differs.' }
        # Check again after COM resolution; do not assume every Explorer runs
        # unelevated just because its name matches.
        if ([WuWa.SteamShellToken]::NormalDesktopWindow() -ne $expectedWindow) { throw 'Desktop shell changed.' }
        return $desktop.Document.Application
    } catch { throw ('Steam must be launched through your normal Windows desktop. Close any administrator-run Steam/Explorer and try again, or use manual start. ' + $_.Exception.Message) }
}

function Start-WuWaSteamGame {
    param([Parameter(Mandatory)]$Game)
    $verified = Get-WuWaSteamGame $Game.Bootstrap
    $null = Get-WuWaSteamClient
    $desktop = Get-WuWaNormalDesktopShell
    # Fixed app ID only: no executable path, arguments or arbitrary URI from settings.
    $desktop.ShellExecute($verified.Uri, '', '', 'open', 1)
}

function Initialize-WuWaSteamProcessIdentity {
    if ('WuWa.SteamProcessIdentityV1' -as [type]) { return }
    # Get-Process.Path reads MainModule and can require process/module memory
    # access. QueryFullProcessImageName needs only limited query rights. Keep a
    # verified identity only while its original synchronize handle proves that
    # exact process is still alive; never cache a path by PID alone.
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace WuWa {
    public sealed class SteamProcessIdentityResult {
        public int Id;
        public string Path = "";
        public DateTime StartTime;
        public long CreationFileTime;
        public bool Verified;
        public string Source = "unavailable";
        public string FailedStep = "";
        public int Win32Error;
    }
    public static class SteamProcessIdentityV1 {
        const uint QueryLimited = 0x1000, Synchronize = 0x100000, WaitTimeout = 258;
        sealed class Entry {
            public SafeProcessHandle Handle;
            public SteamProcessIdentityResult Identity;
        }
        static readonly Dictionary<int, Entry> Entries = new Dictionary<int, Entry>();
        static readonly object Gate = new object();
        [DllImport("kernel32.dll", SetLastError=true)] static extern SafeProcessHandle OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool QueryFullProcessImageName(SafeProcessHandle handle, uint flags, StringBuilder path, ref int length);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetProcessTimes(SafeProcessHandle handle, out long created, out long exited, out long kernel, out long user);
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(SafeProcessHandle handle, out uint code);
        [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(SafeProcessHandle handle, uint milliseconds);
        static SteamProcessIdentityResult Failure(int pid, string step, int error) {
            return new SteamProcessIdentityResult {Id=pid, FailedStep=step, Win32Error=error};
        }
        static SteamProcessIdentityResult Copy(SteamProcessIdentityResult value, string source) {
            return new SteamProcessIdentityResult {Id=value.Id, Path=value.Path, StartTime=value.StartTime,
                CreationFileTime=value.CreationFileTime, Verified=value.Verified, Source=source};
        }
        public static SteamProcessIdentityResult Read(int pid) {
            lock(Gate) {
                Entry retained;
                if(Entries.TryGetValue(pid, out retained)) {
                    if(!retained.Handle.IsClosed && !retained.Handle.IsInvalid && WaitForSingleObject(retained.Handle, 0) == WaitTimeout)
                        return Copy(retained.Identity, "verified-held-handle");
                    retained.Handle.Dispose(); Entries.Remove(pid);
                }
                bool canRetain = true;
                SafeProcessHandle handle = OpenProcess(QueryLimited | Synchronize, false, pid);
                if(handle.IsInvalid) {
                    handle.Dispose(); canRetain = false;
                    handle = OpenProcess(QueryLimited, false, pid);
                }
                try {
                    if(handle.IsInvalid) return Failure(pid, "OpenProcess(limited query)", Marshal.GetLastWin32Error());
                    long created, exited, kernel, user;
                    if(!GetProcessTimes(handle, out created, out exited, out kernel, out user))
                        return Failure(pid, "GetProcessTimes", Marshal.GetLastWin32Error());
                    int length = 32768; var path = new StringBuilder(length);
                    if(!QueryFullProcessImageName(handle, 0, path, ref length))
                        return Failure(pid, "QueryFullProcessImageName", Marshal.GetLastWin32Error());
                    if(canRetain) {
                        uint wait = WaitForSingleObject(handle, 0);
                        if(wait != WaitTimeout) return Failure(pid, "process exited or wait failed", wait == 0 ? 0 : Marshal.GetLastWin32Error());
                    } else {
                        uint exitCode;
                        if(!GetExitCodeProcess(handle, out exitCode)) return Failure(pid, "GetExitCodeProcess", Marshal.GetLastWin32Error());
                        if(exitCode != 259) return Failure(pid, "process exited", 0);
                    }
                    var result = new SteamProcessIdentityResult {Id=pid, Path=path.ToString(), CreationFileTime=created,
                        StartTime=DateTime.FromFileTimeUtc(created).ToLocalTime(), Verified=true, Source="limited-query"};
                    // At most32 handles per startup worker. Uncached queries remain
                    // useful when synchronize rights are unavailable or full.
                    if(canRetain && Entries.Count < 32) {
                        Entries.Add(pid, new Entry {Handle=handle, Identity=result}); handle = null;
                    }
                    return Copy(result, result.Source);
                } finally { if(handle != null) handle.Dispose(); }
            }
        }
        public static void Clear() {
            lock(Gate) { foreach(var value in Entries.Values) value.Handle.Dispose(); Entries.Clear(); }
        }
        public static int RetainedCount { get { lock(Gate) { return Entries.Count; } } }
    }
}
'@
}

function Get-WuWaSteamProcessIdentity {
    param([Parameter(Mandatory)]$Process)
    Initialize-WuWaSteamProcessIdentity
    return [WuWa.SteamProcessIdentityV1]::Read([int]$Process.Id)
}

function Get-WuWaSteamProcesses {
    param([Parameter(Mandatory)][string]$ExpectedPath)
    foreach ($process in @(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($ExpectedPath)) -ErrorAction SilentlyContinue)) {
        $identity = Get-WuWaSteamProcessIdentity -Process $process
        if ($identity.Verified -and $identity.Path -and [string]::Equals([IO.Path]::GetFullPath($identity.Path), $ExpectedPath, [StringComparison]::OrdinalIgnoreCase)) {
            # Use the same handle-derived creation time for continuity checks;
            # do not reopen MainModule or StartTime with stronger permissions.
            [pscustomobject]@{Id=$identity.Id;Path=$identity.Path;StartTime=$identity.StartTime;
                ProcessName=[IO.Path]::GetFileNameWithoutExtension($identity.Path)}
        }
    }
}

function Get-WuWaSteamProcessSnapshot {
    param([Parameter(Mandatory)]$Game, [AllowNull()]$Injector)
    $snapshot = Get-LaunchProcessSnapshot -TargetName 'Client-Win64-Shipping' -Injector $Injector
    $candidates = @(Get-Process -Name 'Client-Win64-Shipping' -ErrorAction SilentlyContinue)
    $target = @(); $unverified = 0; $evidence = @()
    foreach ($process in $candidates) {
        $identity = Get-WuWaSteamProcessIdentity -Process $process
        $path = if($identity.Verified) { $identity.Path } else { $null }
        $matches = $false
        if ($path) {
            try { $matches = [string]::Equals([IO.Path]::GetFullPath($path), $Game.Shipping, [StringComparison]::OrdinalIgnoreCase) } catch { $path = $null }
        }
        if (-not $path) { $unverified++ }
        if ($matches) { $target += $process }
        if ($evidence.Count -lt 8) {
            $processId = $null
            if ($process.PSObject.Properties['Id']) { $processId = $process.Id }
            # Sharing diagnostics does not require disclosing executable paths.
            $evidence += [pscustomobject]@{pid=$processId;pathReadable=[bool]$path;matchesSelected=$matches;
                identitySource=$identity.Source;creationFileTime=$identity.CreationFileTime;
                win32Error=$identity.Win32Error;failedStep=$identity.FailedStep}
        }
    }
    $bootstrap = @(Get-WuWaSteamProcesses $Game.Bootstrap)
    $snapshot.GameRunning = $target.Count -gt 0 -or $bootstrap.Count -gt 0
    $snapshot.TargetRunning = $target.Count -eq 1
    $snapshot.SteamTargetCount = $target.Count
    $snapshot.SteamTargetCandidateCount = $candidates.Count
    $snapshot.SteamTargetUnverifiedCount = $unverified
    $snapshot.SteamTargetProcesses = $evidence
    $snapshot.LauncherRunning = $false # Steam is not a game-specific Play window.
    return $snapshot
}
