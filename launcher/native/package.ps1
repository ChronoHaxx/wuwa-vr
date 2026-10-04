param([string]$OutputDirectory = (Join-Path $PSScriptRoot '../../../extracted/installer-20261003'), [string]$OfflineArchive)
$ErrorActionPreference = 'Stop'
if ($OfflineArchive) {
    $release = (Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'catalog.json') | ConvertFrom-Json).releases[0]
    if ($release.id -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]{0,127}$') { throw 'Invalid bundled release ID.' }
    $payload = Get-Item -LiteralPath $OfflineArchive
    if ($payload.Length -ne $release.size -or (Get-FileHash -LiteralPath $payload.FullName -Algorithm SHA256).Hash -ne $release.sha256) {
        throw 'Offline ZIP does not match the first catalog release.'
    }
    # Hashes alone cannot prove catalog -> portable release identity. Exercise
    # the exact compiled launcher's real installer before sealing the delivery.
    $installTestRoot = Join-Path $PSScriptRoot ('work/verify-install-' + [Guid]::NewGuid().ToString('N'))
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'verify-package-install.ps1') `
        -Launcher (Join-Path $PSScriptRoot 'bin/Release/WuWa VR.exe') -Archive $payload.FullName `
        -TestRoot $installTestRoot -Catalog (Join-Path $PSScriptRoot 'catalog.json')
    if ($LASTEXITCODE -ne 0) { throw 'The actual launcher could not install this offline payload in isolation. Delivery stopped.' }
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
$stage = Join-Path $PSScriptRoot ('work/package-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'bin/Release/WuWa VR.exe') -Destination $stage
foreach ($runtimeFile in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'bin/Release') -File | Where-Object { $_.Extension -in @('.dll', '.config') }) {
    Copy-Item -LiteralPath $runtimeFile.FullName -Destination $stage
}
foreach ($name in @('README.txt','TEST THIS.txt','PlayerGuide.html','LICENSE.txt','THIRD-PARTY-NOTICES.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $stage
}
if ($OfflineArchive) {
    $bundled = Join-Path $stage ('packages/' + $release.id)
    New-Item -ItemType Directory -Path $bundled -Force | Out-Null
    Copy-Item -LiteralPath $payload.FullName -Destination (Join-Path $bundled 'WuWa-VR-Launcher.zip')
}
$hashes = Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.FullName.Substring($stage.Length + 1).Replace('\','/')
}
[IO.File]::WriteAllLines((Join-Path $stage 'SHA256SUMS.txt'), $hashes, [Text.UTF8Encoding]::new($false))
$kind = if ($OfflineArchive) { 'Complete' } else { 'Desktop' }
$zip = Join-Path $output ('WuWa-VR-' + $kind + '-candidate-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.zip')
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
Get-Item -LiteralPath $zip | Select-Object FullName,Length
Get-FileHash -LiteralPath $zip -Algorithm SHA256
