# Read/plan/write helpers. Importing this file never changes the runtime.
$script:WuWaOpenXRKey='HKLM:\SOFTWARE\Khronos\OpenXR\1'
function Get-WuWaOpenXRValues {
    $r=Get-ItemProperty -LiteralPath $script:WuWaOpenXRKey -ErrorAction Stop
    $previous=$r.PSObject.Properties['PreviousActiveRuntime']
    [pscustomobject]@{Active=[string]$r.ActiveRuntime;Previous=$(if($previous){[string]$previous.Value}else{''});HasPrevious=($null -ne $previous)}
}
function Assert-WuWaRuntimeManifest([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path) -or $Path -match '["\r\n]' -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw 'The OpenXR runtime manifest is missing or invalid. Select your headset runtime in its own app first.' }
    $json=Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $library=[string]$json.runtime.library_path
    if (-not $library) { throw 'The OpenXR manifest has no runtime library.' }
    if (-not [IO.Path]::IsPathRooted($library)) { $library=Join-Path (Split-Path -Parent $Path) $library }
    if (-not (Test-Path -LiteralPath $library -PathType Leaf)) { throw 'The OpenXR runtime library is missing.' }
}
function Test-WuWaSimulatorManifest([string]$Path) {
    # A different extracted package may own the current simulator. Do not save
    # that simulator as the headset when switching to a new package's copy.
    try {
        $manifest=Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json
        return ($manifest.runtime.name -eq 'OpenXR Simulator' -and
            [IO.Path]::GetFileName([string]$manifest.runtime.library_path) -eq 'openxr_simulator.dll')
    } catch { return $false }
}
function Get-WuWaRuntimePlan([ValidateSet('simulator','headset')][string]$Mode,[string]$Root,$Values) {
    $simulator=[IO.Path]::GetFullPath((Join-Path $Root 'dev-tools\OpenXR-Simulator\openxr_simulator.json'))
    $missingSimulator=(-not (Test-Path -LiteralPath $Values.Active -PathType Leaf)) -and
        [IO.Path]::GetFileName($Values.Active) -eq 'openxr_simulator.json'
    $isSimulator=$Values.Active -eq $simulator -or (Test-WuWaSimulatorManifest $Values.Active) -or $missingSimulator
    $target=if ($Mode -eq 'simulator') { $simulator } elseif ($isSimulator) { $Values.Previous } else { $Values.Active }
    if (-not $target -or ($Mode -eq 'headset' -and ($target -eq $simulator -or (Test-WuWaSimulatorManifest $target)))) { throw 'No saved headset runtime is available. Set SteamVR, Quest Link or Virtual Desktop as OpenXR runtime in its own app first.' }
    Assert-WuWaRuntimeManifest $target
    if ($Mode -eq 'simulator' -and -not $isSimulator) { Assert-WuWaRuntimeManifest $Values.Active }
    [pscustomobject]@{Mode=$Mode;Before=$Values.Active;Previous=$Values.Previous;HasPrevious=$Values.HasPrevious;Target=$target;Changed=($target -ne $Values.Active);SavePrevious=($Mode -eq 'simulator' -and -not $isSimulator);ClearPrevious=($Mode -eq 'headset' -and $isSimulator)}
}
function Set-WuWaRuntimeRegistryValue([string]$Name,[string]$Value) {
    Set-ItemProperty -LiteralPath $script:WuWaOpenXRKey -Name $Name -Value $Value -Type String -ErrorAction Stop
}
function Remove-WuWaRuntimePrevious {
    Remove-ItemProperty -LiteralPath $script:WuWaOpenXRKey -Name PreviousActiveRuntime -ErrorAction Stop
}
function Invoke-WuWaRuntimePlan($Plan) {
    $current=Get-WuWaOpenXRValues
    if ($current.Active -ne $Plan.Before -or $current.Previous -ne $Plan.Previous -or $current.HasPrevious -ne $Plan.HasPrevious) { throw 'The OpenXR runtime changed while waiting. Refresh the dashboard and choose again.' }
    if (-not $Plan.Changed) { return }
    try {
        if ($Plan.SavePrevious) { Set-WuWaRuntimeRegistryValue 'PreviousActiveRuntime' $Plan.Before }
        Set-WuWaRuntimeRegistryValue 'ActiveRuntime' $Plan.Target
        if ($Plan.ClearPrevious -and $Plan.HasPrevious) { Remove-WuWaRuntimePrevious }
        $after=Get-WuWaOpenXRValues
        if ($after.Active -ne $Plan.Target -or
            ($Plan.SavePrevious -and $after.Previous -ne $Plan.Before) -or
            ($Plan.ClearPrevious -and $after.HasPrevious)) { throw 'Runtime change did not read back correctly.' }
    } catch {
        $failure=$_.Exception.Message
        try {
            Set-WuWaRuntimeRegistryValue 'ActiveRuntime' $Plan.Before
            if ($Plan.HasPrevious) { Set-WuWaRuntimeRegistryValue 'PreviousActiveRuntime' $Plan.Previous }
            elseif ((Get-WuWaOpenXRValues).HasPrevious) { Remove-WuWaRuntimePrevious }
            $restored=Get-WuWaOpenXRValues
            if ($restored.Active -ne $Plan.Before -or $restored.Previous -ne $Plan.Previous -or $restored.HasPrevious -ne $Plan.HasPrevious) { throw 'Restoration did not read back.' }
        } catch { throw "$failure Automatic restoration also failed: $($_.Exception.Message). Select the runtime in its own app before launching." }
        throw "$failure Original runtime restored."
    }
}
