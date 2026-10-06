# Saved-profile policy only. This never queries or changes a running game.
# Called by the closed-game, snapshot-backed build-selection transaction.
Set-StrictMode -Version 2

$script:WuWaGraphicsPolicyName = 'game-settings-v2'
$script:WuWaGraphicsPolicyNames = @('game-settings-v1','game-settings-v2')
$script:WuWaGraphicsPolicyReceipt = 'wuwa-graphics-policy.json'
$script:WuWaGraphicsTemplates = [ordered]@{
    'user_script.txt' = @(
        'sg.ResolutionQuality 100', 'sg.ViewDistanceQuality 0', 'sg.AntiAliasingQuality 0',
        'sg.PostProcessQuality 0', 'sg.ShadowQuality 0', 'sg.TextureQuality 0',
        'sg.EffectsQuality 0', 'sg.FoliageQuality 0', 'sg.ShadingQuality 0',
        'sg.ReflectionQuality 0', 'r.VSync 1', 'r.VolumetricCloud 0',
        'sg.GlobalIlluminationQuality 0', 'r.ReflectionMethod 0'
    ) -join "`n"
    'cvars_standard.txt' = @('Core_r.ScreenPercentage=100',
        'Renderer_r.DefaultFeature.AmbientOcclusion=1', 'Renderer_r.DefaultFeature.AntiAliasing=2') -join "`n"
    'cvars_data.txt' = @('Engine_r.DepthOfFieldQuality=2', 'Engine_r.OneFrameThreadLag=0') -join "`n"
}

# Exact files recreated by the graphics-r1 injector after a completed v1
# cleanup. These are not general presets: repair requires the paired files,
# timing-only data, and a receipt proving the original inherited-only cleanup.
$script:WuWaR1Generated = [ordered]@{
    'user_script.txt' = @(
        'sg.ResolutionQuality 99.99', 'sg.ViewDistanceQuality 2', 'sg.AntiAliasingQuality 2',
        'sg.PostProcessQuality 2', 'sg.ShadowQuality 2', 'sg.TextureQuality 2',
        'sg.EffectsQuality 2', 'sg.FoliageQuality 2', 'sg.ShadingQuality 2',
        'sg.ReflectionQuality 2', 'r.VSync 1', 'r.VolumetricCloud 0',
        'sg.GlobalIlluminationQuality 2', 'r.ReflectionMethod 0'
    ) -join "`n"
    'cvars_standard.txt' = 'Core_r.ScreenPercentage=99.99'
    'cvars_data.txt' = 'Engine_r.OneFrameThreadLag=0'
}

function ConvertTo-WuWaGraphicsNormalizedText {
    param([AllowEmptyString()][string]$Text)
    # Only encoding/BOM, line endings and final newlines are normalized. A
    # comment, value edit, reordered line or additional command is custom.
    return $Text.TrimStart([char]0xFEFF).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd([char]10)
}

function Read-WuWaGraphicsPolicyReceipt {
    param([string]$Profile)
    $path = Join-Path $Profile $script:WuWaGraphicsPolicyReceipt
    if (-not (Test-Path -LiteralPath $path)) { return [pscustomobject]@{status='missing'; value=$null} }
    try {
        $item = Get-Item -LiteralPath $path
        if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -gt 65536) { throw 'Invalid receipt file' }
        $receipt = [IO.File]::ReadAllText($path) | ConvertFrom-Json
        if ($receipt -isnot [pscustomobject] -or $receipt.schema -ne 1 -or $receipt.policy -cnotin $script:WuWaGraphicsPolicyNames) { throw 'Unknown receipt' }
        # Minimal v1 markers remain recognized for compatibility, but cannot
        # authorize injector repair. v2 is only emitted with full provenance.
        $hasProvenance = $receipt.PSObject.Properties['sources'] -or $receipt.PSObject.Properties['changedFiles'] -or $receipt.PSObject.Properties['preservedCustomFiles']
        if ($receipt.policy -ceq 'game-settings-v2' -and -not $hasProvenance) { throw 'Missing v2 receipt provenance' }
        if ($hasProvenance) {
            foreach ($field in @('sources','changedFiles','preservedCustomFiles')) {
                if (-not $receipt.PSObject.Properties[$field] -or $null -eq $receipt.$field) { throw 'Incomplete receipt provenance' }
            }
            if (@($receipt.sources).Count -ne 3) { throw 'Invalid receipt source count' }
            $seen=@()
            foreach ($source in $receipt.sources) {
                if ($source.filename -cnotin $script:WuWaGraphicsTemplates.Keys -or $source.filename -cin $seen) { throw 'Invalid receipt source' }
                if ($source.classification -cnotin @('missing','custom','inherited-template','no-overrides','timing-only','injector-r1-generated')) { throw 'Invalid receipt classification' }
                if ($source.sha256 -and $source.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid receipt source hash' }
                if ($source.classification -cin @('inherited-template','injector-r1-generated') -and -not $source.sha256) { throw 'Missing receipt source hash' }
                $seen += $source.filename
            }
            foreach ($field in @('changedFiles','preservedCustomFiles')) {
                $seen=@()
                foreach ($name in $receipt.$field) {
                    if ($name -cnotin $script:WuWaGraphicsTemplates.Keys -or $name -cin $seen) { throw 'Invalid receipt file list' }
                    $seen += $name
                }
            }
        }
        return [pscustomobject]@{status='applied'; value=$receipt}
    } catch { return [pscustomobject]@{status='invalid'; value=$null} }
}

function Get-WuWaGraphicsPolicyReceipt {
    param([string]$Profile)
    return (Read-WuWaGraphicsPolicyReceipt $Profile).status
}

function Test-WuWaV1InheritedCleanupProof {
    param($Receipt)
    if (-not $Receipt -or $Receipt.policy -cne 'game-settings-v1') { return $false }
    foreach ($field in @('sources','changedFiles','preservedCustomFiles')) {
        if (-not $Receipt.PSObject.Properties[$field]) { return $false }
    }
    if (@($Receipt.sources).Count -ne 3 -or @($Receipt.changedFiles).Count -ne 3 -or @($Receipt.preservedCustomFiles).Count -ne 0) { return $false }
    foreach ($source in $Receipt.sources) {
        if ($source.classification -cne 'inherited-template' -or $source.filename -cnotin $Receipt.changedFiles) { return $false }
    }
    return $true
}

function Get-WuWaGraphicsPolicyAudit {
    param([Parameter(Mandatory)][string]$Profile, [string]$Policy = $script:WuWaGraphicsPolicyName)
    $record = Read-WuWaGraphicsPolicyReceipt $Profile
    $receipt = $record.status
    $sources = @()
    $remaining = @()
    $generatedMatches = @()
    foreach ($name in $script:WuWaGraphicsTemplates.Keys) {
        $path = Join-Path $Profile $name
        $classification = 'missing'; $hash = ''; $entries = @(); $reason = ''
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path
            if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.Length -gt 1048576) {
                $classification = 'custom'; $reason = 'Not a regular file of at most 1 MiB; preserved without parsing.'
            } else {
                $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
                $text = ConvertTo-WuWaGraphicsNormalizedText ([IO.File]::ReadAllText($path))
                if ($text -ceq $script:WuWaR1Generated[$name]) { $generatedMatches += $name }
                if ($text -ceq $script:WuWaGraphicsTemplates[$name]) {
                    if ($receipt -eq 'applied') {
                        $classification = 'custom'; $reason = 'Known template present after the policy decision; preserved as a later user choice.'
                    } else { $classification = 'inherited-template' }
                }
                elseif (-not $text.Trim()) { $classification = 'no-overrides' }
                elseif ($name -eq 'cvars_data.txt' -and $text -ceq 'Engine_r.OneFrameThreadLag=0') { $classification = 'timing-only' }
                else { $classification = 'custom' }
                foreach ($line in ($text -split "`n")) {
                    if (-not $line.Trim() -or $line -match '^\s*(#|;|//)') { continue }
                    $timing = $line -match '^\s*(Engine_)?r\.OneFrameThreadLag(?:\s*=|\s+)'
                    $entry = [pscustomobject]@{ source=$name; text=$line; kind=$(if ($timing) {'vr-timing'} else {'configured-override'}) }
                    $entries += $entry
                    if (-not $timing) { $remaining += $entry }
                }
            }
        }
        $sources += [pscustomobject]@{
            filename=$name; path=$path; classification=$classification; sha256=$hash; reason=$reason
            overrides=@($entries | Where-Object kind -ne 'vr-timing'); timing=@($entries | Where-Object kind -eq 'vr-timing')
        }
    }
    if ($Policy -ceq 'game-settings-v2' -and $generatedMatches.Count -eq 3 -and
        (Test-WuWaV1InheritedCleanupProof $record.value)) {
        foreach ($source in $sources | Where-Object filename -ne 'cvars_data.txt') {
            $source.classification = 'injector-r1-generated'
            $source.reason = 'Exact graphics-r1 injector pair after a recorded inherited-only v1 cleanup; eligible for one-time v2 repair.'
        }
    }
    $custom = @($sources | Where-Object classification -eq 'custom' | ForEach-Object filename)
    $inherited = @($sources | Where-Object { $_.classification -in @('inherited-template','injector-r1-generated') } | ForEach-Object filename)
    $mode = if ($custom.Count) {'custom'} elseif ($inherited.Count) {'legacy'} else {'game-settings'}
    [pscustomobject]@{
        schema=1; policy=$Policy; scope='saved-profile-files'; liveEffective=$false
        mode=$mode; receipt=$receipt; sources=@($sources)
        inheritedFiles=@($inherited); customFiles=@($custom); remainingOverrides=@($remaining)
        message='Saved user_script.txt, cvars_standard.txt and cvars_data.txt only, not live or effective settings. Custom files are preserved. VR timing is listed separately; other scripts and engine settings are not inspected.'
    }
}

function Test-WuWaGraphicsPolicyNeeded {
    param([string]$Profile, [string]$Policy)
    if (-not $Policy) { return $false }
    if ($Policy -cnotin $script:WuWaGraphicsPolicyNames) { throw "Unsupported graphics policy: $Policy" }
    $record = Read-WuWaGraphicsPolicyReceipt $Profile
    if ($record.status -eq 'invalid') { throw 'The graphics policy receipt is invalid. Restore a verified profile snapshot or repair the package before applying it.' }
    if ($record.status -ne 'applied') { return $true }
    # A v2 decision also satisfies older v1 catalogs; never downgrade it.
    return $Policy -ceq 'game-settings-v2' -and $record.value.policy -ceq 'game-settings-v1'
}

function Invoke-WuWaGraphicsPolicy {
    param([Parameter(Mandatory)][string]$Profile, [string]$Policy)
    if (-not (Test-WuWaGraphicsPolicyNeeded $Profile $Policy)) { return }
    # Active-profile callers must use Select-WuWaBuild's closed-game, verified
    # snapshot transaction. Package tooling may also call this on inert staging.
    $record = Read-WuWaGraphicsPolicyReceipt $Profile
    $audit = Get-WuWaGraphicsPolicyAudit -Profile $Profile -Policy $Policy
    $changed = @()
    foreach ($source in $audit.sources) {
        $timingDefault = $source.filename -eq 'cvars_data.txt' -and $source.classification -in @('missing','no-overrides')
        if ($source.classification -notin @('inherited-template','injector-r1-generated') -and -not $timingDefault) { continue }
        $replacement = if ($source.filename -eq 'cvars_data.txt') { "Engine_r.OneFrameThreadLag=0`r`n" } else { '' }
        [IO.File]::WriteAllText($source.path, $replacement, [Text.UTF8Encoding]::new($false))
        $changed += $source.filename
    }
    [ordered]@{
        schema=1; policy=$Policy; appliedAt=(Get-Date).ToUniversalTime().ToString('o')
        sources=@($audit.sources | Select-Object filename,classification,sha256)
        changedFiles=@($changed); preservedCustomFiles=@($audit.customFiles)
        upgradedFromPolicy=$(if ($record.status -eq 'applied') { $record.value.policy } else { '' })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Profile $script:WuWaGraphicsPolicyReceipt) -Encoding UTF8
}
