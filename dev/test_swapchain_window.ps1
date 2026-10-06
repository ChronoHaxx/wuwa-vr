param(
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [switch]$UseWarp,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$output = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw 'Choose a new output directory; previous evidence is never overwritten.' }
$source = Join-Path $PSScriptRoot 'test_swapchain_window.cpp'
$policy = Join-Path $repo 'mod\uevr\src\utility\WuWaSwapchainWindow.hpp'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
foreach ($path in @($source,$policy,$vswhere)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing dependency: $path" }
}
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC C++ toolchain not found.' }
$vcvars = Join-Path ($vs | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($path in @($source,$policy,$output,$vcvars)) {
    if ($path -match '["%\r\n]') { throw 'Paths with quotes, percent signs or line breaks are unsupported by the MSVC batch runner.' }
}
New-Item -ItemType Directory -Path $output | Out-Null
$exe = Join-Path $output 'swapchain-window.exe'
$cmd = Join-Path $output 'build.cmd'
$buildLog = Join-Path $output 'build.log'
# One small translation unit, serial compiler invocation, no backend build.
$batch = @"
@echo off
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$output"
cl /nologo /std:c++20 /EHsc /W4 /MT /O1 "$source" /Fe:"$exe" /link d3d11.lib d3d12.lib dxgi.lib user32.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($cmd, $batch, [Text.Encoding]::ASCII)
# Keep the native process handle from Start(), avoiding PS5 Start-Process's
# detached Process object / null ExitCode behavior after fast child exit.
$buildInfo = New-Object Diagnostics.ProcessStartInfo
$buildInfo.FileName = $env:ComSpec
$buildInfo.Arguments = '/d /c ' + ('""{0}" > "{1}" 2>&1"' -f $cmd,$buildLog)
$buildInfo.UseShellExecute = $false; $buildInfo.CreateNoWindow = $true
$buildInfo.WorkingDirectory = $output
$build = New-Object Diagnostics.Process
$build.StartInfo = $buildInfo
if (-not $build.Start()) { throw 'Could not start isolated probe compiler.' }
$buildHandle = $build.Handle
try { $build.PriorityClass = 'BelowNormal' } catch { }
if (-not $build.WaitForExit(120000)) {
    # Only this freshly-created compiler command and its descendants. A build
    # timeout must not leave an invisible compiler running after the runner ends.
    & (Join-Path $env:SystemRoot 'System32\taskkill.exe') /PID $build.Id /T /F | Out-Null
    $build.WaitForExit(10000) | Out-Null
    throw "Isolated compiler timed out. Build log: $buildLog"
}
$buildExitCode = [int]$build.ExitCode
if ($buildExitCode -ne 0) { Get-Content -LiteralPath $buildLog; throw "Probe build failed ($buildExitCode)." }
$evidence = [ordered]@{
    schema = 1; createdUtc = [DateTime]::UtcNow.ToString('o'); os = [Environment]::OSVersion.VersionString
    driver = $(if ($UseWarp) { 'WARP' } else { 'hardware' }); buildOnly = [bool]$BuildOnly
    source = $source; sourceSha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    resolverSource = $policy; resolverSha256 = (Get-FileHash -LiteralPath $policy -Algorithm SHA256).Hash
    executable = $exe; executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    scope = 'Own hidden DXGI windows and production window resolver; no Present, game/backend/VR runtime or profile access.'
}
if (-not $BuildOnly) {
    $stdout = Join-Path $output 'probe.stdout.txt'; $stderr = Join-Path $output 'probe.stderr.txt'
    $childInfo = New-Object Diagnostics.ProcessStartInfo
    $childInfo.FileName = $exe; $childInfo.WorkingDirectory = $output
    $childInfo.UseShellExecute = $false; $childInfo.CreateNoWindow = $true
    $childInfo.RedirectStandardOutput = $true; $childInfo.RedirectStandardError = $true
    if ($UseWarp) { $childInfo.Arguments = '--warp' }
    $child = New-Object Diagnostics.Process
    $child.StartInfo = $childInfo
    if (-not $child.Start()) { throw 'Could not start isolated window resolver test.' }
    $childHandle = $child.Handle
    # Drain both pipes concurrently; waiting before reading can deadlock if a
    # driver prints enough diagnostics to fill the pipe buffer.
    $stdoutRead = $child.StandardOutput.ReadToEndAsync()
    $stderrRead = $child.StandardError.ReadToEndAsync()
    try { $child.PriorityClass = 'BelowNormal' } catch { }
    if (-not $child.WaitForExit(30000)) {
        # Only the exact child created here; never enumerate or terminate games.
        $child.Kill(); $child.WaitForExit(); $evidence.timedOut = $true
    } else { $evidence.timedOut = $false }
    $childExitCode = [int]$child.ExitCode
    [IO.File]::WriteAllText($stdout, $stdoutRead.GetAwaiter().GetResult(), [Text.Encoding]::UTF8)
    [IO.File]::WriteAllText($stderr, $stderrRead.GetAwaiter().GetResult(), [Text.Encoding]::UTF8)
    $evidence.exitCode = $childExitCode
    $evidence.passed = (-not $evidence.timedOut -and $childExitCode -eq 0)
    Get-Content -LiteralPath $stdout
    if ((Get-Item -LiteralPath $stderr).Length) { Get-Content -LiteralPath $stderr }
}
$evidence | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
if (-not $BuildOnly -and -not $evidence.passed) { throw "Probe failed or unsupported. Review evidence in $output" }
Write-Output "Evidence: $output"
