# Build/profile switching shared by the command line and the WuWa launcher.
# Profiles are immutable snapshots; switching never overwrites a saved snapshot.
Set-StrictMode -Version 2

$script:WuWaManaged = @('config.txt','wuwa-profile-defaults.txt','user_script.txt','cameras.txt','imgui.ini','cvars_standard.txt','cvars_data.txt','scripts','plugins')

# Writable state location. The development workspace keeps its existing
# extracted\build-manager folder. A portable package (app\portable.json) keeps
# read-only files in its own folder and user state under LOCALAPPDATA, so a
# re-extracted or moved package still finds its backups. Elevated helpers get
# -DataRoot explicitly: UAC does not pass the caller's environment through.
function Get-WuWaDataRoot {
    param([Parameter(Mandatory)][string]$Root, [string]$DataRoot)
    if ($DataRoot) { return [IO.Path]::GetFullPath($DataRoot).TrimEnd('\') }
    if (Test-Path -LiteralPath (Join-Path $Root 'portable.json') -PathType Leaf) {
        if (-not $env:LOCALAPPDATA) { throw 'LOCALAPPDATA is unavailable, so launcher settings have nowhere to be saved.' }
        return [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'WuWa VR Launcher')).TrimEnd('\')
    }
    return [IO.Path]::GetFullPath((Join-Path $Root 'extracted\build-manager')).TrimEnd('\')
}

function Get-WuWaBuildContext {
    param([string]$Root = (Split-Path -Parent $PSScriptRoot), [string]$Profile, [string]$DataRoot)
    if (-not $Profile) { $Profile = Join-Path $env:APPDATA 'UnrealVRMod\Client-Win64-Shipping' }
    $data = Get-WuWaDataRoot -Root $Root -DataRoot $DataRoot
    [pscustomobject]@{
        Root = [IO.Path]::GetFullPath($Root)
        Profile = [IO.Path]::GetFullPath($Profile)
        Data = $data
        Catalog = Join-Path $Root 'dev\wuwa-builds.json'
        State = Join-Path $data 'state.json'
        Backups = Join-Path $data 'snapshots'
        Settings = Join-Path $data 'settings.json'
    }
}

# Game start preference written by the portable launcher. Without a settings
# file the original behavior applies: open the official launcher at its
# default path. 'manual' means the player presses Play in Steam/Epic/etc.
function Get-WuWaLaunchSettings {
    param($Context)
    $result = [pscustomobject]@{ Mode = 'launcher'; Launcher = '' }
    if (-not (Test-Path -LiteralPath $Context.Settings -PathType Leaf)) { return $result }
    $saved = Get-Content -LiteralPath $Context.Settings -Raw -Encoding UTF8 | ConvertFrom-Json
    $mode = ''
    if ($saved.PSObject.Properties['gameStart']) { $mode = [string]$saved.gameStart }
    if ($mode -eq 'manual') { $result.Mode = 'manual'; return $result }
    if ($mode -and $mode -ne 'launcher') { throw 'Launcher settings name an unknown game start method. Choose the game location again.' }
    $path = ''
    if ($saved.PSObject.Properties['gameLauncher']) { $path = [string]$saved.gameLauncher }
    if ($path) {
        if (-not [IO.Path]::IsPathRooted($path) -or $path -match '["\r\n]' -or [IO.Path]::GetFileName($path) -ne 'launcher.exe' -or -not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "The saved game launcher was not found: $path. Choose the game location again, or select 'I will start the game myself'."
        }
        $result.Launcher = [IO.Path]::GetFullPath($path)
    }
    return $result
}

function Get-WuWaBuildCatalog {
    param($Context)
    $catalog = Get-Content -LiteralPath $Context.Catalog -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($catalog.schema -ne 1) { throw 'Unsupported WuWa build catalog.' }
    return $catalog.builds
}

function Get-WuWaBuildState {
    param($Context)
    if (Test-Path -LiteralPath $Context.State) {
        return Get-Content -LiteralPath $Context.State -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    [pscustomobject]@{ selected = ''; savedProfiles = [pscustomobject]@{}; lastBackup = ''; changedAt = '' }
}

function Get-WuWaBusyProcesses {
    @(Get-Process -Name 'Client-Win64-Shipping','Wuthering Waves','Custom_UEVR_Injector','UEVRInjector' -ErrorAction SilentlyContinue)
}

function Assert-WuWaProfileIdle {
    $busy = @(Get-WuWaBusyProcesses)
    if ($busy.Count) { throw ('Close WuWa and the injector before changing builds. Running: ' + (($busy | ForEach-Object { $_.ProcessName + ' (' + $_.Id + ')' }) -join ', ')) }
}

function Assert-WuWaBuildFiles {
    param($Context, $Build)
    $runtime = [IO.Path]::GetFullPath((Join-Path $Context.Root $Build.runtime))
    $runtimeRoot = [IO.Path]::GetFullPath((Join-Path $Context.Root 'runtime')) + '\'
    if (-not $runtime.StartsWith($runtimeRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Runtime is outside this project.' }
    foreach ($name in @('UEVRBackend.dll','Custom_UEVR_Injector.exe','openxr_loader.dll','openvr_api.dll','LuaVR.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $runtime $name) -PathType Leaf)) { throw "Missing build file: $name" }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $runtime 'UEVRBackend.dll') -Algorithm SHA256).Hash -ne $Build.sha256) { throw "Backend hash differs from the registered build: $($Build.name)" }
    return $runtime
}

function Get-WuWaSelectedRuntime {
    param($Context)
    $path = Join-Path $Context.Profile 'injector_config.txt'
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    # Written as UTF-8 below; Windows PowerShell would otherwise read ANSI and
    # garble a folder name with non-English characters.
    $lines = @(Get-Content -LiteralPath $path -Encoding UTF8 | Where-Object { $_ -like 'custom_var_urvr_folder=*' })
    if ($lines.Count -ne 1) { throw 'Injector must have exactly one backend selection.' }
    return $lines[0].Substring('custom_var_urvr_folder='.Length).Trim()
}

function Get-WuWaPreviousBuildId {
    param($Context, $Catalog, $State, [string]$Runtime)
    if (-not $Runtime) { return '' }
    $samePath = @($Catalog | Where-Object { [IO.Path]::GetFullPath((Join-Path $Context.Root $_.runtime)) -eq $Runtime })
    if ($samePath.Count) {
        if (@($samePath | Where-Object id -eq $State.selected).Count) { return $State.selected }
        return $samePath[0].id
    }
    # Portable packages share user state but have different absolute runtime
    # paths. A matching ID alone is insufficient: verify the old backend too.
    $selected = @($Catalog | Where-Object id -eq $State.selected)
    if ($selected.Count -ne 1) { return '' }
    $backend = Join-Path $Runtime 'UEVRBackend.dll'
    if (Test-Path -LiteralPath $backend -PathType Leaf) {
        if ((Get-FileHash -LiteralPath $backend -Algorithm SHA256).Hash -eq $selected[0].sha256) { return $State.selected }
        return ''
    }
    # A folder move can remove the old path. Only reuse a previously recorded
    # selection of this exact path/hash; legacy unknown selections stay separate.
    if ($State.PSObject.Properties['selectedRuntime'] -and $State.PSObject.Properties['selectedBackendSha256'] -and
        $State.selectedRuntime -eq $Runtime -and $State.selectedBackendSha256 -eq $selected[0].sha256) {
        return $State.selected
    }
    return ''
}

function Copy-WuWaManagedProfile {
    param([string]$Source, [string]$Destination)
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    foreach ($name in $script:WuWaManaged) {
        $path = Join-Path $Source $name
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $Destination $name) -Recurse -Force }
    }
}

function Get-WuWaProfileHashes {
    param([string]$Path)
    $base = [IO.Path]::GetFullPath($Path).TrimEnd('\') + '\'
    $result = @{}
    foreach ($name in $script:WuWaManaged) {
        $item = Join-Path $base $name
        if (-not (Test-Path -LiteralPath $item)) { continue }
        $files = @(Get-Item -LiteralPath $item)
        if ($files[0].PSIsContainer) { $files = @(Get-ChildItem -LiteralPath $item -File -Recurse) }
        foreach ($file in $files) { $result[$file.FullName.Substring($base.Length)] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
    }
    return $result
}

function Assert-WuWaSameProfile {
    param([string]$First, [string]$Second)
    $a = Get-WuWaProfileHashes $First
    $b = Get-WuWaProfileHashes $Second
    if ($a.Count -ne $b.Count) { throw 'Profile file count differs after copying.' }
    foreach ($key in $a.Keys) { if (-not $b.ContainsKey($key) -or $a[$key] -ne $b[$key]) { throw "Profile verification failed: $key" } }
}

function Get-WuWaSnapshotHashes {
    param([string]$Profile)
    $hashes = Get-WuWaProfileHashes $Profile
    $injector = Join-Path $Profile 'injector_config.txt'
    if (Test-Path -LiteralPath $injector -PathType Leaf) {
        $hashes['injector_config.txt'] = (Get-FileHash -LiteralPath $injector -Algorithm SHA256).Hash
    }
    return $hashes
}

function Save-WuWaSnapshotManifest {
    param([string]$Snapshot)
    $hashes = Get-WuWaSnapshotHashes (Join-Path $Snapshot 'profile')
    [ordered]@{schema=1;files=$hashes} | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $Snapshot 'profile-manifest.json') -Encoding UTF8
}

function Assert-WuWaSnapshot {
    param([string]$Snapshot)
    $manifestPath = Join-Path $Snapshot 'profile-manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw 'This backup has no recorded file inventory. Automatic restore cannot verify older or incomplete backups; your active settings were not changed. Keep the backup for manual recovery.'
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if (-not $manifest.PSObject.Properties['schema'] -or $manifest.schema -ne 1 -or
        -not $manifest.PSObject.Properties['files'] -or $manifest.files -isnot [pscustomobject]) {
        throw 'The backup file inventory is invalid. Your active settings were not changed.'
    }
    $profile = Join-Path $Snapshot 'profile'
    if (-not (Test-Path -LiteralPath $profile -PathType Container)) { throw 'The backup profile folder is missing. Your active settings were not changed.' }
    if ((Get-Item -LiteralPath $profile).Attributes -band [IO.FileAttributes]::ReparsePoint -or
        @(Get-ChildItem -LiteralPath $profile -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw 'The backup contains linked files or folders. Your active settings were not changed.'
    }
    $actual = Get-WuWaSnapshotHashes $profile
    $expected = @($manifest.files.PSObject.Properties)
    if ($expected.Count -ne $actual.Count) { throw 'The backup is incomplete or has extra files. Your active settings were not changed.' }
    foreach ($entry in $expected) {
        if (-not $actual.ContainsKey($entry.Name) -or $entry.Value -notmatch '^[a-fA-F0-9]{64}$' -or $actual[$entry.Name] -ne $entry.Value) {
            throw ('The backup is missing or changed: ' + $entry.Name + '. Your active settings were not changed.')
        }
    }
}

function Set-WuWaManagedProfile {
    # A verified snapshot may legitimately have no config, even if its folder existed.
    param([string]$Source, [string]$Destination, [switch]$AllowEmpty)
    if (-not $AllowEmpty -and -not (Test-Path -LiteralPath (Join-Path $Source 'config.txt') -PathType Leaf)) { throw 'Saved profile has no config.txt.' }
    $root = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw 'Active profile directory does not exist.' }
    if ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing a linked active profile directory.' }
    foreach ($name in $script:WuWaManaged) {
        $target = [IO.Path]::GetFullPath((Join-Path $root $name))
        # Resolve and bound every recursive removal to an exact managed child.
        if ([IO.Path]::GetDirectoryName($target) -ne $root) { throw 'Invalid managed profile path.' }
        if (Test-Path -LiteralPath $target) {
            $item = Get-Item -LiteralPath $target
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Refusing a linked profile component: $name" }
            if ($item.PSIsContainer) {
                if (@(Get-ChildItem -LiteralPath $target -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw "Refusing links inside $name" }
            }
        }
    }
    foreach ($name in $script:WuWaManaged) {
        $target = [IO.Path]::GetFullPath((Join-Path $root $name))
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    }
    Copy-WuWaManagedProfile $Source $root
    Assert-WuWaSameProfile $Source $root
}

function Save-WuWaBuildState {
    param($Context, $State)
    New-Item -ItemType Directory -Path (Split-Path -Parent $Context.State) -Force | Out-Null
    $temp = $Context.State + '.tmp'
    $State | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temp -Encoding UTF8
    Move-Item -LiteralPath $temp -Destination $Context.State -Force
}

function Select-WuWaBuild {
    param($Context, [string]$Id, [switch]$ResetToSupplied)
    $mutex = [Threading.Mutex]::new($false, 'Local\WuWaVRBuildProfileSwitch')
    $held = $false
    try {
        try { $held = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
        if (-not $held) { throw 'Another build switch is running.' }
        Assert-WuWaProfileIdle
        $catalog = @(Get-WuWaBuildCatalog $Context)
        $build = @($catalog | Where-Object id -eq $Id)
        if ($build.Count -ne 1) { throw 'Unknown or duplicate build ID.' }
        $build = $build[0]
        $runtime = Assert-WuWaBuildFiles $Context $build
        $state = Get-WuWaBuildState $Context
        $oldRuntime = Get-WuWaSelectedRuntime $Context
        $oldId = Get-WuWaPreviousBuildId $Context $catalog $state $oldRuntime
        $source = [IO.Path]::GetFullPath((Join-Path $Context.Root $build.seed))
        $saved = $state.savedProfiles.PSObject.Properties[$Id]
        if (-not $ResetToSupplied -and $saved -and (Test-Path -LiteralPath $saved.Value)) { $source = $saved.Value }
        if (-not (Test-Path -LiteralPath (Join-Path $source 'config.txt'))) { throw 'Target profile is missing.' }
        if (-not $ResetToSupplied -and $state.selected -eq $Id -and $oldRuntime -eq $runtime) { return [pscustomobject]@{ selected=$Id; changed=$false; backup=$state.lastBackup } }
        $backup = Join-Path $Context.Backups ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
        $before = Join-Path $backup 'profile'
        # A fresh PC has no UEVR profile or injector settings yet. Record what
        # existed, so rollback can return to exactly that and not invent files.
        $profileExisted = Test-Path -LiteralPath $Context.Profile -PathType Container
        Copy-WuWaManagedProfile $Context.Profile $before
        Assert-WuWaSameProfile $Context.Profile $before
        $injector = Join-Path $Context.Profile 'injector_config.txt'
        $injectorExisted = Test-Path -LiteralPath $injector -PathType Leaf
        if ($injectorExisted) { Copy-Item -LiteralPath $injector -Destination (Join-Path $before 'injector_config.txt') }
        Save-WuWaSnapshotManifest $backup
        if (Test-Path -LiteralPath (Join-Path $Context.Profile 'log.txt')) { Copy-Item -LiteralPath (Join-Path $Context.Profile 'log.txt') -Destination (Join-Path $backup 'backend.log') }
        $state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $backup 'state-before.json') -Encoding UTF8
        [pscustomobject]@{ from=$oldId; to=$Id; previousRuntime=$oldRuntime; profile=$Context.Profile; targetHash=$build.sha256; startedAt=(Get-Date).ToString('o'); profileExisted=$profileExisted; injectorExisted=$injectorExisted } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'transaction.json') -Encoding UTF8
        if (-not $ResetToSupplied -and $oldId -eq $Id -and (Test-Path -LiteralPath (Join-Path $before 'config.txt') -PathType Leaf)) {
            # Copy from the verified snapshot, never from the active directory
            # while Set-WuWaManagedProfile is replacing its managed children.
            $source = $before
        }
        Assert-WuWaProfileIdle
        try {
            if (-not $profileExisted) { New-Item -ItemType Directory -Path $Context.Profile -Force | Out-Null }
            Set-WuWaManagedProfile $source $Context.Profile
            $injectorLines = @()
            if ($injectorExisted) { $injectorLines = @(Get-Content -LiteralPath (Join-Path $before 'injector_config.txt') -Encoding UTF8 | Where-Object { $_ -notmatch '^custom_var_(urvr_folder|last_pid|auto_focus|auto_inject|auto_close)=' }) }
            $injectorLines += @(('custom_var_urvr_folder=' + $runtime), 'custom_var_last_pid=0', 'custom_var_auto_focus=0', 'custom_var_auto_inject=1', 'custom_var_auto_close=1')
            [IO.File]::WriteAllLines($injector, [string[]]$injectorLines, [Text.UTF8Encoding]::new($false))
            if ((Get-WuWaSelectedRuntime $Context) -ne $runtime) { throw 'Injector selection verification failed.' }
            if ($oldId) { $state.savedProfiles | Add-Member -NotePropertyName $oldId -NotePropertyValue $before -Force }
            # The first change made from this data folder is the player's
            # pre-mod state. Keep a pointer so it can be restored later.
            if (-not $state.selected -and -not $state.PSObject.Properties['originalBackup']) {
                $state | Add-Member -NotePropertyName originalBackup -NotePropertyValue $backup -Force
            }
            $state.selected = $Id
            $state | Add-Member -NotePropertyName selectedRuntime -NotePropertyValue $runtime -Force
            $state | Add-Member -NotePropertyName selectedBackendSha256 -NotePropertyValue $build.sha256 -Force
            $state.lastBackup = $backup
            $state.changedAt = (Get-Date).ToString('o')
            Save-WuWaBuildState $Context $state
        } catch {
            Set-WuWaManagedProfile $before $Context.Profile -AllowEmpty
            if ($injectorExisted) { Copy-Item -LiteralPath (Join-Path $before 'injector_config.txt') -Destination $injector -Force }
            elseif (Test-Path -LiteralPath $injector) { Remove-Item -LiteralPath $injector -Force }
            throw
        }
        return [pscustomobject]@{ selected=$Id; changed=$true; runtime=$runtime; backup=$backup; source=$source; sha256=$build.sha256 }
    } finally { if ($held) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
}

# Put the managed UEVR profile and injector selection back to the snapshot
# taken before this data folder first changed them. The current settings are
# snapshotted first and stay available if that build is applied again. Files
# UEVR itself created (logs, caches) are left in place, not deleted.
function Restore-WuWaOriginalProfile {
    param($Context)
    $mutex = [Threading.Mutex]::new($false, 'Local\WuWaVRBuildProfileSwitch')
    $held = $false
    try {
        try { $held = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
        if (-not $held) { throw 'Another build switch is running.' }
        Assert-WuWaProfileIdle
        $state = Get-WuWaBuildState $Context
        $recorded = $state.PSObject.Properties['originalBackup']
        if (-not $recorded -or -not $recorded.Value) { throw 'This launcher has not changed your UEVR profile, so there is nothing to restore.' }
        $original = [IO.Path]::GetFullPath([string]$recorded.Value).TrimEnd('\')
        $backupRoot = [IO.Path]::GetFullPath($Context.Backups).TrimEnd('\') + '\'
        if (-not $original.StartsWith($backupRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'The recorded pre-install backup is outside this launcher''s backup folder.' }
        $transactionPath = Join-Path $original 'transaction.json'
        if (-not (Test-Path -LiteralPath $transactionPath -PathType Leaf)) { throw "The pre-install backup is incomplete: $original" }
        $transaction = Get-Content -LiteralPath $transactionPath -Raw | ConvertFrom-Json
        if ([IO.Path]::GetFullPath([string]$transaction.profile) -ne $Context.Profile) { throw 'The original backup belongs to a different UEVR profile. Nothing was changed.' }
        Assert-WuWaSnapshot $original
        $source = Join-Path $original 'profile'
        $profileExisted = $true
        if ($transaction.PSObject.Properties['profileExisted']) { $profileExisted = [bool]$transaction.profileExisted }
        $injectorExisted = Test-Path -LiteralPath (Join-Path $source 'injector_config.txt') -PathType Leaf
        if ($transaction.PSObject.Properties['injectorExisted']) { $injectorExisted = [bool]$transaction.injectorExisted }
        if ($injectorExisted -and -not (Test-Path -LiteralPath (Join-Path $source 'injector_config.txt') -PathType Leaf)) { throw 'The pre-install injector settings are missing from the backup.' }
        $injector = Join-Path $Context.Profile 'injector_config.txt'
        $backup = Join-Path $Context.Backups ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6))
        $current = Join-Path $backup 'profile'
        Copy-WuWaManagedProfile $Context.Profile $current
        Assert-WuWaSameProfile $Context.Profile $current
        $hadInjector = Test-Path -LiteralPath $injector -PathType Leaf
        if ($hadInjector) { Copy-Item -LiteralPath $injector -Destination (Join-Path $current 'injector_config.txt') }
        Save-WuWaSnapshotManifest $backup
        $state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $backup 'state-before.json') -Encoding UTF8
        [pscustomobject]@{ action='restore-original'; from=$state.selected; original=$original; profile=$Context.Profile; startedAt=(Get-Date).ToString('o'); profileExisted=$true; injectorExisted=$hadInjector } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'transaction.json') -Encoding UTF8
        Assert-WuWaProfileIdle
        try {
            if (-not (Test-Path -LiteralPath $Context.Profile -PathType Container)) { New-Item -ItemType Directory -Path $Context.Profile -Force | Out-Null }
            Set-WuWaManagedProfile $source $Context.Profile -AllowEmpty
            if ($injectorExisted) { Copy-Item -LiteralPath (Join-Path $source 'injector_config.txt') -Destination $injector -Force }
            elseif (Test-Path -LiteralPath $injector) { Remove-Item -LiteralPath $injector -Force }
            if ($state.selected) { $state.savedProfiles | Add-Member -NotePropertyName ([string]$state.selected) -NotePropertyValue $current -Force }
            $state.selected = ''
            $state | Add-Member -NotePropertyName selectedRuntime -NotePropertyValue '' -Force
            $state | Add-Member -NotePropertyName selectedBackendSha256 -NotePropertyValue '' -Force
            $state.lastBackup = $backup
            $state.changedAt = (Get-Date).ToString('o')
            $state | Add-Member -NotePropertyName restoredOriginalAt -NotePropertyValue $state.changedAt -Force
            Save-WuWaBuildState $Context $state
        } catch {
            Set-WuWaManagedProfile $current $Context.Profile -AllowEmpty
            if ($hadInjector) { Copy-Item -LiteralPath (Join-Path $current 'injector_config.txt') -Destination $injector -Force }
            elseif (Test-Path -LiteralPath $injector) { Remove-Item -LiteralPath $injector -Force }
            throw
        }
        return [pscustomobject]@{ restored=$true; original=$original; backup=$backup; profile=$Context.Profile; profileExisted=$profileExisted }
    } finally { if ($held) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
}
