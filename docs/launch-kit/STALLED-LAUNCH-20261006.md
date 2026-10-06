# Stalled-launch follow-up, 6 October 2026

Target: launcher 1.0.4, `beta-2026-10-06-stalled-launch`, VR build `steam-20261006`.
Renderer, injector, profiles and game graphics are unchanged.

## Report and diagnosis

The Windows 11 Steam trial of 1.0.3 failed after the app and recovery payload
successfully updated. The supplied diagnostics still showed an October 4
`lightfix2-20261001` preflight as current/running/cancellable while the selected
build was `steam-20261006`. The game/injector list was empty. Closing the window
was blocked; the user subsequently stopped a worker by PID and tried reinstalling.

The released helper only reports a live owner when PID and creation time match.
The old state is therefore consistent with a genuinely stalled legacy worker;
it is not proof of a particular blocking call. No launch-state file was included
in the published ZIP. The exact remote blocking call remains unconfirmed.

An isolated reproduction against the actual published 1.0.3 Python helper,
substituting only the identity of a newly created inert child, reproduced the
misleading old launch message. The corrected helper classifies it as stalled,
retains provenance, and clears busy/current status as soon as that child exits.

## Changes

- Report stalled heartbeat, owner PID/check, legacy provenance, cancellation
  age, helper-script PID/elapsed time and bounded startup-log excerpts.
- Treat cancellation as a request until observed; offer visible process recovery
  when it fails or remains pending. Preserve actual live-worker mutation guards.
- Re-read status on Close with a six-second overall deadline. If the helper
  cannot close, offer an explicit default-No window-only exit that stops no work.
  Unfinished local package writes must still reach their safe cancellation boundary.
- Find the exact recorded worker in older portable packages using same-user,
  session and process-creation checks plus known shipped script bytes. Show
  unverifiable recorded owners as read-only rows instead of silently omitting them.
- Add bounded uninstall cleanup for verified downloads, cooperative helper
  shutdown, and a report of retained files. No uninstaller force-kill. The native
  preparation action exposes confirmed process recovery before Windows uninstall.
  Preserve personal data, active OpenXR dependencies, unknown files and busy work.

## Verification scope

Evidence folder: `extracted/stalled-launch-recovery-20261006`.
Reproduction and helper tests: `extracted/startup-recovery-stale-worker-20261006`.
Final build, isolated installation, packaged-helper lifecycle, publication and
updater receipts are recorded separately. Background tests do not establish
Windows 11 UAC, actual Steam injection, physical headset use, or the remote PC's
original blocking call. Those acceptance checks remain pending.

## Remote review

- [ ] Update launcher and explicitly install the matching stalled-launch payload.
- [ ] Old worker shows an identity and recovery guidance, not endless fresh progress.
- [ ] Stop waiting distinguishes acknowledged exit from a pending request.
- [ ] Close can exit the window after confirmation when recovery is unavailable.
- [ ] Confirm selected-worker recovery, then retry one Steam launch and record result.
- [ ] If uninstalling: prepare cleanup, review retained reasons, then remove the app
      through Windows. Recordings, backups and settings must remain untouched.

Previous 1.0.3 release and its evidence remain available; no published asset is
overwritten. Keep this release labelled beta until the remote trial succeeds.

## Release evidence

- `build2.json` / `build2.log`: native Release build passed with one compiler
  worker; 95 core checks, 10 controller groups, 33 process-recovery checks,
  19 uninstall checks, 17 updater checks and 50 offscreen WPF case results.
  The first build exposed a test-only mixed path-separator assertion; the final
  run uses a canonical expected path. No recovery implementation was weakened.
- `real-helper-result.json`: exact compiled launcher connected to the packaged
  Python helper, verified its manifest, reproduced a stale legacy live owner,
  requested cancellation without killing it, refused normal shutdown while it
  remained active, then cleared busy state after the owned inert fixture exited.
  The isolated helper exited and removed its receipt.
- `historical-pins.log`: the actual October 4 and October 6 public portable
  startup scripts pass historical identity recognition without executing them.
- `verify-install.json`: the exact launcher installed and reopened the exact
  archive in an isolated store; embedded catalog and manifest identity match.
- `pack.json` and `packed/pack-receipt.json`: 1.0.4 installer/update pack passed.
  The original 1.0.3 assets are retained for rollback.
- Website checks: 25 layout checks, 15 community checks and 14 current-release
  checks passed. EN/Chinese installer links and three-step setup were checked at
  desktop and phone widths using existing Chromium 1228; nothing was downloaded
  to obtain a browser and no live installation was performed.

SHA-256 identities:

- Launcher: `792982820f6c4ce700776b695f3dc22dafcccf6e7ae69951a07f9e759aeadf7d`
- Setup: `f0b120bcd998a8019e1789391cadea05d58f007d1953d73c4fb1bc59616f2108`
- VR ZIP: `5354367678eab69f83fb23fdb3ba30ef728bffe960f9aca3f0118d449d85759c`
- Full app update: `b39d526c13dba9cca7b51980af83eedcd06e7dfc6127c71fb4ddbe2cb3396bca`

The Windows uninstall hook itself, live UAC and the affected remote launch are
not physically verified by these background fixtures. A retained-file report is
written under `%LOCALAPPDATA%\WuWa VR Manager\uninstall-result.txt`.

Full-package cleanup: `uninstall-package-test/receipt.json` records the final
compiled service removing all 842 manifest files, two metadata files and the
cached ZIP from an isolated installation in 4.81 seconds (20-second budget).
Installed-package state was cleared; settings, recording, backup and log fixtures
survived. Only environment/process/runtime hooks were replaced with inert test
functions; production hashing, deletion, state writes and report generation ran.

Offscreen native review: `native-review` contains 10 EN/Chinese images of stalled
startup, process recovery and uninstall preparation at normal and reduced window
sizes. Actions remain visible/reachable; no window, HWND or helper was started.
Backend diagnostic text retains its documented English fallback.
