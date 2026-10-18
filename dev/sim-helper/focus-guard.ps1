# WuWa VR dev helper: the focus guard that helper.ps1 starts (elevated, hidden) with the game.
# The game runs as administrator and takes the foreground while it loads, which pulls the owner out
# of whatever they were using. Windows' foreground lock lets only an elevated process take it back
# from an administrator's window, so this lives with the helper. For a few minutes, whenever a game
# window (anything under the game folder) becomes the foreground without the owner clicking it, the
# window they were using gets it back; the game keeps loading behind it. It switches windows only:
# no cursor movement, no input. It stops when the owner clicks or Alt-Tabs into the game, when the
# game exits, or after the time limit.
param(
    [Parameter(Mandatory=$true)][ValidateSet('kuro', 'steam')][string]$Copy,
    [ValidateRange(10, 600)][int]$Seconds = 240
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$config = [IO.File]::ReadAllText((Join-Path $root 'config.json')) | ConvertFrom-Json
$gameFolder = [IO.Path]::GetFullPath((Split-Path -Parent ([string]$config.$Copy))).TrimEnd('\') + '\'

function Write-GuardLog([string]$text) {
    Add-Content -LiteralPath (Join-Path $root 'helper.log') -Value ((Get-Date).ToString('s') + ' focus guard: ' + $text) -Encoding UTF8
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WuWaFocusGuard {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    public static uint ProcessOf(IntPtr window) { uint pid; GetWindowThreadProcessId(window, out pid); return pid; }
    // A mouse button or Alt held as the game came forward: the owner chose it.
    public static bool OwnerChose() {
        return (GetAsyncKeyState(0x01) & 0x8000) != 0 || (GetAsyncKeyState(0x02) & 0x8000) != 0 || (GetAsyncKeyState(0x12) & 0x8000) != 0;
    }
    // Sharing the foreground thread's input state lifts the foreground lock for this call only.
    public static bool GiveBack(IntPtr target) {
        IntPtr foreground = GetForegroundWindow(); uint pid;
        uint thread = GetWindowThreadProcessId(foreground, out pid), self = GetCurrentThreadId();
        bool attached = thread != 0 && thread != self && AttachThreadInput(self, thread, true);
        try { SetForegroundWindow(target); }
        finally { if (attached) AttachThreadInput(self, thread, false); }
        return GetForegroundWindow() == target;
    }
}
'@

$gamePids = @{}
function Test-GameWindow([IntPtr]$window) {
    if ($window -eq [IntPtr]::Zero) { return $false }
    $id = [WuWaFocusGuard]::ProcessOf($window)
    if (-not $gamePids.ContainsKey($id)) {
        $path = try { (Get-Process -Id $id -ErrorAction Stop).Path } catch { $null }
        $gamePids[$id] = [bool]($path -and [IO.Path]::GetFullPath($path).StartsWith($gameFolder, [StringComparison]::OrdinalIgnoreCase))
    }
    return $gamePids[$id]
}
function Get-WindowOwner([IntPtr]$window) {
    try { (Get-Process -Id ([WuWaFocusGuard]::ProcessOf($window)) -ErrorAction Stop).ProcessName } catch { 'unknown' }
}

$started = Get-Date
$checked = $started
$deadline = $started.AddSeconds($Seconds)
$owner = [WuWaFocusGuard]::GetForegroundWindow()
if (Test-GameWindow $owner) { $owner = [IntPtr]::Zero }
$seen = $owner
$given = 0
Write-GuardLog ("on for $Seconds s ($Copy); the owner's window: " + $(if ($owner -ne [IntPtr]::Zero) { Get-WindowOwner $owner } else { 'none yet' }))
try {
    while ((Get-Date) -lt $deadline) {
        $now = [WuWaFocusGuard]::GetForegroundWindow()
        if ($now -ne $seen) {
            if (-not (Test-GameWindow $now)) {
                if ($now -ne [IntPtr]::Zero) { $owner = $now }
            } elseif ([WuWaFocusGuard]::OwnerChose()) {
                Write-GuardLog 'the owner went to the game; off'
                break
            } elseif ($owner -ne [IntPtr]::Zero -and [WuWaFocusGuard]::IsWindow($owner) -and [WuWaFocusGuard]::IsWindowVisible($owner)) {
                $back = [WuWaFocusGuard]::GiveBack($owner)
                $given++
                Write-GuardLog ('the game took the foreground; ' + $(if ($back) { 'gave it back to ' + (Get-WindowOwner $owner) } else { 'could not give it back' }))
            }
            $seen = [WuWaFocusGuard]::GetForegroundWindow()
        }
        # Once the game has had time to start, its exit ends the guard (checked every 2 s).
        if (((Get-Date) - $started).TotalSeconds -gt 30 -and ((Get-Date) - $checked).TotalSeconds -ge 2) {
            $checked = Get-Date
            if (-not (Get-Process -Name 'Client-Win64-Shipping', 'Wuthering Waves' -ErrorAction SilentlyContinue)) {
                Write-GuardLog 'the game is not running; off'
                break
            }
        }
        Start-Sleep -Milliseconds 50
    }
} catch {
    Write-GuardLog ('stopped: ' + $_.Exception.Message)
}
Write-GuardLog "done; gave the foreground back $given time(s)"
