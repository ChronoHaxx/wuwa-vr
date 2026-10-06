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
        # A protected object exists, but access denial does not prove ownership.
        return [pscustomobject]@{ Status = 'Busy'; Reason = 'AccessDenied'; Mutex = $null; Owned = $false }
    }
    $abandoned = $false
    try {
        $owned = $mutex.WaitOne(0)
    } catch [Threading.AbandonedMutexException] {
        # The previous owner died without releasing. The wait still grants
        # ownership, so this attempt proceeds instead of failing forever.
        $owned = $true
        $abandoned = $true
    } catch [UnauthorizedAccessException] {
        $mutex.Dispose()
        return [pscustomobject]@{ Status = 'Busy'; Reason = 'AccessDenied'; Mutex = $null; Owned = $false }
    }
    if (-not $owned) {
        $mutex.Dispose()
        return [pscustomobject]@{ Status = 'Busy'; Reason = 'Held'; Mutex = $null; Owned = $false }
    }
    $status = 'Acquired'
    if ($abandoned) { $status = 'AcquiredAbandoned' }
    return [pscustomobject]@{ Status = $status; Reason = $status; Mutex = $mutex; Owned = $true }
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

# A protected mutex can be unowned while another process merely holds a handle.
# Keep the conservative legacy Held result, but expose uncertainty to new callers.
function New-LaunchLockPresenceResult {
    param([string]$State, [bool]$AccessDenied = $false, [switch]$Detailed)
    if ($Detailed) { return [pscustomobject]@{State=$State;AccessDenied=$AccessDenied} }
    if ($AccessDenied) { return 'Held' }
    return $State
}

function Get-LaunchLockPresence {
    param([Parameter(Mandatory)][string]$Name, [switch]$Detailed)
    $mutex = $null
    try {
        if (-not [Threading.Mutex]::TryOpenExisting($Name, [ref]$mutex)) { return (New-LaunchLockPresenceResult 'Absent' -Detailed:$Detailed) }
    } catch [UnauthorizedAccessException] {
        return (New-LaunchLockPresenceResult 'Unknown' -AccessDenied $true -Detailed:$Detailed)
    }
    try {
        $got = $false
        try { $got = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $got = $true }
        catch [UnauthorizedAccessException] { return (New-LaunchLockPresenceResult 'Unknown' -AccessDenied $true -Detailed:$Detailed) }
        if ($got) {
            $mutex.ReleaseMutex()
            return (New-LaunchLockPresenceResult 'Free' -Detailed:$Detailed)
        }
        return (New-LaunchLockPresenceResult 'Held' -Detailed:$Detailed)
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

# A legacy record also carries process creation time in started; PID alone is
# never enough to identify an owner after a crash or reboot.
function Get-LaunchStateOwner {
    param([AllowNull()][object]$State)
    if (-not $State) { return $null }
    $ownerId = if ($State.PSObject.Properties['ownerPid']) { $State.ownerPid } elseif ($State.PSObject.Properties['pid']) { $State.pid } else { $null }
    $ownerStart = if ($State.PSObject.Properties['ownerStartedUtc']) { $State.ownerStartedUtc } elseif ($State.PSObject.Properties['started']) { $State.started } else { $null }
    if (-not $ownerId -or -not $ownerStart) { return $null }
    try {
        $candidate = Get-Process -Id ([int]$ownerId) -ErrorAction Stop
        $started = [DateTimeOffset]::Parse([string]$ownerStart, [Globalization.CultureInfo]::InvariantCulture)
        if (-not $candidate.HasExited -and [Math]::Abs(($candidate.StartTime.ToUniversalTime() - $started.UtcDateTime).TotalMilliseconds) -lt 10) { return $candidate }
    } catch { }
    return $null
}

# True while a non-terminal startup keeps its heartbeat fresh and its process is
# alive. The worker lock is authoritative; this only explains who holds it.
function Test-LaunchStateLive {
    param(
        [AllowNull()][object]$State,
        [datetime]$Now = (Get-Date),
        [int]$StaleSeconds = 90
    )
    if ($null -eq $State -or -not $State.PSObject.Properties['phase'] -or
            $State.phase -in @('finished', 'failed', 'cancelled') -or -not $State.PSObject.Properties['heartbeat']) { return $false }
    # Windows PowerShell keeps ISO strings; PowerShell 7 converts them to DateTime.
    $heartbeat = $State.heartbeat
    if ($heartbeat -isnot [datetime]) {
        $parsed = [datetime]::MinValue
        if (-not [datetime]::TryParse([string]$heartbeat, [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) { return $false }
        $heartbeat = $parsed
    }
    $age = ($Now - $heartbeat.ToLocalTime()).TotalSeconds
    if ($age -gt $StaleSeconds -or $age -lt -5) { return $false }
    return $null -ne (Get-LaunchStateOwner $State)
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

function Format-LaunchLockBlockedMessage {
    param([bool]$AccessDenied, [AllowNull()][object]$State)
    if ($AccessDenied) {
        $message = 'Windows denied access to the WuWa startup lock. This does not confirm a running launch. Nothing else was started.'
    } else {
        $message = 'Another startup owns the WuWa launch lock. No second launch was started.'
    }
    # A reused PID or an old copied state file cannot identify a stoppable worker.
    $owner = if ($State -and $State.phase -notin @('finished','failed','cancelled','first-frame','firstFrame')) { Get-LaunchStateOwner $State } else { $null }
    if ($owner -and $State.PSObject.Properties['cancelPath'] -and $State.cancelPath) {
        if (Test-LaunchStateLive $State) {
            $message += ' The recorded startup is still running (PID ' + $owner.Id + '). Use Stop waiting to request cancellation; this does not confirm it has stopped, and the game is not closed.'
        } else {
            $message += ' The previous startup worker is still alive (PID ' + $owner.Id + ') but has stopped reporting progress. A new launch has not started.'
        }
    } else {
        $message += ' The lock owner could not be identified; an old saved launch status is not evidence of an active launch.'
    }
    return ($message + ' Open Troubleshooting > Stuck launcher processes to inspect the worker and confirm recovery. Copy diagnostics if no matching worker is found.')
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

# One attempt owns its diagnostic folder and cancellation marker before UAC.
# These are launcher files only; no game/profile/runtime operation happens here.
function New-LaunchAttemptState {
    param([Parameter(Mandatory)][string]$Path, [string]$AttemptId = ([guid]::NewGuid().ToString('N')),
        [string]$BuildId, [string]$Phase = 'preflight', [string]$Message = 'Checking launch readiness.')
    if ($AttemptId -notmatch '^[a-fA-F0-9]{32}$') { throw 'Invalid launch attempt identity.' }
    $run = Join-Path (Split-Path -Parent $Path) ('runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $AttemptId)
    $null = New-Item -ItemType Directory -Path $run -Force
    Write-LaunchState -Path $Path -State ([ordered]@{
        attemptId=$AttemptId; requestedAt=[DateTime]::UtcNow.ToString('o'); pid=$PID;
        ownerPid=$PID; ownerStartedUtc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o');
        started=(Get-Process -Id $PID).StartTime.ToString('o'); buildId=$BuildId;
        runDir=$run; cancelPath=(Join-Path $run 'cancel.request'); phase=$Phase; message=$Message;
        elevated=$false; injectorStarted=$false
    })
    return Read-LaunchState -Path $Path
}

function Assert-LaunchNotCancelled {
    param([AllowEmptyString()][string]$CancelPath)
    if ($CancelPath -and (Test-Path -LiteralPath $CancelPath)) {
        throw [OperationCanceledException]::new('Launch cancelled. No further game or injector startup was requested.')
    }
}

function Assert-LaunchUserIdentity {
    param([Parameter(Mandatory)][string]$ExpectedSid,
        [string]$CurrentSid = ([Security.Principal.WindowsIdentity]::GetCurrent().User.Value))
    if ($ExpectedSid -notmatch '^S-1-[0-9-]+$' -or $ExpectedSid -cne $CurrentSid) {
        throw 'Windows permission was granted using a different user account. Nothing was launched. Open WuWa VR while signed in to the same Windows account that grants administrator permission; settings cannot safely be shared across those accounts.'
    }
}

# Checks are read-only. A launch records progress and errors even if its hidden
# elevated worker exits before loading the rest of the packaged scripts.
function Invoke-LaunchFrontEnd {
    param(
        [Parameter(Mandatory)][string]$WorkerLockName,
        [Parameter(Mandatory)][string]$RequestLockName,
        [Parameter(Mandatory)][string]$StatePath,
        [Parameter(Mandatory)][scriptblock]$Preflight,
        [Parameter(Mandatory)][scriptblock]$Elevate,
        [switch]$CheckOnly,
        [int]$HandoffTimeoutSeconds = 30,
        [string]$AttemptId = ([guid]::NewGuid().ToString('N')),
        [string]$BuildId
    )
    $state = Read-LaunchState -Path $StatePath
    $presence = Get-LaunchLockPresence -Name $WorkerLockName -Detailed
    if ($presence.AccessDenied -or $presence.State -eq 'Held') {
        $detail = Format-LaunchLockBlockedMessage -AccessDenied $presence.AccessDenied -State $state
        return (New-LaunchFrontEndResult 'Busy' 2 $detail)
    }
    if ($CheckOnly) {
        try { & $Preflight }
        catch { return (New-LaunchFrontEndResult 'PreflightFailed' 1 ('Not started: ' + $_.Exception.Message)) }
        return (New-LaunchFrontEndResult 'CheckPassed' 0 'PASS: selected build files, game/injector state and active OpenXR runtime verified. Nothing changed.')
    }
    $request = Enter-LaunchLock -Name $RequestLockName
    if ($request.Status -eq 'Busy') {
        if ($request.Reason -eq 'AccessDenied') {
            return (New-LaunchFrontEndResult 'PromptPending' 3 'Windows denied access to the WuWa permission-request lock. A waiting administrator prompt could not be confirmed. Nothing was started. Open Troubleshooting > Stuck launcher processes to inspect and confirm recovery, or copy diagnostics if no matching worker is found.')
        }
        return (New-LaunchFrontEndResult 'PromptPending' 3 'A Windows administrator prompt is already waiting for an answer. Answer or cancel that prompt; no second prompt was opened.')
    }
    $attemptCreated=$false
    try {
        # The previous request can hand its lock over while this caller is
        # acquiring ours. Recheck before replacing its live worker's status.
        $presence = Get-LaunchLockPresence -Name $WorkerLockName -Detailed
        if ($presence.AccessDenied -or $presence.State -eq 'Held') {
            return (New-LaunchFrontEndResult 'Busy' 2 (Format-LaunchLockBlockedMessage -AccessDenied $presence.AccessDenied -State (Read-LaunchState -Path $StatePath)))
        }
        $null = New-LaunchAttemptState -Path $StatePath -AttemptId $AttemptId -BuildId $BuildId
        $attemptCreated=$true
        try { & $Preflight }
        catch {
            $message = 'Not started: ' + $_.Exception.Message
            Update-LaunchState $StatePath @{phase='failed';outcome='PreflightFailed';message=$message}
            return (New-LaunchFrontEndResult 'PreflightFailed' 1 $message)
        }
        Update-LaunchState $StatePath @{phase='permission-pending';message='Waiting for Windows administrator permission. Answer the Windows prompt; cancelling leaves the game closed.'}
        $child = & $Elevate
        $childId = ''
        if ($child -and $child.Id) { $childId = " (PID $($child.Id))" }
        $handoff = Wait-LaunchHandoff -Child $child -WorkerLockName $WorkerLockName -TimeoutSeconds $HandoffTimeoutSeconds
        $latest = Read-LaunchState -Path $StatePath
        if ($latest -and $latest.PSObject.Properties['attemptId'] -and $latest.attemptId -ne $AttemptId) {
            return (New-LaunchFrontEndResult 'Superseded' 1 'A newer launch attempt owns the status. This attempt did not replace it; follow the current launch progress.')
        }
        # Preserve the worker's concrete failure instead of replacing it with
        # an optimistic generic launch result or a stale previous attempt.
        if ($latest -and $latest.PSObject.Properties['attemptId'] -and $latest.attemptId -eq $AttemptId -and $latest.phase -in @('failed','cancelled')) {
            return (New-LaunchFrontEndResult 'ChildExited' 1 ([string]$latest.message))
        }
        if ($handoff -eq 'Exited') {
            $code = ''; try { $code = ' Exit code: ' + $child.ExitCode + '.' } catch { }
            $message = "The elevated startup$childId exited before it began.$code Open Troubleshooting and copy diagnostics. Check Windows Security protection history if it blocked a packaged file; do not disable security software."
            Update-LaunchState $StatePath @{phase='failed';outcome='ChildExited';message=$message}
            return (New-LaunchFrontEndResult 'ChildExited' 1 $message)
        }
        if ($handoff -eq 'Timeout') {
            $message = "Startup$childId has not confirmed it is ready after $HandoffTimeoutSeconds s. Use Stop waiting to cancel, or open Troubleshooting and copy diagnostics. Do not start another injector."
            $values = @{phase='handoff-unconfirmed';outcome='StartedUnconfirmed';message=$message}
            if ($child -is [Diagnostics.Process]) {
                try { $values.pid=$child.Id; $values.started=$child.StartTime.ToString('o'); $values.ownerPid=$child.Id; $values.ownerStartedUtc=$child.StartTime.ToUniversalTime().ToString('o') } catch { }
            }
            Update-LaunchState $StatePath $values
            return (New-LaunchFrontEndResult 'StartedUnconfirmed' 5 $message)
        }
        return (New-LaunchFrontEndResult 'Started' 0 'Windows permission accepted. Follow launch progress here; Steam opens the selected game, while Kuro may require pressing Play in its launcher.')
    } catch {
        $message = 'Could not open the elevated startup: ' + $_.Exception.Message
        $outcome='ElevationFailed'; $code=1; $phase='failed'
        if (Test-ElevationCancelled $_.Exception) {
            $message='Windows administrator permission was declined. The game was not launched; use Launch in VR again when ready.'
            $outcome='ElevationCancelled'; $code=4; $phase='cancelled'
        }
        try {
            $owned=Read-LaunchState $StatePath
            if ($attemptCreated -and $owned -and $owned.PSObject.Properties['attemptId'] -and $owned.attemptId -eq $AttemptId) {
                Update-LaunchState $StatePath @{phase=$phase;outcome=$outcome;message=$message}
            }
        } catch { }
        return (New-LaunchFrontEndResult $outcome $code $message)
    } finally { Exit-LaunchLock $request }
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

function Get-LaunchProcessExitDetail {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process)
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) { return '' }
        $code=[int]$Process.ExitCode
        $unsigned=[BitConverter]::ToUInt32([BitConverter]::GetBytes($code),0)
        return (' Injector exit code: {0} (0x{1:X8}).' -f $code,$unsigned)
    } catch { return '' }
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
