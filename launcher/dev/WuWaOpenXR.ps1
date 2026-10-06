# Read/plan/write helpers. Importing this file never changes the runtime.
$script:WuWaOpenXRKey='HKLM:\SOFTWARE\Khronos\OpenXR\1'
function Get-WuWaOpenXRValues {
    # No runtime is a valid fresh-PC state. Access failures are not absence.
    try { $r=Get-ItemProperty -LiteralPath $script:WuWaOpenXRKey -ErrorAction Stop }
    catch [System.Management.Automation.ItemNotFoundException] {
        return [pscustomobject]@{Active='';HasActive=$false;Previous='';HasPrevious=$false}
    }
    $active=$r.PSObject.Properties['ActiveRuntime']
    $previous=$r.PSObject.Properties['PreviousActiveRuntime']
    [pscustomobject]@{Active=$(if($active){[string]$active.Value}else{''});HasActive=($null -ne $active);Previous=$(if($previous){[string]$previous.Value}else{''});HasPrevious=($null -ne $previous)}
}
function Test-WuWaHasActiveRuntime($Values) {
    # Older callers/fixtures only supplied Active. Explicit HasActive keeps an
    # existing empty value distinct from an absent registration.
    if ($null -ne $Values.PSObject.Properties['HasActive']) { return [bool]$Values.HasActive }
    return -not [string]::IsNullOrEmpty([string]$Values.Active)
}
function Assert-WuWaExpectedRuntime([AllowEmptyString()][string]$ExpectedActive,$Values) {
    $hasActive=Test-WuWaHasActiveRuntime $Values
    if (($ExpectedActive -eq '' -and $hasActive) -or
        ($ExpectedActive -ne '' -and (-not $hasActive -or $Values.Active -ne $ExpectedActive))) {
        throw 'The runtime changed. Refresh the dashboard and choose again.'
    }
}
function Assert-WuWaRuntimeManifest([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path) -or $Path -match '["\r\n]' -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'The OpenXR runtime manifest is missing or invalid. Select your headset runtime in its own app first.' }
    $json=Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    $library=[string]$json.runtime.library_path
    if (-not $library) { throw 'The OpenXR manifest has no runtime library.' }
    if (-not [IO.Path]::IsPathRooted($library)) { $library=Join-Path (Split-Path -Parent $Path) $library }
    if (-not (Test-Path -LiteralPath $library -PathType Leaf)) { throw 'The OpenXR runtime library is missing.' }
}
function Test-WuWaSimulatorManifest([string]$Path) {
    # A different extracted package may own the current simulator. Do not save
    # that simulator as the headset when switching to a new package's copy.
    try {
        $manifest=Get-Content -LiteralPath $Path -Raw -Encoding UTF8 -ErrorAction Stop | ConvertFrom-Json
        return ($manifest.runtime.name -eq 'OpenXR Simulator' -and
            [IO.Path]::GetFileName([string]$manifest.runtime.library_path) -eq 'openxr_simulator.dll')
    } catch { return $false }
}
function Assert-WuWaSimulatorPrerequisites([string]$ManifestPath,
        [string]$SystemDirectory=[Environment]::GetFolderPath([Environment+SpecialFolder]::System)) {
    # Inspect files only: loading a runtime DLL here would execute it. These
    # dependencies are imported by the bundled x64 simulator, not by Python.
    $manifest=Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $library=[string]$manifest.runtime.library_path
    if (-not [IO.Path]::IsPathRooted($library)) { $library=Join-Path (Split-Path -Parent $ManifestPath) $library }
    $directory=Split-Path -Parent $library
    $missing=@('MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll' | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $directory $_) -PathType Leaf) -and
        -not (Test-Path -LiteralPath (Join-Path $SystemDirectory $_) -PathType Leaf)
    })
    if ($missing.Count) {
        throw ('The bundled simulator requires Microsoft Visual C++ 2015-2022 Redistributable (x64). Missing: ' +
            ($missing -join ', ') + '. Install it from Microsoft, then retry. No OpenXR setting was changed.')
    }
}
function Get-WuWaRuntimePlan([ValidateSet('simulator','headset')][string]$Mode,[string]$Root,$Values) {
    $simulator=[IO.Path]::GetFullPath((Join-Path $Root 'dev-tools\OpenXR-Simulator\openxr_simulator.json'))
    $hasActive=Test-WuWaHasActiveRuntime $Values
    if ($hasActive -and [string]::IsNullOrWhiteSpace([string]$Values.Active)) { throw 'The registered OpenXR runtime path is empty. Select your headset runtime in its own app first.' }
    $missingSimulator=$hasActive -and (-not (Test-Path -LiteralPath $Values.Active -PathType Leaf)) -and
        [IO.Path]::GetFileName($Values.Active) -eq 'openxr_simulator.json'
    $isSimulator=$hasActive -and ($Values.Active -eq $simulator -or (Test-WuWaSimulatorManifest $Values.Active) -or $missingSimulator)
    $target=if ($Mode -eq 'simulator') { $simulator } elseif ($isSimulator) { $Values.Previous } else { $Values.Active }
    if (-not $target -or ($Mode -eq 'headset' -and ($target -eq $simulator -or (Test-WuWaSimulatorManifest $target)))) { throw 'No saved headset runtime is available. Set SteamVR, Quest Link or Virtual Desktop as OpenXR runtime in its own app first.' }
    Assert-WuWaRuntimeManifest $target
    if ($Mode -eq 'simulator') { Assert-WuWaSimulatorPrerequisites $target }
    if ($Mode -eq 'simulator' -and $hasActive -and -not $isSimulator) { Assert-WuWaRuntimeManifest $Values.Active }
    [pscustomobject]@{Mode=$Mode;Before=$Values.Active;HasActive=$hasActive;Previous=$Values.Previous;HasPrevious=$Values.HasPrevious;Target=$target;Changed=(-not $hasActive -or $target -ne $Values.Active);SavePrevious=($Mode -eq 'simulator' -and $hasActive -and -not $isSimulator);ClearPrevious=($Mode -eq 'headset' -and $isSimulator)}
}
function Set-WuWaRuntimeRegistryValue([string]$Name,[string]$Value) {
    if (-not (Test-Path -LiteralPath $script:WuWaOpenXRKey)) {
        New-Item -Path $script:WuWaOpenXRKey -Force -ErrorAction Stop | Out-Null
    }
    Set-ItemProperty -LiteralPath $script:WuWaOpenXRKey -Name $Name -Value $Value -Type String -ErrorAction Stop
}
function Remove-WuWaRuntimeActive {
    if (Test-WuWaHasActiveRuntime (Get-WuWaOpenXRValues)) {
        Remove-ItemProperty -LiteralPath $script:WuWaOpenXRKey -Name ActiveRuntime -ErrorAction Stop
    }
}
function Remove-WuWaRuntimePrevious {
    Remove-ItemProperty -LiteralPath $script:WuWaOpenXRKey -Name PreviousActiveRuntime -ErrorAction Stop
}
function Invoke-WuWaRuntimePlan($Plan) {
    $current=Get-WuWaOpenXRValues
    $hadActive=Test-WuWaHasActiveRuntime $Plan
    if ((Test-WuWaHasActiveRuntime $current) -ne $hadActive -or $current.Active -ne $Plan.Before -or $current.Previous -ne $Plan.Previous -or $current.HasPrevious -ne $Plan.HasPrevious) { throw 'The OpenXR runtime changed while waiting. Refresh the dashboard and choose again.' }
    if (-not $Plan.Changed) { return }
    try {
        if ($Plan.SavePrevious) { Set-WuWaRuntimeRegistryValue 'PreviousActiveRuntime' $Plan.Before }
        Set-WuWaRuntimeRegistryValue 'ActiveRuntime' $Plan.Target
        if ($Plan.ClearPrevious -and $Plan.HasPrevious) { Remove-WuWaRuntimePrevious }
        $after=Get-WuWaOpenXRValues
        if (-not (Test-WuWaHasActiveRuntime $after) -or $after.Active -ne $Plan.Target -or
            ($Plan.SavePrevious -and $after.Previous -ne $Plan.Before) -or
            ($Plan.ClearPrevious -and $after.HasPrevious)) { throw 'Runtime change did not read back correctly.' }
    } catch {
        $failure=$_.Exception.Message
        try {
            if ($hadActive) { Set-WuWaRuntimeRegistryValue 'ActiveRuntime' $Plan.Before }
            else { Remove-WuWaRuntimeActive }
            if ($Plan.HasPrevious) { Set-WuWaRuntimeRegistryValue 'PreviousActiveRuntime' $Plan.Previous }
            elseif ((Get-WuWaOpenXRValues).HasPrevious) { Remove-WuWaRuntimePrevious }
            $restored=Get-WuWaOpenXRValues
            if ((Test-WuWaHasActiveRuntime $restored) -ne $hadActive -or $restored.Active -ne $Plan.Before -or $restored.Previous -ne $Plan.Previous -or $restored.HasPrevious -ne $Plan.HasPrevious) { throw 'Restoration did not read back.' }
        } catch { throw "$failure Automatic restoration also failed: $($_.Exception.Message). Select the runtime in its own app before launching." }
        throw "$failure Original runtime restored."
    }
}
