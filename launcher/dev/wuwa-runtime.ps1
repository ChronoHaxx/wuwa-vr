[CmdletBinding()]
param([Parameter(Mandatory)][ValidateSet('simulator','headset')][string]$Mode,
      [Parameter(Mandatory)][AllowEmptyString()][string]$ExpectedActive,[switch]$Elevated,[string]$DataRoot,
      [ValidatePattern('^[a-f0-9]{32}$')][string]$RequestId=([guid]::NewGuid().ToString('N')))
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'WuWaOpenXR.ps1')
. (Join-Path $PSScriptRoot 'WuWaBuildProfiles.ps1')
. (Join-Path $PSScriptRoot 'WuWaLaunchLifecycle.ps1')
$runtimeRoot=Split-Path -Parent $PSScriptRoot
if ($DataRoot) {
    if (-not [IO.Path]::IsPathRooted($DataRoot) -or $DataRoot -match '["\r\n]') { throw 'Invalid launcher data folder.' }
    $resultPath=Join-Path $DataRoot ('runtime\'+$RequestId+'.json')
} else { $resultPath=Join-Path $runtimeRoot ('extracted\build-manager\runtime\'+$RequestId+'.json') }
if ($ExpectedActive -match '["\r\n]') { throw 'Invalid active runtime path.' }
function Assert-RuntimeReady {
    Assert-WuWaProfileIdle
    $v=Get-WuWaOpenXRValues
    Assert-WuWaExpectedRuntime $ExpectedActive $v
    Get-WuWaRuntimePlan $Mode $runtimeRoot $v
}
$lock=$null
try {
    $plan=Assert-RuntimeReady
    if (-not $plan.Changed) { Write-Output 'The selected OpenXR runtime is already active.'; exit 0 }
    $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $admin) {
        if ($Elevated) { throw 'Windows did not grant permission. Nothing changed.' }
        $lock=Enter-LaunchLock -Name 'Local\WuWaVRRenderingTestElevationRequest'
        if ($lock.Status -eq 'Busy') {
            if ($lock.Reason -eq 'AccessDenied') { throw 'Windows denied access to the WuWa permission-request lock. A waiting administrator prompt could not be confirmed. No runtime change was made. Open Troubleshooting and copy diagnostics; if this persists after closing WuWa VR, restart Windows.' }
            throw 'Another Windows launch/runtime prompt is already open. Answer or cancel that prompt, then retry.'
        }
        # A fixed script and enum are the only operation. Manifest paths are
        # validated before quoting and compared again after elevation.
        $info=[Diagnostics.ProcessStartInfo]::new((Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'))
        $info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Mode '+$Mode+' -ExpectedActive "'+$ExpectedActive+'" -Elevated -RequestId '+$RequestId
        if ($DataRoot) { $info.Arguments+=' -DataRoot "'+$DataRoot+'"' }
        $info.Verb='runas'; $info.UseShellExecute=$true; $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $child=[Diagnostics.Process]::Start($info); $child.WaitForExit()
        if (-not (Test-Path -LiteralPath $resultPath)) { throw 'Windows permission was cancelled or the runtime helper did not finish. Refresh to check the current runtime.' }
        $result=Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        if ($child.ExitCode -ne 0 -or -not $result.ok) { throw $result.message }
        if ((Get-WuWaOpenXRValues).Active -ne $result.target) { throw 'Runtime changed again after the request. Refresh the dashboard.' }
        Write-Output $result.message
        exit 0
    }
    $lock=Enter-LaunchLock -Name 'Local\WuWaVRRenderingTestLaunch'
    if ($lock.Status -eq 'Busy') { throw (Format-LaunchLockBlockedMessage -AccessDenied ($lock.Reason -eq 'AccessDenied') -State $null) }
    $profileLock=Enter-LaunchLock -Name 'Local\WuWaVRBuildProfileSwitch'
    try {
        if ($profileLock.Status -eq 'Busy') {
            if ($profileLock.Reason -eq 'AccessDenied') { throw 'Windows denied access to the WuWa profile lock. A running profile change could not be confirmed. No runtime change was made. Open Troubleshooting and copy diagnostics; if this persists after closing WuWa VR, restart Windows.' }
            throw 'A build/profile change is already running. No runtime change was made. Open Troubleshooting and copy diagnostics if it does not finish.'
        }
        $plan=Assert-RuntimeReady
        Invoke-WuWaRuntimePlan $plan
    } finally { if ($profileLock) { Exit-LaunchLock $profileLock } }
    $result=[ordered]@{ok=$true;time=[DateTimeOffset]::Now.ToString('o');mode=$Mode;before=$plan.Before;target=$plan.Target;message=('OpenXR runtime selected: '+$plan.Target+'. Applies to the next game launch.');requestId=$RequestId}
    New-Item -ItemType Directory -Path (Split-Path -Parent $resultPath) -Force | Out-Null
    $result | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding utf8
    Write-Output $result.message
} catch {
    $message=$_.Exception.Message
    if ($Elevated) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $resultPath) -Force | Out-Null
        [ordered]@{ok=$false;message=$message;requestId=$RequestId} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding utf8
    }
    Write-Output $message
    exit 1
} finally { if ($lock) { Exit-LaunchLock $lock } }
