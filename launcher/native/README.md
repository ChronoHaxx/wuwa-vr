# WuWa VR desktop manager — public beta 1.0.0

Release **beta-2026-10-04-launcher**, backend **npc-rim-20261004**, game **3.7**.
Players run **WuWa-VR-Setup.exe** once for a per-user installation, then open the
normal **WuWa VR** shortcut. The thin installer does not bundle the VR mod:
first installation needs internet for the separate approximately **54 MB** ZIP.
The portable **WuWa-VR-Launcher.zip** remains an advanced fallback using the older
web interface; it does not install or self-update the desktop app.

Use **01 Game → 02 Install VR → 03 Headset or simulator**, then **Launch in VR**,
Windows permission and **Play** in the official game launcher. For updates, use
**02 → Versions & updates → Check updates**. **Update launcher** downloads and
verifies the desktop update; **Restart to update** applies it after confirming
the app is idle. Updating the desktop app preserves the selected VR package.
In step 02, choose **beta-2026-10-04-launcher** and install or select it to use
**npc-rim-20261004**, formerly **candidate-20261004-npc-rim-r1**. Settings,
backups, logs and recordings are preserved.

The new NPC rim option starts off and removes intended nearby toon-rim lighting
too. Its underlying stereo fault remains unresolved and headset checking is
pending. The owner confirmed retained 2D-brightness and ultimate-camera fixes in
earlier builds. This documentation does not assert public installer acceptance;
fresh-PC and Steam/Epic game injection remain unverified.

A small Windows x64/.NET Framework 4.8 app. The English and Simplified Chinese
screens reuse the portable launcher's Python/PowerShell backend for detection,
injection, recording, profile backup and recovery. There is no second injector.

The native app now discovers the game on startup, before installing a VR package
or connecting its helper. It preserves saved custom paths and manual-start mode,
then checks both Windows uninstall-registry views (including DisplayIcon-only
records) and exact default folders on fixed drives. It does not scan whole disks.
Multiple official installs require a choice; missing saved paths are reported
instead of replaced. Steam/Epic registrations are not promoted to official routes.
Change and Troubleshooting > Repair & recovery > Find game again remain available. Automatic suggestions
are not persisted as explicit overrides of the helper's saved startup choices.

```powershell
.\launcher\native\build.ps1 -Test
.\launcher\native\package.ps1
# Optional complete bundle; the archive must match the first embedded release:
.\launcher\native\package.ps1 -OfflineArchive 'C:\Downloads\WuWa-VR-Launcher.zip'
```

Visual Studio Build Tools with the .NET Framework 4.8 targeting pack is required
to build. No NuGet restore, Electron or bundled browser is needed. Builds use one
MSBuild worker with compiler-server reuse off and below-normal priority.

## Package contract

- `catalog.json` pins project-owned GitHub Release ZIP URLs, SHA-256, size, game
  version and build identity. The latest-release API cannot find our prereleases.
- A download uses a hash-keyed `.partial` file and validated HTTP ranges. A verified
  archive is extracted into a new directory; all manifest files and the package
  identity must match before the selection is committed.
- Complete bundles put a portable ZIP at `packages/<release>/WuWa-VR-Launcher.zip`
  beside the EXE. It must match the selected catalog entry's size and SHA-256;
  normal per-file verification still follows. A missing version downloads
  normally. A corrupt included file fails visibly and is not silently replaced.
  The same ZIP can be extracted and used directly as the portable fallback.
- Unpublished test bundles use `channel: candidate`, a `created` timestamp and
  no public URLs or publication date. They must be bundled or already verified
  in cache; a missing candidate never triggers a download. An online refresh
  retains embedded candidates, and remote catalogs may contain public betas only.
- `%LOCALAPPDATA%\WuWa VR Manager` contains cache, version folders and `manager.json`.
  The existing `%LOCALAPPDATA%\WuWa VR Launcher` keeps recordings, logs and backups.
  The manager does not directly edit the game or UEVR profile.
- Repair creates a fresh copy. Older installations remain available for rollback.
  Removal stages manifest-owned files before committing state; a locked file rolls
  that attempt back. Unrecognized personal files remain in the original folder.
- Loopback requests validate helper identity/root and use its existing Origin/token
  contract. Switching packages requires an idle game, injector, recorder and helper.
  A leftover first-frame flag cannot confirm a different injector PID or build.

The VR catalog URL is `main/launcher/native/catalog.public.json`; it contains
public betas only. Private candidates stay in the embedded `catalog.json`.
The installed launcher uses Velopack 1.2.161 and a separate stable Pages feed at
`https://chronohaxx.github.io/wuwa-vr/launcher-updates/` for app updates,
not GitHub's latest-release selection. Publish and verify the app-feed artifacts
and VR catalog before announcing downloads. On a failed check, a cached list
does not establish latest-version status; display the error and allow retry.
The bundled simulator is selected only
through an explicit action on the home screen; it is never selected on startup.

## Public launcher and developer tools

Player story: "I want to try WuWa in VR without learning the mod's developer
tools. Find my game, tell me which VR version is installed, and let me launch."

The home screen has three cards: game location, VR version/updates, and the
current headset/simulator runtime. Versions and update checks belong to card 02.
One primary button installs on first use, then becomes Launch in VR. Returning
players with a compatible installation and saved choices use that one launcher
button; Windows permission and Play in the official game launcher can still be
required. We do not claim a completely unattended game launch.

There are no public Setup/Record/Help tabs. The footer's Developer tools link
opens the original web launcher for recording, playtests and advanced settings,
using the selected package and existing data. Troubleshooting is a separate
compact view with Back, read-only controller checks and recovery. Recovery stays
native because the web helper cannot repair the manager's package installation.
Reset mod settings is distinct from restoring the settings saved before the mod.
Installation does not launch the game or silently switch the system runtime.
Errors remain visible beside the primary action, including offline update checks.

Controller check works without the web helper. It reports all four system XInput
slots, HidHide read-query results and known input-helper processes, then offers
Refresh and Copy report. It never changes hiding rules, closes apps, sends input
or uploads a report. Copied output uses fixed labels/counts and excludes device
IDs, serials, usernames and full paths. A connected slot is evidence of visibility
to this launcher, not proof of button forwarding or in-game input. API/permission
failures remain unknown rather than being labelled disconnected.

Historical 3 October verification used `portal-toggle-20261003` (50,744,779-byte
portable ZIP). Its F7/L3+LT shortcut was compiled and the controller policy tests
pass. The profile transaction recognizes the exact published
`lightfix2-20261001` backend hash, preserves personal settings and extra files,
and overlays this release's supplied scripts. Saved target profiles take
precedence; explicit reset still uses defaults. Unknown predecessor builds use
the supplied profile after a verified backup. Returning through older launchers
may require restoring a saved backup rather than automatic settings migration.

`TEST THIS.txt` is the single grouped player checklist. That earlier candidate ZIP
passed all 704 file hashes, isolated installation, helper identity/status,
authenticated language saving and helper shutdown. No game launch or system
runtime change was performed by that smoke check. The test host's duplicate
PATH/Path environment entries were normalized for the .NET 4.8 child process.

The 1 October published ZIP was 50,737,307 bytes. Its 27 non-English launcher/site/README text
files occupy 61,215 compressed bytes, but that excludes fonts embedded in the
backend DLL. The candidate's Chinese/Japanese/Korean/Arabic font resources account
for approximately 12,702,144 compressed bytes (12.1 MiB, 25.0% of its portable ZIP).
This is an in-memory compression comparison, not a rebuilt English-only product;
no modified DLL was written. See `extracted/portal-shortcut-20261003/language-size-estimate.json`
in the project evidence folder. Optional font downloads or language-specific builds
could materially reduce size, but need a separate implementation and owner decision.
That historical 48.2 MiB complete bundle retained all languages.

## Historical evidence and remaining acceptance

The receipts below describe earlier local candidates. They are not measurements
or acceptance results for the public 1.0.0 installer.

The 4 October `controller-check-slc-20261004` shell reuses the sealed
`candidate-20261004-diorama-r1` / `diorama-20261004` portable payload. The new
wrapper bundles a current bilingual `PlayerGuide.html`; the main Guide link
prefers it, while detailed Controls retain the installed guide. No backend
rebuild or profile replacement is part of this launcher revision.

Current launcher checks: 81 component groups, 10 controller diagnostic groups
and 29 offscreen WPF checks pass. English/Chinese startup, installed, expanded
updates and controller views were rendered offscreen at minimum/default sizes.
The real read-only probe reported slot 0 connected and HidHide off. It did not
send button input, and this does not establish in-game recovery or headset
acceptance. Older isolated-helper evidence below remains attributed to its
original run, not relabelled as a new live end-to-end game test.

`build.ps1 -Test` exercises download recovery, failed verification, Unicode paths,
update/rollback/removal preservation, authenticated backend requests, cancellation,
helper conflicts and truthful launch status. Test fixtures never launch a game.
The component checks also cover long-path installation/removal, rollback after
reselecting the active version, game discovery and saved-choice preservation,
offline installation without network traffic,
corrupt bundles, download fallback, cancellation before bundled acquisition,
and the separation between private candidates and public releases.

The actual WPF startup event was also exercised with empty isolated data and no
installed package: it found this host's official launcher, displayed it in both
languages, and left helper settings untouched. No helper or game was launched.

An earlier isolated smoke downloaded the real `beta-2026-10-01-1817` public asset:
50,737,307 bytes, SHA-256
`1c040eead4ec30acc6fc0a91de7f4d47bf5c36d91107aa51652b8f5830f262ac`.
Its complete manifest passed; helper identity/status, a saved language change and
helper shutdown passed. No launch, profile-apply or runtime-switch endpoint was used.

74 component groups and 14 offscreen WPF scenarios exercise actual control events.
They cover language/selection/error retention, repair/rollback, first-use install,
headset/simulator requests and declined confirmation, capability/busy gates,
visible readiness, the matching developer web URL, reset confirmation, launch
success/error/cancel/retry, recording start/stop/error and wrong-package recovery.
Regression cases include a missing active helper executable, failure after helper
shutdown, and cancelling before the first connection. UI scenarios use isolated
files, fake HTTP and browser/confirmation substitutes: they do not establish
in-game injection, runtime switching or video capture.

The real `--e2e` harness installs two complete copies of the bundled portable ZIP,
starts the actual Python helpers with separate data/profile folders and drives WPF
events offscreen. It reproduces the old-helper conflict, opens its verified URL
through an intercepted browser action, gracefully switches to the intended helper,
runs real PowerShell readiness, reconnects after window restart, recovers after
helper exit and repairs an active package with its Python executable removed.
Settings and rollback are checked. Helpers are stopped gracefully afterwards;
no desktop window, game launch, injection, runtime switch or profile apply occurs.

```powershell
.\launcher\native\tests\bin\Manager.Tests.exe --e2e E:\Coding\wuwa-vr\extracted\recovery-test-new E:\Coding\wuwa-vr\extracted\portal-shortcut-20261003\portable-r2\WuWa-VR-Launcher.zip
```

Use a new isolated output folder each run. Recovery only stops a freshly identified
idle helper; a game, injector, job, recording or unknown activity blocks switching.
Repair checks native process names independently so a broken helper need not start
before it can be repaired. Download and target verification happen before stopping
a healthy helper; failures after shutdown expose Retry and clear stale readiness.

English/Chinese Setup, Record and Help layouts render from WPF at default and
compact content sizes without horizontal overflow. Recording uses two columns to
reduce scrolling. The live desktop attempt was stopped with Escape; the user then
requested background testing. Physical click/keyboard feel, UAC cancellation,
live recording, system runtime switching and headset acceptance are **pending**.

The native app opts into Windows/.NET long-path handling; a >260-character install,
verification and removal test now passes on this host. No system policy is edited.
See [Microsoft's long-path documentation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
for OS requirements. Keep compatibility checks separate from other PCs' acceptance.

To repeat candidate installation/startup with the exact bundled portable ZIP in an isolated folder (no network request):

```powershell
.\launcher\native\tests\bin\Manager.Tests.exe --smoke E:\Coding\wuwa-vr\extracted\installer-smoke-new E:\Coding\wuwa-vr\extracted\portal-shortcut-20261003\portable-r2\WuWa-VR-Launcher.zip
```

Do not point a smoke run at production manager data. `WUWA_VR_MANAGER_DATA` and
`WUWA_VR_DATA` override the two data roots; the test harness also supplies a separate
APPDATA to keep the real UEVR profile out of the test.

Preview without starting a helper:

```powershell
$p = Start-Process '.\launcher\native\bin\Release\WuWa VR.exe' -ArgumentList '--preview','E:\Coding\wuwa-vr\extracted\preview.png','zh-Hans' -WindowStyle Hidden -PassThru
$p.WaitForExit()
```

All local preview renders should finish before continuing; their `.render.log`
records construction and completion. Public packaging must retain the adjacent
licence/attribution files. CircuitLord's installer is the MIT-licensed reference;
none of his closed mod payloads, branding or binaries are included.

The earlier onboarding follow-up made missing-runtime and selected-package states explicit, limited backend Cancel to launch jobs, and retained native errors when helper diagnostics failed. Help includes offline bilingual Xbox essentials and exact-version local portable access. The public release adds current installer instructions at `https://chronohaxx.github.io/wuwa-vr/l/en.html` and `https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html`; deploy these with the release. See `docs/launch-kit/GOAL-AUDIT-20261003.md` for the earlier release audit.
