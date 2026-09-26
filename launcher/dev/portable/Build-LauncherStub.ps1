# Compile WuWa VR Launcher.exe from launcher-stub.c with the installed MSVC
# toolset. Writes only to -Output (a new or empty folder). Original project tooling; licence scope: see LICENSE.md.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$source=$PSScriptRoot
$Output=[IO.Path]::GetFullPath($Output)
if ((Test-Path -LiteralPath $Output) -and @(Get-ChildItem -LiteralPath $Output -Force).Count) { throw "Output folder is not empty: $Output" }
New-Item -ItemType Directory -Path $Output -Force | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Build Tools were not found (vswhere.exe missing).' }
$vs=& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the x64 C++ tools was found.' }
$vcvars=Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
foreach ($name in 'launcher-stub.c','launcher-stub.rc','wuwa-vr.ico') {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name))) { throw "Missing stub source: $name" }
}
$work=Join-Path $Output 'obj'
New-Item -ItemType Directory -Path $work | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'launcher-stub.c'),(Join-Path $source 'launcher-stub.rc'),(Join-Path $source 'wuwa-vr.ico') -Destination $work
$exe=Join-Path $Output 'WuWa VR Launcher.exe'
$log=Join-Path $Output 'build.log'
# /MT: static CRT, so the stub needs no redistributable. /guard:cf, /DYNAMICBASE,
# /NXCOMPAT and /CETCOMPAT keep standard exploit mitigations. asInvoker means
# the stub never asks for administrator rights itself.
$installer=Split-Path -Parent $vswhere
$batch=@"
@echo off
set "PATH=%PATH%;$installer"
call "$vcvars" >nul || exit /b 20
cd /d "$work" || exit /b 21
rc.exe /nologo /fo launcher-stub.res launcher-stub.rc || exit /b 22
cl.exe /nologo /O2 /W4 /WX /MT /GS /guard:cf /sdl /DUNICODE /D_UNICODE launcher-stub.c launcher-stub.res /Fe"$exe" /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /MANIFEST:EMBED "/MANIFESTUAC:level='asInvoker' uiAccess='false'" user32.lib || exit /b 23
"@
$cmd=Join-Path $work 'build.cmd'
[IO.File]::WriteAllText($cmd,$batch,[Text.Encoding]::ASCII)
# Redirect inside cmd: Windows PowerShell treats native stderr as an error record.
& $env:ComSpec /d /c "`"`"$cmd`" > `"$log`" 2>&1`""
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) { throw "Stub build failed ($LASTEXITCODE). See $log" }
Remove-Item -LiteralPath $work -Recurse -Force
$version=(Get-Item -LiteralPath $exe).VersionInfo
[pscustomobject]@{ exe=$exe; sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant(); bytes=(Get-Item -LiteralPath $exe).Length; product=$version.ProductVersion; toolset=$vs } | ConvertTo-Json
