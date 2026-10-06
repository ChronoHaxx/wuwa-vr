[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot, [string]$AssemblyPath)
$ErrorActionPreference='Stop'
# Windows PowerShell 5.1. Only compile the small argument/identity policy, inspect
# optional PE metadata, and write inert text fixtures. No Forms, process discovery,
# injector Main, OpenProcess, DLL loading, game, or player-profile access.
$source=Join-Path $PSScriptRoot '../mod/injector/GUI/TargetPathBinding.cs'
Add-Type -Path $source
$root=Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ('injector-target-'+[Guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $root
$target=Join-Path $root 'Client-Win64-Shipping.exe'
[IO.File]::WriteAllText($target,'Inert text fixture. Never execute.')
$script:checks=0
function Check($pass,$message) { if(-not $pass) { throw $message }; $script:checks++ }
function Reject([scriptblock]$action,$message) {
    $rejected=$false
    try { $null=& $action } catch { $rejected=$true }
    Check $rejected $message
}
function Candidate($pidValue,$created,$path) { [Custom_UEVR_Injector.TargetProcessIdentity]::new($pidValue,$created,$path) }
function Select-Target($items) {
    $script:selectionStatus=''
    [Custom_UEVR_Injector.TargetPathPolicy]::Select($target,[Custom_UEVR_Injector.TargetProcessIdentity[]]$items,[ref]$script:selectionStatus)
}
$empty=[Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@())
Check (-not $empty.IsPathBound -and $null -eq $empty.GameExecutable) 'No-argument manual workflow changed'
$legacy=[Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@('Client-Win64-Shipping.exe'))
Check (-not $legacy.IsPathBound -and $legacy.GameExecutable -eq 'Client-Win64-Shipping.exe') 'Official name-only argument changed'
$request=[Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@('--target-path',$target))
Check ($request.IsPathBound -and $request.TargetPath -ceq $target -and $request.GameExecutable -ceq 'Client-Win64-Shipping.exe') 'Target-path binding was lost'
$script:reads=0
$noRead=[Action[string]]{ param($path) $script:reads++; throw 'Unexpected filesystem read' }
$capability=[Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@('--capabilities'),$noRead)
Check ($capability.CapabilitiesOnly -and $script:reads -eq 0 -and [Custom_UEVR_Injector.InjectorArguments]::Capability -ceq 'WUWA_INJECTOR_TARGET_PATH_V1') 'Capability parsing touched target state'
foreach($argsCase in @(
    ,@('--target-path'), ,@('--target-path',$target,'extra'), ,@('--target-path',$target,'--target-path',$target),
    ,@('--unknown'), ,@('--capabilities',$target), ,@('Client-Win64-Shipping.exe','--target-path',$target),
    ,@('--target-path',''), ,@('--target-path','Client-Win64-Shipping.exe'), ,@('--target-path','C:Client-Win64-Shipping.exe'),
    ,@('--target-path','\Client-Win64-Shipping.exe'), ,@('--target-path','C:\test\..\Client-Win64-Shipping.exe'),
    ,@('--target-path','C:\test\other.exe'), ,@('--target-path','C:\test\Client-Win64-Shipping.exe:stream'),
    ,@('--target-path','\\?\C:\test\Client-Win64-Shipping.exe'), ,@('--target-path','\\.\test\Client-Win64-Shipping.exe'),
    ,@('--target-path','C:\test.\Client-Win64-Shipping.exe'), ,@('--target-path',('"'+$target+'"'))
)) { Reject { [Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]$argsCase,$noRead) } ('Malformed args accepted: '+($argsCase -join ' ')) }
Check ($script:reads -eq 0) 'Malformed syntax reached file access'
Reject { [Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@('--target-path',(Join-Path $root 'missing/Client-Win64-Shipping.exe'))) } 'Missing target fell back to name-only'
$denied=[Action[string]]{ param($path) throw [UnauthorizedAccessException]::new('Fixture denied') }
Reject { [Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@('--target-path',$target),$denied) } 'Unreadable target fell back to name-only'
$allow=[Action[string]]{ param($path) }
foreach($path in @('C:\Steam Library\鸣潮\Client-Win64-Shipping.exe','\\server\share\game\Client-Win64-Shipping.exe')) {
    $unicode=[Custom_UEVR_Injector.InjectorArguments]::Parse([string[]]@('--target-path',$path),$allow)
    Check ($unicode.IsPathBound -and $unicode.TargetPath -ceq $path) 'Absolute Unicode/space/UNC syntax changed'
}
$good=Candidate 101 1001 $target
$wrong=Candidate 102 1002 (Join-Path $root 'other/Client-Win64-Shipping.exe')
Check ((Select-Target @()) -eq -1 -and $script:selectionStatus -like 'Waiting*') 'Missing process became injectable'
Check ((Select-Target @($wrong)) -eq -1) 'Wrong same-name game became injectable'
Check ((Select-Target @($wrong,$good)) -eq 1) 'Exact target was not distinguished from same-name game'
Check ((Select-Target @($good,(Candidate 103 1003 $target))) -eq -1 -and $script:selectionStatus -like 'Multiple*') 'Multiple exact-path matches were arbitrarily selected'
Check ((Select-Target @($good,(Candidate 104 1004 $null))) -eq -1 -and $script:selectionStatus -like 'Cannot verify*') 'Unreadable same-name process was silently ignored'
Check ([Custom_UEVR_Injector.TargetPathPolicy]::SameInstance($target,$good,(Candidate 101 1001 $target.ToUpperInvariant()))) 'Windows path case prevented identity match'
Check (-not [Custom_UEVR_Injector.TargetPathPolicy]::SameInstance($target,$good,(Candidate 101 1005 $target))) 'Reused PID passed opened-handle identity check'
Check (-not [Custom_UEVR_Injector.TargetPathPolicy]::SameInstance($target,$good,(Candidate 101 1001 $wrong.ImagePath))) 'Changed image path passed opened-handle identity check'
Check (-not [Custom_UEVR_Injector.TargetPathPolicy]::SameInstance($target,$good,$null)) 'Unavailable opened-handle identity passed'
if($AssemblyPath) {
    $info=[Diagnostics.FileVersionInfo]::GetVersionInfo([IO.Path]::GetFullPath($AssemblyPath))
    Check ($info.Comments -ceq [Custom_UEVR_Injector.InjectorArguments]::Capability) 'Built EXE lacks passive path-binding capability marker'
}
Write-Output "PASS: $script:checks inert argument/path-selection/identity checks. No process enumeration or injection. Fixtures: $root"
