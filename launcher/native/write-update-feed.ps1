# Creates the static web feed from a verified, single-version Velopack release.
# No network access, publication, installation or application execution.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [Parameter(Mandatory = $true)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$')][string]$Version,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$Tag,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [ValidateSet('ChronoHaxx/wuwa-vr')][string]$Repository = 'ChronoHaxx/wuwa-vr',
    [ValidateSet('win-beta')][string]$Channel = 'win-beta',
    [switch]$ReplaceExisting
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$appId = 'ChronoHaxx.WuWaVR'
function Full-Path([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}
function Regular-File([string]$Path) {
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Expected a regular file: $Path"
    }
    return $item
}
function Required-Field($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) { throw "Missing feed field: $Name" }
    return $property.Value
}
$source = Full-Path $ReleaseDirectory
$output = Full-Path $OutputPath
$feedName = "releases.$Channel.json"
if ([IO.Path]::GetFileName($output) -cne $feedName) { throw "Output filename must be $feedName" }
$sourcePrefix = $source.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if ($output.StartsWith($sourcePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Write the web feed outside the original release directory; keep generated metadata intact.'
}
if ((Test-Path -LiteralPath $output) -and -not $ReplaceExisting) {
    throw "Feed already exists; use -ReplaceExisting only for the intended update: $output"
}
if (Test-Path -LiteralPath $output) { [void](Regular-File $output) }
$sourceItem = Get-Item -LiteralPath $source -ErrorAction Stop
if (-not $sourceItem.PSIsContainer -or ($sourceItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'ReleaseDirectory must be a regular directory.'
}
$feedPath = Join-Path $source $feedName
[void](Regular-File $feedPath)
$feed = Get-Content -LiteralPath $feedPath -Raw -Encoding UTF8 | ConvertFrom-Json
$assets = @(Required-Field $feed 'Assets')
if ($assets.Count -eq 0) { throw 'Source feed has no assets.' }
$names = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
$fullCount = 0
$checks = @()
foreach ($asset in $assets) {
    if ((Required-Field $asset 'PackageId') -cne $appId -or (Required-Field $asset 'Version') -cne $Version) {
        throw "Every source entry must be $appId $Version. Do not assign older packages to a new GitHub tag."
    }
    $type = [string](Required-Field $asset 'Type')
    if ($type -notin @('Full','Delta')) { throw "Unsupported update asset type: $type" }
    if ($type -eq 'Full') { $fullCount++ }
    $name = [string](Required-Field $asset 'FileName')
    if ($name -match '[:/\\\x00-\x1f]' -or $name -notmatch '\.nupkg$' -or -not $names.Add($name)) {
        throw "Expected a unique local .nupkg filename, not a rewritten URL: $name"
    }
    $sha256 = [string](Required-Field $asset 'SHA256')
    if ($sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Missing or invalid SHA256 for $name" }
    $size = [long]0
    if (-not [long]::TryParse([string](Required-Field $asset 'Size'), [ref]$size) -or $size -le 0) {
        throw "Invalid package size for $name"
    }
    $package = Regular-File (Join-Path $source $name)
    if ($package.Length -ne $size -or (Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256).Hash -ine $sha256) {
        throw "Package size/SHA256 mismatch: $name"
    }
    $sha1 = $asset.PSObject.Properties['SHA1']
    if ($null -ne $sha1 -and -not [string]::IsNullOrEmpty([string]$sha1.Value)) {
        if ([string]$sha1.Value -notmatch '^[0-9a-fA-F]{40}$' -or
            (Get-FileHash -LiteralPath $package.FullName -Algorithm SHA1).Hash -ine [string]$sha1.Value) {
            throw "Package SHA1 mismatch: $name"
        }
    }
    $url = 'https://github.com/' + $Repository + '/releases/download/' +
        [Uri]::EscapeDataString($Tag) + '/' + [Uri]::EscapeDataString($name)
    $asset.FileName = $url
    $checks += [ordered]@{ file = $name; bytes = $size; sha256 = $sha256.ToLowerInvariant(); url = $url }
}
if ($fullCount -ne 1) { throw 'The source feed must contain exactly one matching full package.' }
$parent = [IO.Path]::GetDirectoryName($output)
if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -ErrorAction Stop | Out-Null }
$parentItem = Get-Item -LiteralPath $parent
if (-not $parentItem.PSIsContainer -or ($parentItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Output parent must be a regular directory.'
}
$temporary = Join-Path $parent ('.launcher-feed-' + [Guid]::NewGuid().ToString('N') + '.tmp')
try {
    [IO.File]::WriteAllText($temporary, ($feed | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding($false)))
    if (Test-Path -LiteralPath $output) {
        if (-not $ReplaceExisting) { throw 'Feed appeared during generation; refusing to replace it.' }
        [void](Regular-File $output)
        # PowerShell otherwise coerces $null to an empty backup path for this overload.
        [IO.File]::Replace($temporary, $output, [NullString]::Value)
    } else {
        [IO.File]::Move($temporary, $output)
    }
} finally {
    if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
}
[pscustomobject]@{
    status = 'written'; output = $output; sha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
    appId = $appId; version = $Version; channel = $Channel; tag = $Tag; assets = $checks
    published = $false
} | ConvertTo-Json -Depth 8
