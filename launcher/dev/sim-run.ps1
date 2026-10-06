<#
.SYNOPSIS
    Run Wuthering Waves + UEVR against the OpenXR Simulator, with no headset.

.DESCRIPTION
    Uses the machine-wide OpenXR runtime because the elevated game ignores
    XR_RUNTIME_JSON. Activate and Deactivate wrap the simulator's scripts to
    save and restore the previous runtime; Probe is read-only.

    The script starts the UEVR injector (which auto-waits for the game), then
    reads only the slice of the profile log produced by this run and asserts on
    it. It never truncates log.txt.

.PARAMETER Probe
    Read-only environment report. Changes nothing. Run this first.

.PARAMETER ObserveOnly
    Use exactly one manually started injector; do not start another. Run this
    before launching the game so the existing log cannot count as this run.

.PARAMETER Stop
    Graceful teardown of the game and injector.

.PARAMETER StatePath
    Optional startup state file (rendering-test-state.json) to keep current.

.PARAMETER CancelPath
    Stop waiting as soon as this file exists. The game is never closed.

.PARAMETER InjectorPath
    Injector executable to start. Default: the one in -RuntimeName. The loaded
    backend is still chosen by custom_var_urvr_folder, not by this path.

.PARAMETER LauncherPath
    Game launcher for -StartLauncher; its folder bounds launcher detection.

.PARAMETER PromptOnLauncherClosed
    Visible console only: offer to reopen a launcher closed before Play, and
    let Q stop the wait.

.EXAMPLE
    .\dev\sim-run.ps1 -Probe
    .\dev\sim-run.ps1 -StartLauncher
    .\dev\sim-run.ps1 -Stop
#>
[CmdletBinding()]
param(
    [string]$SimulatorPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dev-tools\OpenXR-Simulator'),
    [string]$RuntimeName   = 'Wuthering Waves UEVR - native UI diagnostics',
    [switch]$StartLauncher,
    [switch]$ObserveOnly,
    [switch]$Probe,
    [switch]$Stop,
    [switch]$Activate,
    [switch]$Deactivate,
    [switch]$Headset,
    [switch]$Force,
    [int]$WaitSeconds = 240,
    [int]$SettleSeconds = 30,
    [int]$LauncherGraceSeconds = 15,
    [string]$InjectorPath,
    [string]$LauncherPath = 'C:\Program Files\Wuthering Waves\launcher.exe',
    [ValidateSet('launcher','manual','steam')][string]$GameStart = 'launcher',
    [string]$StatePath,
    [string]$CancelPath,
    [string]$DiagnosticDirectory,
    [switch]$PromptOnLauncherClosed
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Get-SceneCaptureCheck.ps1')
. (Join-Path $PSScriptRoot 'Get-RunContinuityCheck.ps1')
. (Join-Path $PSScriptRoot 'WuWaLaunchLifecycle.ps1')
. (Join-Path $PSScriptRoot 'WuWaSteamStart.ps1')

# Activate / deactivate the simulator machine-wide. Both scripts self-elevate
# and prompt for UAC. activate stashes the outgoing runtime in
# HKLM\SOFTWARE\Khronos\OpenXR\1\PreviousActiveRuntime; deactivate restores it.
if ($Activate -or $Deactivate) {
    $verb = 'activate'
    if ($Deactivate) { $verb = 'deactivate' }
    $vendorScript = Join-Path $SimulatorPath ($verb + '_simulator.ps1')
    if (-not (Test-Path -LiteralPath $vendorScript)) { throw "Not found: $vendorScript" }
    Write-Output "Running ${verb}_simulator.ps1 (it will ask for administrator rights)..."
    & $vendorScript
    Write-Output ''
    Write-Output 'Current machine-wide runtime:'
    Write-Output ('  ' + [string](Get-ItemPropertyValue -Path 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime -ErrorAction SilentlyContinue))
    return
}

$repoRoot     = Split-Path -Parent $PSScriptRoot
$injectorExe  = Join-Path $repoRoot ('runtime\' + $RuntimeName + '\Custom_UEVR_Injector.exe')
if ($InjectorPath) { $injectorExe = $InjectorPath }
$launcherExe  = $LauncherPath
$gameProcName = 'Client-Win64-Shipping'
$injProcName  = 'Custom_UEVR_Injector'
# launcher.exe starts launcher_main.exe, which stays open (minimised) while the
# game runs and starts Wuthering Waves.exe, the parent of the shipping client.
$launcherRoot  = Split-Path -Parent $launcherExe
$launcherNames = @('launcher', 'launcher_main', 'launcher_updater')
$gameNames     = @($gameProcName, 'Wuthering Waves')
$profileDir   = Join-Path $env:APPDATA 'UnrealVRMod\Client-Win64-Shipping'
$profileLog   = Join-Path $profileDir 'log.txt'
$profileCfg   = Join-Path $profileDir 'config.txt'
$injectorCfg  = Join-Path $profileDir 'injector_config.txt'
$simJson      = Join-Path $SimulatorPath 'openxr_simulator.json'
$steamGame = $null
if ($GameStart -eq 'steam' -and -not $Stop) {
    $steamGame = Get-WuWaSteamGame -Bootstrap $launcherExe
    if (-not $Probe) {
        if ($ObserveOnly) { throw 'Steam launch requires its own path-bound injector; it cannot adopt a waiting injector.' }
        if (-not $StartLauncher) { throw 'Use -StartLauncher with the selected Steam installation, or choose manual start.' }
        Assert-WuWaSteamInjector -Injector $injectorExe
        $null = Get-WuWaSteamClient
    }
}
function Get-SelectedGameProcesses {
    if ($steamGame) { Get-WuWaSteamProcesses $steamGame.Shipping }
    else { Get-Process -Name $gameProcName -ErrorAction SilentlyContinue }
}

# Which backend actually gets injected is decided by custom_var_urvr_folder in
# the AppData injector config - NOT by which copy of Custom_UEVR_Injector.exe
# you launch. The two runtime folders ship byte-identical injectors.
function Get-ConfiguredRuntimeFolder {
    if (-not (Test-Path -LiteralPath $injectorCfg)) { return $null }
    $line = Get-Content -LiteralPath $injectorCfg -Encoding UTF8 |
        Where-Object { $_ -like 'custom_var_urvr_folder=*' } |
        Select-Object -First 1
    if (-not $line) { return $null }
    return $line.Substring('custom_var_urvr_folder='.Length).Trim()
}

function Get-ActiveRuntime {
    try {
        return Get-ItemPropertyValue -Path 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime -ErrorAction Stop
    } catch {
        return $null
    }
}

function Get-ImplicitLayers {
    $found = @()
    foreach ($hive in @('HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit',
                        'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit')) {
        if (-not (Test-Path $hive)) { continue }
        $props = Get-ItemProperty -Path $hive
        foreach ($p in $props.PSObject.Properties) {
            if ($p.Name -like 'PS*') { continue }
            # OpenXR loader convention: value 0 = ENABLED, non-zero = disabled.
            $state = 'disabled'
            if ($p.Value -eq 0) { $state = 'ENABLED' }
            $found += [pscustomobject]@{
                Hive  = $hive.Substring(0, 4)
                State = $state
                Layer = $p.Name
            }
        }
    }
    return $found
}

function Write-Report($label, $value) {
    Write-Output ('  {0,-28} {1}' -f $label, $value)
}

# XR_RUNTIME_JSON CANNOT be used with this game, at any scope. Verified
# 2026-09-06: Client-Win64-Shipping.exe and Wuthering Waves.exe both declare
# requestedExecutionLevel level="requireAdministrator", and the Khronos OpenXR
# loader ignores every environment variable in a high-integrity process as an
# anti-hijacking measure. Both process scope and user scope were tried and the
# game silently used the machine-wide runtime (SteamVR) both times.
# The machine-wide HKLM key is the only route that survives elevation.
function Get-UserRuntimePin {
    return [Environment]::GetEnvironmentVariable('XR_RUNTIME_JSON', 'User')
}

function Set-UserRuntimePin($value) {
    [Environment]::SetEnvironmentVariable('XR_RUNTIME_JSON', $value, 'User')
}

function Test-SimulatorActive($activeRuntime) {
    if (-not $activeRuntime) { return $false }
    if (-not (Test-Path -LiteralPath $simJson)) { return $false }
    return ($activeRuntime.TrimEnd('\') -eq (Resolve-Path -LiteralPath $simJson).Path.TrimEnd('\'))
}

# ---------------------------------------------------------------- teardown ---
if ($Stop) {
    foreach ($name in @($gameProcName, $injProcName)) {
        $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        if ($procs.Count -eq 0) {
            Write-Output "not running: $name"
            continue
        }
        foreach ($p in $procs) { $null = $p.CloseMainWindow() }
        Start-Sleep -Seconds 6

        $still = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
        if ($still.Count -eq 0) {
            Write-Output "closed: $name"
            continue
        }
        if ($name -eq $injProcName -and $Force) {
            Stop-Process -Name $name -Force -Confirm:$false
            Write-Output "force-stopped: $name"
            continue
        }
        Write-Warning "$name did not close on request. Not force-killing it: ACE is resident in the game process and an abrupt kill is not a risk worth taking. Close it from its own window."
    }

    if (Get-UserRuntimePin) {
        Set-UserRuntimePin $null
        Write-Output 'Cleared the user-scope XR_RUNTIME_JSON pin. Other VR apps go back to the machine-wide runtime.'
    }
    return
}

# ----------------------------------------------------------- report / probe ---
Write-Output ''
Write-Output 'Environment (read-only):'

$active = Get-ActiveRuntime
if ($active) { Write-Report 'HKLM ActiveRuntime' $active } else { Write-Report 'HKLM ActiveRuntime' '<unset>' }

if (Test-SimulatorActive $active) {
    Write-Report 'Runtime in effect' 'OpenXR Simulator (headset-free)'
} elseif ($active -like '*SteamVR*') {
    Write-Report 'Runtime in effect' 'SteamVR - run -Activate to switch to the simulator'
} else {
    Write-Report 'Runtime in effect' 'something else'
}

try {
    $prev = Get-ItemPropertyValue -Path 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name PreviousActiveRuntime -ErrorAction Stop
    Write-Report 'Restores to (on -Deactivate)' $prev
} catch { }

$userPin = Get-UserRuntimePin
if ($userPin) {
    Write-Report 'Stale user env pin' $userPin
    Write-Output ''
    Write-Warning 'A leftover user-scope XR_RUNTIME_JSON is set. It cannot affect this game (elevated processes ignore it) but it will affect non-elevated VR apps. Clear it with: .\dev\sim-run.ps1 -Stop'
}

if (Test-Path -LiteralPath $simJson) {
    Write-Report 'Simulator manifest' $simJson
} else {
    Write-Report 'Simulator manifest' "MISSING -> $simJson"
}

if (Test-Path -LiteralPath $injectorExe) {
    Write-Report 'Injector exe' "$RuntimeName"
} else {
    Write-Report 'Injector exe' "MISSING -> $injectorExe"
}

$configuredRuntime = Get-ConfiguredRuntimeFolder
if ($configuredRuntime) {
    Write-Report 'Backend that loads' (Split-Path -Leaf $configuredRuntime)
    $wantedFolder = Join-Path $repoRoot ('runtime\' + $RuntimeName)
    if ($configuredRuntime.TrimEnd('\') -ne $wantedFolder.TrimEnd('\')) {
        Write-Output ''
        Write-Warning "The injector will load its DLLs from '$configuredRuntime', NOT from -RuntimeName '$RuntimeName'. The runtime folder is chosen by custom_var_urvr_folder in $injectorCfg - the injector exe you launch does not decide it. Change it in the injector UI, or edit that file, before trusting a comparison between builds."
    }
} else {
    Write-Report 'Backend that loads' 'unknown (no injector_config.txt yet)'
}

if (Test-Path -LiteralPath $profileLog) {
    Write-Report 'Profile log' ('{0:N0} bytes' -f (Get-Item -LiteralPath $profileLog).Length)
} else {
    Write-Report 'Profile log' 'absent (first run)'
}

$on = @()
if (Test-Path -LiteralPath $profileCfg) {
    $cfg = Get-Content -LiteralPath $profileCfg
    foreach ($key in @('VR_RenderingMethod', 'VR_NativeStereoFix', 'VR_NativeStereoFixSwapEyes',
                       'VR_HorizontalProjectionOverride', 'VR_VerticalProjectionOverride',
                       'VR_DesktopRecordingFix_V2', 'VR_2DScreenMode')) {
        $line = $cfg | Where-Object { $_ -like ($key + '=*') } | Select-Object -First 1
        if ($line) { Write-Report $key (($line -split '=')[1]) }
    }

    # FFakeStereoRenderingHook.cpp:603 refuses to install the FSceneView
    # constructor hook unless at least one of these four is on. Without that hook
    # there are no [WuWaDiag] records and no native stereo pass rewrite, however
    # well the constructor scan itself succeeds.
    $gates = @('VR_GhostingFix', 'VR_Compatibility_SplitScreen', 'VR_Compatibility_SceneView', 'VR_NativeStereoFix')
    $on = @()
    foreach ($g in $gates) {
        $line = $cfg | Where-Object { $_ -like ($g + '=*') } | Select-Object -First 1
        if ($line -and (($line -split '=')[1]).Trim() -eq 'true') { $on += $g }
    }
    if ($on.Count -gt 0) {
        Write-Report 'FSceneView hook gate' ('open via ' + ($on -join ', '))
    } else {
        Write-Report 'FSceneView hook gate' 'off in the saved profile (optional)'
        Write-Output '  The optional scene-view diagnostics/pass rewrite are off. This does not disable'
        Write-Output '  native stereo or the separate LGUI HUD/menu hooks. Keep the chosen profile;'
        Write-Output '  Native Stereo Fix has caused SteamVR stutter in our headset comparison.'
    }
}

$layers = @(Get-ImplicitLayers)
if ($layers.Count -gt 0) {
    Write-Output ''
    Write-Output '  Implicit OpenXR API layers (these load against ANY runtime):'
    foreach ($l in $layers) {
        Write-Output ('    [{0,-8}] {1}  {2}' -f $l.State, $l.Hive, $l.Layer)
    }
    Write-Output '    An ENABLED layer is the first thing to disable if xrCreateInstance fails oddly.'
}

if ($Probe) {
    Write-Output ''
    Write-Output 'Probe only. Nothing was changed.'
    return
}

# --------------------------------------------------------------- preflight ---
if (-not $Headset -and -not (Test-Path -LiteralPath $simJson)) {
    throw "Simulator manifest not found: $simJson  (download the release and run build_simulator.ps1, or pass -SimulatorPath)"
}
if (-not (Test-Path -LiteralPath $injectorExe)) {
    throw "Injector not found: $injectorExe  (check -RuntimeName)"
}

$running = @(Get-Process -Name $gameProcName -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    throw "$gameProcName is already running. Stop it first (.\dev\sim-run.ps1 -Stop) so the runtime pin applies from process start."
}
$waitingInjectors = @(Get-Process -Name $injProcName -ErrorAction SilentlyContinue)
if ($ObserveOnly) {
    if ($waitingInjectors.Count -ne 1) {
        throw "-ObserveOnly requires exactly one waiting injector; found $($waitingInjectors.Count). Start it manually before running this command."
    }
} elseif ($waitingInjectors.Count -gt 0) {
    throw "An injector is already running. Use -ObserveOnly to watch its next game launch without starting another."
}

# ------------------------------------------------------------------ launch ---
if ($Headset) {
    if (Test-SimulatorActive $active) {
        throw "-Headset was passed but the OpenXR Simulator is still the machine-wide runtime. Run .\dev\sim-run.ps1 -Deactivate first, then start SteamVR."
    }
    Write-Output ''
    Write-Output "Headset mode. Machine-wide runtime: $active"
    Write-Output '  Start SteamVR and put the headset on before pressing Play.'
} elseif (-not (Test-SimulatorActive $active)) {
    throw @"
The simulator is not the machine-wide OpenXR runtime, and this game cannot be
pointed at one any other way.

  HKLM ActiveRuntime = $active

Wuthering Waves runs elevated (requestedExecutionLevel requireAdministrator),
and the Khronos OpenXR loader ignores XR_RUNTIME_JSON in high-integrity
processes. Environment variables cannot reach it at any scope.

Switch the machine-wide runtime, then run this again:

    .\dev\sim-run.ps1 -Activate

Put SteamVR back when you are done:

    .\dev\sim-run.ps1 -Deactivate
"@
}

if (-not $Headset) {
    Write-Output ''
    Write-Output 'Machine-wide OpenXR runtime is the simulator.'
}

# UEVR TRUNCATES log.txt on every launch - it is not append-only. Seeking to the
# pre-launch length would therefore skip the start of the NEW log, which is
# exactly where "UnrealVR entry", the swapchain creation lines and the whole
# FSceneView scan live. Fingerprint the first line instead: when it changes, the
# file was rewritten and the run's slice starts at 0.
function Get-LogHeader {
    if (-not (Test-Path -LiteralPath $profileLog)) { return '' }
    try {
        $fs = [System.IO.File]::Open($profileLog, 'Open', 'Read', 'ReadWrite')
        try {
            $buf = New-Object byte[] 160
            $n = $fs.Read($buf, 0, 160)
            # Do not include later lines in a short log's identity: appending
            # an error must not look like a different game session.
            return ([System.Text.Encoding]::ASCII.GetString($buf, 0, $n) -split "`n", 2)[0].TrimEnd("`r")
        } finally { $fs.Dispose() }
    } catch { return '' }
}

function Read-LogSlice($from) {
    return (Read-LaunchBackendLogWindow -Path $profileLog -From $from).Text
}

$logOffset = 0
if (Test-Path -LiteralPath $profileLog) { $logOffset = (Get-Item -LiteralPath $profileLog).Length }
$logHeaderBefore = Get-LogHeader

if ($StartLauncher -and -not (Test-Path -LiteralPath $launcherExe)) { throw "Launcher not found: $launcherExe" }

# Watch the processes as well as the log: a launcher or injector closed before
# Play ends the wait in seconds instead of leaving a silent ten-minute blocker.
$injectorProcess = $null
$watchedInjector = $null
$injectorHandled = $false
$progress = @{ Message = '' }
$backendEvidence = @{ Header = ''; ReadFrom = 0; Captured = $false; Error = ''; RendererInitialized = $false; ProjectionSeen = $false; TargetVerificationLost = $false; ShuttingDown = $false }
function Save-ObservedBackendLog {
    if (-not $DiagnosticDirectory -or -not $backendEvidence.Header) { return }
    try {
        # Preserve the prior snapshot if the profile log now belongs to a new
        # game process, disappears or is temporarily unreadable during shutdown.
        if ((Get-LogHeader) -cne $backendEvidence.Header) { return }
        $window = Read-LaunchBackendLogWindow -Path $profileLog -From $backendEvidence.ReadFrom
        if ((Get-LogHeader) -cne $backendEvidence.Header) { return }
        Save-LaunchBackendLogSnapshot -Directory $DiagnosticDirectory -Window $window -Header $backendEvidence.Header -SourcePath $profileLog
        $backendEvidence.Captured = $true
    } catch { Write-Warning ('Could not snapshot this attempt backend log: ' + $_.Exception.Message) }
}
$effectiveStart = if ($steamGame) { 'steam' } elseif ($StartLauncher) { 'launcher' } else { 'manual' }
$effectiveTarget = if ($steamGame) { $steamGame.Shipping } else { $gameProcName }
Write-Output ('Startup selection: requested={0}; effective={1}; startLauncher={2}; target={3}' -f $GameStart, $effectiveStart, [bool]$StartLauncher, $effectiveTarget)
Update-LaunchState -Path $StatePath -Values @{gameStartRequested=$GameStart;gameStartEffective=$effectiveStart;
    gameStartLauncher=[bool]$StartLauncher;gameTarget=$effectiveTarget}
$observe = {
    if ($steamGame) { $observation = Get-WuWaSteamProcessSnapshot -Game $steamGame -Injector $watchedInjector }
    else {
    $observation = Get-LaunchProcessSnapshot -TargetName $gameProcName -GameNames $gameNames `
        -LauncherNames $launcherNames -LauncherRoot $launcherRoot -Injector $watchedInjector
    }
    $observation.BackendLogStarted = $false
    $observation.FirstFrameSeen = $false
    $observation.BackendError = ''
    $observation.BackendEvidencePresent = $false
    $observation.BackendRendererInitialized = $false
    $observation.BackendProjectionSeen = $false
    $observation.BackendShuttingDown = $false
    $observation.TargetVerificationLost = $false
    $header = ''
    $observation.Slice = ''
    $observation.ReadFrom = $logOffset
    if (Test-Path -LiteralPath $profileLog) {
        # If the first line changed, UEVR rewrote the file for this launch and the
        # whole thing is ours. Otherwise it is still the previous run's log and we
        # read only what was appended past the pre-launch length. An unreadable
        # header is not a change: rereading the old log could match its old frame.
        $header = Get-LogHeader
        if ($header -and $header -ne $logHeaderBefore) {
            $observation.ReadFrom = 0
            $observation.BackendLogStarted = $true
        }
        if ($observation.BackendLogStarted -or (Get-Item -LiteralPath $profileLog).Length -gt $logOffset) {
            $observation.Slice = Read-LogSlice $observation.ReadFrom
            $observation.FirstFrameSeen = $observation.Slice -match 'texture bounds right eye'
        }
    }
    # Once this attempt's log was attributed to the selected process, retain
    # same-header evidence if Path becomes unreadable or multiple candidates
    # appear. It cannot establish readiness while process identity is ambiguous.
    $sameAttributedLog = $backendEvidence.Header -and $header -ceq $backendEvidence.Header
    $canAttribute = -not $steamGame -or $observation.TargetRunning -or $sameAttributedLog
    if ($observation.Slice -and $canAttribute) {
        $backendEvidence.Header = $header
        $backendEvidence.ReadFrom = $observation.ReadFrom
        $observation.BackendEvidencePresent = $true
        $observation.BackendRendererInitialized = $observation.Slice -match '(?im)^.*\[info\].*(?:Framework initialized|Creating OpenXR swapchains).*$'
        $observation.BackendProjectionSeen = $observation.Slice -match 'texture bounds right eye'
        $backendEvidence.RendererInitialized = $observation.BackendRendererInitialized
        $backendEvidence.ProjectionSeen = $observation.BackendProjectionSeen
        # Shutdown belongs to this attempt only after the same exact attribution
        # gate as renderer evidence. Old/foreign logs must never end this wait.
        $observation.BackendShuttingDown = $observation.Slice -match '(?im)^.*\[info\]\s+Framework shutting down\.\.\.\s*$'
        $backendEvidence.ShuttingDown = $observation.BackendShuttingDown
        if ($observation.BackendShuttingDown) { $observation.FirstFrameSeen = $false }
        $observation.BackendError = Get-LaunchBackendError -Text $observation.Slice
        $backendEvidence.Error = $observation.BackendError
        Save-ObservedBackendLog
    }
    if ($steamGame -and -not $observation.TargetRunning) {
        $observation.FirstFrameSeen = $false
        $observation.BackendLogStarted = $observation.BackendEvidencePresent
        $observation.TargetVerificationLost = [bool]$sameAttributedLog
        $backendEvidence.TargetVerificationLost = [bool]$sameAttributedLog
    } elseif ($steamGame) { $backendEvidence.TargetVerificationLost = $false }
    $observation
}
$onPoll = {
    param($observation, $decision, $seen)
    $phase = 'waiting-for-play'
    $message = 'Launcher open; press Play.'
    if (-not $observation.LauncherRunning) { $message = 'Launcher not detected; the attempt ends if it stays closed.' }
    if (-not $StartLauncher) { $message = 'Waiting for the game to start.' }
    if ($steamGame) { $message = 'Steam launch requested; waiting for the selected installation. VR compatibility is unverified.' }
    if ($seen) { $phase = 'game-starting'; $message = 'Game process detected; waiting for UEVR.' }
    if ($observation.BackendLogStarted) { $phase = 'injected'; $message = 'UEVR log started; waiting for the first stereo frame.' }
    if ($observation.BackendError -and -not $observation.FirstFrameSeen) { $message = 'UEVR startup needs attention; still waiting in case it recovers. ' + $observation.BackendError }
    if ($observation.TargetVerificationLost) {
        $message = 'UEVR log evidence is present, but the selected Steam process can no longer be uniquely verified. Open Troubleshooting and copy diagnostics; do not start another injector.'
        if ($observation.BackendRendererInitialized) { $message = 'UEVR reached renderer initialization, but the selected Steam process can no longer be uniquely verified. Open Troubleshooting and copy diagnostics; do not start another injector.' }
    }
    if ($observation.BackendShuttingDown) { $phase = 'stopping'; $message = 'UEVR reported Framework shutdown; ending this startup wait. The game may still be open.' }
    if ($decision.Action -eq 'Cancelled') { $phase = 'stopping'; $message = 'Stopping this startup wait on request; the game is left open.' }
    if ($message -ne $progress.Message) {
        Write-Host ('  {0}  {1}' -f (Get-Date -Format 'HH:mm:ss'), $message)
        $progress.Message = $message
    }
    Update-LaunchState -Path $StatePath -Values @{ phase = $phase; message = $message;
        injectorRunning = [bool]$observation.InjectorRunning; backendLogStarted = [bool]$observation.BackendLogStarted;
        firstFrameSeen = [bool]$observation.FirstFrameSeen; backendError = $observation.BackendError;
        backendLogCaptured = [bool]$backendEvidence.Captured;
        backendEvidencePresent = [bool]$observation.BackendEvidencePresent;
        backendRendererInitialized = [bool]$observation.BackendRendererInitialized;
        backendProjectionSeen = [bool]$observation.BackendProjectionSeen;
        backendShuttingDown = [bool]$observation.BackendShuttingDown;
        targetVerificationLost = [bool]$observation.TargetVerificationLost }
    if ($steamGame) {
        Update-LaunchState -Path $StatePath -Values @{steamTargetCount=$observation.SteamTargetCount;
            steamTargetCandidateCount=$observation.SteamTargetCandidateCount;
            steamTargetUnverifiedCount=$observation.SteamTargetUnverifiedCount;
            steamTargetProcesses=$observation.SteamTargetProcesses}
    }
}
$cancelCheck = {
    ($CancelPath -and (Test-Path -LiteralPath $CancelPath)) -or ($PromptOnLauncherClosed -and (Test-LaunchCancelKey))
}
$launcherPrompt = $null
if ($PromptOnLauncherClosed -and $StartLauncher -and -not $steamGame) {
    $launcherPrompt = {
        Write-Host ''
        Write-Host 'The game launcher closed before Play was pressed.' -ForegroundColor Yellow
        Write-Host 'Press R within 60 s to reopen it and keep waiting, or any other key to stop now.'
        $key = Read-LaunchKey -TimeoutSeconds 60 -StopWhen { $CancelPath -and (Test-Path -LiteralPath $CancelPath) }
        if ($key -and $key.Key -eq [ConsoleKey]::R) {
            Write-Host 'Reopening the launcher.'
            $null = Start-Process -FilePath $launcherExe -WorkingDirectory (Split-Path -Parent $launcherExe)
            $progress.Message = ''
            return 'Retry'
        }
        return 'Stop'
    }
}
Write-Output ''
try {
    if ($ObserveOnly) {
        $watchedInjector = $waitingInjectors[0]
        Write-Output "Observing existing injector (PID $($watchedInjector.Id)); no new injector started."
    } else {
        Assert-LaunchNotCancelled -CancelPath $CancelPath
        Write-Output 'Starting injector...'
        $injectorArguments = 'Client-Win64-Shipping.exe'
        if ($steamGame) { $injectorArguments = '--target-path "' + $steamGame.Shipping + '"' }
        $injectorStart = @{FilePath=$injectorExe;WorkingDirectory=(Split-Path -Parent $injectorExe);
            WindowStyle='Hidden';ArgumentList=$injectorArguments;PassThru=$true}
        if ($DiagnosticDirectory) {
            if (-not (Test-Path -LiteralPath $DiagnosticDirectory -PathType Container)) { throw 'The launch diagnostics folder is missing. Retry from the launcher.' }
            $injectorStart.RedirectStandardOutput=Join-Path $DiagnosticDirectory 'injector.stdout.log'
            $injectorStart.RedirectStandardError=Join-Path $DiagnosticDirectory 'injector.stderr.log'
        }
        $injectorProcess = Start-Process @injectorStart
        $watchedInjector = $injectorProcess
        Update-LaunchState -Path $StatePath -Values @{ injectorPid = $injectorProcess.Id; injectorStarted = $true }
        Write-Output "Injector started (PID $($injectorProcess.Id)); waiting for the selected game."
    }

    Assert-LaunchNotCancelled -CancelPath $CancelPath
    if ($steamGame) {
        Write-Output 'Asking Steam to start app 3513350 through the normal desktop. Steam VR injection is unverified.'
        Start-WuWaSteamGame -Game $steamGame
    } elseif ($StartLauncher) {
        Write-Output 'Starting the Wuthering Waves launcher. Sign in and press Play yourself.'
        $null = Start-Process -FilePath $launcherExe -WorkingDirectory (Split-Path -Parent $launcherExe)
    } else {
        Write-Output 'Now start Wuthering Waves through its launcher. Re-run with -StartLauncher to have this open it for you.'
    }

    Write-Output ''
    Write-Output "Waiting up to $WaitSeconds s for injection and the first stereo frame..."
    if ($PromptOnLauncherClosed) { Write-Output 'Press Q in this window to stop waiting. This script never closes the game.' }

    $outcome = Wait-LaunchOutcome -Observe $observe -TimeoutSeconds $WaitSeconds `
        -InjectorExpected ($null -ne $watchedInjector) -LauncherExpected ($StartLauncher.IsPresent -and -not $steamGame) `
        -CancelRequested $cancelCheck -OnPoll $onPoll -OnLauncherClosed $launcherPrompt `
        -LauncherGraceSeconds $LauncherGraceSeconds
    $slice = [string]$outcome.Observation.Slice
    $readFrom = [long]$outcome.Observation.ReadFrom
    $runContinuity = Get-RunContinuityCheck

    if ($outcome.Action -ne 'Ready') {
        $cleanup = ''
        if ($injectorProcess) { $cleanup = Format-InjectorCleanup (Stop-OwnedInjector -Process $injectorProcess -GameNames $gameNames) }
        $injectorHandled = $true
        $advice = switch ($outcome.Action) {
            'LauncherClosed' { ' Run the shortcut again and press Play in the launcher.' }
            'InjectorExited' { ' Open Troubleshooting and copy diagnostics; include injector.stdout.log and injector.stderr.log from this attempt if present. Check Windows Security protection history for a blocked file. Do not disable security software.' }
            'GameExited'     { ' The game may have crashed or been closed manually; the launcher cannot distinguish them. Open Troubleshooting and copy diagnostics, including this attempt backend.log.' }
            'BackendStopped' { ' Open Troubleshooting and copy diagnostics, including this attempt backend.log. This does not establish whether the game crashed or was closed manually.' }
            'NotInjected'    { ' Open Troubleshooting > Copy diagnostics in the launcher. Include the injector log when reporting this failure. Close the game normally before retrying.' }
            default          { '' }
        }
        $phase = 'failed'
        if ($outcome.Action -eq 'Cancelled') { $phase = 'cancelled' }
        $exitDetail = ''
        if ($outcome.Action -eq 'InjectorExited' -and $watchedInjector) { $exitDetail = Get-LaunchProcessExitDetail -Process $watchedInjector }
        $backendDetail = ''; if ($backendEvidence.Error -and $outcome.Action -ne 'Cancelled') { $backendDetail = ' Last backend startup error: ' + $backendEvidence.Error }
        $message = $outcome.Detail + $exitDetail + $cleanup + $advice + $backendDetail
        $reportedOutcome = $outcome.Action
        if ($backendEvidence.TargetVerificationLost -and $outcome.Action -in @('GameExited','TimedOut','NotInjected')) {
            $reportedOutcome = 'TargetUnverified'
            $message = 'The selected Steam process could not be continuously verified. UEVR log evidence was preserved, but the launcher cannot establish whether the game is still running. Open Troubleshooting and copy diagnostics before retrying.' + $cleanup
            if ($backendEvidence.RendererInitialized) { $message = 'UEVR reached renderer initialization, but the selected Steam process could not be continuously verified. This observation did not establish a renderer startup failure. Open Troubleshooting and copy diagnostics before retrying.' + $cleanup }
        }
        Update-LaunchState -Path $StatePath -Values @{ phase = $phase; outcome = $reportedOutcome; message = $message }
        throw $message
    }

    Assert-LaunchNotCancelled -CancelPath $CancelPath
    if ($slice -match 'texture bounds right eye') {
        # The first stereo frame is far too early to sample per-frame
        # instrumentation. [WuWaDiag] records accumulate over the session, and
        # the scene-capture lifecycle only starts churning once a world is
        # loaded. Sampling here reported "0 records" while the log went on to
        # collect hundreds. Let it run, then re-read.
        Update-LaunchState -Path $StatePath -Values @{ phase = 'settling'; message = "First stereo frame seen; sampling for $SettleSeconds s." }
        $gameProcesses = @(Get-SelectedGameProcesses)
        $expectedProcess = $null
        if ($gameProcesses.Count -eq 1) {
            $expectedProcess = [pscustomobject]@{
                Id = $gameProcesses[0].Id
                StartTime = $gameProcesses[0].StartTime
            }
        }
        $entryMatch = [regex]::Match($slice, '^\[(?<time>[^\]]+)\].*UnrealVR entry', 'Multiline')
        $entryTime = [datetime]::MinValue
        if ($entryMatch.Success) {
            $entryTime = [datetime]::ParseExact($entryMatch.Groups['time'].Value,
                'yyyy-MM-dd HH:mm:ss.fff', [Globalization.CultureInfo]::InvariantCulture)
        }
        $expectedHeader = Get-LogHeader
        $runContinuity = Get-RunContinuityCheck -ExpectedProcess $expectedProcess `
            -BackendEntryTime $entryTime -ExpectedLogHeader $expectedHeader `
            -CurrentProcesses $gameProcesses -CurrentLogHeader (Get-LogHeader)
        Write-Output "First stereo frame seen. Sampling for $SettleSeconds s so per-frame instrumentation can accumulate..."
        $settleDeadline = (Get-Date).AddSeconds($SettleSeconds)
        while ($runContinuity.Result -eq 'PASS' -and (Get-Date) -lt $settleDeadline) {
            Assert-LaunchNotCancelled -CancelPath $CancelPath
            $remainingMs = [int][Math]::Ceiling(($settleDeadline - (Get-Date)).TotalMilliseconds)
            if ($remainingMs -le 0) { break }
            Start-Sleep -Milliseconds ([Math]::Min(3000, $remainingMs))
            Assert-LaunchNotCancelled -CancelPath $CancelPath
            $gameProcesses = @(Get-SelectedGameProcesses)
            $runContinuity = Get-RunContinuityCheck -ExpectedProcess $expectedProcess `
                -BackendEntryTime $entryTime -ExpectedLogHeader $expectedHeader `
                -CurrentProcesses $gameProcesses -CurrentLogHeader (Get-LogHeader)
            if ($runContinuity.Result -ne 'PASS') {
                Write-Warning "Settle window interrupted: $($runContinuity.Detail)"
                break
            }
            # Preserve the last known slice if the process or log is replaced.
            $candidateSlice = Read-LogSlice $readFrom
            $runContinuity = Get-RunContinuityCheck -ExpectedProcess $expectedProcess `
                -BackendEntryTime $entryTime -ExpectedLogHeader $expectedHeader `
                -CurrentProcesses @(Get-SelectedGameProcesses) `
                -CurrentLogHeader (Get-LogHeader)
            if ($runContinuity.Result -eq 'PASS') { $slice = $candidateSlice }
        }
    }
} catch {
    # Any failure after the injector started (a launcher that would not start,
    # an unreadable log) must not leave it waiting to block the next attempt.
    # Stop-OwnedInjector leaves everything alone while a game process runs.
    if ($injectorHandled -or -not $injectorProcess) { throw }
    $message = $_.Exception.Message + (Format-InjectorCleanup (Stop-OwnedInjector -Process $injectorProcess -GameNames $gameNames))
    $phase=if($_.Exception -is [OperationCanceledException]){'cancelled'}else{'failed'}
    try { Update-LaunchState -Path $StatePath -Values @{ phase = $phase; outcome = 'Error'; message = $message } } catch { }
    throw $message
} finally {
    Save-ObservedBackendLog
}

if ([string]::IsNullOrWhiteSpace($slice)) {
    throw "No new output in $profileLog. The game never started, or UEVR never injected."
}

# -------------------------------------------------------------- assertions ---
$lines = $slice -split "`r?`n"
$results = New-Object System.Collections.ArrayList
$null = $results.Add($runContinuity)

function Add-Check($name, $ok, $detail) {
    $verdict = 'FAIL'
    if ($ok) { $verdict = 'PASS' }
    $null = $results.Add([pscustomobject]@{ Check = $name; Result = $verdict; Detail = $detail })
}

Add-Check 'injected'          ($slice -match 'UnrealVR entry')                       'UEVR backend entered the game process'
Add-Check 'openxr d3d12'      ($slice -match 'Creating OpenXR swapchains for D3D12') 'swapchains created on the D3D12 path'
Add-Check 'left eye fov'      ($slice -match 'Original FOV for left eye')            'per-eye projection logged, left'
Add-Check 'right eye fov'     ($slice -match 'Original FOV for right eye')           'per-eye projection logged, right'
Add-Check 'bounds both eyes'  (($slice -match 'texture bounds left eye') -and ($slice -match 'texture bounds right eye')) 'derived texture bounds logged for both eyes'
Add-Check 'no xrEndFrame err' (-not ($slice -match 'xrEndFrame failed'))             'no rejected frame submissions'

# The FSceneView constructor hook hosts every [WuWaDiag] site and the native
# stereo pass rewrite. The independent LGUI/input fixes can run without it.
$fsvFound  = $slice -match 'Found FSceneView constructor at'
$fsvFailed = $slice -match 'Failed to find FSceneView constructor'
$fsvDetail = 'hook not resolved - [WuWaDiag] and the stereo pass rewrite cannot run'
if ($fsvFound) {
    $m = [regex]::Match($slice, 'Found FSceneView constructor at 0x([0-9a-fA-F]+)')
    $fsvDetail = 'constructor at 0x' + $m.Groups[1].Value
}
if ($on.Count -gt 0 -or $fsvFound -or $fsvFailed) {
    Add-Check 'fsceneview hook' ($fsvFound -and -not $fsvFailed) $fsvDetail
} else {
    $null = $results.Add([pscustomobject]@{ Check = 'fsceneview hook'; Result = 'N/A'; Detail = 'optional hook disabled in the saved profile' })
}

# Equal FOVs are valid for a symmetric headset. They do not establish whether
# the rendered eye images or poses differ; image/pose checks must establish that.
$leftFov  = $lines | Select-String -Pattern 'Original FOV for left eye:\s*(.+)$'  | Select-Object -First 1
$rightFov = $lines | Select-String -Pattern 'Original FOV for right eye:\s*(.+)$' | Select-Object -First 1
if ($leftFov -and $rightFov) {
    $l = $leftFov.Matches[0].Groups[1].Value.Trim()
    $r = $rightFov.Matches[0].Groups[1].Value.Trim()
    Add-Check 'eye fovs parsed' $true "L($l) R($r); scene eye order requires image/pose validation"
} else {
    Add-Check 'eye fovs parsed' $false 'could not parse both FOV lines'
}

# The diagnostics backend emits [WuWaDiag] records. Their absence when that
# backend is configured only matters when the optional scene-view route is on.
$diag = @($lines | Where-Object { $_ -match '\[WuWaDiag\]' })
if ($configuredRuntime -and $configuredRuntime -like '*diagnostics*' -and ($on.Count -gt 0 -or $fsvFound)) {
    Add-Check 'wuwadiag records' ($diag.Count -gt 0) ('{0} record(s) from the instrumented backend' -f $diag.Count)
}

# NativeStereoFix supplies the second eye through a scene capture. Availability
# can change during loading; classify observations in order, not by whether a
# transient error ever occurred. Resource readiness alone does not prove stereo.
$null = $results.Add((Get-SceneCaptureCheck -LogText $slice))

Write-Output ''
$results | Format-Table -AutoSize

if ($diag.Count -gt 0) {
    Write-Output '[WuWaDiag] record kinds seen this run:'
    $diag |
        ForEach-Object { ($_ -replace '^.*\[WuWaDiag\]\s*', '') -replace '[-0-9]*\.?[0-9]+', 'N' } |
        Group-Object |
        Sort-Object Count -Descending |
        Select-Object -First 20 |
        ForEach-Object { Write-Output ('  {0,5}x  {1}' -f $_.Count, $_.Name) }
    Write-Output ''
}

$sliceFile = Join-Path $env:TEMP ('wuwa-sim-run-{0}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
Set-Content -LiteralPath $sliceFile -Value $slice -Encoding utf8
Write-Output "Log slice for this run: $sliceFile"
Write-Output 'Teardown when done:     .\dev\sim-run.ps1 -Stop'

$failed = @($results | Where-Object { $_.Result -eq 'FAIL' })
if ($failed.Count -gt 0) {
    Write-Output ''
    Write-Warning ('{0} check(s) failed.' -f $failed.Count)
}
$pending = @($results | Where-Object { $_.Result -eq 'PENDING' })
if ($pending.Count -gt 0) {
    Write-Warning ('{0} check(s) pending; this log slice does not establish a complete startup.' -f $pending.Count)
}
Update-LaunchState -Path $StatePath -Values @{
    outcome = 'Ready'; continuity = [string]$runContinuity.Result; checksFailed = $failed.Count
    checksPending = $pending.Count; sliceFile = $sliceFile
}
