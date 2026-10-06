param([Parameter(Mandatory)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../launcher/dev/WuWaBuildProfiles.ps1')
# Inert files and isolated roots only; never inspect or launch the real game.
$script:FixtureBusy = $false
function Get-WuWaBusyProcesses {
    if ($script:FixtureBusy) { [pscustomobject]@{ProcessName='FixtureGame';Id=123} }
}
function Check([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Write-Text([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
$script:LegacyScript = @(
    'sg.ResolutionQuality 100','sg.ViewDistanceQuality 0','sg.AntiAliasingQuality 0',
    'sg.PostProcessQuality 0','sg.ShadowQuality 0','sg.TextureQuality 0',
    'sg.EffectsQuality 0','sg.FoliageQuality 0','sg.ShadingQuality 0',
    'sg.ReflectionQuality 0','r.VSync 1','r.VolumetricCloud 0',
    'sg.GlobalIlluminationQuality 0','r.ReflectionMethod 0'
) -join "`r`n"
$script:LegacyScript += "`r`n"
$script:LegacyStandard = "Core_r.ScreenPercentage=100`r`nRenderer_r.DefaultFeature.AmbientOcclusion=1`r`nRenderer_r.DefaultFeature.AntiAliasing=2`r`n"
$script:LegacyData = "Engine_r.DepthOfFieldQuality=2`r`nEngine_r.OneFrameThreadLag=0`r`n"
function Write-Legacy([string]$Profile) {
    Write-Text (Join-Path $Profile 'user_script.txt') $script:LegacyScript
    Write-Text (Join-Path $Profile 'cvars_standard.txt') $script:LegacyStandard
    Write-Text (Join-Path $Profile 'cvars_data.txt') $script:LegacyData
}
function Fixture {
    $root = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) ([guid]::NewGuid().ToString('N'))
    foreach ($id in @('old','new')) {
        foreach ($file in @('UEVRBackend.dll','Custom_UEVR_Injector.exe','openxr_loader.dll','openvr_api.dll','LuaVR.dll')) {
            Write-Text (Join-Path $root "runtime/$id/$file") "$id fixture, not executable"
        }
        $seed=Join-Path $root "builds/$id/profile"
        Write-Text (Join-Path $seed 'config.txt') "VR_CinematicFramingFix=true`r`nWuWaStereo_SyncEyeLod=true`r`nWindowMode_PlaneWidth=2.4"
        Write-Legacy $seed
    }
    $ctx=Get-WuWaBuildContext -Root $root -Profile (Join-Path $root 'profile') -DataRoot (Join-Path $root 'data')
    $old=[pscustomobject]@{id='old';name='Old';runtime='runtime/old';seed='builds/old/profile';sha256=(Hash (Join-Path $root 'runtime/old/UEVRBackend.dll'))}
    $new=[pscustomobject]@{id='new';name='New';runtime='runtime/new';seed='builds/new/profile';sha256=(Hash (Join-Path $root 'runtime/new/UEVRBackend.dll'));graphicsPolicy='game-settings-v1';settingsFrom=@([pscustomobject]@{id='old';sha256=$old.sha256})}
    Write-Text $ctx.Catalog ([pscustomobject]@{schema=1;builds=@($old,$new)} | ConvertTo-Json -Depth 8)
    Write-Text (Join-Path $ctx.Profile 'config.txt') "VR_CinematicFramingFix=true`r`nWuWaStereo_SyncEyeLod=true`r`nWindowMode_PlaneWidth=7.25"
    Write-Text (Join-Path $ctx.Profile 'cameras.txt') 'personal camera'
    Write-Legacy $ctx.Profile
    Write-Text (Join-Path $ctx.Profile 'injector_config.txt') ('custom_var_urvr_folder='+(Join-Path $root 'runtime/old'))
    Save-WuWaBuildState $ctx ([pscustomobject]@{selected='old';savedProfiles=[pscustomobject]@{};lastBackup='';changedAt=''})
    return $ctx
}

$c=Fixture
Check ((Hash (Join-Path $c.Profile 'user_script.txt')) -eq 'f3a265b75285faa6569498894bfa82e5656c2f56e9d7301786727ca8c3828e48') 'Legacy fixture does not match delivered bytes'
$config=Hash (Join-Path $c.Profile 'config.txt')
$initial=Get-WuWaProfileHashes $c.Profile
$audit=Get-WuWaGraphicsPolicyAudit $c.Profile
Check ($audit.mode -eq 'legacy' -and $audit.inheritedFiles.Count -eq 3 -and $audit.remainingOverrides.Count -eq 18 -and -not $audit.liveEffective) 'Inherited audit incorrect'
Check ($initial.Count -eq (Get-WuWaProfileHashes $c.Profile).Count) 'Read-only audit wrote files'
$r=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq '') 'Inherited script retained'
Check ((Text (Join-Path $c.Profile 'cvars_standard.txt')) -eq '') 'Inherited standard CVars retained'
Check ((Text (Join-Path $c.Profile 'cvars_data.txt')).Trim() -eq 'Engine_r.OneFrameThreadLag=0') 'Accepted timing not retained alone'
Check ((Hash (Join-Path $c.Profile 'config.txt')) -eq $config) 'Accepted fixes or personal scale changed'
Check ((Text (Join-Path $c.Profile 'cameras.txt')) -eq 'personal camera') 'Camera changed'
Check ((Hash (Join-Path $r.backup 'profile/user_script.txt')) -eq $initial['user_script.txt']) 'Snapshot rewritten'
Assert-WuWaSnapshot $r.backup
$audit=Get-WuWaGraphicsPolicyAudit $c.Profile
Check ($audit.mode -eq 'game-settings' -and $audit.receipt -eq 'applied' -and $audit.remainingOverrides.Count -eq 0) 'Post-policy audit incorrect'
Check (-not (Select-WuWaBuild $c 'new').changed) 'Second selection was not idempotent'

# An exact old template written after migration is now a deliberate user edit.
Write-Legacy $c.Profile
Write-Text (Join-Path $c.Profile 'cvars_data.txt') 'Engine_r.DepthOfFieldQuality=3'
$custom=Hash (Join-Path $c.Profile 'cvars_data.txt')
Check (-not (Select-WuWaBuild $c 'new').changed) 'Same-build launch reprocessed custom choices'
Check ((Hash (Join-Path $c.Profile 'cvars_data.txt')) -eq $custom) 'Timing added again after deliberate removal'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq $script:LegacyScript) 'Exact user edit removed after receipt'
Check ((Get-WuWaGraphicsPolicyAudit $c.Profile).customFiles.Count -eq 3) 'Post-policy choices mislabelled as pending inherited cleanup'
$null=Select-WuWaBuild $c 'old'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq $script:LegacyScript) 'Old rollback did not restore inherited script'
Check (-not (Test-Path -LiteralPath (Join-Path $c.Profile 'wuwa-graphics-policy.json'))) 'Old rollback retained new receipt'
$null=Select-WuWaBuild $c 'new'
Check ((Hash (Join-Path $c.Profile 'cvars_data.txt')) -eq $custom) 'Returning to new package lost saved custom choice'
Assert-WuWaSnapshot $r.backup

# Missing receipt must defeat the same-build fast path.
$c=Fixture
$state=Get-WuWaBuildState $c; $state.selected='new'; Save-WuWaBuildState $c $state
Write-Text (Join-Path $c.Profile 'injector_config.txt') ('custom_var_urvr_folder='+(Join-Path $c.Root 'runtime/new'))
Check ((Select-WuWaBuild $c 'new').changed) 'Same-build pending policy skipped'
Check ((Get-WuWaGraphicsPolicyAudit $c.Profile).mode -eq 'game-settings') 'Same-build policy not applied'

# A genuinely older helper restores legacy files but leaves our unknown marker.
$c=Fixture
$null=Select-WuWaBuild $c 'new'
Write-Legacy $c.Profile
Write-Text (Join-Path $c.Profile 'injector_config.txt') ('custom_var_urvr_folder='+(Join-Path $c.Root 'runtime/old'))
$state=Get-WuWaBuildState $c; $state.selected='old'; Save-WuWaBuildState $c $state
$receiptBefore=Hash (Join-Path $c.Profile 'wuwa-graphics-policy.json')
# Match a portable package containing only its new build and settingsFrom metadata.
$catalog=Get-Content -LiteralPath $c.Catalog -Raw | ConvertFrom-Json
$catalog.builds=@($catalog.builds | Where-Object id -eq 'new')
Write-Text $c.Catalog ($catalog | ConvertTo-Json -Depth 8)
$r=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq '') 'Orphaned old-helper receipt skipped migration'
Check ((Hash (Join-Path $r.backup 'profile/wuwa-graphics-policy.json')) -eq $receiptBefore) 'Discarding orphaned receipt modified snapshot'
Assert-WuWaSnapshot $r.backup

# A policy-aware predecessor keeps its post-policy user choices and receipt.
$c=Fixture
$catalog=Get-Content -LiteralPath $c.Catalog -Raw | ConvertFrom-Json
$catalog.builds[0] | Add-Member -NotePropertyName graphicsPolicy -NotePropertyValue 'game-settings-v1'
Write-Text $c.Catalog ($catalog | ConvertTo-Json -Depth 8)
Write-Text (Join-Path $c.Profile 'wuwa-graphics-policy.json') '{"schema":1,"policy":"game-settings-v1"}'
$r=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq $script:LegacyScript) 'Policy-aware predecessor user choice lost'
Check ((Get-WuWaGraphicsPolicyAudit $c.Profile).mode -eq 'custom') 'Policy-aware inherited choices not labelled custom'

# BOM/LF normalization is accepted; any content edit is preserved in full.
$c=Fixture
Write-Text (Join-Path $c.Profile 'user_script.txt') (([string][char]0xFEFF)+$script:LegacyScript.Replace("`r`n","`n"))
$null=Select-WuWaBuild $c 'new'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq '') 'Normalized inherited template not recognized'
$c=Fixture
Write-Text (Join-Path $c.Profile 'user_script.txt') ($script:LegacyScript+'# personal choice')
Write-Text (Join-Path $c.Profile 'cvars_standard.txt') $script:LegacyStandard.Replace('ScreenPercentage=100','ScreenPercentage=80')
Write-Text (Join-Path $c.Profile 'cvars_data.txt') 'Engine_r.OneFrameThreadLag=1'
$custom=Get-WuWaProfileHashes $c.Profile
$null=Select-WuWaBuild $c 'new'
foreach ($name in @('user_script.txt','cvars_standard.txt','cvars_data.txt')) {
    Check ((Hash (Join-Path $c.Profile $name)) -eq $custom[$name]) "Custom file changed: $name"
}
Check ((Get-WuWaGraphicsPolicyAudit $c.Profile).customFiles.Count -eq 3) 'Custom files not labelled'

# Missing graphics files are not filled with inherited overrides; timing is supplied once.
$c=Fixture
foreach ($name in @('user_script.txt','cvars_standard.txt','cvars_data.txt')) { Remove-Item -LiteralPath (Join-Path $c.Profile $name) }
$null=Select-WuWaBuild $c 'new'
Check (-not (Test-Path -LiteralPath (Join-Path $c.Profile 'user_script.txt'))) 'Missing script invented'
Check ((Text (Join-Path $c.Profile 'cvars_data.txt')).Trim() -eq 'Engine_r.OneFrameThreadLag=0') 'Missing timing default not supplied'

# Restore-original reverses policy state as well as content.
$c=Fixture
$state=Get-WuWaBuildState $c; $state.selected=''; Save-WuWaBuildState $c $state
$r=Select-WuWaBuild $c 'new'
$null=Restore-WuWaOriginalProfile $c
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq $script:LegacyScript) 'Restore-original lost graphics content'
Check (-not (Test-Path -LiteralPath (Join-Path $c.Profile 'wuwa-graphics-policy.json'))) 'Restore-original retained policy receipt'
Assert-WuWaSnapshot $r.backup

# A later transaction failure must undo the cleanup and its receipt together.
$c=Fixture; $before=Hash $c.State; $runtimeReader=${function:Get-WuWaSelectedRuntime}; $script:RuntimeReadCount=0
function Get-WuWaSelectedRuntime {
    param($Context)
    $script:RuntimeReadCount++
    if ($script:RuntimeReadCount -gt 1) { return 'verification failure fixture' }
    & $runtimeReader $Context
}
$rejected=$false
try { $null=Select-WuWaBuild $c 'new' } catch { $rejected=$_.Exception.Message -eq 'Injector selection verification failed.' }
finally { Set-Item -Path Function:Get-WuWaSelectedRuntime -Value $runtimeReader }
Check $rejected 'Post-migration verification failure was not exercised'
Check ((Hash $c.State) -eq $before) 'Failed transaction changed selected build'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq $script:LegacyScript) 'Failed transaction retained cleanup'
Check (-not (Test-Path -LiteralPath (Join-Path $c.Profile 'wuwa-graphics-policy.json'))) 'Failed transaction retained receipt'

# Busy profiles fail before snapshots, content or selection changes.
$c=Fixture; $before=Hash $c.State; $script:FixtureBusy=$true; $rejected=$false
try { $null=Select-WuWaBuild $c 'new' } catch { $rejected=$_.Exception.Message -like 'Close WuWa*' }
finally { $script:FixtureBusy=$false }
Check $rejected 'Busy profile was accepted'
Check ((Hash $c.State) -eq $before -and -not (Test-Path -LiteralPath $c.Backups)) 'Busy gate mutated state'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq $script:LegacyScript) 'Busy gate mutated graphics'

# R2 repairs only the exact pair recreated by the R1 injector, and only with
# evidence that v1 removed the original inherited sources without custom files.
function Write-R1Generated([string]$Profile) {
    $lines = @(
        'sg.ResolutionQuality 99.99','sg.ViewDistanceQuality 2','sg.AntiAliasingQuality 2',
        'sg.PostProcessQuality 2','sg.ShadowQuality 2','sg.TextureQuality 2',
        'sg.EffectsQuality 2','sg.FoliageQuality 2','sg.ShadingQuality 2',
        'sg.ReflectionQuality 2','r.VSync 1','r.VolumetricCloud 0',
        'sg.GlobalIlluminationQuality 2','r.ReflectionMethod 0'
    )
    Write-Text (Join-Path $Profile 'user_script.txt') (($lines -join "`r`n")+"`r`n")
    Write-Text (Join-Path $Profile 'cvars_standard.txt') "Core_r.ScreenPercentage=99.99`r`n"
    Write-Text (Join-Path $Profile 'cvars_data.txt') "Engine_r.OneFrameThreadLag=0`r`n"
}
function Add-R2($Context, [string]$PreviousPolicy='game-settings-v1') {
    # A one-build portable package must retain policy provenance in settingsFrom.
    $catalog=Get-Content -LiteralPath $Context.Catalog -Raw | ConvertFrom-Json
    $previous=@($catalog.builds | Where-Object id -eq 'new')[0]
    foreach ($file in @('UEVRBackend.dll','Custom_UEVR_Injector.exe','openxr_loader.dll','openvr_api.dll','LuaVR.dll')) {
        Write-Text (Join-Path $Context.Root "runtime/r2/$file") 'r2 fixture, not executable'
    }
    $seed=Join-Path $Context.Root 'builds/r2/profile'
    Write-Text (Join-Path $seed 'config.txt') 'WindowMode_PlaneWidth=2.4'
    Write-Text (Join-Path $seed 'user_script.txt') ''
    Write-Text (Join-Path $seed 'cvars_standard.txt') ''
    Write-Text (Join-Path $seed 'cvars_data.txt') 'Engine_r.OneFrameThreadLag=0'
    $r2=[pscustomobject]@{id='r2';name='R2';runtime='runtime/r2';seed='builds/r2/profile';sha256=(Hash (Join-Path $Context.Root 'runtime/r2/UEVRBackend.dll'));graphicsPolicy='game-settings-v2';settingsFrom=@([pscustomobject]@{id='new';sha256=$previous.sha256;graphicsPolicy=$PreviousPolicy})}
    Write-Text $Context.Catalog ([pscustomobject]@{schema=1;builds=@($r2)} | ConvertTo-Json -Depth 8)
}
function Same-Graphics($Context, $Expected, [string]$Label) {
    foreach ($name in @('user_script.txt','cvars_standard.txt','cvars_data.txt')) {
        Check ((Hash (Join-Path $Context.Profile $name)) -eq $Expected[$name]) "$Label changed $name"
    }
}
$c=Fixture; $null=Select-WuWaBuild $c 'new'; Write-R1Generated $c.Profile; Add-R2 $c
$before=Get-WuWaProfileHashes $c.Profile
$r=Select-WuWaBuild $c 'r2'
Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq '' -and (Text (Join-Path $c.Profile 'cvars_standard.txt')) -eq '') 'R1 generated pair not repaired'
Check ((Hash (Join-Path $c.Profile 'cvars_data.txt')) -eq $before['cvars_data.txt']) 'R2 rewrote accepted timing'
$receipt=Read-WuWaGraphicsPolicyReceipt $c.Profile
Check ($receipt.status -eq 'applied' -and $receipt.value.policy -eq 'game-settings-v2' -and $receipt.value.upgradedFromPolicy -eq 'game-settings-v1') 'R2 receipt transition missing'
Check (@($receipt.value.sources | Where-Object classification -eq 'injector-r1-generated').Count -eq 2 -and $receipt.value.changedFiles.Count -eq 2) 'R2 repair provenance incorrect'
Check ((Hash (Join-Path $r.backup 'profile/user_script.txt')) -eq $before['user_script.txt'] -and (Hash (Join-Path $r.backup 'profile/wuwa-graphics-policy.json')) -eq $before['wuwa-graphics-policy.json']) 'R2 changed its rollback evidence'
Check ((Hash (Join-Path $c.Profile 'config.txt')) -eq $before['config.txt']) 'R2 lost compatible personal settings'
Assert-WuWaSnapshot $r.backup
Check (-not (Select-WuWaBuild $c 'r2').changed) 'R2 reapplied after completion'
# Identical text written later is an intentional choice, not another repair.
Write-R1Generated $c.Profile; $after=Get-WuWaProfileHashes $c.Profile
Check (-not (Select-WuWaBuild $c 'r2').changed) 'R2 reprocessed later choices'
Same-Graphics $c $after 'Repeated R2'
Check (-not (Test-WuWaGraphicsPolicyNeeded $c.Profile 'game-settings-v1')) 'Older v1 policy attempted to downgrade v2'
$hash=Hash (Join-Path $c.Profile 'wuwa-graphics-policy.json')
Invoke-WuWaGraphicsPolicy $c.Profile 'game-settings-v1'
Check ((Hash (Join-Path $c.Profile 'wuwa-graphics-policy.json')) -eq $hash) 'v1 rewrote the v2 receipt'

# A failed v2 switch restores its pre-repair v1 receipt and the generated pair.
$c=Fixture; $null=Select-WuWaBuild $c 'new'; Write-R1Generated $c.Profile; Add-R2 $c
$before=Get-WuWaProfileHashes $c.Profile; $stateBefore=Hash $c.State
$runtimeReader=${function:Get-WuWaSelectedRuntime}; $script:RuntimeReadCount=0
function Get-WuWaSelectedRuntime {
    param($Context)
    $script:RuntimeReadCount++
    if ($script:RuntimeReadCount -gt 1) { return 'verification failure fixture' }
    & $runtimeReader $Context
}
$rejected=$false
try { $null=Select-WuWaBuild $c 'r2' } catch { $rejected=$_.Exception.Message -eq 'Injector selection verification failed.' }
finally { Set-Item -Path Function:Get-WuWaSelectedRuntime -Value $runtimeReader }
Check $rejected 'v2 rollback failure was not exercised'
Same-Graphics $c $before 'Failed v2 switch'
Check ((Hash $c.State) -eq $stateBefore -and (Hash (Join-Path $c.Profile 'wuwa-graphics-policy.json')) -eq $before['wuwa-graphics-policy.json']) 'Failed v2 switch lost v1 receipt/state'

# Normalization still only covers BOM/line endings/final newlines.
$c=Fixture; $null=Select-WuWaBuild $c 'new'; Write-R1Generated $c.Profile; Add-R2 $c
$path=Join-Path $c.Profile 'user_script.txt'
Write-Text $path (([string][char]0xFEFF)+(Text $path).Replace("`r`n","`n"))
$null=Select-WuWaBuild $c 'r2'
Check ((Text $path) -eq '') 'Normalized exact R1 pair not repaired'

# Any custom edit anywhere in the three-file signature preserves the full pair.
foreach ($edit in @('script-comment','standard-value','data-comment')) {
    $c=Fixture; $null=Select-WuWaBuild $c 'new'; Write-R1Generated $c.Profile; Add-R2 $c
    if ($edit -eq 'script-comment') {
        $path=Join-Path $c.Profile 'user_script.txt'; Write-Text $path ((Text $path)+'# keep my preset')
    } elseif ($edit -eq 'standard-value') {
        Write-Text (Join-Path $c.Profile 'cvars_standard.txt') 'Core_r.ScreenPercentage=90'
    } else {
        Write-Text (Join-Path $c.Profile 'cvars_data.txt') "Engine_r.OneFrameThreadLag=0`n# custom timing"
    }
    $before=Get-WuWaProfileHashes $c.Profile; $null=Select-WuWaBuild $c 'r2'
    Same-Graphics $c $before $edit
    Check ((Read-WuWaGraphicsPolicyReceipt $c.Profile).value.changedFiles.Count -eq 0) 'Partial R1 pair was cleaned'
}

# A minimal marker or a real v1 decision that preserved custom settings cannot
# prove that the injector alone created the new files.
foreach ($proof in @('minimal','custom-source')) {
    $c=Fixture
    if ($proof -eq 'custom-source') { Write-Text (Join-Path $c.Profile 'user_script.txt') '# personal' }
    $null=Select-WuWaBuild $c 'new'; Write-R1Generated $c.Profile; Add-R2 $c
    if ($proof -eq 'minimal') { Write-Text (Join-Path $c.Profile 'wuwa-graphics-policy.json') '{"schema":1,"policy":"game-settings-v1"}' }
    $before=Get-WuWaProfileHashes $c.Profile; $null=Select-WuWaBuild $c 'r2'
    Same-Graphics $c $before $proof
    Check ((Read-WuWaGraphicsPolicyReceipt $c.Profile).value.policy -eq 'game-settings-v2') 'No-repair v2 decision was not recorded'
}

# Fresh/pre-v1 R2 selection retains the original cleanup; missing proof never
# gives it permission to identify the newer generated pair as inherited.
foreach ($kind in @('original','unproven-r1')) {
    $c=Fixture
    $catalog=Get-Content -LiteralPath $c.Catalog -Raw | ConvertFrom-Json
    $catalog.builds[1].graphicsPolicy='game-settings-v2'
    Write-Text $c.Catalog ($catalog | ConvertTo-Json -Depth 8)
    if ($kind -eq 'unproven-r1') { Write-R1Generated $c.Profile }
    $before=Get-WuWaProfileHashes $c.Profile; $null=Select-WuWaBuild $c 'new'
    if ($kind -eq 'original') {
        Check ((Text (Join-Path $c.Profile 'user_script.txt')) -eq '') 'Fresh R2 skipped original inherited cleanup'
    } else { Same-Graphics $c $before $kind }
}

# v2 settingsFrom metadata keeps the receipt protecting later user choices.
$c=Fixture; $null=Select-WuWaBuild $c 'new'
Invoke-WuWaGraphicsPolicy $c.Profile 'game-settings-v2'
Write-Legacy $c.Profile; Add-R2 $c 'game-settings-v2'
$before=Get-WuWaProfileHashes $c.Profile; $null=Select-WuWaBuild $c 'r2'
Same-Graphics $c $before 'Policy-aware v2 predecessor'
Check ((Hash (Join-Path $c.Profile 'wuwa-graphics-policy.json')) -eq $before['wuwa-graphics-policy.json']) 'Policy-aware v2 lineage discarded receipt'

# Invalid receipt data must fail closed even when an old helper would normally
# cause an orphaned marker to be discarded during compatible staging.
foreach ($invalid in @('not json','{"schema":1,"policy":"other"}','{"schema":1,"policy":"game-settings-v2"}','{"schema":1,"policy":"game-settings-v1","sources":[]}')) {
    $c=Fixture
    $catalog=Get-Content -LiteralPath $c.Catalog -Raw | ConvertFrom-Json
    $catalog.builds[1].graphicsPolicy='game-settings-v2'
    Write-Text $c.Catalog ($catalog | ConvertTo-Json -Depth 8)
    Write-Text (Join-Path $c.Profile 'wuwa-graphics-policy.json') $invalid
    $before=Get-WuWaProfileHashes $c.Profile; $stateBefore=Hash $c.State; $rejected=$false
    try { $null=Select-WuWaBuild $c 'new' } catch { $rejected=$_.Exception.Message -like 'The graphics policy receipt is invalid.*' }
    Check $rejected 'Invalid receipt did not fail closed'
    Same-Graphics $c $before 'Invalid receipt'
    Check ((Hash $c.State) -eq $stateBefore -and -not (Test-Path -LiteralPath $c.Backups)) 'Invalid receipt created transaction state'
}

# A superficially valid marker with corrupted provenance cannot authorize repair.
foreach ($corruption in @('duplicate-source','bad-hash','partial-provenance')) {
    $c=Fixture; $null=Select-WuWaBuild $c 'new'; Write-R1Generated $c.Profile; Add-R2 $c
    $receipt=Read-WuWaGraphicsPolicyReceipt $c.Profile
    if ($corruption -eq 'duplicate-source') { $receipt.value.sources[1].filename=$receipt.value.sources[0].filename }
    elseif ($corruption -eq 'bad-hash') { $receipt.value.sources[0].sha256='not a hash' }
    else { $receipt.value.PSObject.Properties.Remove('changedFiles') }
    Write-Text (Join-Path $c.Profile 'wuwa-graphics-policy.json') ($receipt.value | ConvertTo-Json -Depth 8)
    $before=Get-WuWaProfileHashes $c.Profile; $stateBefore=Hash $c.State
    $backupCount=@(Get-ChildItem -LiteralPath $c.Backups -Directory).Count; $rejected=$false
    try { $null=Select-WuWaBuild $c 'r2' } catch { $rejected=$_.Exception.Message -like 'The graphics policy receipt is invalid.*' }
    Check $rejected 'Invalid v1 provenance did not fail closed'
    Same-Graphics $c $before $corruption
    Check ((Hash $c.State) -eq $stateBefore -and @(Get-ChildItem -LiteralPath $c.Backups -Directory).Count -eq $backupCount) 'Invalid v1 provenance created transaction state'
}

Write-Output 'PASS: v1 regressions; v2 exact paired repair and provenance; comments/values/custom proof preserved; later edits protected; v1/v2 portable lineage; invalid receipts fail closed; snapshot rollback; busy gate.'

