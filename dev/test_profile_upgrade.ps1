param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../launcher/dev/WuWaBuildProfiles.ps1')
# Test only: fixtures use inert text files as DLLs and never launch or touch UEVR.
function Assert-WuWaProfileIdle { }
function Check([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Write-Text([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Fixture {
    $root = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([guid]::NewGuid().ToString('N'))
    foreach ($id in @('old','new')) {
        foreach ($file in @('UEVRBackend.dll','Custom_UEVR_Injector.exe','openxr_loader.dll','openvr_api.dll','LuaVR.dll')) {
            Write-Text (Join-Path $root "runtime/$id/$file") "$id fixture, never executable"
        }
    }
    $ctx = Get-WuWaBuildContext -Root $root -Profile (Join-Path $root 'profile') -DataRoot (Join-Path $root 'data')
    $oldHash = (Get-FileHash (Join-Path $root 'runtime/old/UEVRBackend.dll')).Hash
    $build = [pscustomobject]@{id='new';name='New';runtime='runtime/new';seed='builds/new/profile';sha256=(Get-FileHash (Join-Path $root 'runtime/new/UEVRBackend.dll')).Hash;settingsFrom=@([pscustomobject]@{id='old';sha256=$oldHash})}
    Write-Text $ctx.Catalog ([pscustomobject]@{schema=1;builds=@($build)} | ConvertTo-Json -Depth 8)
    Write-Text (Join-Path $root 'builds/new/profile/config.txt') 'WindowMode_PlaneWidth=2.4'
    Write-Text (Join-Path $root 'builds/new/profile/scripts/02_controls.lua') 'new controls'
    Write-Text (Join-Path $root 'builds/new/profile/scripts/lang/zh.json') 'new language'
    Write-Text (Join-Path $root 'builds/new/profile/wuwa-profile-defaults.txt') 'new defaults marker'
    Write-Text (Join-Path $ctx.Profile 'config.txt') 'WindowMode_PlaneWidth=7.25'
    Write-Text (Join-Path $ctx.Profile 'cameras.txt') 'personal camera'
    Write-Text (Join-Path $ctx.Profile 'cvars_data.txt') 'Engine_r.OneFrameThreadLag=1'
    Write-Text (Join-Path $ctx.Profile 'scripts/02_controls.lua') 'old controls'
    Write-Text (Join-Path $ctx.Profile 'scripts/lang/zh.json') 'old language'
    Write-Text (Join-Path $ctx.Profile 'scripts/personal.lua') 'personal script'
    Write-Text (Join-Path $ctx.Profile 'injector_config.txt') ('custom_var_urvr_folder='+(Join-Path $root 'runtime/old'))
    $state = [pscustomobject]@{selected='old';selectedRuntime=(Join-Path $root 'runtime/old');selectedBackendSha256=$oldHash;savedProfiles=[pscustomobject]@{};lastBackup='';changedAt=''}
    Save-WuWaBuildState $ctx $state
    return $ctx
}
function Text([string]$Path) { Get-Content -LiteralPath $Path -Raw }

$c=Fixture
$result=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'config.txt')) -eq 'WindowMode_PlaneWidth=7.25') 'Width reset'
Check ((Text (Join-Path $c.Profile 'cameras.txt')) -eq 'personal camera') 'Camera reset'
Check ((Text (Join-Path $c.Profile 'cvars_data.txt')) -eq 'Engine_r.OneFrameThreadLag=1') 'Explicit CVar overridden'
Check ((Text (Join-Path $c.Profile 'scripts/02_controls.lua')) -eq 'new controls') 'Controls not upgraded'
Check ((Text (Join-Path $c.Profile 'scripts/lang/zh.json')) -eq 'new language') 'Nested localization not upgraded'
Check ((Text (Join-Path $c.Profile 'scripts/personal.lua')) -eq 'personal script') 'Extra personal script lost'
Check ((Text (Join-Path $result.backup 'profile/scripts/02_controls.lua')) -eq 'old controls') 'Snapshot mutated'
Assert-WuWaSnapshot $result.backup
$again=Select-WuWaBuild $c 'new'
Check (-not $again.changed) 'Same selected package should be a no-op'

$c=Fixture
Write-Text (Join-Path $c.Root 'runtime/old/UEVRBackend.dll') 'different untrusted backend'
$null=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'config.txt')) -eq 'WindowMode_PlaneWidth=2.4') 'Wrong predecessor hash inherited'

$c=Fixture
$null=Select-WuWaBuild $c 'new' -ResetToSupplied
Check ((Text (Join-Path $c.Profile 'config.txt')) -eq 'WindowMode_PlaneWidth=2.4') 'Explicit reset ignored'

$c=Fixture
$state=Get-WuWaBuildState $c
$saved=Join-Path $c.Root 'saved-new'
Write-Text (Join-Path $saved 'config.txt') 'saved target preference'
$state.savedProfiles | Add-Member -NotePropertyName new -NotePropertyValue $saved
Save-WuWaBuildState $c $state
$null=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'config.txt')) -eq 'saved target preference') 'Saved target lost precedence'

# First use -> restore -> apply again must revoke the restoration receipt.
# Otherwise a removal UI can delete a package the injector points at again.
$c=Fixture
$state=Get-WuWaBuildState $c
$state.selected=''
Save-WuWaBuildState $c $state
$first=Select-WuWaBuild $c 'new'
$null=Restore-WuWaOriginalProfile $c
$state=Get-WuWaBuildState $c
Check ([bool]$state.restoredOriginalAt -and -not $state.selected) 'Restore receipt not established'
Check ((Get-WuWaSelectedRuntime $c) -eq (Join-Path $c.Root 'runtime/old')) 'Original injector selection not restored'
$null=Select-WuWaBuild $c 'new'
$state=Get-WuWaBuildState $c
Check ($state.selected -eq 'new' -and -not $state.restoredOriginalAt) 'Reapply retained obsolete restoration receipt'
Check ((Get-WuWaSelectedRuntime $c) -eq (Join-Path $c.Root 'runtime/new')) 'Reapply injector selection wrong'
Assert-WuWaSnapshot $first.backup

# Re-selecting an already active build must repair stale receipts from older helpers.
$state | Add-Member -NotePropertyName restoredOriginalAt -NotePropertyValue '2026-10-01T00:00:00Z' -Force
Save-WuWaBuildState $c $state
$again=Select-WuWaBuild $c 'new'
Check (-not $again.changed -and -not (Get-WuWaBuildState $c).restoredOriginalAt) 'No-op selection retained obsolete restoration receipt'

Write-Output 'PASS: compatible upgrade/settings/scripts/backup; wrong hash/reset/saved-target precedence; restore-reapply and stale no-op restoration receipts.'
