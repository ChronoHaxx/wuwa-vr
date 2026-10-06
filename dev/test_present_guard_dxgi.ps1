param(
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$ProductionSnapshot = '',
    [switch]$Baseline,
    [switch]$BuildOnly,
    [ValidateSet('11-pending','12-pending','12-present1-pending','11-retired','12-retired','12-present1-retired','stable','delegation','unrecoverable','e9-relay','cross-class','repatch')]
    [string[]]$Cases = @()
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$upstream = Join-Path (Split-Path $repo -Parent) 'upstream\UEVR'
$buildRoot = Join-Path $upstream 'out\build\x64-RelWithDebInfo'
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw 'Choose a new evidence directory; previous results are never overwritten.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC toolchain unavailable.' }
$vcvars = Join-Path ($vs | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
$kananInclude = Join-Path $buildRoot '_deps\kananlib-src\include'
$bddInclude = Join-Path $buildRoot '_deps\bddisasm-src\inc'
$spdInclude = Join-Path $upstream 'dependencies\submodules\spdlog\include'
$libPaths = @(
    (Join-Path $buildRoot '_deps\kananlib-build\RelWithDebInfo\kananlib.lib'),
    (Join-Path $buildRoot 'RelWithDebInfo\spdlog.lib'),
    (Join-Path $buildRoot '_deps\bddisasm-build\RelWithDebInfo\bddisasm.lib'),
    (Join-Path $buildRoot '_deps\bddisasm-build\RelWithDebInfo\bdshemu.lib')
)
$source = Join-Path $PSScriptRoot 'test_present_guard_dxgi.cpp'
$inputs = [ordered]@{}
foreach ($relative in @('hooks\D3D11Hook.cpp','hooks\D3D12Hook.cpp','hooks\D3D11Hook.hpp','hooks\D3D12Hook.hpp','WindowFilter.cpp','WindowFilter.hpp','utility\WuWaSwapchainWindow.hpp','utility\WuWaPresentGuard.hpp')) {
    $path = Join-Path $repo ('mod\uevr\src\' + $relative)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $path = Join-Path $upstream ('src\' + $relative) }
    if ($ProductionSnapshot) {
        $name = if ($relative.StartsWith('hooks\')) { Split-Path $relative -Leaf } else { $relative }
        $path = Join-Path $ProductionSnapshot $name
        # Older pinned hooks do not include the newly added recovery header.
        # Do not mislabel today's unused helper as part of that old snapshot.
        if ($relative -eq 'utility\WuWaPresentGuard.hpp' -and -not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Production source missing: $relative" }
    $inputs[$relative] = $path
}
foreach ($path in @($output,$vcvars,$source,$kananInclude,$bddInclude,$spdInclude) + $libPaths + @($inputs.Values)) {
    if ($path -match '["%\r\n]') { throw 'Unsupported path characters for isolated MSVC batch.' }
}
foreach ($path in $libPaths) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Built dependency missing: $path" } }
Add-Type -AssemblyName Microsoft.VisualBasic
$memory = New-Object Microsoft.VisualBasic.Devices.ComputerInfo
if ($memory.AvailablePhysicalMemory -lt 8GB -or ($memory.AvailablePhysicalMemory / $memory.TotalPhysicalMemory) -lt 0.15) {
    throw 'Defer isolated compiler: require at least8GiB free and memory load at most85%.'
}
New-Item -ItemType Directory -Path $output | Out-Null
$fixture = Join-Path $output 'source'
New-Item -ItemType Directory -Path $fixture | Out-Null
New-Item -ItemType Directory -Path (Join-Path $fixture 'utility') | Out-Null
$records = @()
foreach ($relative in $inputs.Keys) {
    $name = if ($relative.StartsWith('hooks\')) { Split-Path $relative -Leaf } else { $relative }
    $destination = Join-Path $fixture $name
    Copy-Item -LiteralPath $inputs[$relative] -Destination $destination
    $hash = (Get-FileHash -LiteralPath $inputs[$relative] -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash) { throw 'Production source snapshot mismatch.' }
    $records += [ordered]@{relative=$relative; original=$inputs[$relative]; snapshot=$destination; sha256=$hash}
}
$framework = @'
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>
#include <mutex>
// TEST STUB ONLY. No Framework lifecycle, monitor, renderer or game code.
class Framework {
    std::recursive_mutex monitor;
public:
    std::recursive_mutex& get_hook_monitor_mutex() { return monitor; }
};
extern std::unique_ptr<Framework> g_framework;
'@
$frameworkPath = Join-Path $fixture 'Framework.hpp'
[IO.File]::WriteAllText($frameworkPath,$framework,[Text.Encoding]::ASCII)
$exe = Join-Path $output 'present-guard-dxgi.exe'
$cmd = Join-Path $output 'build.cmd'
$buildLog = Join-Path $output 'build.log'
$libraries = ($libPaths | ForEach-Object { '"' + $_ + '"' }) -join ' '
$helperDefine = if ($inputs.Contains('utility\WuWaPresentGuard.hpp')) { '/DWUWA_GUARD_AVAILABLE' } else { '' }
$batch = @"
@echo off
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$output"
cl /nologo /std:c++latest /EHa /W3 /MT /O1 /DNDEBUG /DNOMINMAX /DSPDLOG_COMPILED_LIB /DFMT_UNICODE=0 $helperDefine /FI"$frameworkPath" /I"$fixture" /I"$kananInclude" /I"$bddInclude" /I"$spdInclude" "$source" "$fixture\D3D11Hook.cpp" "$fixture\D3D12Hook.cpp" "$fixture\WindowFilter.cpp" /Fe:"$exe" /link $libraries d3d11.lib d3d12.lib dxgi.lib user32.lib shlwapi.lib psapi.lib dbghelp.lib advapi32.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($cmd,$batch,[Text.Encoding]::ASCII)
$info = New-Object Diagnostics.ProcessStartInfo
$info.FileName=$env:ComSpec; $info.Arguments='/d /c ' + ('""{0}" > "{1}" 2>&1"' -f $cmd,$buildLog)
$info.WorkingDirectory=$output; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
$compiler = New-Object Diagnostics.Process; $compiler.StartInfo=$info
if (-not $compiler.Start()) { throw 'Could not start isolated compiler.' }
$ownedHandle=$compiler.Handle
try { $compiler.PriorityClass='BelowNormal' } catch { }
if (-not $compiler.WaitForExit(120000)) {
    & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /PID $compiler.Id /T /F | Out-Null
    $compiler.WaitForExit(5000) | Out-Null
    throw "Owned compiler timeout: $buildLog"
}
if ($compiler.ExitCode -ne 0) { Get-Content -LiteralPath $buildLog; throw "Actual hook fixture build failed: $($compiler.ExitCode)" }
$receipt = [ordered]@{
    schema=1; createdUtc=[DateTime]::UtcNow.ToString('o'); baseline=[bool]$Baseline; buildOnly=[bool]$BuildOnly
    source=$source; sourceSha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    productionSources=$records; productionSnapshot=$ProductionSnapshot
    executable=$exe; executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    scope='Actual production hooks/filter; Framework mutex-only stub; private hidden hardware DXGI chains. Inline detour is a bounded controlled model, not Steam implementation. No game/backend/runtime/profile access.'
    cases=@()
}
if (-not $BuildOnly) {
    if (-not $Cases.Count) {
        $Cases = @('11-pending','12-pending','12-present1-pending','11-retired','12-retired','12-present1-retired','unrecoverable')
        if (-not $Baseline) { $Cases += @('stable','delegation','e9-relay','cross-class','repatch') }
    }
    foreach ($case in $Cases) {
        $info = New-Object Diagnostics.ProcessStartInfo
        $info.FileName=$exe; $info.Arguments=$case + $(if ($Baseline) { ' --baseline' } else { '' })
        $info.WorkingDirectory=$output; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
        $child = New-Object Diagnostics.Process; $child.StartInfo=$info
        if (-not $child.Start()) { throw 'Could not start owned recovery fixture.' }
        $ownedHandle=$child.Handle
        try { $child.PriorityClass='BelowNormal' } catch { }
        $stdout=$child.StandardOutput.ReadToEndAsync(); $stderr=$child.StandardError.ReadToEndAsync()
        $timedOut=-not $child.WaitForExit(30000)
        if ($timedOut) { $child.Kill(); $child.WaitForExit() }
        $code=[int]$child.ExitCode
        $outPath=Join-Path $output ($case+'.stdout.txt'); $errPath=Join-Path $output ($case+'.stderr.txt')
        [IO.File]::WriteAllText($outPath,$stdout.GetAwaiter().GetResult(),[Text.Encoding]::UTF8)
        [IO.File]::WriteAllText($errPath,$stderr.GetAwaiter().GetResult(),[Text.Encoding]::UTF8)
        $receipt.cases += [ordered]@{case=$case; exitCode=$code; timedOut=$timedOut; passed=($code -eq 0 -and -not $timedOut); stdout=$outPath; stderr=$errPath}
        Get-Content -LiteralPath $outPath
        if ((Get-Item -LiteralPath $errPath).Length) { Get-Content -LiteralPath $errPath }
        if ($code -ne 0 -or $timedOut) { break }
    }
}
$receipt.passed=($BuildOnly -or (($receipt.cases | Where-Object { -not $_.passed }).Count -eq 0 -and $receipt.cases.Count -eq $Cases.Count))
$receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
if (-not $receipt.passed) { throw "Present recovery fixture failed or unsupported; inspect $output" }
Write-Output "Evidence: $output"
