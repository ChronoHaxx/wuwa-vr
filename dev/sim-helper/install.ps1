<#
.SYNOPSIS
    Install (or remove) the WuWa VR dev helper: one UAC prompt now instead of one per test session.

.DESCRIPTION
    For the owner's test PC only; never packaged for players. Copies helper.ps1 into
    C:\ProgramData\WuWaVR-DevHelper (writable by administrators only), pins the paths it may use in
    config.json there, and registers the scheduled task "WuWa VR dev helper", which runs that copy
    with highest privileges for this Windows user. dev/sim-session.py then switches the OpenXR
    runtime and starts the injector and the game without a prompt.

    Trade-off: any program running as this user can ask the task to switch the OpenXR runtime or to
    start the game with the applied build's injector, without a prompt. It cannot run anything else
    or pass paths. Remove it with -Uninstall when you stop testing.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File dev\sim-helper\install.ps1
    powershell -ExecutionPolicy Bypass -File dev\sim-helper\install.ps1 -Uninstall
#>
[CmdletBinding()]
param([switch]$Uninstall, [switch]$Elevated, [ValidatePattern('^S-1-[0-9-]+$')][string]$OriginSid)
$ErrorActionPreference = 'Stop'
$taskName = 'WuWa VR dev helper'
$target = Join-Path $env:ProgramData 'WuWaVR-DevHelper'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$admin = ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $admin) {
    if ($Elevated) { throw 'Windows did not grant administrator rights. Nothing changed.' }
    $arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Elevated -OriginSid ' + $identity.User.Value
    if ($Uninstall) { $arguments += ' -Uninstall' }
    Write-Output 'Asking Windows for administrator rights (one prompt)...'
    $child = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    if ($child.ExitCode -ne 0) { throw ('The elevated step failed (exit ' + $child.ExitCode + '). See ' + (Join-Path $env:TEMP 'wuwa-dev-helper-install.log')) }
    Get-Content -LiteralPath (Join-Path $env:TEMP 'wuwa-dev-helper-install.log')
    exit 0
}

Start-Transcript -LiteralPath (Join-Path $env:TEMP 'wuwa-dev-helper-install.log') -Force | Out-Null
try {
    # Over-the-shoulder credentials would install the task for another account.
    if ($OriginSid -and $OriginSid -ne $identity.User.Value) { throw 'The administrator prompt was answered with a different account. Sign in as the owner and run this again.' }
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false }
    if (Test-Path -LiteralPath $target) {
        if ((Get-Item -LiteralPath $target -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "$target is a link; remove it by hand." }
        # rd removes links inside without following them.
        & cmd.exe /d /c rd /s /q "$target"
        if (Test-Path -LiteralPath $target) { throw "Could not remove $target." }
    }
    if ($Uninstall) { Write-Output 'Removed the WuWa VR dev helper task and its folder.'; return }

    $repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $workspace = Split-Path -Parent $repo
    $steamGame = 'C:\Program Files (x86)\Steam\steamapps\common\Wuthering Waves\Wuthering Waves.exe'
    $kuroGame = 'C:\Program Files\Wuthering Waves\Wuthering Waves Game\Wuthering Waves.exe'
    $headset = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win64.json'
    $config = [ordered]@{
        simulator = Join-Path $workspace 'dev-tools\OpenXR-Simulator\openxr_simulator.json'
        headset = $headset
        kuro = $kuroGame
        steam = $steamGame
        # The client refuses to start without the quality argument its launcher passes ("launch not by
        # commandlet, use kuro quality!"): the Kuro launcher's log shows -krqlv=uhd, Steam's app info -krqlv=hd.
        kuroArguments = '-krqlv=uhd'
        steamArguments = '-krqlv=hd'
        profile = Join-Path $env:APPDATA 'UnrealVRMod\Client-Win64-Shipping'
        # Not under AppData: the Claude desktop app is an MSIX package, and Windows diverts the new
        # AppData folders its shells create into the package's private copy, unseen by this task.
        requests = Join-Path $workspace 'data\dev-helper'
        injectorRoots = @((Join-Path $env:LOCALAPPDATA 'WuWa VR Manager\versions'), (Join-Path $workspace 'builds'))
    }
    foreach ($name in 'simulator', 'headset', 'kuro', 'steam') {
        if (-not (Test-Path -LiteralPath $config[$name] -PathType Leaf)) { Write-Warning ("Not found now, pinned anyway: " + $config[$name]) }
    }

    # Administrators and SYSTEM only may write; Users read the script, config and results.
    New-Item -ItemType Directory -Path $target | Out-Null
    $acl = New-Object Security.AccessControl.DirectorySecurity
    $acl.SetAccessRuleProtection($true, $false)
    $acl.SetOwner([Security.Principal.SecurityIdentifier]'S-1-5-32-544')
    $inherit = [Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
    foreach ($rule in @(@('S-1-5-18', 'FullControl'), @('S-1-5-32-544', 'FullControl'), @('S-1-5-32-545', 'ReadAndExecute'))) {
        $acl.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule ([Security.Principal.SecurityIdentifier]$rule[0]), $rule[1], $inherit, 'None', 'Allow'))
    }
    Set-Acl -LiteralPath $target -AclObject $acl
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'helper.ps1') -Destination (Join-Path $target 'helper.ps1')
    $config | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'config.json') -Encoding UTF8
    New-Item -ItemType Directory -Path (Join-Path $target 'results') | Out-Null

    $user = ([Security.Principal.SecurityIdentifier]$identity.User.Value).Translate([Security.Principal.NTAccount]).Value
    $action = New-ScheduledTaskAction -Execute (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') `
        -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + (Join-Path $target 'helper.ps1') + '"')
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew `
        -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings `
        -Description 'WuWa VR test sessions (dev/sim-session.py): OpenXR runtime switch and game start without a prompt.' | Out-Null
    Write-Output ('Installed "' + $taskName + '" for ' + $user + ' in ' + $target + '.')
    Write-Output 'Remove it any time: powershell -ExecutionPolicy Bypass -File dev\sim-helper\install.ps1 -Uninstall'
} finally { Stop-Transcript | Out-Null }
