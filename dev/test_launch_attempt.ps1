param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$checkout=Split-Path -Parent $PSScriptRoot
. (Join-Path $checkout 'launcher/dev/WuWaLaunchLifecycle.ps1')
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$ps=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$script:count=0
function Check([bool]$Condition,[string]$Message) { if(-not $Condition){throw $Message};$script:count++;Write-Output ('PASS '+$Message) }
function New-FixtureCase {
 $id=[guid]::NewGuid().ToString('N')
 return @{WorkerLockName=('Local\WuWaTestWorker'+$id);RequestLockName=('Local\WuWaTestRequest'+$id);StatePath=(Join-Path $root ($id+'/launch-state.json'));AttemptId=$id;BuildId='inert';HandoffTimeoutSeconds=3}
}
function Start-InertScript([string]$Body,[string]$Arguments='') {
 $path=Join-Path $root (([guid]::NewGuid().ToString('N'))+'.ps1')
 [IO.File]::WriteAllText($path,$Body,[Text.UTF8Encoding]::new($true))
 $info=[Diagnostics.ProcessStartInfo]::new($ps)
 $info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$path+'" '+$Arguments
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 return [Diagnostics.Process]::Start($info)
}
$c=New-FixtureCase
$r=Invoke-LaunchFrontEnd @c -CheckOnly -Preflight {} -Elevate {throw 'Must not launch'}
Check ($r.Outcome -eq 'CheckPassed' -and -not (Test-Path -LiteralPath $c.StatePath)) 'CheckOnly remains read-only'
$c=New-FixtureCase
$r=Invoke-LaunchFrontEnd @c -Preflight {throw 'fixture preflight error'} -Elevate {throw 'Must not launch'}
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'PreflightFailed' -and $s.phase -eq 'failed' -and $s.message -like '*fixture preflight error*') 'preflight failure is retained'
Check ($s.attemptId -eq $c.AttemptId -and $s.ownerPid -eq $PID -and $s.ownerStartedUtc -and ([IO.File]::ReadAllText($c.StatePath) -match '"requestedAt"\s*:\s*"[^"]+Z"')) 'attempt identity and UTC owner are recorded'
$c=New-FixtureCase
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate {
 $s=Read-LaunchState $c.StatePath
 if($s.phase -ne 'permission-pending' -or -not (Test-Path -LiteralPath $s.runDir)){throw 'State missing before permission'}
 throw [ComponentModel.Win32Exception]::new(1223)
}
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'ElevationCancelled' -and $s.phase -eq 'cancelled') 'denied UAC records cancellation before any game startup'
$c=New-FixtureCase
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate {throw 'fixture Windows dispatch failed'}
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'ElevationFailed' -and $s.message -like '*fixture Windows dispatch failed*') 'dispatch error remains actionable'
$c=New-FixtureCase
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate { Start-InertScript 'exit 23' }
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'ChildExited' -and $s.phase -eq 'failed' -and $s.message -like '*Exit code: 23*' -and $s.message -notlike '*start-wuwa-rendering*') 'early real child exit is persisted with public recovery and exit code'
$c=New-FixtureCase
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate {
 Update-LaunchState $c.StatePath @{phase='failed';message='fixture exact worker failure'}
 Start-InertScript 'exit 19'
}
Check ($r.Message -eq 'fixture exact worker failure') 'worker error survives frontend handoff'
$c=New-FixtureCase
$replacement=[guid]::NewGuid().ToString('N')
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate {
 Update-LaunchState $c.StatePath @{attemptId=$replacement;phase='preflight';message='newer attempt'}
 Start-InertScript 'exit 17'
}
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'Superseded' -and $s.attemptId -eq $replacement -and $s.message -eq 'newer attempt') 'superseded handoff never replaces a newer attempt'
# A real live child that has not acquired the launch lock must remain distinguishable
# from a completed or failed attempt; it is short lived and does no work.
$c=New-FixtureCase;$c.HandoffTimeoutSeconds=0
$script:inertChild=$null
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate {$script:inertChild=Start-InertScript 'Start-Sleep -Seconds 1';$script:inertChild}
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'StartedUnconfirmed' -and $s.phase -eq 'handoff-unconfirmed' -and $s.ownerPid -eq $script:inertChild.Id -and $s.ownerStartedUtc) 'handoff timeout retains real child ownership instead of claiming completion'
$script:inertChild.WaitForExit();$script:inertChild.Dispose()
# Real successful handoff through the production mutex and state helpers.
$c=New-FixtureCase
$helpers=Join-Path $checkout 'launcher/dev/WuWaLaunchLifecycle.ps1'
$body=@'
param([string]$Helpers,[string]$State,[string]$Mutex)
$ErrorActionPreference='Stop'
. $Helpers
$l=Enter-LaunchLock -Name $Mutex
try {
 Update-LaunchState $State @{phase='preflight';ownerPid=$PID;ownerStartedUtc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')}
 Start-Sleep -Seconds 1
} finally {Exit-LaunchLock $l}
'@
$script:inertChild=$null
$r=Invoke-LaunchFrontEnd @c -Preflight {} -Elevate {
 $script:inertChild=Start-InertScript $body ('"'+$helpers+'" "'+$c.StatePath+'" "'+$c.WorkerLockName+'"')
 $script:inertChild
}
$script:inertChild.WaitForExit()
$s=Read-LaunchState $c.StatePath
Check ($r.Outcome -eq 'Started' -and $s.phase -eq 'preflight' -and $s.ownerPid -eq $script:inertChild.Id) 'successful real handoff retains worker progress and ownership'
$script:inertChild.Dispose()
# Busy checks must preserve the existing launch bytes; mock only the read-only lock probe.
$probe=(Get-Command Get-LaunchLockPresence).ScriptBlock
try {
 function Get-LaunchLockPresence {param($Name,[switch]$Detailed) New-LaunchLockPresenceResult 'Held' -Detailed:$Detailed}
 $before=[IO.File]::ReadAllText($c.StatePath)
 $r=Invoke-LaunchFrontEnd @c -Preflight {throw 'Must not preflight'} -Elevate {throw 'Must not launch'}
 Check ($r.Outcome -eq 'Busy' -and [IO.File]::ReadAllText($c.StatePath) -ceq $before) 'busy refusal preserves the active attempt without replacing it'
} finally { Set-Item -Path Function:Get-LaunchLockPresence -Value $probe }
$script:probeCalls=0
try {
 function Get-LaunchLockPresence {param($Name,[switch]$Detailed) $script:probeCalls++;if($script:probeCalls -eq 1){New-LaunchLockPresenceResult 'Absent' -Detailed:$Detailed}else{New-LaunchLockPresenceResult 'Held' -Detailed:$Detailed}}
 $before=[IO.File]::ReadAllText($c.StatePath)
 $r=Invoke-LaunchFrontEnd @c -Preflight {throw 'Must not preflight'} -Elevate {throw 'Must not launch'}
 Check ($r.Outcome -eq 'Busy' -and $script:probeCalls -eq 2 -and [IO.File]::ReadAllText($c.StatePath) -ceq $before) 'worker acquiring during request-lock handoff cannot have its state overwritten'
} finally { Set-Item -Path Function:Get-LaunchLockPresence -Value $probe }
# A real, unowned mutex with restricted access must not be diagnosed as running
# work. The ACL belongs only to this random fixture object and is restored.
$c=New-FixtureCase
$null=New-LaunchAttemptState -Path $c.StatePath -AttemptId $c.AttemptId -BuildId 'inert'
Update-LaunchState $c.StatePath @{phase='preflight';ownerStartedUtc='2001-01-01T00:00:00Z'}
$before=[IO.File]::ReadAllText($c.StatePath)
$aclMutex=[Threading.Mutex]::new($false,$c.WorkerLockName)
$originalAcl=$null
try {
 $originalAcl=$aclMutex.GetAccessControl()
 $restricted=$aclMutex.GetAccessControl()
 $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User
 $rights=[Security.AccessControl.MutexRights]::Synchronize -bor [Security.AccessControl.MutexRights]::Modify
 $restricted.AddAccessRule([Security.AccessControl.MutexAccessRule]::new($sid,$rights,[Security.AccessControl.AccessControlType]::Deny))
 $aclMutex.SetAccessControl($restricted)
 $legacy=Get-LaunchLockPresence -Name $c.WorkerLockName
 $detail=Get-LaunchLockPresence -Name $c.WorkerLockName -Detailed
 $entry=Enter-LaunchLock -Name $c.WorkerLockName
 $unowned=$aclMutex.WaitOne(0);if($unowned){$aclMutex.ReleaseMutex()}
 Check ($unowned -and $legacy -eq 'Held' -and $detail.State -eq 'Unknown' -and $detail.AccessDenied -and $entry.Status -eq 'Busy' -and $entry.Reason -eq 'AccessDenied') 'real denied unowned mutex remains blocked without claiming a live owner'
 $r=Invoke-LaunchFrontEnd @c -Preflight {throw 'Must not preflight'} -Elevate {throw 'Must not launch'}
 Check ($r.Outcome -eq 'Busy' -and $r.Message -like '*denied access*' -and $r.Message -notlike '*Use Stop waiting*' -and [IO.File]::ReadAllText($c.StatePath) -ceq $before) 'denied lock preserves stale evidence and does not promise an unavailable stop action'
} finally {if($originalAcl){$aclMutex.SetAccessControl($originalAcl)};$aclMutex.Dispose()}
# Only a matching process creation time permits cancellation guidance.
$live=Read-LaunchState $c.StatePath
$live.ownerStartedUtc=(Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
$message=Format-LaunchLockBlockedMessage -AccessDenied $false -State $live
Check ($message -like '*Use Stop waiting*') 'verified recorded owner receives cancellation guidance'
Assert-LaunchUserIdentity -ExpectedSid 'S-1-5-21-1' -CurrentSid 'S-1-5-21-1'
$denied=$false;try {Assert-LaunchUserIdentity -ExpectedSid 'S-1-5-21-1' -CurrentSid 'S-1-5-21-2'}catch{$denied=$_.Exception.Message -like '*different user account*'}
Check $denied 'different-account elevation is rejected explicitly'
$marker=Join-Path $root 'cancel-before-launch.request'
Assert-LaunchNotCancelled -CancelPath $marker
[IO.File]::WriteAllText($marker,'requested')
$cancelled=$false
try {Assert-LaunchNotCancelled -CancelPath $marker}catch [OperationCanceledException] {$cancelled=$true}
Check $cancelled 'queued cancellation is detected by the production dispatch guard'
# Execute the actual readiness function in an inert scope. All external reads
# are stand-ins; this checks an already-selected simulator still gets preflight.
function Test-SelectedSimulatorGuard {
 $ctx=[pscustomobject]@{Root=$root};$build=[pscustomobject]@{};$runtime=Join-Path $root 'runtime'
 $fixtureManifest=Join-Path $root 'other-package/openxr_simulator.json'
 $script:preflightManifest=''
 function Assert-WuWaProfileIdle { }
 function Assert-WuWaBuildFiles { param($Context,$Build) $runtime }
 function Get-WuWaSelectedRuntime { param($Context) $runtime }
 function Get-ItemPropertyValue { param($LiteralPath,$Name) $fixtureManifest }
 function Assert-WuWaRuntimeManifest { param($Path) }
 function Test-WuWaSimulatorManifest { param($Path) $true }
 function Assert-WuWaSimulatorPrerequisites { param($ManifestPath) $script:preflightManifest=$ManifestPath;throw 'fixture simulator dependency missing' }
 function Get-WuWaLaunchSettings { throw 'Settings should not be reached after missing simulator dependencies' }
 $tokens=$null;$errors=$null
 $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $checkout 'launcher/dev/start-wuwa-build.ps1'),[ref]$tokens,[ref]$errors)
 $definition=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Test-WuWaBuildReady'},$true)
 $blocked=$false
 try {& $definition.Body.GetScriptBlock()}catch{$blocked=$_.Exception.Message -like '*fixture simulator dependency missing*'}
 Check ($blocked -and $script:preflightManifest -eq $fixtureManifest) 'already-selected simulator gets dependency preflight before launch'
}
Test-SelectedSimulatorGuard
# Run the actual worker script with a deliberately broken packaged catalog.
# It fails before preflight and never calls game/process/runtime operations.
$package=Join-Path $root 'fixture package';$dev=Join-Path $package 'dev';$null=New-Item -ItemType Directory -Path $dev
foreach($f in @('start-wuwa-build.ps1','WuWaBuildProfiles.ps1','WuWaGraphicsPolicy.ps1','WuWaSteamStart.ps1','WuWaLaunchLifecycle.ps1','WuWaOpenXR.ps1')) {
 Copy-Item -LiteralPath (Join-Path $checkout ('launcher/dev/'+$f)) -Destination (Join-Path $dev $f)
}
[IO.File]::WriteAllText((Join-Path $dev 'wuwa-builds.json'),'{"schema":1,"builds":[]}')
$c=New-FixtureCase;$null=New-LaunchAttemptState -Path $c.StatePath -AttemptId $c.AttemptId -BuildId 'inert'
$origin=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$worker=Join-Path $dev 'start-wuwa-build.ps1';$data=Split-Path -Parent $c.StatePath
$output=& $ps -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $worker -Id inert -Elevated -NoDialog -DataRoot $data -AttemptId $c.AttemptId -OriginUserSid $origin 2>&1
$s=Read-LaunchState $c.StatePath
Check ($LASTEXITCODE -eq 1 -and $s.phase -eq 'failed' -and $s.message -like '*Unknown build*' -and (Test-Path -LiteralPath (Join-Path $s.runDir 'startup-error.txt'))) 'actual hidden worker persists early catalog failure'
$c=New-FixtureCase;$null=New-LaunchAttemptState -Path $c.StatePath -AttemptId $c.AttemptId -BuildId 'inert'
$data=Split-Path -Parent $c.StatePath
$output=& $ps -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $worker -Id inert -Elevated -NoDialog -DataRoot $data -AttemptId $c.AttemptId -OriginUserSid S-1-0-0 2>&1
$s=Read-LaunchState $c.StatePath
Check ($LASTEXITCODE -eq 1 -and $s.message -like '*different user account*') 'actual worker rejects another account before catalog/profile access'
# Numeric Windows startup status stays intact (a real inert child, not an injector).
$child=Start-InertScript 'exit -1073741515';$child.WaitForExit()
$detail=Get-LaunchProcessExitDetail $child;$child.Dispose()
Check ($detail -like '*0xC0000135*') 'native dependency-style exit code remains readable'
Write-Output ('PASS launch-attempt checks: '+$script:count+'; fixtures: '+$root)
