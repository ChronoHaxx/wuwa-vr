param(
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$ProductionSource = '',
    [switch]$ExpectOriginal
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $ProductionSource) {
    $overlay = Join-Path $repo 'mod\uevr\src\WindowFilter.cpp'
    $ProductionSource = if (Test-Path -LiteralPath $overlay) { $overlay } else {
        Join-Path (Split-Path $repo -Parent) 'upstream\UEVR\src\WindowFilter.cpp'
    }
}
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw 'Choose a new output directory; previous evidence is never overwritten.' }
$source = Join-Path $PSScriptRoot 'test_window_filter_regression.cpp'
$include = Split-Path $ProductionSource -Parent
$header = Join-Path $include 'WindowFilter.hpp'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
foreach ($path in @($source,$ProductionSource,$header,$vswhere)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing dependency: $path" }
}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC C++ toolchain not found.' }
$vcvars = Join-Path ($vs | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($path in @($source,$ProductionSource,$include,$output,$vcvars)) {
    if ($path -match '["%\r\n]') { throw 'Paths containing quotes, percent signs or line breaks are unsupported.' }
}
Add-Type -AssemblyName Microsoft.VisualBasic
$memory = New-Object Microsoft.VisualBasic.Devices.ComputerInfo
if ($memory.AvailablePhysicalMemory -lt 2GB) { throw 'Less than 2 GiB available; defer the isolated compiler.' }
New-Item -ItemType Directory -Path $output | Out-Null
$exe = Join-Path $output 'window-filter-regression.exe'
$cmd = Join-Path $output 'build.cmd'
$buildLog = Join-Path $output 'build.log'
$batch = @"
@echo off
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$output"
cl /nologo /std:c++20 /EHsc /W4 /MT /O1 /I"$include" "$source" "$ProductionSource" /Fe:"$exe" /link user32.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($cmd, $batch, [Text.Encoding]::ASCII)
$buildInfo = New-Object Diagnostics.ProcessStartInfo
$buildInfo.FileName = $env:ComSpec
$buildInfo.Arguments = '/d /c ' + ('""{0}" > "{1}" 2>&1"' -f $cmd,$buildLog)
$buildInfo.UseShellExecute = $false; $buildInfo.CreateNoWindow = $true; $buildInfo.WorkingDirectory = $output
$build = New-Object Diagnostics.Process
$build.StartInfo = $buildInfo
if (-not $build.Start()) { throw 'Could not start isolated compiler.' }
$buildHandle = $build.Handle
try { $build.PriorityClass = 'BelowNormal' } catch { }
if (-not $build.WaitForExit(60000)) {
    & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /PID $build.Id /T /F | Out-Null
    $build.WaitForExit(5000) | Out-Null
    throw "Owned compiler timed out: $buildLog"
}
if ($build.ExitCode -ne 0) { Get-Content -LiteralPath $buildLog; throw "Probe build failed: $($build.ExitCode)" }
$evidence = [ordered]@{
    schema = 1; createdUtc = [DateTime]::UtcNow.ToString('o'); os = [Environment]::OSVersion.VersionString
    availableGiB = [Math]::Round($memory.AvailablePhysicalMemory/1GB,2)
    source = $source; sourceSha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    productionSource = $ProductionSource; productionSha256 = (Get-FileHash -LiteralPath $ProductionSource -Algorithm SHA256).Hash
    productionHeaderSha256 = (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash
    executable = $exe; executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    expectOriginal = [bool]$ExpectOriginal
    scope = 'Actual production WindowFilter source; own hidden windows and bounded child processes. No game, backend, GPU, runtime or profile actions. Reproduction/repair does not establish the remote game failure cause.'
    tests = @(); passed = $true
}
$modes = @('active-control','empty-queue','query-lock')
if (-not $ExpectOriginal) { $modes += @('nonpumping-shutdown','late-response') }
foreach ($mode in $modes) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $exe; $info.Arguments = '--' + $mode; $info.WorkingDirectory = $output
    if ($ExpectOriginal) { $info.Arguments += ' --expect-original' }
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $child = New-Object Diagnostics.Process
    $child.StartInfo = $info
    if (-not $child.Start()) { throw "Could not start $mode." }
    $childHandle = $child.Handle
    $stdoutRead = $child.StandardOutput.ReadToEndAsync(); $stderrRead = $child.StandardError.ReadToEndAsync()
    try { $child.PriorityClass = 'BelowNormal' } catch { }
    $timedOut = -not $child.WaitForExit(10000)
    if ($timedOut) { $child.Kill(); $child.WaitForExit() }
    $stdout = $stdoutRead.GetAwaiter().GetResult(); $stderr = $stderrRead.GetAwaiter().GetResult()
    [IO.File]::WriteAllText((Join-Path $output "$mode.stdout.txt"), $stdout, [Text.Encoding]::UTF8)
    [IO.File]::WriteAllText((Join-Path $output "$mode.stderr.txt"), $stderr, [Text.Encoding]::UTF8)
    $passed = -not $timedOut -and $child.ExitCode -eq 0
    $evidence.tests += [ordered]@{ mode=$mode; pid=$child.Id; exitCode=[int]$child.ExitCode; timedOut=$timedOut; passed=$passed }
    if (-not $passed) { $evidence.passed = $false }
    Write-Output $stdout
    if ($stderr) { Write-Output $stderr }
    $child.Dispose()
}
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
if (-not $evidence.passed) { throw "Reproduction failed or behavior differs; inspect $output" }
Write-Output "Evidence: $output"
