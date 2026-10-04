param(
    [Parameter(Mandatory=$true)][string]$Launcher,
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$TestRoot,
    [Parameter(Mandatory=$true)][string]$Catalog
)
$ErrorActionPreference = 'Stop'
# Use the exact shipped assembly's installer, never its application entry point.
# This only extracts into a fresh isolated store. No helper, game, runtime or
# production profile is started or modified.
$launcherPath = (Get-Item -LiteralPath $Launcher).FullName
$archivePath = (Get-Item -LiteralPath $Archive).FullName
$isolatedRoot = [IO.Path]::GetFullPath($TestRoot)
if (Test-Path -LiteralPath $isolatedRoot) { throw 'Package-install test requires a new isolated directory.' }
[void][Reflection.Assembly]::LoadFrom($launcherPath)
[WuWaVR.Manager.Program]::ConfigureRuntime()
$embedded = [WuWaVR.Manager.RepoClient]::BundledCatalog()
$deserialize = [WuWaVR.Manager.Json].GetMethod('Read').MakeGenericMethod([WuWaVR.Manager.Catalog])
$expected = $deserialize.Invoke($null, @([IO.File]::ReadAllText((Get-Item -LiteralPath $Catalog).FullName)))
$expected.Validate()
# Compare all fields, including any additional release entries.
if (($embedded | ConvertTo-Json -Depth 8 -Compress) -ne ($expected | ConvertTo-Json -Depth 8 -Compress)) {
    throw 'The compiled launcher catalog differs from the packaging catalog. Rebuild the launcher.'
}
$release = $embedded.releases[0]
$store = New-Object WuWaVR.Manager.PackageStore($isolatedRoot)
$first = $store.Install($archivePath, $release, [Threading.CancellationToken]::None)
[WuWaVR.Manager.PackageStore]::Verify($store.Folder($first), $release, [Threading.CancellationToken]::None)
$manifest = Get-Content -Raw -LiteralPath (Join-Path ($store.Folder($first)) 'manifest.json') | ConvertFrom-Json
if ($manifest.packageId -ne ('wuwa-vr-launcher-' + $release.id)) { throw 'Manifest release identity differs.' }
# Reopening proves that selection/installed state was actually saved.
$reopened = New-Object WuWaVR.Manager.PackageStore($isolatedRoot)
if ($reopened.Selected.folder -ne $first.folder -or $reopened.Selected.release.id -ne $release.id) {
    throw 'Installed selection did not survive reopening the store.'
}
$receipt = [ordered]@{
    status = 'isolated_real_installer_passed'
    release = $release.id
    build = $release.buildId
    launcherSha256 = (Get-FileHash -LiteralPath $launcherPath -Algorithm SHA256).Hash.ToLowerInvariant()
    archiveSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    testRoot = $isolatedRoot
    installedFolder = $store.Folder($first)
    productionInstall = $false
    gameLaunched = $false
}
$json = $receipt | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $isolatedRoot 'install-test-receipt.json'), $json, [Text.UTF8Encoding]::new($false))
$json
