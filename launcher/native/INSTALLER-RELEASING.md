# Releasing the Windows launcher

The public download is one Velopack Setup EXE. It installs the small WPF launcher;
the launcher downloads verified WuWa VR backend releases through the existing
catalog. No game, helper process, backend ZIP or mutable user data belongs in
Velopack's replaceable `current` directory.

Pinned first release:

- Application ID: `ChronoHaxx.WuWaVR` (keep stable across launcher updates).
- Launcher version: `1.0.0`; update channel: `win-beta`.
- Velopack SDK and `vpk`: `1.2.161`; Newtonsoft.Json: `13.0.4`.
- Release tag: `beta-2026-10-04-launcher`; website asset: `WuWa-VR-Setup.exe`.
- Application target: .NET Framework 4.8, Windows x64. The packaging tool requires
  a build-machine .NET SDK; it does not retarget the application to modern .NET.

## Package an already tested build

Root/integration owns the native build, catalog URLs, public backend package and
website. This script only stages and packages the compiled launcher; it never
builds, installs, starts the normal player UI or uploads anything. Velopack's own
bootstrap verification remains enabled. Coordinate the
pack command with the single-worker build slot because compression consumes CPU.

After the Release build and launcher tests pass, run this one packaging command
from `showcase-wt` (choose a **new** output folder for every attempt):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\launcher\native\pack-installer.ps1 `
  -VpkPath E:\Coding\wuwa-vr\extracted\launcher-release-20261004\tools\vpk.exe `
  -BuildDirectory .\launcher\native\bin\Release `
  -IconPath .\launcher\native\assets\wuwa-vr.ico `
  -Version 1.0.0 -Channel win-beta `
  -ExpectedVpkVersion 1.2.161 -ExpectedSdkVersion 1.2.161 -ExpectedJsonVersion 13.0.4 `
  -OutputDirectory E:\Coding\wuwa-vr\extracted\launcher-release-20261004\pack-1.0.0-attempt1
```

The script verifies DLL product versions and the CLI version reported by `vpk
--help` (`--version` is unsupported in this pinned CLI). It refuses existing output,
missing inputs, unexpected top-level runtime DLLs, or an output under the source
or build directories. It invokes `vpk` below normal priority, suppresses tool update
checks, ignores inherited `VPK_*` overrides, and requests `net48` prerequisite installation if a player's framework is
missing. It does not download the framework during packaging.

Only these files enter `app-stage`:

- `WuWa VR.exe`, `Velopack.dll`, `Newtonsoft.Json.dll`, and `WuWa VR.exe.config`
  when the build emits one.
- Current `README.txt`, `TEST THIS.txt`, `PlayerGuide.html`, `LICENSE.txt` and
  `THIRD-PARTY-NOTICES.txt` from this native source directory.

The allowlist excludes PDBs, test executables, source/obj folders, stale backend DLLs
and every subdirectory. Add a newly required runtime dependency deliberately;
do not broaden this to recursively copy `bin` or the old portable bundle.

Output contains `app-stage/`, `releases/`, CLI logs and `pack-receipt.json`.
The receipt records source paths, sizes and SHA-256 values, pinned tool/runtime
versions, the source icon identity, pack arguments and generated artifact hashes.
The generated Setup is copied to `WuWa-VR-Setup.exe` with an identical verified
SHA-256; the original filename remains available for Velopack metadata.
`status: packed` means
packaging succeeded; it does not establish clean installation or update behavior.
Failures after staging begins retain their output and receipt; preflight failures
create no output. Fix the cause and choose a new folder;
do not publish a failed or partial result. The script currently starts from an
empty release directory, so it creates a full update rather than a delta.

## Update ownership and startup

Launcher SemVer and backend candidate/build IDs are separate. A launcher update
must not silently select/install a different backend or change headset/runtime
settings. Keep the existing data root, backend releases, recordings and settings
outside the entire Velopack installation root so launcher updates/uninstall cannot
remove them. Only copy shipped, immutable help/resources into `current`.

`VelopackApp.Build().SetAutoApplyOnStartup(false).Run()` must run exactly once at
the beginning of `Main`, before the single-instance guard, WPF or helper startup.
Checking/downloading can be asynchronous; applying is an explicit restart after
the existing game/injector/recording/job idle checks. Do not automatically stop
the game or recorder. Do not use timed apply-on-exit as a replacement for that
guard. Velopack may terminate processes locking its installation directory, which
is another reason backend processes must run from the separate release store.

## Verify, then publish

Before public delivery, independently record:

1. Clean per-user installation from Setup; shortcut opens the simple launcher.
2. An installed launcher updating through a local test feed to a higher version;
   original settings, selected backend and user files survive.
3. Offline/failed update check remains usable, corrupt downloads fail safely,
   and an active game/recording prevents applying an update.
4. Restart/single-instance behavior and uninstall preserve the separate user and
   backend data. A second version is required to prove self-updating.

Do not treat packaging or component-test success as these acceptance results.
This script does not sign the generated Setup/update artifacts. If signing is
required, integrate the existing authorized certificate into packaging and rerun
verification; unsigned output must not be described as signed.

For an isolated installer check, official Setup flags are
`Setup.exe --installto <isolated-directory> --silent`; silent suppresses the
post-install app launch. **A different folder alone is not isolation:** the
production package still registers its application ID and creates shortcuts.
Use Windows Sandbox/a disposable VM for the actual public package. Alternatively,
use a separate test application ID/title with `--shortcuts None`, an isolated user
data root, and a headless test entry point that cannot launch the game/helper.
Create test versions 1.0.0 and 1.0.1, use a local feed for download/apply, and verify
the new version and retained marker data after restart. Never publish the test
1.0.1 or its feed. This production script intentionally fixes the real app ID;
integration owns a separate test harness. `TestVelopackLocator` with
`SimpleFileSource` can check discovery/download without installation, but cannot
replace evidence of the real apply/restart step.

Publication scaffold, owned by integration:

1. Prepare the approved public backend assets and catalog separately; verify every
   URL, size and SHA-256 before advertising them in the launcher.
2. Create GitHub **prerelease** `beta-2026-10-04-launcher`. Attach
   `WuWa-VR-Setup.exe` plus the generated Setup EXE, `*-full.nupkg`,
   `releases.win-beta.json` and generated release metadata;
   keep their generated filenames. An optional portable ZIP is secondary.
3. Use the dedicated static launcher feed base
   `https://chronohaxx.github.io/wuwa-vr/launcher-updates/` with `SimpleWebSource`
   and channel `win-beta`. Publish `releases.win-beta.json` there. In this **web
   copy only**, replace each asset's `FileName` with its absolute immutable HTTPS
   GitHub launcher-release download URL; retain its package ID, version, type,
   size and hashes. Keep the unmodified generated feed beside GitHub assets too.
   The pinned SDK explicitly supports absolute HTTPS `FileName` values. Relative
   filenames resolve against the Pages feed directory, so leaving them relative
   would incorrectly request the `.nupkg` from Pages. Upload and verify the GitHub
   package first; publish the Pages feed last. No embedded credentials are needed.
4. Point the EN/zh website Download button to the exact verified
   `beta-2026-10-04-launcher/WuWa-VR-Setup.exe` GitHub asset and
   verify the resulting download/update feed. Upload only intended public assets,
   not `app-stage`, local source paths, internal receipts or build logs.

Generate the Pages feed from that exact local pack for source review (this writes
source content only; it does not upload or commit). Publish this feed to Pages
only after the referenced release assets have been uploaded and verified:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\launcher\native\write-update-feed.ps1 `
  -ReleaseDirectory E:\Coding\wuwa-vr\extracted\launcher-release-20261004\pack-1.0.0-attempt1\releases `
  -Version 1.0.0 -Tag beta-2026-10-04-launcher `
  -OutputPath .\site\launcher-updates\releases.win-beta.json
```

The writer verifies each local package's size, SHA-256 and any supplied SHA-1,
requires the one expected app/version and a full package, preserves other feed
fields, and rewrites only package URLs. It refuses existing output unless
`-ReplaceExisting` is explicitly supplied for a later intended feed update.
Replacement is atomic. Wrong hashes, path-like filenames and mixed-version feeds
fail before changing the destination. It performs no network requests: integration
must independently verify the uploaded bytes before publishing this web feed.

This separate app feed is intentional. In Velopack 1.2.161, `GithubSource` requests
only page 1 of ten GitHub releases; `GitBase` skips a release without the matching
channel feed. One mod-only release does not break discovery, but ten newer mod
releases can push the launcher out of that window. Mod-only publication must never
touch the dedicated launcher feed. Launcher publication must never rewrite the
backend catalog unless that separate release is intended. A fixed GitHub tag URL
would also remain stuck on that one release; it is not a future-update feed.

If all update assets are hosted directly on the same static server, relative
filenames work when the `.nupkg` assets are beside `releases.win-beta.json`. Do not
mix a static-feed URL with the GitHub Releases source format. Test the published
feed through the actual SDK, including a later unrelated mod-only release.
Retain the previous verified delivery for rollback; do not overwrite historical
artifacts or publish acceptance-test versions.

The legacy `package.ps1` remains a separate portable ZIP workflow. It is not called
by this installer script, and its backend bundle must not become app-update data.

## Ordinary launcher-only updates

Bump the launcher SemVer in `LauncherUpdateService.Version`, `AssemblyInfo.cs`
and the pack command together, then build/test and package to a new output folder.
Keep `ChronoHaxx.WuWaVR`, `win-beta`, the backend catalog, backend checkpoint and
existing backend archives unchanged. An app update does not require repackaging,
reinstalling or selecting a mod. The current first public catalog entry supplies
backend context for the source release; its `buildId` must match the checkout's
`mod/checkpoint.json` build. Use a new app tag distinct from every backend entry.

Generate the local Pages feed for the new app tag/version, review and commit the
source and feed in a clean release checkout, and push that reviewed commit to a
release branch without publishing Pages yet. After the usual acceptance checks,
publish the installer/update packages and source using this command (example
version and paths; **omit `--portable`**):

```powershell
python .\dev\publish-launcher-release.py `
  --checkout E:\Coding\wuwa-vr\release-checkout `
  --packed E:\Coding\wuwa-vr\extracted\launcher-release-20261004\pack-1.0.1-attempt1 `
  --notes E:\Coding\wuwa-vr\extracted\launcher-release-20261004\START-HERE-1.0.1.txt `
  --tag beta-2026-10-05-launcher `
  --out E:\Coding\wuwa-vr\extracted\launcher-release-20261004\publish-1.0.1 `
  --publish
```

Omit `--publish` first to prepare and inspect the local plan. Launcher-only plans
contain `release_mode: launcher-only` and the existing backend release/build;
they upload no `WuWa-VR-Launcher.zip` and never modify the catalog. The script
checks the successful pack receipt, copied guides/icon/packer, package hashes,
committed Pages feed and exact Git tag target. After successful upload and remote
hash verification, integration publishes the reviewed Pages feed and new Setup
link. Keep the previous app release and backend URLs available.

For an intentionally paired new mod release, add `--portable <verified-ZIP>` and
prepare the matching new-tag catalog entry separately. That mode retains archive
size/hash, manifest identity and every manifest-file hash check. The publisher
does not create or rewrite either mode's catalog entries.

## Official references and licences

- [Velopack integration and explicit update lifecycle](https://docs.velopack.io/integrating/overview)
- [Windows layout and locked-file behavior](https://docs.velopack.io/packaging/operating-systems/windows)
- [Packaging outputs](https://docs.velopack.io/packaging/overview)
- [Framework prerequisites](https://docs.velopack.io/packaging/bootstrapping)
- [GitHub and static update sources](https://docs.velopack.io/integrating/update-sources)
- [Pinned GitHub ten-release query](https://github.com/velopack/velopack/blob/1.2.161/src/lib-csharp/Sources/GithubSource.cs#L85)
- [Pinned missing-feed handling](https://github.com/velopack/velopack/blob/1.2.161/src/lib-csharp/Sources/GitBase.cs#L65)
- [Pinned absolute/relative package URL handling](https://github.com/velopack/velopack/blob/1.2.161/src/lib-csharp/Sources/SimpleWebSource.cs#L60)
- [Release publication order](https://docs.velopack.io/distributing/overview)
- [Update test helpers](https://docs.velopack.io/integrating/testing)
- [Shortcut suppression for disposable test packages](https://docs.velopack.io/integrating/shortcuts)
- [Velopack 1.2.161 licence](https://github.com/velopack/velopack/blob/1.2.161/LICENSE)
- [Newtonsoft.Json 13.0.4 licence](https://github.com/JamesNK/Newtonsoft.Json/blob/13.0.4/LICENSE.md)

Ship `THIRD-PARTY-NOTICES.txt` with both installer and portable launcher outputs.
CircuitLord attribution already in that file remains intact. No CircuitLord mod
assets, branding or closed-source code are included.
