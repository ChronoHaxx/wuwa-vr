# WuWa VR dev helper: the elevated half of dev/sim-session.py, for the owner's test PC only (never
# packaged). The scheduled task "WuWa VR dev helper" runs this protected copy in
# C:\ProgramData\WuWaVR-DevHelper with highest privileges, so test sessions need no UAC prompt.
# It reads one request from the owner's request folder (data\dev-helper in the workspace) and can only:
#   runtime simulator|headset  point HKLM OpenXR ActiveRuntime at one of the two pinned manifests
#   launch kuro|steam          start the applied build's injector, then that copy's game exe
# Every path comes from config.json, pinned by install.ps1; a request carries only enum values.
# Results go to the protected results folder (readable by Users), never into a user folder.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$config = [IO.File]::ReadAllText((Join-Path $root 'config.json')) | ConvertFrom-Json
$results = Join-Path $root 'results'

function Write-HelperLog([string]$text) {
    Add-Content -LiteralPath (Join-Path $root 'helper.log') -Value ((Get-Date).ToString('s') + ' ' + $text) -Encoding UTF8
}
function Test-Linked([string]$path) {
    # Any reparse point (junction, symlink) from the drive root down to the path.
    $item = Get-Item -LiteralPath $path -Force
    while ($item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { return $true }
        $item = if ($item -is [IO.DirectoryInfo]) { $item.Parent } else { $item.Directory }
    }
    return $false
}
function Test-Under([string]$path, [string]$folder) {
    $folder = [IO.Path]::GetFullPath($folder).TrimEnd('\') + '\'
    return [IO.Path]::GetFullPath($path).StartsWith($folder, [StringComparison]::OrdinalIgnoreCase)
}
function Get-GameRunning {
    return @(Get-Process -Name 'Client-Win64-Shipping', 'Wuthering Waves', 'Custom_UEVR_Injector' -ErrorAction SilentlyContinue)
}

function Invoke-Runtime($request) {
    $mode = [string]$request.mode
    if ($mode -cne 'simulator' -and $mode -cne 'headset') { throw 'mode must be simulator or headset' }
    $target = [string]$config.$mode
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "manifest missing: $target" }
    # A running game keeps the runtime it started with; switching under it would only confuse the
    # next check, and the owner may be playing in the headset.
    if ((Get-GameRunning).Count -gt 0) { throw 'the game or an injector is running' }
    $key = 'HKLM:\SOFTWARE\Khronos\OpenXR\1'
    if (-not (Test-Path $key)) { New-Item -Path $key -Force | Out-Null }
    $before = [string](Get-ItemProperty -Path $key -ErrorAction SilentlyContinue).ActiveRuntime
    if ($before -ne $target) {
        if ($before) { Set-ItemProperty -Path $key -Name PreviousActiveRuntime -Value $before }
        Set-ItemProperty -Path $key -Name ActiveRuntime -Value $target
    }
    return [ordered]@{ before = $before; active = [string](Get-ItemProperty -Path $key).ActiveRuntime }
}

function Invoke-Launch($request) {
    $copy = [string]$request.copy
    if ($copy -cne 'kuro' -and $copy -cne 'steam') { throw 'copy must be kuro or steam' }
    if ((Get-GameRunning).Count -gt 0) { throw 'the game or an injector is already running' }
    $game = [string]$config.$copy
    if (-not (Test-Path -LiteralPath $game -PathType Leaf)) { throw "game missing: $game" }
    # The backend that loads is the runtime folder the applied build wrote into the profile.
    $line = Get-Content -LiteralPath (Join-Path $config.profile 'injector_config.txt') -Encoding UTF8 |
        Where-Object { $_ -like 'custom_var_urvr_folder=*' } | Select-Object -First 1
    if (-not $line) { throw 'no build is applied (injector_config.txt has no runtime folder)' }
    $folder = [IO.Path]::GetFullPath($line.Substring('custom_var_urvr_folder='.Length).Trim())
    if (-not @($config.injectorRoots | Where-Object { Test-Under $folder $_ }).Count) { throw "runtime folder outside the allowed roots: $folder" }
    $injector = Join-Path $folder 'Custom_UEVR_Injector.exe'
    if (-not (Test-Path -LiteralPath $injector -PathType Leaf)) { throw "injector missing: $injector" }
    if (Test-Linked $injector) { throw 'the injector path contains a link' }
    $arguments = 'Client-Win64-Shipping.exe'
    if ($copy -eq 'steam') {
        if ([Diagnostics.FileVersionInfo]::GetVersionInfo($injector).Comments -cne 'WUWA_INJECTOR_TARGET_PATH_V1') { throw 'this build has no Steam-aware injector' }
        $arguments = '--target-path "' + (Join-Path (Split-Path -Parent $game) 'Client\Binaries\Win64\Client-Win64-Shipping.exe') + '"'
    }
    $injectorProcess = Start-Process -FilePath $injector -WorkingDirectory $folder -ArgumentList $arguments -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 2
    if ($injectorProcess.HasExited) { throw ('the injector exited at once, code ' + $injectorProcess.ExitCode) }
    $gameProcess = Start-Process -FilePath $game -WorkingDirectory (Split-Path -Parent $game) -PassThru
    return [ordered]@{ copy = $copy; runtimeFolder = $folder; injectorPid = $injectorProcess.Id; gamePid = $gameProcess.Id }
}

$requestPath = Join-Path $config.requests 'request.json'
if (-not (Test-Path -LiteralPath $requestPath -PathType Leaf)) { Write-HelperLog "run with no request in $($config.requests)"; exit 0 }
if (Test-Linked $requestPath) { Remove-Item -LiteralPath $requestPath -Force; Write-HelperLog 'refused a linked request'; exit 1 }
$raw = [IO.File]::ReadAllText($requestPath)
Remove-Item -LiteralPath $requestPath -Force
if ($raw.Length -gt 4096) { Write-HelperLog 'refused an oversized request'; exit 1 }
$request = $raw | ConvertFrom-Json
$id = [string]$request.id
if ($id -notmatch '^[a-f0-9]{32}$') { Write-HelperLog 'refused a request without a valid id'; exit 1 }
$result = [ordered]@{ id = $id; action = [string]$request.action; ok = $false; time = (Get-Date).ToString('o') }
try {
    $detail = switch -CaseSensitive ([string]$request.action) {
        'runtime' { Invoke-Runtime $request }
        'launch' { Invoke-Launch $request }
        default { throw 'unknown action' }
    }
    foreach ($name in $detail.Keys) { $result[$name] = $detail[$name] }
    $result.ok = $true
} catch { $result.error = $_.Exception.Message }
Write-HelperLog ($result.action + ' ' + $(if ($result.ok) { 'ok' } else { 'failed: ' + $result.error }))
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $results ('result-' + $id + '.json')) -Encoding UTF8
