# Startup lifecycle shared by start-wuwa-rendering-test.ps1 and sim-run.ps1.
#
# September 23: a launcher closed eight seconds after opening left a hidden
# elevated script polling log.txt for ten minutes while a hidden injector waited
# for a game that was never started. Every retry then failed invisibly on the
# startup mutex. The pieces below replace that blind wait: decisions are pure
# functions, and the process/mutex/state-file glue is small enough for
# test-launch-lifecycle.ps1 to drive with real stand-in processes.

# ------------------------------------------------------------------ locks ---
# Mutex ownership is per thread; a PowerShell script body runs on one thread.
function Enter-LaunchLock {
    param([Parameter(Mandatory)][string]$Name)
    try {
        $mutex = [Threading.Mutex]::new($false, $Name)
    } catch [UnauthorizedAccessException] {
        # Created by an elevated process that is still alive.
        return [pscustomobject]@{ Status = 'Busy'; Mutex = $null; Owned = $false }
    }
    $abandoned = $false
    try {
        $owned = $mutex.WaitOne(0)
    } catch [Threading.AbandonedMutexException] {
        # The previous owner died without releasing. The wait still grants
        # ownership, so this attempt proceeds instead of failing forever.
        $owned = $true
        $abandoned = $true
    }
    if (-not $owned) {
        $mutex.Dispose()
        return [pscustomobject]@{ Status = 'Busy'; Mutex = $null; Owned = $false }
    }
    $status = 'Acquired'
    if ($abandoned) { $status = 'AcquiredAbandoned' }
    return [pscustomobject]@{ Status = $status; Mutex = $mutex; Owned = $true }
}

function Exit-LaunchLock {
    param([AllowNull()][object]$Lock)
    if ($null -eq $Lock -or $null -eq $Lock.Mutex) { return }
    if ($Lock.Owned) {
        try { $Lock.Mutex.ReleaseMutex() } catch { }
        $Lock.Owned = $false
    }
    $Lock.Mutex.Dispose()
    $Lock.Mutex = $null
}

# Read-only probe for a lock another process may own. A medium-integrity caller
# cannot open a mutex created by the elevated startup; that denial still proves
# a live holder because the object disappears with its last handle.
function Get-LaunchLockPresence {
    param([Parameter(Mandatory)][string]$Name)
    $mutex = $null
    try {
        if (-not [Threading.Mutex]::TryOpenExisting($Name, [ref]$mutex)) { return 'Absent' }
    } catch [UnauthorizedAccessException] {
        return 'Held'
    }
    try {
        $got = $false
        try { $got = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $got = $true }
        if ($got) {
            $mutex.ReleaseMutex()
            return 'Free'
        }
        return 'Held'
    } finally {
        $mutex.Dispose()
    }
}

# ------------------------------------------------------------- state file ---
function ConvertTo-LaunchStateTable {
    param([AllowNull()][object]$State)
    $table = [ordered]@{}
    if ($null -eq $State) { return $table }
    foreach ($property in $State.PSObject.Properties) { $table[$property.Name] = $property.Value }
    return $table
}

function Read-LaunchState {
    param([Parameter(Mandatory)][string]$Path)
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        if (-not (Test-Path -LiteralPath $Path)) { return $null }
        try {
            return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json)
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    return $null
}

function Write-LaunchState {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][Collections.IDictionary]$State
    )
    $State['heartbeat'] = (Get-Date).ToString('o')
    $json = [pscustomobject]$State | ConvertTo-Json -Depth 6
    $encoding = [Text.UTF8Encoding]::new($false)
    $temporary = "$Path.$PID.tmp"
    [IO.File]::WriteAllText($temporary, $json, $encoding)
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        try {
            if (Test-Path -LiteralPath $Path) {
                [IO.File]::Replace($temporary, $Path, $null)
            } else {
                [IO.File]::Move($temporary, $Path)
            }
            return
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    # A reader kept the file open; a direct write is still better than no state.
    [IO.File]::WriteAllText($Path, $json, $encoding)
    Remove-Item -LiteralPath $temporary -ErrorAction SilentlyContinue
}

function Update-LaunchState {
    param(
        [AllowEmptyString()][string]$Path,
        [Parameter(Mandatory)][Collections.IDictionary]$Values
    )
    if ([string]::IsNullOrWhiteSpace($Path)) { return }
    $table = ConvertTo-LaunchStateTable (Read-LaunchState -Path $Path)
    foreach ($key in $Values.Keys) { $table[$key] = $Values[$key] }
    Write-LaunchState -Path $Path -State $table
}

# True while a non-terminal startup keeps its heartbeat fresh and its process is
# alive. The worker lock is authoritative; this only explains who holds it.
function Test-LaunchStateLive {
    param(
        [AllowNull()][object]$State,
        [datetime]$Now = (Get-Date),
        [int]$StaleSeconds = 90
    )
    if ($null -eq $State -or $State.phase -in @('finished', 'failed', 'cancelled')) { return $false }
    # Windows PowerShell keeps ISO strings; PowerShell 7 converts them to DateTime.
    $heartbeat = $State.heartbeat
    if ($heartbeat -isnot [datetime]) {
        $parsed = [datetime]::MinValue
        if (-not [datetime]::TryParse([string]$heartbeat, [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) { return $false }
        $heartbeat = $parsed
    }
    if (($Now - $heartbeat.ToLocalTime()).TotalSeconds -gt $StaleSeconds) { return $false }
    return $null -ne (Get-Process -Id ([int]$State.pid) -ErrorAction SilentlyContinue)
}

function Format-LaunchState {
    param([AllowNull()][object]$State)
    if ($null -eq $State) { return 'No startup has been recorded.' }
    $lines = @("Last startup: PID $($State.pid), phase '$($State.phase)', started $($State.started).")
    if ($State.message) { $lines += [string]$State.message }
    if ($State.runDir) { $lines += "Evidence: $($State.runDir)" }
    return ($lines -join [Environment]::NewLine)
}

# ------------------------------------------------------------- front end ---
function Test-ElevationCancelled {
    param([AllowNull()][Exception]$Exception)
    for ($current = $Exception; $null -ne $current; $current = $current.InnerException) {
        if ($current -is [ComponentModel.Win32Exception] -and $current.NativeErrorCode -eq 1223) { return $true }
    }
    return $false
}

function New-LaunchFrontEndResult {
    param([string]$Outcome, [int]$ExitCode, [string]$Message)
    return [pscustomobject]@{ Outcome = $Outcome; ExitCode = $ExitCode; Message = $Message }
}

# Process.Start returns once Windows has approved elevation, before the child
# has loaded PowerShell and taken the worker lock. Until one of those happens a
# second click would see neither lock and ask for elevation again.
function Wait-LaunchHandoff {
    param(
        [AllowNull()][object]$Child,
        [Parameter(Mandatory)][string]$WorkerLockName,
        [int]$TimeoutSeconds = 30
    )
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if ((Get-LaunchLockPresence -Name $WorkerLockName) -eq 'Held') { return 'Acquired' }
        if ($Child -is [Diagnostics.Process]) {
            try { if ($Child.HasExited) { return 'Exited' } } catch { }
        }
        Start-Sleep -Milliseconds 100
    }
    return 'Timeout'
}

# The non-elevated half of the shortcut: never changes anything, refuses to open
# a second administrator prompt while one is pending, and turns every failure
# into a message instead of a silently closing window.
function Invoke-LaunchFrontEnd {
    param(
        [Parameter(Mandatory)][string]$WorkerLockName,
        [Parameter(Mandatory)][string]$RequestLockName,
        [Parameter(Mandatory)][string]$StatePath,
        [Parameter(Mandatory)][scriptblock]$Preflight,
        [Parameter(Mandatory)][scriptblock]$Elevate,
        [switch]$CheckOnly,
        [int]$HandoffTimeoutSeconds = 30
    )
    $state = Read-LaunchState -Path $StatePath
    if ((Get-LaunchLockPresence -Name $WorkerLockName) -eq 'Held') {
        $detail = 'Another rendering-test startup is still running, so nothing new was started.'
        if (Test-LaunchStateLive -State $state) {
            $detail += [Environment]::NewLine + (Format-LaunchState $state)
            $detail += [Environment]::NewLine + "Its window is titled 'WuWa VR rendering test'. Press Q there to stop it, or run: dev\start-wuwa-rendering-test.ps1 -Cancel"
        } else {
            $detail += [Environment]::NewLine + 'It did not record a live heartbeat (an older hidden startup). It stops on its own within ten minutes of its start.'
        }
        return (New-LaunchFrontEndResult 'Busy' 2 $detail)
    }
    try {
        & $Preflight
    } catch {
        return (New-LaunchFrontEndResult 'PreflightFailed' 1 ('Not started: ' + $_.Exception.Message))
    }
    if ($CheckOnly) {
        return (New-LaunchFrontEndResult 'CheckPassed' 0 'PASS: game/injector/SteamVR closed, compiled DLL matches receipt, selected backend and simulator files verified. Nothing changed.')
    }
    $request = Enter-LaunchLock -Name $RequestLockName
    if ($request.Status -eq 'Busy') {
        return (New-LaunchFrontEndResult 'PromptPending' 3 'A Windows administrator prompt from this shortcut is already waiting for an answer. Answer that prompt (it may be behind other windows or flashing in the taskbar); no second prompt was opened.')
    }
    try {
        $child = & $Elevate
        $childId = ''
        if ($child -and $child.Id) { $childId = " (PID $($child.Id))" }
        # Keep the request lock until the child owns the worker lock or ends.
        $handoff = Wait-LaunchHandoff -Child $child -WorkerLockName $WorkerLockName -TimeoutSeconds $HandoffTimeoutSeconds
        if ($handoff -eq 'Exited') {
            return (New-LaunchFrontEndResult 'ChildExited' 1 "The elevated startup$childId closed before it began. Nothing was started; run dev\start-wuwa-rendering-test.ps1 -Status for the last recorded reason.")
        }
        if ($handoff -eq 'Timeout') {
            return (New-LaunchFrontEndResult 'StartedUnconfirmed' 5 "Startup$childId has not confirmed it is ready after $HandoffTimeoutSeconds s. Do not start another injector. Check the launch progress and use Recovery > Copy diagnostics; permission and injection are not confirmed by this result.")
        }
        return (New-LaunchFrontEndResult 'Started' 0 "Opened the 'WuWa VR rendering test' window$childId. Follow it there; press Play in the launcher.")
    } catch {
        if (Test-ElevationCancelled $_.Exception) {
            return (New-LaunchFrontEndResult 'ElevationCancelled' 4 'Windows administrator permission was declined, so nothing was changed. Run the shortcut again and choose Yes to start the rendering test.')
        }
        return (New-LaunchFrontEndResult 'ElevationFailed' 1 ('Could not open the elevated startup: ' + $_.Exception.Message))
    } finally {
        Exit-LaunchLock $request
    }
}

# ------------------------------------------------------ wait-for-game loop ---
function Get-LaunchWaitDecision {
    param(
        [bool]$FirstFrameSeen,
        [bool]$CancelRequested,
        [bool]$GameRunning,
        [bool]$GameSeen,
        [double]$GameGoneSeconds,
        [bool]$InjectorExpected,
        [bool]$InjectorRunning,
        [bool]$LauncherExpected,
        [double]$LauncherGoneSeconds,
        [bool]$TargetRunning,
        [bool]$BackendLogStarted,
        [double]$TargetWithoutLogSeconds,
        [bool]$TimedOut,
        [int]$LauncherGraceSeconds = 15,
        [int]$GameGoneGraceSeconds = 10,
        [int]$InjectionGraceSeconds = 120
    )
    $action = 'Continue'
    $detail = ''
    if ($FirstFrameSeen) {
        $action = 'Ready'; $detail = 'UEVR produced its first stereo frame.'
    } elseif ($CancelRequested) {
        $action = 'Cancelled'; $detail = 'Stopped waiting on request.'
    } elseif ($GameSeen -and -not $GameRunning -and $GameGoneSeconds -ge $GameGoneGraceSeconds) {
        $action = 'GameExited'; $detail = 'The game closed before UEVR produced a stereo frame.'
    } elseif (-not $GameSeen -and $InjectorExpected -and -not $InjectorRunning) {
        $action = 'InjectorExited'; $detail = 'The injector closed before the game started, so nothing could be injected.'
    } elseif (-not $GameSeen -and $LauncherExpected -and $LauncherGoneSeconds -ge $LauncherGraceSeconds) {
        $action = 'LauncherClosed'; $detail = 'The game launcher closed before Play was pressed.'
    } elseif ($TargetRunning -and -not $BackendLogStarted -and $TargetWithoutLogSeconds -ge $InjectionGraceSeconds) {
        $action = 'NotInjected'; $detail = "The game has run for $([int]$TargetWithoutLogSeconds) s without UEVR starting a new log; it is running without VR."
    } elseif ($TimedOut) {
        $action = 'TimedOut'; $detail = 'No stereo frame arrived before the time limit.'
        if (-not $GameSeen) { $detail = 'The game was not started before the time limit.' }
    }
    return [pscustomobject]@{ Action = $action; Detail = $detail }
}

function Get-LaunchProcessSnapshot {
    param(
        [Parameter(Mandatory)][string]$TargetName,
        [string[]]$GameNames = @(),
        [string[]]$LauncherNames = @(),
        [string]$LauncherRoot,
        [AllowNull()][object]$Injector
    )
    $names = @($TargetName) + @($GameNames | Where-Object { $_ -and $_ -ne $TargetName })
    $game = @(Get-Process -Name $names -ErrorAction SilentlyContinue)
    $launchers = @()
    if ($LauncherNames.Count) {
        $launchers = @(Get-Process -Name $LauncherNames -ErrorAction SilentlyContinue | Where-Object {
            # Generic names such as launcher.exe only count inside the game install.
            $path = $null
            try { $path = $_.Path } catch { }
            -not $LauncherRoot -or -not $path -or $path.StartsWith($LauncherRoot, [StringComparison]::OrdinalIgnoreCase)
        })
    }
    $injectorRunning = $false
    if ($null -ne $Injector) {
        try {
            $Injector.Refresh()
            $injectorRunning = -not $Injector.HasExited
        } catch {
            $injectorRunning = $null -ne (Get-Process -Id $Injector.Id -ErrorAction SilentlyContinue)
        }
    }
    return @{
        GameRunning = $game.Count -gt 0
        TargetRunning = @($game | Where-Object { $_.ProcessName -eq $TargetName }).Count -gt 0
        LauncherRunning = $launchers.Count -gt 0
        InjectorRunning = $injectorRunning
    }
}

# Poll until the first stereo frame or a definite reason to stop. $Observe
# returns a hashtable with GameRunning, TargetRunning, LauncherRunning,
# InjectorRunning, BackendLogStarted and FirstFrameSeen (other keys pass through).
function Wait-LaunchOutcome {
    param(
        [Parameter(Mandatory)][scriptblock]$Observe,
        [int]$TimeoutSeconds = 600,
        [int]$PollMilliseconds = 3000,
        [bool]$InjectorExpected,
        [bool]$LauncherExpected,
        [scriptblock]$CancelRequested,
        [scriptblock]$OnPoll,
        [scriptblock]$OnLauncherClosed,
        [int]$LauncherGraceSeconds = 15,
        [int]$GameGoneGraceSeconds = 10,
        [int]$InjectionGraceSeconds = 120
    )
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $gameSeen = $false
    $gameGoneSince = $null
    $launcherGoneSince = $null
    $targetSince = $null
    while ($true) {
        $observation = & $Observe
        $now = $clock.Elapsed.TotalSeconds
        if ($observation.GameRunning) { $gameSeen = $true; $gameGoneSince = $null }
        elseif ($gameSeen -and $null -eq $gameGoneSince) { $gameGoneSince = $now }
        if ($observation.LauncherRunning -or $observation.GameRunning) { $launcherGoneSince = $null }
        elseif ($null -eq $launcherGoneSince) { $launcherGoneSince = $now }
        if (-not $observation.TargetRunning) { $targetSince = $null }
        elseif ($null -eq $targetSince) { $targetSince = $now }

        $cancel = $false
        if ($CancelRequested) { $cancel = [bool](& $CancelRequested) }
        $gameGone = 0; if ($null -ne $gameGoneSince) { $gameGone = $now - $gameGoneSince }
        $launcherGone = 0; if ($null -ne $launcherGoneSince) { $launcherGone = $now - $launcherGoneSince }
        $targetAlone = 0; if ($null -ne $targetSince) { $targetAlone = $now - $targetSince }
        $decision = Get-LaunchWaitDecision -FirstFrameSeen ([bool]$observation.FirstFrameSeen) `
            -CancelRequested $cancel -GameRunning ([bool]$observation.GameRunning) -GameSeen $gameSeen `
            -GameGoneSeconds $gameGone -InjectorExpected $InjectorExpected `
            -InjectorRunning ([bool]$observation.InjectorRunning) -LauncherExpected $LauncherExpected `
            -LauncherGoneSeconds $launcherGone -TargetRunning ([bool]$observation.TargetRunning) `
            -BackendLogStarted ([bool]$observation.BackendLogStarted) -TargetWithoutLogSeconds $targetAlone `
            -TimedOut ($now -ge $TimeoutSeconds) -LauncherGraceSeconds $LauncherGraceSeconds `
            -GameGoneGraceSeconds $GameGoneGraceSeconds -InjectionGraceSeconds $InjectionGraceSeconds
        if ($OnPoll) { & $OnPoll $observation $decision $gameSeen }
        if ($decision.Action -eq 'LauncherClosed' -and $OnLauncherClosed -and (& $OnLauncherClosed) -eq 'Retry') {
            $launcherGoneSince = $null
            continue
        }
        if ($decision.Action -ne 'Continue') {
            return [pscustomobject]@{
                Action = $decision.Action
                Detail = $decision.Detail
                Observation = $observation
                GameSeen = $gameSeen
                ElapsedSeconds = [int]$now
            }
        }
        Start-Sleep -Milliseconds $PollMilliseconds
    }
}

# Close the injector this run started. Never used while any game process is
# alive; before that the injector has injected nothing, so ending it is safe.
function Stop-OwnedInjector {
    param(
        [Parameter(Mandatory)][Diagnostics.Process]$Process,
        [string[]]$GameNames = @(),
        [int]$GraceSeconds = 5
    )
    try { if ($Process.HasExited) { return 'AlreadyExited' } } catch { }
    if ($GameNames.Count -and @(Get-Process -Name $GameNames -ErrorAction SilentlyContinue).Count) {
        return 'LeftRunning'
    }
    $Process.Refresh()
    $requested = $false
    try { $requested = $Process.CloseMainWindow() } catch { }
    if ($requested -and $Process.WaitForExit($GraceSeconds * 1000)) { return 'Closed' }
    try {
        $Process.Kill()
        [void]$Process.WaitForExit(5000)
        return 'Stopped'
    } catch {
        return 'StopFailed: ' + $_.Exception.Message
    }
}

function Format-InjectorCleanup {
    param([AllowEmptyString()][string]$Stopped)
    switch -Wildcard ($Stopped) {
        'Closed'      { return ' Closed the injector this run started.' }
        'Stopped'     { return ' Ended the injector this run started; it had injected nothing.' }
        'LeftRunning' { return ' A game process is running, so its injector was left alone.' }
        'StopFailed*' { return " Could not close the injector this run started ($Stopped); close it from the taskbar." }
    }
    return ''
}

# Summarise a harness run that reached a stereo frame. Failed checks, and above
# all a failed continuity check, must not be reported as a running game.
function Get-LaunchCompletion {
    param(
        [AllowEmptyString()][string]$Continuity,
        [int]$ChecksFailed,
        [int]$ChecksPending
    )
    if ($Continuity -eq 'FAIL') {
        return [pscustomobject]@{ Phase = 'failed'; Failed = $true
            Message = 'UEVR reached a stereo frame, but the game did not stay the same running process through the check window. See startup.log.' }
    }
    if ($ChecksFailed -gt 0) {
        return [pscustomobject]@{ Phase = 'finished'; Failed = $true
            Message = "UEVR produced stereo frames, but $ChecksFailed startup check(s) failed. See the table in startup.log." }
    }
    if ($Continuity -ne 'PASS' -or $ChecksPending -gt 0) {
        $pending = [Math]::Max($ChecksPending, 1)
        return [pscustomobject]@{ Phase = 'finished'; Failed = $false
            Message = "UEVR produced stereo frames; $pending check(s) are pending, so this is not a complete startup check." }
    }
    return [pscustomobject]@{ Phase = 'finished'; Failed = $false
        Message = 'UEVR produced stereo frames and every startup check passed. This is not headset or gameplay acceptance.' }
}

# ------------------------------------------------------------ console keys ---
function Test-LaunchCancelKey {
    try {
        while ([Console]::KeyAvailable) {
            $key = [Console]::ReadKey($true)
            if ($key.Key -eq [ConsoleKey]::Q -or $key.Key -eq [ConsoleKey]::Escape) { return $true }
        }
    } catch { }
    return $false
}

function Read-LaunchKey {
    param([int]$TimeoutSeconds = 60, [scriptblock]$StopWhen)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($TimeoutSeconds -le 0 -or (Get-Date) -lt $deadline) {
        try {
            if ([Console]::KeyAvailable) { return [Console]::ReadKey($true) }
        } catch {
            return $null
        }
        if ($StopWhen -and (& $StopWhen)) { return $null }
        Start-Sleep -Milliseconds 200
    }
    return $null
}
