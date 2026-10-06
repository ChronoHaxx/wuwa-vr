param(
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$ProductionSource = '',
    [switch]$Baseline
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $ProductionSource) { $ProductionSource = Join-Path $repo 'mod\uesdk\src\sdk' }
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw 'Choose a new evidence directory; existing evidence is never overwritten.' }
foreach ($name in @('cl','MSBuild','ffmpeg')) {
    if (Get-Process -Name $name -ErrorAction SilentlyContinue) { throw 'A heavy job is running; defer this fixture.' }
}
Add-Type -AssemblyName Microsoft.VisualBasic
$memory = New-Object Microsoft.VisualBasic.Devices.ComputerInfo
if ($memory.AvailablePhysicalMemory -lt 8GB -or $memory.AvailablePhysicalMemory / $memory.TotalPhysicalMemory -lt 0.15) {
    throw 'Require 8 GiB free and less than 85% physical memory use.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC toolchain unavailable.' }
$vcvars = Join-Path ($vs | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
$testSource = Join-Path $PSScriptRoot 'test_fenum.cpp'
$stubSource = Join-Path $PSScriptRoot 'test_fenum_stubs.hpp'
foreach ($p in @($output,$vcvars,$ProductionSource,$testSource,$stubSource)) {
    if ($p -match '["%\r\n]') { throw 'Unsupported batch path characters.' }
}
New-Item -ItemType Directory -Path $output | Out-Null
$snapshot = Join-Path $output 'source'
New-Item -ItemType Directory -Path $snapshot | Out-Null
New-Item -ItemType Directory -Path (Join-Path $snapshot 'spdlog'),(Join-Path $snapshot 'utility') | Out-Null
$records = @()
foreach ($name in @('FEnumProperty.cpp','FEnumProperty.hpp')) {
    $src = Join-Path $ProductionSource $name
    $dst = Join-Path $snapshot $name
    Copy-Item -LiteralPath $src -Destination $dst
    $hash = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -ne $hash) { throw 'Production snapshot mismatch.' }
    $records += [ordered]@{relative=$name; original=$src; snapshot=$dst; sha256=$hash}
}
Copy-Item -LiteralPath $stubSource -Destination (Join-Path $snapshot 'test_fenum_stubs.hpp')
foreach ($name in @('UObjectArray.hpp','UClass.hpp','UEnum.hpp','FProperty.hpp')) {
    [IO.File]::WriteAllText((Join-Path $snapshot $name), '#pragma once' + "`r`n" + '#include "test_fenum_stubs.hpp"' + "`r`n")
}
[IO.File]::WriteAllText((Join-Path $snapshot 'utility\String.hpp'), '#pragma once')
[IO.File]::WriteAllText((Join-Path $snapshot 'spdlog\spdlog.h'), "#pragma once`r`n#define SPDLOG_INFO(...) ((void)0)`r`n#define SPDLOG_ERROR(...) ((void)0)`r`n")
$exe = Join-Path $output 'fenum-discovery.exe'
$batchPath = Join-Path $output 'build.cmd'
$buildLog = Join-Path $output 'build.log'
$batch = @"
@echo off
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$output"
cl /nologo /std:c++20 /EHa /W3 /MT /O1 /DNOMINMAX /I"$snapshot" "$testSource" "$snapshot\FEnumProperty.cpp" /Fe:"$exe"
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($batchPath,$batch,[Text.Encoding]::ASCII)
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = $env:ComSpec
$start.Arguments = '/d /c ' + ('""{0}" > "{1}" 2>&1"' -f $batchPath,$buildLog)
$start.WorkingDirectory=$output; $start.UseShellExecute=$false; $start.CreateNoWindow=$true
$compiler = New-Object Diagnostics.Process; $compiler.StartInfo=$start
if (-not $compiler.Start()) { throw 'Cannot start fixture compiler.' }
$ownedHandle=$compiler.Handle
try { $compiler.PriorityClass='BelowNormal' } catch { }
if (-not $compiler.WaitForExit(120000)) {
    & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /PID $compiler.Id /T /F | Out-Null
    $compiler.WaitForExit(5000) | Out-Null
    throw 'Owned fixture compiler timed out.'
}
if ($compiler.ExitCode -ne 0) { Get-Content -LiteralPath $buildLog; throw 'Fixture compilation failed.' }
$cases = if ($Baseline) { @('valid80') } else {
    @('valid80','shifted','wrongclass','badindex','wrongidentity','unreadable','onesided','missingclass',
        'unregisteredclass','missingarray','missingproperty','missingsecond','nounderlying','reentry','beforeinit')
}
$results = @()
foreach ($case in $cases) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName=$exe; $start.WorkingDirectory=$output; $start.Arguments=$case
    if ($Baseline) { $start.Arguments += ' --baseline' }
    $start.UseShellExecute=$false; $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true; $start.RedirectStandardError=$true
    $child=New-Object Diagnostics.Process; $child.StartInfo=$start
    if (-not $child.Start()) { throw 'Cannot start fixture case.' }
    $ownedHandle=$child.Handle
    $outRead=$child.StandardOutput.ReadToEndAsync(); $errRead=$child.StandardError.ReadToEndAsync()
    try { $child.PriorityClass='BelowNormal' } catch { }
    $timedOut = -not $child.WaitForExit(10000)
    if ($timedOut) { $child.Kill(); $child.WaitForExit() }
    $text=$outRead.GetAwaiter().GetResult()+$errRead.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $output ($case+'.txt')),$text,[Text.Encoding]::UTF8)
    Write-Output $text.Trim()
    $results += [ordered]@{name=$case; exitCode=[int]$child.ExitCode; timedOut=$timedOut; passed=(-not $timedOut -and $child.ExitCode -eq 0)}
}
$evidence=[ordered]@{
    schema=1; mode=$(if ($Baseline) {'BaselineUnsafeBoundary'} else {'GuardedDiscovery'}); createdUtc=[DateTime]::UtcNow.ToString('o')
    productionSources=$records; testSourceSha256=(Get-FileHash -LiteralPath $testSource -Algorithm SHA256).Hash
    stubSourceSha256=(Get-FileHash -LiteralPath $stubSource -Algorithm SHA256).Hash
    executable=$exe; executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    cases=$results; passed=(@($results | Where-Object { -not $_.passed }).Count -eq 0)
    scope='Exact production enum source/header; controlled SDK boundary and real pointer reads. No game, native engine FName call, runtime or profile access; not remote-PC acceptance.'
}
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
if (-not $evidence.passed) { throw 'Enum fixture failed; preserve evidence and investigate.' }
Write-Output ('Evidence: ' + $output)
