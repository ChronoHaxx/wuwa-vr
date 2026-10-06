param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$checkout=Split-Path -Parent $PSScriptRoot
. (Join-Path $checkout 'launcher/dev/WuWaLaunchLifecycle.ps1')
. (Join-Path $checkout 'launcher/dev/WuWaBuildProfiles.ps1')
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$script:count=0
function Check([bool]$Value,[string]$Message) { if(-not $Value){throw $Message};$script:count++;Write-Output ('PASS '+$Message) }
function Write-Fixture([string]$Path,[string]$Text) {
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
$log=Join-Path $root 'window.log'
Write-Fixture $log ('header-start'+('x'*700000)+'tail-error')
$window=Read-LaunchBackendLogWindow $log
Check ($window.Truncated -and $window.Text.StartsWith('header-start') -and $window.Text.EndsWith('tail-error') -and $window.Text.Length -lt 525000) 'large backend logs retain bounded head and tail'
$small=Read-LaunchBackendLogWindow $log -From 700012
Check ($small.Text -eq 'tail-error' -and -not $small.Truncated) 'prelaunch offset excludes old bytes'
Check ((Read-LaunchBackendLogWindow $log -From 900000).Text -eq '') 'truncated log never reuses old offset contents'
$snapshot=Join-Path $root 'saved';$null=New-Item -ItemType Directory -Path $snapshot
Save-LaunchBackendLogSnapshot $snapshot $window 'fixture-header' $log
$meta=Get-Content (Join-Path $snapshot 'backend-log.json') -Raw | ConvertFrom-Json
Check ($meta.truncated -and $meta.sourceLength -eq 700022 -and $meta.sourceHeader -eq 'fixture-header') 'snapshot keeps exact source identity and truncation metadata'
Check ((Get-LaunchBackendError '[info] OpenVR failed to load: headset unavailable') -eq '') 'normal OpenVR fallback is not a backend startup error'
Check ((Get-LaunchBackendError '[error] Failed to find optional widget') -eq '') 'unrelated error does not become a startup failure'
Check ((Get-LaunchBackendError '[error] [VR] Could not create openxr instance: -51') -match '-51') 'specific OpenXR failure remains actionable'
Check ((Get-LaunchBackendError '[error] Initialization of mods failed. Reason: unsupported engine') -match 'unsupported engine') 'mod initialization reason retained'
Check ((Get-LaunchBackendError "[error] Failed to get type info: unknown exception`n[info] Hooked DirectX 12") -eq '') 'one RTTI warning and dummy hook do not imply renderer failure'
$retryLog=("[info] Sending rehook request for D3D`n[error] Failed to get type info: unknown exception`n[info] Hooked DirectX 12`n" * 3)
Check ((Get-LaunchBackendError $retryLog) -match 'has not attached to the game renderer') 'persistent graphics retries expose renderer attachment stall'
Check ((Get-LaunchBackendError ($retryLog+"[info] Framework initialized`n")) -eq '') 'real renderer initialization clears prior retry warning'
Check ((Get-LaunchBackendError ($retryLog+"[info] Attempting to initialize DirectX 12`n[info] Sending rehook request for D3D`n")) -match 'has not attached to the game renderer') 'an attempted API initialization does not erase an unresolved attachment stall'
Check ((Get-LaunchBackendError ("[error] Failed to initialize Framework on DirectX 11`n" * 60)) -eq '') 'normal Framework warmup does not falsely identify a DirectX 11 failure'
$probeMiss="[info] [WuWaD3DProbe] No real Present reached the D3D12 probe; trying D3D11`n"
Check ((Get-LaunchBackendError ($probeMiss * 3)) -match 'retry events: 3;') 'actual native no-Present wording is classified without other retry messages'
$probeCycle=$probeMiss+"[error] Failed to initialize Framework on DirectX 11`n[info] Attempting to initialize DirectX 11`n[info] Device or SwapChain null. DirectX 12 may be in use. Unhooking D3D11...`n"
Check ((Get-LaunchBackendError ($probeCycle * 3)) -match 'has not attached to the game renderer') 'repeated wrong-API probes report attachment failure without claiming game API'
Check ((Get-LaunchBackendError ($probeCycle + "[error] [VR] Could not create openxr session: -7`n")) -match '-7') 'specific runtime error retains priority over earlier graphics probing'
Check ((Get-LaunchBackendError '[error] Failed to init D3D12') -match 'Failed to init D3D12') 'specific renderer initialization failure remains visible'
Check ((Get-LaunchBackendError (($probeCycle * 3)+"[info] Framework initialized`n")) -eq '') 'successful Framework initialization clears API-probe retry warning'
Check ((Get-LaunchWaitDecision -FirstFrameSeen $true -TimedOut $true).Action -eq 'Ready') 'recovery evidence wins instead of failing on an earlier logged error'
Check ((Get-LaunchWaitDecision -FirstFrameSeen $true -BackendShuttingDown $true).Action -eq 'BackendStopped') 'positive shutdown overrides earlier projection evidence'
Check ((Get-LaunchWaitDecision -FirstFrameSeen $true -BackendShuttingDown $true -CancelRequested $true).Action -eq 'Cancelled') 'explicit cancellation remains authoritative during shutdown or apparent readiness'
Check ((Get-LaunchWaitDecision -GameSeen $true -GameRunning $false -GameStateUnknown $true -GameGoneSeconds 90).Action -eq 'Continue') 'ambiguous process observation alone never proves game exit'
Check ((Get-LaunchWaitDecision -TargetExitConfirmed $true -GameStateUnknown $true -FirstFrameSeen $true).Action -eq 'GameExited') 'original signaled process handle overrides stale projection and unknown enumeration'
Check ((Get-LaunchWaitDecision -TargetExitConfirmed $true -CancelRequested $true).Action -eq 'Cancelled') 'explicit cancellation remains authoritative over confirmed game exit'
$script:lostEnumerationPolls=0
$missingEnumeration=Wait-LaunchOutcome -TimeoutSeconds 1 -PollMilliseconds 10 -GameGoneGraceSeconds 0 -Observe {
    $script:lostEnumerationPolls++
    $present=$script:lostEnumerationPolls -eq 1
    @{GameRunning=$present;TargetRunning=$present;LauncherRunning=$false;InjectorRunning=$false;
        BackendLogStarted=$true;FirstFrameSeen=$false;SteamTargetCount=$(if($present){1}else{0});TargetVerificationLost=$false}
}
Check ($missingEnumeration.Action -eq 'TimedOut' -and $missingEnumeration.GameSeen) 'actual wait never treats missing Steam enumeration and unreadable log as confirmed process exit'
# Run the actual sim-run script and actual worker delegate statements in an
# isolated package. Only system boundaries and wait duration are replaced.
# Production Steam path validation and argument binding still run.
$source=Join-Path $checkout 'launcher/dev'
$dev=Join-Path $root 'app/dev';$null=New-Item -ItemType Directory -Path $dev -Force
foreach($file in @('sim-run.ps1','WuWaLaunchLifecycle.ps1','WuWaSteamStart.ps1','Get-SceneCaptureCheck.ps1','Get-RunContinuityCheck.ps1')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination (Join-Path $dev $file)
}
$lifecycleMocks=@'
function Stop-OwnedInjector { param($Process,$GameNames) 'LeftRunning' }
$script:ProductionWaitLaunchOutcome=${function:Wait-LaunchOutcome}
function Wait-LaunchOutcome {
    param($Observe,$TimeoutSeconds,$InjectorExpected,$LauncherExpected,$CancelRequested,$OnPoll,$OnLauncherClosed,$LauncherGraceSeconds)
    $global:FixtureGameVisible=$true
    $prior=& $Observe
    if ($prior.FirstFrameSeen -or $prior.BackendLogStarted -or $prior.BackendShuttingDown -or (Test-Path -LiteralPath (Join-Path $global:FixtureRun 'backend.log'))) { throw 'Previous log was attributed to this attempt' }
    [IO.File]::WriteAllText($global:FixtureProfileLog,"[2026-10-06 15:00:00.000] [info] UnrealVR entry`r`n[error] [VR] Could not create openxr instance: -51`r`n")
    if($global:FixtureMode -eq 'steam') {
        $selected=$global:FixtureShipping;$global:FixtureShipping='C:\Other\Client-Win64-Shipping.exe'
        $wrong=& $Observe
        $global:FixtureShipping=$selected
        if($wrong.BackendLogStarted -or $wrong.FirstFrameSeen -or (Test-Path -LiteralPath (Join-Path $global:FixtureRun 'backend.log'))) { throw 'Another game installation log was attributed to Steam target' }
    }
    $current=& $Observe
    if (-not $current.BackendLogStarted -or $current.FirstFrameSeen -or $current.BackendError -notmatch '-51') { throw 'Fresh backend error not observed correctly' }
    if($global:FixtureSpecial) {
        if($global:FixtureSpecial -like 'settle-*') {
            [IO.File]::AppendAllText($global:FixtureProfileLog,"[info] Framework initialized`r`n[info] Creating OpenXR swapchains for D3D12`r`n[info] Original FOV for left eye: -1, 1, 1, -1`r`n[info] Original FOV for right eye: -1, 1, 1, -1`r`n[info] Derived texture bounds left eye: 0, 1, 0, 1`r`n[info] Derived texture bounds right eye: 0, 1, 0, 1`r`n[info] Found FSceneView constructor at 0x1234`r`n")
            if($global:FixtureSpecial -ne 'settle-pending') {
                [IO.File]::AppendAllText($global:FixtureProfileLog,"[info] right-eye composite: capture=100x100 game=200x100 expected_eye=100x100 capture_resource=0x1`r`n")
            }
        } elseif($global:FixtureSpecial -like 'confirmed-exit*') {
            $global:FixtureGameVisible=$false
            if($global:FixtureSpecial -eq 'confirmed-exit-cancel') { [IO.File]::WriteAllText((Join-Path $global:FixtureRun 'cancel.request'),'stop') }
        } elseif($global:FixtureSpecial -like 'shutdown*') {
            [IO.File]::AppendAllText($global:FixtureProfileLog,"[info] Framework initialized`r`n[info] Derived texture bounds right eye: 0, 1, 0, 1`r`n[info] Framework shutting down...`r`n")
            $global:FixtureGameVisible=$global:FixtureSpecial -eq 'shutdown-live'
        } else {
            $global:FixtureProcessMode='unreadable'
            if($global:FixtureSpecial -eq 'empty-cancel') { $global:FixtureGameVisible=$false }
            if($global:FixtureSpecial -like '*cancel') { [IO.File]::WriteAllText((Join-Path $global:FixtureRun 'cancel.request'),'stop') }
        }
        # Exercise the production wait decision and cancellation loop, with
        # actual sim-run observation/state writes; only OS boundaries are fake.
        $result=& $script:ProductionWaitLaunchOutcome -Observe $Observe -TimeoutSeconds 0 -PollMilliseconds 1 `
            -InjectorExpected $InjectorExpected -LauncherExpected $LauncherExpected -CancelRequested $CancelRequested `
            -OnPoll $OnPoll -GameGoneGraceSeconds 0
        if($global:FixtureSpecial -eq 'settle-cancel') { [IO.File]::WriteAllText((Join-Path $global:FixtureRun 'cancel.request'),'stop') }
        if($global:FixtureSpecial -like 'settle-*') { $global:FixtureSettleActive=$true }
        [IO.File]::AppendAllText($global:FixtureProfileLog,"[error] Initialization of mods failed. Reason: final fixture detail`r`n")
        return $result
    }
    if($global:FixtureLoss) {
        [IO.File]::AppendAllText($global:FixtureProfileLog,"[info] Framework initialized`r`n[info] Creating OpenXR swapchains for D3D12`r`n[info] Derived texture bounds right eye: 0, 1, 0, 1`r`n")
        $global:FixtureProcessMode=$global:FixtureLoss
        $lost=& $Observe
        if($lost.FirstFrameSeen -or -not $lost.BackendEvidencePresent -or -not $lost.BackendRendererInitialized -or -not $lost.BackendProjectionSeen -or -not $lost.TargetVerificationLost) { throw 'Lost identity erased attributed renderer evidence or falsely declared Ready' }
        if($global:FixtureLoss -eq 'unreadable' -and ($lost.SteamTargetUnverifiedCount -ne 1 -or $lost.SteamTargetCount -ne 0)) { throw 'Unreadable process identity was not recorded' }
        if($global:FixtureLoss -eq 'duplicate' -and ($lost.SteamTargetCount -ne 2 -or $lost.SteamTargetProcesses.Count -ne 2)) { throw 'Ambiguous process identities were not recorded' }
        $global:FixtureProcessMode='normal'
        $recovered=& $Observe
        if(-not $recovered.FirstFrameSeen -or $recovered.TargetVerificationLost) { throw 'Recovered exact identity did not restore readiness' }
        $global:FixtureProcessMode=$global:FixtureLoss
        $current=& $Observe
    }
    $decision=[pscustomobject]@{Action='GameExited';Detail='The game closed before VR startup completed.'}
    & $OnPoll $current $decision $true
    [IO.File]::AppendAllText($global:FixtureProfileLog,"[error] Initialization of mods failed. Reason: final fixture detail`r`n")
    if($global:FixtureMode -eq 'manual') { [IO.File]::WriteAllText($global:FixtureProfileLog,'[different session] unrelated replacement') }
    [pscustomobject]@{Action='GameExited';Detail=$decision.Detail;Observation=$current;GameSeen=$true;ElapsedSeconds=0}
}
'@
Add-Content -LiteralPath (Join-Path $dev 'WuWaLaunchLifecycle.ps1') -Value $lifecycleMocks
$steamMocks=@'
function Assert-WuWaSteamInjector { param($Injector) }
function Get-WuWaSteamClient { 'C:\fixture\Steam.exe' }
function Start-WuWaSteamGame { param($Game) [IO.File]::WriteAllText((Join-Path $global:FixtureRun 'dispatch.txt'),$Game.Uri) }
function Get-WuWaSteamProcessIdentity {
    param($Process)
    [pscustomobject]@{Id=$Process.Id;Path=$Process.Path;StartTime=[datetime]'2026-10-06 14:59:59';Verified=[bool]$Process.Path;
        Source='inert-fixture';CreationFileTime=0;Win32Error=0;FailedStep=''}
}
function Get-WuWaSteamRetainedProcessStates {
    param($ExpectedPath)
    if($global:FixtureSpecial -like 'confirmed-exit*' -and -not $global:FixtureGameVisible) {
        [pscustomobject]@{Id=101;CreationFileTime=123456;Exited=$true;ExitCodeKnown=$true;ExitCode=3221225477;Source='signaled-original-handle'}
    }
}
function Get-ItemPropertyValue { param($Path,$LiteralPath,$Name) if($Name -eq 'ActiveRuntime'){$global:FixtureRuntime}else{throw 'Fixture registry value absent'} }
function Get-ItemProperty { param($Path,$LiteralPath) throw 'Unexpected registry read' }
function Test-Path {
    [CmdletBinding()]param([Parameter(Position=0)][string[]]$Path,[string[]]$LiteralPath,[string]$PathType)
    $value=@($LiteralPath);if(-not $LiteralPath){$value=@($Path)}
    if ($value[0] -match '^HK(LM|CU):') { return $false }
    $args=@{LiteralPath=$value};if($PathType){$args.PathType=$PathType}
    Microsoft.PowerShell.Management\Test-Path @args
}
function Get-Process {
    [CmdletBinding()]param($Name,$Id)
    if($global:FixtureGameVisible -and @($Name) -contains 'Client-Win64-Shipping') {
        $readable=if($global:FixtureProcessMode -eq 'unreadable'){$null}else{$global:FixtureShipping}
        [pscustomobject]@{Id=101;ProcessName='Client-Win64-Shipping';Path=$readable}
        if($global:FixtureProcessMode -eq 'duplicate') { [pscustomobject]@{Id=102;ProcessName='Client-Win64-Shipping';Path=$global:FixtureShipping} }
    }
}
function Start-Sleep {
    param([int]$Milliseconds,[int]$Seconds)
    if($global:FixtureSettleActive -and $Milliseconds -ge 500) {
        switch($global:FixtureSpecial) {
            'settle-gone' { $global:FixtureGameVisible=$false }
            'settle-unreadable' { $global:FixtureProcessMode='unreadable' }
            'settle-rotated' { [IO.File]::WriteAllText($global:FixtureProfileLog,'[different session] unrelated replacement') }
            default { throw 'Unexpected sleep during a zero-duration settle fixture' }
        }
        return
    }
    Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds ($Milliseconds + $Seconds * 1000)
}
function Start-Process {
    [CmdletBinding()]param($FilePath,$WorkingDirectory,$WindowStyle,$ArgumentList,[switch]$PassThru,$RedirectStandardOutput,$RedirectStandardError)
    if([IO.Path]::GetFileName($FilePath) -eq 'Custom_UEVR_Injector.exe') {
        [IO.File]::WriteAllText((Join-Path $global:FixtureRun 'injector-args.txt'),[string]$ArgumentList)
        $fake=[pscustomobject]@{Id=31415;HasExited=$false}
        $fake | Add-Member -MemberType ScriptMethod -Name Refresh -Value {}
        return $fake
    }
    if([IO.Path]::GetFileName($FilePath) -eq 'launcher.exe') {
        [IO.File]::WriteAllText((Join-Path $global:FixtureRun 'dispatch.txt'),'launcher')
        return
    }
    throw 'Unexpected process dispatch'
}
'@
Add-Content -LiteralPath (Join-Path $dev 'WuWaSteamStart.ps1') -Value $steamMocks
# Preserve exact production splatting and dispatch rather than recreating them.
$worker=[IO.File]::ReadAllText((Join-Path $source 'start-wuwa-build.ps1'))
$begin=$worker.IndexOf('    $simArguments=@{')
$last=$worker.IndexOf("    & (Join-Path `$PSScriptRoot 'sim-run.ps1') @simArguments",$begin)
if($begin -lt 0 -or $last -lt 0){throw 'Worker delegate block moved; update integration fixture'}
$end=$worker.IndexOf("`n",$last)
$delegate='param($wuwaGameStart,$runtime,$wuwaSelectedOpenXR,$statePath,$cancel,$run)' + "`r`n" + '$wuwaUseHeadset=$false;$NoDialog=$true' + "`r`n" + $worker.Substring($begin,$end-$begin)
# Shorten only the wait duration; the production settle loop and completion
# state writes run unchanged against the fake OS process/log boundaries.
$delegate=$delegate.Replace("    & (Join-Path `$PSScriptRoot 'sim-run.ps1') @simArguments", "    `$simArguments.SettleSeconds=1; if(`$global:FixtureSpecial -in @('settle-ready','settle-pending')) { `$simArguments.SettleSeconds=0 }; & (Join-Path `$PSScriptRoot 'sim-run.ps1') @simArguments")
Write-Fixture (Join-Path $dev 'delegate.ps1') $delegate
$runtime=Join-Path $root 'app/runtime/fixture'
Write-Fixture (Join-Path $runtime 'Custom_UEVR_Injector.exe') 'inert'
$bootstrap=Join-Path $root 'steamapps/common/Wuthering Waves/Wuthering Waves.exe'
$shipping=Join-Path (Split-Path -Parent $bootstrap) 'Client/Binaries/Win64/Client-Win64-Shipping.exe'
Write-Fixture $bootstrap 'inert';Write-Fixture $shipping 'inert'
Write-Fixture (Join-Path $root 'steamapps/appmanifest_3513350.acf') '"appid" "3513350" "installdir" "Wuthering Waves"'
$official=Join-Path $root 'official/launcher.exe';Write-Fixture $official 'inert'
$global:FixtureRuntime=Join-Path $root 'simulator/openxr_simulator.json';Write-Fixture $global:FixtureRuntime '{}'
$global:FixtureShipping=$shipping
$beforeAppData=$env:APPDATA
try {
    $env:APPDATA=Join-Path $root 'profile-root'
    $profile=Join-Path $env:APPDATA 'UnrealVRMod/Client-Win64-Shipping'
    $global:FixtureProfileLog=Join-Path $profile 'log.txt'
    Write-Fixture (Join-Path $profile 'config.txt') "VR_NativeStereoFix=true`r`n"
    Write-Fixture (Join-Path $profile 'injector_config.txt') ('custom_var_urvr_folder='+$runtime)
    foreach($case in @(@{mode='steam';loss=''},@{mode='steam';loss='unreadable'},@{mode='steam';loss='duplicate'},@{mode='launcher';loss=''},@{mode='manual';loss=''},
        @{mode='steam';loss='';special='shutdown-gone'},@{mode='steam';loss='';special='shutdown-live'},
        @{mode='steam';loss='';special='confirmed-exit'},@{mode='steam';loss='';special='confirmed-exit-cancel'},
        @{mode='steam';loss='';special='loss-cancel'},@{mode='steam';loss='';special='empty-cancel'},@{mode='steam';loss='';special='ambiguous-timeout'},
        @{mode='steam';loss='';special='settle-cancel'},@{mode='steam';loss='';special='settle-gone'},
        @{mode='steam';loss='';special='settle-unreadable'},@{mode='steam';loss='';special='settle-rotated'},
        @{mode='steam';loss='';special='settle-ready'},@{mode='steam';loss='';special='settle-pending'})) {
        $mode=$case.mode;$global:FixtureLoss=$case.loss;$global:FixtureProcessMode='normal'
        $global:FixtureSpecial=$case['special']
        $global:FixtureSettleActive=$false
        $global:FixtureMode=$mode
        $global:FixtureRun=Join-Path $root ($mode+'-'+$case.loss+$global:FixtureSpecial);$null=New-Item -ItemType Directory -Path $global:FixtureRun
        $global:FixtureGameVisible=$false
        Write-Fixture $global:FixtureProfileLog "[old attempt] texture bounds right eye`r`n[info] Framework shutting down...`r`n"
        $ctx=[pscustomobject]@{Settings=(Join-Path $global:FixtureRun 'settings.json')}
        $path=if($mode -eq 'steam'){$bootstrap}elseif($mode -eq 'launcher'){$official}else{''}
        Write-Fixture $ctx.Settings (@{gameStart=$mode;gameLauncher=$path}|ConvertTo-Json)
        $settings=Get-WuWaLaunchSettings $ctx
        $state=Join-Path $global:FixtureRun 'launch-state.json';Write-Fixture $state '{}'
        $errorText=''
        try { & (Join-Path $dev 'delegate.ps1') $settings $runtime $global:FixtureRuntime $state (Join-Path $global:FixtureRun 'cancel.request') $global:FixtureRun | Out-Null }
        catch { $errorText=$_.Exception.Message }
        $saved=Get-Content $state -Raw | ConvertFrom-Json
        if($global:FixtureSpecial -eq 'confirmed-exit') {
            Check ($saved.phase -eq 'failed' -and $saved.outcome -eq 'GameExited' -and $saved.targetExitConfirmed -and -not $saved.targetVerificationLost) 'actual observer ends on exact-process exit without relabeling it TargetUnverified'
            Check ($saved.steamTargetCandidateCount -eq 0 -and $saved.steamTargetExitEvidence.pid -eq 101 -and $saved.steamTargetExitEvidence.exitCode -eq 3221225477 -and $errorText -match 'process exited') 'confirmed exit retains unsigned exit code and original identity with empty enumeration'
        } elseif($global:FixtureSpecial -like 'shutdown*') {
            Check ($saved.phase -eq 'failed' -and $saved.outcome -eq 'BackendStopped' -and $saved.backendShuttingDown -and -not $saved.firstFrameSeen -and $errorText -match 'Framework shutdown') ($global:FixtureSpecial+': actual wait ends on attributed shutdown rather than stale progress or readiness')
            Check ($errorText -notmatch 'The game closed|reached renderer initialization, but') ($global:FixtureSpecial+': does not confuse backend shutdown with confirmed game exit or identity-loss failure')
        } elseif($global:FixtureSpecial -eq 'settle-cancel') {
            Check ($saved.phase -eq 'cancelled' -and $saved.firstFrameSeen -and $errorText -match 'Launch cancelled') 'cancellation arriving after readiness prevents the settle window and final Ready result'
        } elseif($global:FixtureSpecial -in @('settle-gone','settle-unreadable','settle-rotated')) {
            Check ($saved.phase -eq 'failed' -and $saved.outcome -eq 'ContinuityLost' -and $saved.continuity -eq 'FAIL') ($global:FixtureSpecial+': actual settle failure cannot leave a Ready result')
            Check ($saved.backendEvidencePresent -and $saved.backendRendererInitialized -and $saved.backendProjectionSeen) ($global:FixtureSpecial+': historical attributed initialization evidence is retained')
            if($global:FixtureSpecial -eq 'settle-gone') {
                Check ($saved.steamTargetCount -eq 0 -and $saved.steamTargetCandidateCount -eq 0 -and $saved.steamTargetProcesses.Count -eq 0 -and $saved.targetVerificationLost) 'settle process disappearance replaces stale PID/count without asserting a crash cause'
            } elseif($global:FixtureSpecial -eq 'settle-unreadable') {
                Check ($saved.steamTargetCount -eq 0 -and $saved.steamTargetCandidateCount -eq 1 -and $saved.steamTargetUnverifiedCount -eq 1 -and $saved.targetVerificationLost) 'settle identity loss remains distinct from a confirmed game exit'
            }
        } elseif($global:FixtureSpecial -eq 'settle-ready') {
            Check ($saved.phase -eq 'finished' -and $saved.outcome -eq 'Ready' -and $saved.continuity -eq 'PASS' -and $saved.checksFailed -eq 0 -and $saved.checksPending -eq 0) 'successful same-process settle and checks retain Ready result'
        } elseif($global:FixtureSpecial -eq 'settle-pending') {
            Check ($saved.phase -eq 'finished' -and $saved.outcome -eq 'StartupChecksPending' -and $saved.checksPending -eq 1) 'incomplete resource observation is pending rather than full Ready result'
        } elseif($global:FixtureSpecial -like '*cancel') {
            Check ($saved.phase -eq 'cancelled' -and $saved.outcome -eq 'Cancelled' -and $errorText -match 'Stopped waiting on request') ($global:FixtureSpecial+': actual wait honors cancellation while selected process identity is unavailable')
        } elseif($global:FixtureSpecial -eq 'ambiguous-timeout') {
            Check ($saved.phase -eq 'failed' -and $saved.outcome -eq 'TargetUnverified' -and $errorText -match 'cannot establish whether the game is still running') 'identity uncertainty times out honestly without claiming game exit or a renderer diagnosis'
        } elseif($case.loss) {
            Check ($errorText -match 'reached renderer initialization' -and $saved.outcome -eq 'TargetUnverified' -and $saved.targetVerificationLost -and $saved.backendRendererInitialized -and -not $saved.firstFrameSeen) ($mode+' '+$case.loss+': identity loss keeps renderer evidence without false Ready or no-VR failure')
            Check ($saved.steamTargetProcesses.Count -gt 0 -and $saved.backendLogCaptured -and $saved.backendProjectionSeen) ($mode+' '+$case.loss+': process identity and previously attributed backend evidence captured')
        } else {
            Check ($errorText -match 'game closed' -and $errorText -match '-51') ($mode+': backend error is included without claiming why game exited; actual='+$errorText)
        }
        Check ($saved.gameStartRequested -eq $mode -and $saved.gameStartEffective -eq $mode) ($mode+': effective startup selection survives actual worker/sim-run delegation')
        $target=[IO.File]::ReadAllText((Join-Path $global:FixtureRun 'injector-args.txt'))
        if($mode -eq 'steam') {
            Check ($target -ceq ('--target-path "'+$shipping+'"') -and [IO.File]::ReadAllText((Join-Path $global:FixtureRun 'dispatch.txt')) -eq 'steam://rungameid/3513350') 'Steam delegates exact-path injector and fixed Steam dispatch'
        } elseif($mode -eq 'launcher') {
            Check ($target -eq 'Client-Win64-Shipping.exe' -and [IO.File]::ReadAllText((Join-Path $global:FixtureRun 'dispatch.txt')) -eq 'launcher') 'Kuro delegates launcher startup'
        } else {
            Check ($target -eq 'Client-Win64-Shipping.exe' -and -not (Test-Path (Join-Path $global:FixtureRun 'dispatch.txt'))) 'Manual start dispatches no game'
        }
        $savedLog=[IO.File]::ReadAllText((Join-Path $global:FixtureRun 'backend.log'))
        if($mode -eq 'manual') {
            Check ($savedLog -match 'Could not create openxr' -and $savedLog -notmatch 'unrelated replacement') 'A rotated log never overwrites saved attempt evidence in finally'
        } elseif($global:FixtureSpecial -eq 'settle-rotated') {
            Check ($savedLog -match 'Creating OpenXR swapchains' -and $savedLog -notmatch 'unrelated replacement') 'a replacement log during settle cannot overwrite the original run evidence'
        } else {
            Check ($savedLog -match 'final fixture detail' -and $savedLog -notmatch 'old attempt') ($mode+': failure finalizer snapshots final backend bytes without old log')
        }
    }
} finally { $env:APPDATA=$beforeAppData }
Write-Output ('PASS '+$script:count+' backend/dispatch checks. Fixtures: '+$root)
