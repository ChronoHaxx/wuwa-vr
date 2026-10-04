# Packages an already-built launcher. Does not build, install, start the player UI, or publish.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$VpkPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'bin/Release'),
    [string]$IconPath = (Join-Path $PSScriptRoot 'assets/wuwa-vr.ico'),
    [ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$')]
    [string]$Version = '1.0.0',
    [ValidateSet('win-beta')][string]$Channel = 'win-beta',
    [ValidateSet('1.2.161')][string]$ExpectedVpkVersion = '1.2.161',
    [ValidateSet('1.2.161')][string]$ExpectedSdkVersion = '1.2.161',
    [ValidateSet('13.0.4')][string]$ExpectedJsonVersion = '13.0.4'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$appId = 'ChronoHaxx.WuWaVR'
$mainExe = 'WuWa VR.exe'
$utf8 = New-Object Text.UTF8Encoding($false)

function Full-Path([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}
function Assert-RegularFile([string]$Path) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Expected a regular file: $Path"
    }
    return $item
}
function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Quote-Argument([string]$Value) {
    # Windows argv quoting, not shell evaluation; handles spaces, quotes and trailing slashes.
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}
function Invoke-Vpk([string[]]$Arguments, [int]$TimeoutMilliseconds) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $script:vpk
    $start.Arguments = (($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Do not inherit hidden signing, skip-validation or output switches from a developer shell.
    foreach ($key in @($start.EnvironmentVariables.Keys)) {
        if ($key.StartsWith('VPK_', [StringComparison]::OrdinalIgnoreCase)) { $start.EnvironmentVariables.Remove($key) }
    }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'vpk did not start.' }
        try { $process.PriorityClass = 'BelowNormal' } catch { }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            $process.Kill()
            [void]$process.WaitForExit(10000)
            throw "vpk timed out after $TimeoutMilliseconds ms. Output may be incomplete."
        }
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdout, $stderr), 10000)) {
            throw 'vpk output pipes did not finish.'
        }
        return [pscustomobject]@{ ExitCode = $process.ExitCode; Stdout = $stdout.Result; Stderr = $stderr.Result }
    } finally { $process.Dispose() }
}
function Assert-ProductVersion([string]$Path, [string]$Expected) {
    $actual = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path).ProductVersion
    if ([string]::IsNullOrWhiteSpace($actual) -or
        $actual -notmatch ('^' + [regex]::Escape($Expected) + '(?:\+[0-9A-Za-z.-]+)?$')) {
        throw "Pinned runtime version mismatch for $([IO.Path]::GetFileName($Path)): expected $Expected, found '$actual'. Rebuild from the pinned dependencies."
    }
    return $actual
}

$vpk = Full-Path $VpkPath
$build = Full-Path $BuildDirectory
$icon = Full-Path $IconPath
$output = Full-Path $OutputDirectory
if (Test-Path -LiteralPath $output) { throw "Output already exists; choose a new directory: $output" }
[void](Assert-RegularFile $vpk)
$iconItem = Assert-RegularFile $icon
if (-not (Test-Path -LiteralPath $build -PathType Container)) { throw "Build directory missing: $build" }
$buildItem = Get-Item -LiteralPath $build
if ($buildItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Build directory must not be a link.' }
$nativePrefix = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
$buildPrefix = $build.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if ($output.StartsWith($nativePrefix, [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Choose an isolated output outside the native source and build directories.'
}
# These are the complete non-framework runtime dependencies of the pinned net48 app.
# Reject unexpected DLLs instead of silently omitting a new dependency or shipping a stale backend.
$runtimeNames = @('Velopack.dll', 'Newtonsoft.Json.dll')
$unexpected = @(Get-ChildItem -LiteralPath $build -File -Filter '*.dll' | Where-Object { $_.Name -notin $runtimeNames })
if ($unexpected.Count) { throw "Unexpected Release DLLs; review packaging inputs: $($unexpected.Name -join ', ')" }
$inputs = @()
foreach ($name in @($mainExe) + $runtimeNames) {
    $item = Assert-RegularFile (Join-Path $build $name)
    $inputs += [ordered]@{ name = $name; source = $item.FullName; bytes = $item.Length; sha256 = (Get-Sha256 $item.FullName) }
}
$config = Join-Path $build ($mainExe + '.config')
if (Test-Path -LiteralPath $config) {
    $item = Assert-RegularFile $config
    $inputs += [ordered]@{ name = $item.Name; source = $item.FullName; bytes = $item.Length; sha256 = (Get-Sha256 $item.FullName) }
}
foreach ($name in @('README.txt', 'TEST THIS.txt', 'PlayerGuide.html', 'LICENSE.txt', 'THIRD-PARTY-NOTICES.txt')) {
    $item = Assert-RegularFile (Join-Path $PSScriptRoot $name)
    $inputs += [ordered]@{ name = $name; source = $item.FullName; bytes = $item.Length; sha256 = (Get-Sha256 $item.FullName) }
}
$sdkVersion = Assert-ProductVersion (Join-Path $build 'Velopack.dll') $ExpectedSdkVersion
$jsonVersion = Assert-ProductVersion (Join-Path $build 'Newtonsoft.Json.dll') $ExpectedJsonVersion
$toolHelp = Invoke-Vpk @('--help', '--skip-updates', '--legacyConsole') 30000
if ($toolHelp.ExitCode -ne 0) { throw "vpk help failed ($($toolHelp.ExitCode)): $($toolHelp.Stderr)" }
# vpk 1.2.161 has no --version switch; its help header is the supported version output.
$toolVersionMatch = [regex]::Match($toolHelp.Stdout, 'Velopack CLI ([0-9]+\.[0-9]+\.[0-9]+)(?:\+[0-9A-Za-z.-]+)?,')
if (-not $toolVersionMatch.Success -or $toolVersionMatch.Groups[1].Value -ne $ExpectedVpkVersion) {
    throw "Expected Velopack CLI $ExpectedVpkVersion; supplied tool reported: $($toolHelp.Stdout.Trim())"
}

# No overwrite or cleanup: a failed attempt remains available for diagnosis.
New-Item -ItemType Directory -Path $output -ErrorAction Stop | Out-Null
$stage = Join-Path $output 'app-stage'
$releases = Join-Path $output 'releases'
New-Item -ItemType Directory -Path $stage -ErrorAction Stop | Out-Null
$receiptPath = Join-Path $output 'pack-receipt.json'
$receipt = [ordered]@{
    status = 'staging'; createdUtc = [DateTime]::UtcNow.ToString('o')
    appId = $appId; launcherVersion = $Version; channel = $Channel; framework = 'net48'; runtime = 'win-x64'
    vpk = [ordered]@{ path = $vpk; version = $ExpectedVpkVersion; sha256 = (Get-Sha256 $vpk) }
    dependencies = [ordered]@{ Velopack = $sdkVersion; NewtonsoftJson = $jsonVersion }
    icon = [ordered]@{ source = $icon; bytes = $iconItem.Length; sha256 = (Get-Sha256 $icon) }
    scriptSha256 = (Get-Sha256 $PSCommandPath); inputs = $inputs; artifacts = @(); failure = $null
}
function Save-Receipt {
    [IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 8), $utf8)
}
try {
    Save-Receipt
    [IO.File]::WriteAllText((Join-Path $output 'vpk-help.txt'), $toolHelp.Stdout, $utf8)
    foreach ($sourceRecord in $inputs) {
        $destination = Join-Path $stage $sourceRecord.name
        Copy-Item -LiteralPath $sourceRecord.source -Destination $destination -ErrorAction Stop
        if ((Get-Sha256 $destination) -ne $sourceRecord.sha256) { throw "Input changed during staging: $($sourceRecord.name)" }
    }
    $packArguments = @('pack', '--packId', $appId, '--packVersion', $Version, '--packDir', $stage,
        '--mainExe', $mainExe, '--packTitle', 'WuWa VR', '--channel', $Channel,
        '--icon', $icon,
        '--runtime', 'win-x64', '--framework', 'net48', '--outputDir', $releases,
        '--skip-updates', '--legacyConsole', '--yes')
    $receipt['arguments'] = $packArguments
    $receipt.status = 'packing'
    Save-Receipt
    $result = Invoke-Vpk $packArguments 900000
    [IO.File]::WriteAllText((Join-Path $output 'vpk.stdout.log'), $result.Stdout, $utf8)
    [IO.File]::WriteAllText((Join-Path $output 'vpk.stderr.log'), $result.Stderr, $utf8)
    if ($result.ExitCode -ne 0) { throw "vpk pack failed ($($result.ExitCode)); see vpk.stdout.log and vpk.stderr.log." }
    $feedPath = Join-Path $releases ("releases.$Channel.json")
    [void](Assert-RegularFile $feedPath)
    $feed = Get-Content -LiteralPath $feedPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $full = @($feed.Assets | Where-Object { $_.PackageId -eq $appId -and $_.Version -eq $Version -and $_.Type -eq 'Full' })
    if ($full.Count -ne 1) { throw 'Generated feed must contain exactly one matching full launcher package.' }
    if ([IO.Path]::GetFileName($full[0].FileName) -ne $full[0].FileName) { throw 'Generated feed contains a non-local package filename.' }
    [void](Assert-RegularFile (Join-Path $releases $full[0].FileName))
    $setup = @(Get-ChildItem -LiteralPath $releases -File | Where-Object { $_.Name.StartsWith($appId + '-') -and $_.Name.EndsWith('-Setup.exe') })
    if ($setup.Count -ne 1) { throw 'Expected one generated Setup.exe for this application.' }
    if ((Get-Sha256 $icon) -ne $receipt.icon.sha256) { throw 'Icon source changed during packaging.' }
    $downloadAlias = Join-Path $releases 'WuWa-VR-Setup.exe'
    Copy-Item -LiteralPath $setup[0].FullName -Destination $downloadAlias -ErrorAction Stop
    if ((Get-Sha256 $downloadAlias) -ne (Get-Sha256 $setup[0].FullName)) { throw 'Public Setup alias hash differs from generated installer.' }
    $receipt['downloadAlias'] = 'WuWa-VR-Setup.exe'
    $receipt.artifacts = @(Get-ChildItem -LiteralPath $releases -File | Sort-Object Name | ForEach-Object {
        [ordered]@{ name = $_.Name; bytes = $_.Length; sha256 = (Get-Sha256 $_.FullName) }
    })
    $receipt.status = 'packed'
    $receipt['completedUtc'] = [DateTime]::UtcNow.ToString('o')
    Save-Receipt
    Write-Output "Packed $appId $Version ($Channel). No installation, normal player launch or upload was performed."
    Write-Output "Installer: $($setup[0].FullName)"
    Write-Output "Receipt: $receiptPath"
} catch {
    $receipt.status = 'failed'
    $receipt.failure = $_.Exception.Message
    Save-Receipt
    throw
}
