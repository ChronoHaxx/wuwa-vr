[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]+$')][string]$Id,[switch]$CheckOnly,[switch]$Elevated,[switch]$NoDialog,[string]$DataRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'WuWaBuildProfiles.ps1')
. (Join-Path $PSScriptRoot 'WuWaLaunchLifecycle.ps1')
. (Join-Path $PSScriptRoot 'WuWaOpenXR.ps1')
$ctx=Get-WuWaBuildContext -DataRoot $DataRoot
$statePath=Join-Path $ctx.Data 'launch-state.json'
$workerLock='Local\WuWaVRRenderingTestLaunch'
$requestLock='Local\WuWaVRRenderingTestElevationRequest'
$scriptPath=$PSCommandPath
$build=@(Get-WuWaBuildCatalog $ctx | Where-Object id -eq $Id)
if($build.Count -ne 1) { throw 'Unknown build.' }
$build=$build[0]
$runtime=Join-Path $ctx.Root $build.runtime
function Test-WuWaBuildReady {
    Assert-WuWaProfileIdle
    $null=Assert-WuWaBuildFiles $ctx $build
    if((Get-WuWaSelectedRuntime $ctx) -ne [IO.Path]::GetFullPath($runtime)) { throw 'Apply this build before launching it.' }
    $selectedRuntime=Get-ItemPropertyValue -LiteralPath 'HKLM:\SOFTWARE\Khronos\OpenXR\1' -Name ActiveRuntime
    $simulator=Join-Path $ctx.Root 'dev-tools\OpenXR-Simulator\openxr_simulator.json'
    Assert-WuWaRuntimeManifest $selectedRuntime
    # A previous portable package can still own the active simulator. Identify
    # that runtime without replacing its registration or saved headset target.
    $script:wuwaUseHeadset=-not ($selectedRuntime -eq $simulator -or (Test-WuWaSimulatorManifest $selectedRuntime))
    $script:wuwaSelectedOpenXR=$selectedRuntime
    $script:wuwaGameStart=Get-WuWaLaunchSettings $ctx
}
$isAdmin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
# The web launcher captures this front end in a hidden child process. Always
# dispatch the worker separately, even if the caller is elevated. Dashboard
# requests report progress/errors on the page instead of opening a console.
if($CheckOnly -or -not $Elevated) {
    $result=Invoke-LaunchFrontEnd -WorkerLockName $workerLock -RequestLockName $requestLock -StatePath $statePath -CheckOnly:$CheckOnly -Preflight { Test-WuWaBuildReady } -Elevate {
        $info=[Diagnostics.ProcessStartInfo]::new((Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'))
        $info.Arguments='-NoProfile -ExecutionPolicy Bypass -File "' + $scriptPath + '" -Id ' + $Id + ' -Elevated'
        if($NoDialog) { $info.Arguments+=' -NoDialog' }
        # UAC does not pass this environment through; name the data folder.
        if($DataRoot) { $info.Arguments+=' -DataRoot "' + $ctx.Data + '"' }
        $info.Verb='runas'; $info.UseShellExecute=$true
        $info.WindowStyle=if($NoDialog) { [Diagnostics.ProcessWindowStyle]::Hidden } else { [Diagnostics.ProcessWindowStyle]::Normal }
        [Diagnostics.Process]::Start($info)
    }
    if($CheckOnly -and $result.Outcome -eq 'CheckPassed') { Write-Output 'PASS: no running game/injector; registered DLL hash, selected build and active OpenXR runtime verified. Nothing changed.' }
    else { Write-Output $result.Message }
    if($result.ExitCode -ne 0 -and -not $NoDialog) { $null=(New-Object -ComObject WScript.Shell).Popup($result.Message,0,'WuWa VR launcher',48) }
    exit $result.ExitCode
}
if(-not $isAdmin) { throw 'Windows did not grant the launch permission. Nothing started.' }
$lock=Enter-LaunchLock -Name $workerLock
if($lock.Status -eq 'Busy') { Write-Output 'A WuWa startup is already running.'; exit 2 }
$failed=$true
$observerStop=$null
try {
    Test-WuWaBuildReady
    $Host.UI.RawUI.WindowTitle='WuWa VR - ' + $build.name
    $run=Join-Path $ctx.Data ('runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $run | Out-Null
    Start-Transcript -LiteralPath (Join-Path $run 'startup.log') | Out-Null
    $cancel=Join-Path $run 'cancel.request'
    Write-LaunchState -Path $statePath -State ([ordered]@{pid=$PID;started=(Get-Process -Id $PID).StartTime.ToString('o');runDir=$run;cancelPath=$cancel;phase='preflight';message=('Launching ' + $build.name);buildId=$Id;elevated=$isAdmin;injectorStarted=$false})
    $build | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'build.json') -Encoding UTF8
    [ordered]@{mode=$(if($wuwaUseHeadset){'headset'}else{'simulator'});manifest=$wuwaSelectedOpenXR} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'openxr.json') -Encoding UTF8
    Write-Host ('OpenXR runtime: '+$wuwaSelectedOpenXR)
    if($wuwaUseHeadset) { Write-Host 'Headset launch: connect Steam Link / your headset runtime before pressing Play. Simulator captures are unavailable.' }
    Copy-Item -LiteralPath (Join-Path $ctx.Profile 'config.txt') -Destination (Join-Path $run 'config-before.txt')
    # Recording starts only after this explicit launch request, never on page
    # load. It reads status/logs and the simulator capture API; no window/input
    # control, setting change, or retry launch is performed by the recorder.
    if($Id -eq 'ours' -and -not $wuwaUseHeadset) {
        $recordingPython=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
        if(Test-Path -LiteralPath $recordingPython) {
            $observerStop=Join-Path $run 'observer-stop.request'
            $recordingOutput=Join-Path $run 'observations'
            $requestedAt=[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
            $recordingArgs='"' + (Join-Path $PSScriptRoot 'observe-wuwa-session.py') + '" "' + $recordingOutput + '" --after-unix ' + $requestedAt + ' --seconds 1800 --stop-file "' + $observerStop + '"'
            try {
                $recorder=Start-Process -FilePath $recordingPython -ArgumentList $recordingArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'observer.stdout.log') -RedirectStandardError (Join-Path $run 'observer.stderr.log')
                [ordered]@{pid=$recorder.Id;started=$recorder.StartTime.ToString('o');output=$recordingOutput;stopFile=$observerStop;scope='read-only simulator/log observations; no acceptance inferred'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'observer.json') -Encoding UTF8
            } catch {
                Write-Warning ('Automatic recording could not start; manual capture remains available: ' + $_.Exception.Message)
            }
        } else { Write-Warning 'Automatic recording Python is unavailable; manual capture remains available.' }
    }
    $simArguments=@{RuntimeName=(Split-Path -Leaf $runtime);Headset=$wuwaUseHeadset;WaitSeconds=600;SettleSeconds=30;StatePath=$statePath;CancelPath=$cancel}
    if(-not $wuwaUseHeadset) { $simArguments.SimulatorPath=Split-Path -Parent $wuwaSelectedOpenXR }
    if($wuwaGameStart.Mode -eq 'manual') {
        Write-Host 'Start Wuthering Waves in your chosen launcher and press Play. Steam/Epic injection is unverified; Steam previously failed and has not been retested.'
    } else {
        $simArguments.StartLauncher=$true
        $simArguments.PromptOnLauncherClosed=(!$NoDialog)
        if($wuwaGameStart.Launcher) { $simArguments.LauncherPath=$wuwaGameStart.Launcher }
    }
    & (Join-Path $PSScriptRoot 'sim-run.ps1') @simArguments
    $after=Read-LaunchState -Path $statePath
    $done=Get-LaunchCompletion -Continuity ([string]$after.continuity) -ChecksFailed ([int]$after.checksFailed) -ChecksPending ([int]$after.checksPending)
    Update-LaunchState -Path $statePath -Values @{phase=$done.Phase;message=$done.Message}
    Copy-Item -LiteralPath $statePath -Destination (Join-Path $run 'startup.json')
    $failed=$done.Failed
    Write-Host $done.Message
} catch {
    Update-LaunchState -Path $statePath -Values @{pid=$PID;phase='failed';message=$_.Exception.Message}
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    if($failed -and $observerStop) { Set-Content -LiteralPath $observerStop -Value 'Startup did not complete.' -Encoding UTF8 }
    Stop-Transcript -ErrorAction SilentlyContinue | Out-Null
    Exit-LaunchLock $lock
}
if($failed) {
    if(-not $NoDialog) { Write-Host 'Press any key to close.'; $null=Read-LaunchKey -TimeoutSeconds 0 }
    exit 1
}
