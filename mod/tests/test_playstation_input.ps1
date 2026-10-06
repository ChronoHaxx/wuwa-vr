param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$output=[IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $output) { throw 'Use a fresh evidence directory.' }
foreach ($name in @('cl','MSBuild','ffmpeg')) {
    if (Get-Process -Name $name -ErrorAction SilentlyContinue) { throw 'Another heavy job is running.' }
}
Add-Type -AssemblyName Microsoft.VisualBasic
$memory=New-Object Microsoft.VisualBasic.Devices.ComputerInfo
if ($memory.AvailablePhysicalMemory -lt 8GB -or $memory.AvailablePhysicalMemory/$memory.TotalPhysicalMemory -lt .15) { throw 'Insufficient memory headroom.' }
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC unavailable.' }
$vcvars=Join-Path ($vs | Select-Object -First 1) 'VC\Auxiliary\Build\vcvars64.bat'
$source=Join-Path $PSScriptRoot 'test_playstation_input.cpp'
foreach ($p in @($output,$source,$vcvars)) { if ($p -match '["%\r\n]') { throw 'Unsupported batch path.' } }
New-Item -ItemType Directory -Path $output | Out-Null
$exe=Join-Path $output 'playstation-input.exe'
$batch=Join-Path $output 'build.cmd'
$log=Join-Path $output 'build.log'
[IO.File]::WriteAllText($batch,@"
@echo off
call "$vcvars"
if errorlevel 1 exit /b %errorlevel%
cd /d "$output"
cl /nologo /std:c++20 /EHsc /W4 /MT /O1 "$source" /Fe:"$exe"
exit /b %errorlevel%
"@,[Text.Encoding]::ASCII)
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$env:ComSpec; $info.Arguments='/d /c ' + ('""{0}" > "{1}" 2>&1"' -f $batch,$log)
$info.UseShellExecute=$false; $info.CreateNoWindow=$true; $info.WorkingDirectory=$output
$process=New-Object Diagnostics.Process; $process.StartInfo=$info
if (-not $process.Start()) { throw 'Compiler failed to start.' }
try { $process.PriorityClass='BelowNormal' } catch {}
if (-not $process.WaitForExit(60000)) { throw 'Compiler exceeded expected duration; inspect owned compiler before proceeding.' }
if ($process.ExitCode -ne 0) { Get-Content -LiteralPath $log; throw 'Compile failed.' }
$result=& $exe 2>&1
$exit=$LASTEXITCODE
$result | Set-Content -LiteralPath (Join-Path $output 'result.txt') -Encoding UTF8
$files=@($source,(Join-Path $PSScriptRoot '../uevr/src/utility/WuWaPlayStationInput.hpp'),(Join-Path $PSScriptRoot '../uevr/src/utility/WuWaPlayStationHid.hpp'))
$hashes=@($files | ForEach-Object { [ordered]@{path=[IO.Path]::GetFullPath($_);sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$evidence=[ordered]@{passed=$false;exitCode=$exit;files=$hashes;lifecycle=@();scope='Production decoder/source/menu policy plus real own-process Windows reader start/stop and exit-owner release. Shared read-only HID access; no output writes or game/profile/runtime changes. Not physical button acceptance or real DLL-unload acceptance.'}
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
$result
if ($exit -ne 0) { throw 'Fixture failed.' }
foreach ($mode in @('--reader-stop','--reader-process-exit')) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$exe; $info.Arguments=$mode; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    $child=New-Object Diagnostics.Process; $child.StartInfo=$info
    if (-not $child.Start()) { throw 'Lifecycle fixture did not start.' }
    $ownedHandle=$child.Handle
    try { $child.PriorityClass='BelowNormal' } catch {}
    $out=$child.StandardOutput.ReadToEndAsync(); $err=$child.StandardError.ReadToEndAsync()
    $timeout=-not $child.WaitForExit(10000)
    if ($timeout) { $child.Kill(); $child.WaitForExit() }
    $text=$out.GetAwaiter().GetResult()+$err.GetAwaiter().GetResult()
    $text | Set-Content -LiteralPath (Join-Path $output ($mode.Substring(2)+'.txt')) -Encoding UTF8
    $text.Trim()
    $evidence.lifecycle += [ordered]@{mode=$mode;exitCode=$child.ExitCode;timedOut=$timeout;passed=(-not $timeout -and $child.ExitCode -eq 0)}
    $evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
    if ($timeout -or $child.ExitCode -ne 0) { throw ('Lifecycle fixture failed: '+$mode) }
}
$evidence.passed=$true
$evidence | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'evidence.json') -Encoding UTF8
