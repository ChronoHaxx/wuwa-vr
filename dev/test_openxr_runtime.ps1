# Inert Windows PowerShell 5.1 fixtures: registry commands are replaced below.
# No runtime DLL is loaded, no UAC is requested and no application is started.
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'launcher\dev\WuWaOpenXR.ps1')
$fixture=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ('wuwa-openxr-fixture-'+[guid]::NewGuid().ToString('N'))))
$fixturePrefix=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
$script:systemFolder=[Environment]::GetFolderPath([Environment+SpecialFolder]::System)
$script:fakeSystem=Join-Path $fixture 'system'
$script:passed=0
function Check([bool]$Ok,[string]$Message) { if (-not $Ok) { throw $Message } }
function Throws([scriptblock]$Action,[string]$Pattern) {
    $caught=$null
    try { & $Action } catch { $caught=$_.Exception.Message }
    Check ($caught -and $caught -match $Pattern) ('Expected '+$Pattern+'; received '+$caught)
}
function Test([string]$Name,[scriptblock]$Action) { & $Action; $script:passed++; Write-Output ('PASS: '+$Name) }
function Reset-Registry([string]$Active='',[string]$Previous='', [bool]$HasActive=$false) {
    $script:present=$HasActive -or $Previous -ne ''
    $script:values=@{}
    if ($HasActive) { $script:values.ActiveRuntime=$Active }
    if ($Previous -ne '') { $script:values.PreviousActiveRuntime=$Previous }
    $script:writes=0; $script:failWrite=$false; $script:denyRead=$false
}
function Get-ItemProperty([string]$LiteralPath,[string]$ErrorAction) {
    Check ($LiteralPath -eq $script:WuWaOpenXRKey) 'Unexpected registry read'
    if ($script:denyRead) { throw [UnauthorizedAccessException]::new('Fixture access denied') }
    if (-not $script:present) { throw [System.Management.Automation.ItemNotFoundException]::new('Fixture registry key absent') }
    return [pscustomobject]$script:values
}
function Test-Path([string]$LiteralPath,[string]$PathType) {
    if ($LiteralPath -eq $script:WuWaOpenXRKey) { return $script:present }
    if ($LiteralPath.StartsWith($script:systemFolder+'\',[StringComparison]::OrdinalIgnoreCase)) {
        $LiteralPath=Join-Path $script:fakeSystem ([IO.Path]::GetFileName($LiteralPath))
    }
    if ($PathType) { return Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath -PathType $PathType }
    return Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath
}
function New-Item([string]$Path,[switch]$Force,[string]$ErrorAction) {
    Check ($Path -eq $script:WuWaOpenXRKey) 'Unexpected registry creation'
    $script:present=$true
}
function Set-ItemProperty([string]$LiteralPath,[string]$Name,[string]$Value,[string]$Type,[string]$ErrorAction) {
    Check ($LiteralPath -eq $script:WuWaOpenXRKey -and $script:present) 'Unexpected registry write'
    $script:writes++; $script:values[$Name]=$Value
    if ($script:failWrite -and $Name -eq 'ActiveRuntime') { $script:failWrite=$false; throw 'Fixture write failed after mutation' }
}
function Remove-ItemProperty([string]$LiteralPath,[string]$Name,[string]$ErrorAction) {
    Check ($LiteralPath -eq $script:WuWaOpenXRKey) 'Unexpected registry removal'
    $script:writes++; $script:values.Remove($Name)
}
function Make-Manifest([string]$Path,[string]$Library,[string]$Name) {
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $Path))
    [IO.File]::WriteAllText($Path,(@{runtime=@{library_path=$Library;name=$Name}}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $dll=if ([IO.Path]::IsPathRooted($Library)) {$Library} else {Join-Path (Split-Path -Parent $Path) $Library}
    [IO.File]::WriteAllText($dll,'Inert fixture; never load.')
}
try {
    [void][IO.Directory]::CreateDirectory($script:fakeSystem)
    foreach($name in @('MSVCP140.dll','VCRUNTIME140.dll','VCRUNTIME140_1.dll')) { [IO.File]::WriteAllText((Join-Path $script:fakeSystem $name),'Inert fixture; never load.') }
    $root=Join-Path $fixture 'package\app'
    $sim=Join-Path $root 'dev-tools\OpenXR-Simulator\openxr_simulator.json'
    Make-Manifest $sim 'openxr_simulator.dll' 'OpenXR Simulator'
    $headset=Join-Path $fixture 'headset\runtime.json'
    Make-Manifest $headset 'runtime.dll' 'Fixture headset'
    $oldSim=Join-Path $fixture 'older-package\openxr_simulator.json'
    Make-Manifest $oldSim 'openxr_simulator.dll' 'OpenXR Simulator'

    Test 'absent registration permits explicit simulator selection without a headset backup' {
        Reset-Registry
        $v=Get-WuWaOpenXRValues
        Check (-not $v.HasActive -and $v.Active -eq '') 'Absence lost'
        Assert-WuWaExpectedRuntime '' $v
        $plan=Get-WuWaRuntimePlan simulator $root $v
        Check ($plan.Changed -and -not $plan.SavePrevious -and $script:writes -eq 0) 'Planning mutated or fabricated a headset'
        Invoke-WuWaRuntimePlan $plan
        Check ($script:values.ActiveRuntime -eq $sim -and -not $script:values.ContainsKey('PreviousActiveRuntime')) 'Fresh registration incorrect'
    }
    Test 'an empty existing registration is not accepted as absent' {
        Reset-Registry '' '' $true
        Throws { Assert-WuWaExpectedRuntime '' (Get-WuWaOpenXRValues) } 'runtime changed'
        Throws { Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues) } 'path is empty'
        Check ($script:writes -eq 0) 'Invalid registration mutated'
    }
    Test 'access denied is not mistaken for no registration' {
        Reset-Registry; $script:denyRead=$true
        Throws { Get-WuWaOpenXRValues } 'access denied'
        Check ($script:writes -eq 0) 'Unreadable registration mutated'
    }
    Test 'registration installed during permission prompt aborts the old absent plan' {
        Reset-Registry; $plan=Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues)
        Reset-Registry $headset '' $true
        Throws { Invoke-WuWaRuntimePlan $plan } 'changed while waiting'
        Check ($script:writes -eq 0 -and $script:values.ActiveRuntime -eq $headset) 'Concurrent headset replaced'
    }
    Test 'failed first registration restores absence and retains an existing backup' {
        Reset-Registry '' $headset
        $plan=Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues)
        $script:failWrite=$true
        Throws { Invoke-WuWaRuntimePlan $plan } 'Original runtime restored'
        Check (-not $script:values.ContainsKey('ActiveRuntime') -and $script:values.PreviousActiveRuntime -eq $headset) 'Rollback lost absence/backup'
    }
    Test 'headset to simulator then headset preserves and restores the real headset' {
        Reset-Registry $headset '' $true
        Invoke-WuWaRuntimePlan (Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues))
        Check ($script:values.PreviousActiveRuntime -eq $headset) 'Headset was not backed up'
        Invoke-WuWaRuntimePlan (Get-WuWaRuntimePlan headset $root (Get-WuWaOpenXRValues))
        Check ($script:values.ActiveRuntime -eq $headset -and -not $script:values.ContainsKey('PreviousActiveRuntime')) 'Headset not restored'
    }
    Test 'another package simulator migrates without replacing saved headset' {
        Reset-Registry $oldSim $headset $true
        $plan=Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues)
        Check ($plan.Changed -and -not $plan.SavePrevious) 'Foreign simulator treated as headset'
        Invoke-WuWaRuntimePlan $plan
        Check ($script:values.ActiveRuntime -eq $sim -and $script:values.PreviousActiveRuntime -eq $headset) 'Migration changed headset backup'
    }
    Test 'missing old package simulator remains repairable' {
        Reset-Registry (Join-Path $fixture 'removed\openxr_simulator.json') $headset $true
        Invoke-WuWaRuntimePlan (Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues))
        Check ($script:values.ActiveRuntime -eq $sim -and $script:values.PreviousActiveRuntime -eq $headset) 'Missing package migration failed'
    }
    Test 'selecting the current simulator is a no-op' {
        Reset-Registry $sim $headset $true
        Invoke-WuWaRuntimePlan (Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues))
        Check ($script:writes -eq 0) 'No-op wrote registry'
    }
    Test 'missing CRT is actionable and detected before any registry mutation' {
        Reset-Registry
        $crt=Join-Path $script:fakeSystem 'MSVCP140.dll'
        [IO.File]::Delete($crt)
        Throws { Get-WuWaRuntimePlan simulator $root (Get-WuWaOpenXRValues) } 'Visual C\+\+ 2015-2022 Redistributable \(x64\).*MSVCP140.dll'
        Check ($script:writes -eq 0) 'Missing dependency wrote registry'
        [IO.File]::WriteAllText($crt,'Inert fixture; never load.')
    }
    Test 'UTF-8 manifest with non-ASCII library path is decoded consistently on PS5' {
        $unicode=([char]0x6D4B).ToString()+([char]0x8BD5).ToString()+'.dll'
        $manifest=Join-Path $fixture 'utf8\runtime.json'
        Make-Manifest $manifest $unicode 'Fixture'
        Assert-WuWaRuntimeManifest $manifest
    }
    Test 'stale expected path and unavailable headset are rejected without mutation' {
        Reset-Registry $sim '' $true
        Throws { Assert-WuWaExpectedRuntime $oldSim (Get-WuWaOpenXRValues) } 'runtime changed'
        Throws { Get-WuWaRuntimePlan headset $root (Get-WuWaOpenXRValues) } 'No saved headset'
        Check ($script:writes -eq 0) 'Rejected request wrote registry'
    }
    Test 'PS5 File handoff preserves an explicit empty expected registration' {
        $probe=Join-Path $fixture 'argument-probe.ps1'
        [IO.File]::WriteAllText($probe,'param([Parameter(Mandatory)][AllowEmptyString()][string]$ExpectedActive) if ($ExpectedActive -ne '''') { exit 7 }; Write-Output ''EMPTY_EXPECTED_OK''')
        $info=[Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'powershell.exe'))
        $info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$probe+'" -ExpectedActive ""'
        $info.UseShellExecute=$false; $info.CreateNoWindow=$true
        $info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
        $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
        $child=[Diagnostics.Process]::Start($info)
        try {
            $stdout=$child.StandardOutput.ReadToEndAsync(); $stderr=$child.StandardError.ReadToEndAsync()
            Check ($child.WaitForExit(10000)) 'Inert argument probe did not exit'
            Check ($child.ExitCode -eq 0 -and $stdout.Result.Trim() -eq 'EMPTY_EXPECTED_OK') ('Empty argument lost: '+$stderr.Result)
        } finally { $child.Dispose() }
    }
    Write-Output ("PASS: $script:passed inert OpenXR runtime groups; no real registry/process/runtime changes.")
} finally {
    # Only delete this test's resolved GUID directory underneath the temp root.
    if (-not $fixture.StartsWith($fixturePrefix,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($fixture) -notmatch '^wuwa-openxr-fixture-[a-f0-9]{32}$') { throw 'Unsafe fixture cleanup path' }
    if ([IO.Directory]::Exists($fixture)) { Microsoft.PowerShell.Management\Remove-Item -LiteralPath $fixture -Recurse -Force }
}
