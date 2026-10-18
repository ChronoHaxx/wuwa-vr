# Install a private candidate into the owner's version store with the Test Launcher's own installer
# (the code behind Versions & updates > Install this version), for `dev/sim-session.py install`.
# Unelevated; writes only %LOCALAPPDATA%\WuWa VR Manager. The caller makes sure no game, launcher
# window or launcher helper is running, and applies the build afterwards (wuwa-build.ps1 Select).
param(
    [Parameter(Mandatory=$true)][string]$Launcher,
    [Parameter(Mandatory=$true)][string]$Archive
)
$ErrorActionPreference = 'Stop'
[void][Reflection.Assembly]::LoadFrom((Get-Item -LiteralPath $Launcher).FullName)
[WuWaVR.Manager.Program]::ConfigureRuntime()
$release = [WuWaVR.Manager.RepoClient]::BundledCatalog().releases[0]
if ($release.channel -ne 'candidate') { throw "the Test Launcher's first release is not a candidate: $($release.id)" }
$root = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'WuWa VR Manager'
$store = New-Object WuWaVR.Manager.PackageStore($root)
$archivePath = (Get-Item -LiteralPath $Archive).FullName
$none = [Threading.CancellationToken]::None
[WuWaVR.Manager.PackageStore]::VerifyArchiveIdentity($archivePath, $release, $none)
# Same identity already installed: verify and select it, as the window does, never reinstall.
$present = $store.State.installed | Where-Object {
    $_.release.id -eq $release.id -and $_.release.buildId -eq $release.buildId -and $_.release.sha256 -eq $release.sha256
} | Select-Object -First 1
if ($present) {
    [WuWaVR.Manager.PackageStore]::Verify($store.Folder($present), $present.release, $none)
    $store.Select($present)
    $item = $present
} else {
    $item = $store.Install($archivePath, $release, $none)
}
if ($store.Selected.release.id -ne $release.id) { throw 'the installed candidate was not selected' }
[ordered]@{ release = $release.id; build = $release.buildId; folder = $store.Folder($item); reused = [bool]$present } |
    ConvertTo-Json -Compress
