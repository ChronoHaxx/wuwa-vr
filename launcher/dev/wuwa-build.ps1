[CmdletBinding()]
param(
    [ValidateSet('Status','Select','Launch','Restore')][string]$Action='Status',
    [string]$Id,
    [switch]$ResetToSupplied,
    [string]$DataRoot
)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
. (Join-Path $PSScriptRoot 'WuWaBuildProfiles.ps1')
$context=Get-WuWaBuildContext -DataRoot $DataRoot
if($Action -eq 'Status') {
    [pscustomobject]@{ state=(Get-WuWaBuildState $context); runtime=(Get-WuWaSelectedRuntime $context); busy=@(Get-WuWaBusyProcesses | ForEach-Object { [pscustomobject]@{name=$_.ProcessName;pid=$_.Id} }); builds=@(Get-WuWaBuildCatalog $context) } | ConvertTo-Json -Depth 12
    return
}
if($Action -eq 'Restore') { Restore-WuWaOriginalProfile $context | ConvertTo-Json; return }
if(-not $Id) { throw 'Select a build ID first.' }
if($Action -eq 'Select') { Select-WuWaBuild $context $Id -ResetToSupplied:$ResetToSupplied | ConvertTo-Json; return }
Assert-WuWaProfileIdle
if($DataRoot) { & (Join-Path $PSScriptRoot 'start-wuwa-build.ps1') -Id $Id -NoDialog -DataRoot $context.Data }
else { & (Join-Path $PSScriptRoot 'start-wuwa-build.ps1') -Id $Id -NoDialog }
exit $LASTEXITCODE
