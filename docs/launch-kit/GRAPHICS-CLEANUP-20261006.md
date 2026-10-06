# Private graphics settings cleanup

## Current follow-up: r2

Candidate `candidate-20261006-graphics-r2`, backend `graphics-20261006-r2`.
The r1 notes below are retained as history, including its failed live acceptance.

**Live result, 6 October 10:40 BST:** the owner launched r2 (game PID 24968).
Selected backend and injector hashes match this delivery. The v2 migration at
10:35:10 repaired only the exact two r1-generated files; both stayed empty after
startup and after the diagnostic read. The timing-only file remained unchanged.
Saved-profile cleanup passes for this run, independently confirmed from the
receipt and file hashes. This is not full visual/headset acceptance.

The reader's latency check **still failed**: one request timed out at 8 seconds.
Its response arrived after 8.989 seconds, matching the retained nonce and PID.
It confirmed cached-only access, four numeric readings, 34 uncached accessors and
three missing variables. No second query was made. Live `sg.*` quality levels
cannot be claimed from this run. Profile hashes stayed unchanged throughout.
See `live-20261006-093959.json`, `late-response-20261006-094008.json` and
`runtime-findings.json` in the r2 evidence root. The preceding r1 readings remain
historical, not current r2 settings.

Read-only source review found that cached-getter checks happen after console
manager resolution and a separate linear name lookup for each of the 41 names.
Missing names scan the whole array; manager initialization can also resolve
strings/code references. These are remaining candidate costs, not a timed root
cause. The next diagnostic change should measure those stages and bound or
incrementally process name lookup, retaining cached-only getters. This does not
require undoing the successful saved-profile fix. No further live probing,
implementation or rebuild was performed during this acceptance check.

The owner confirmed that they only launched r1. The injector opened its saved
profile over hardcoded level-2 defaults, marked graphics dictionaries dirty and
wrote them from its timer. R2 suppresses that automatic population/write path for
policy-managed profiles. Unset injector controls display **Game**, with no claim
to know the live value; deliberate edits update only the selected command.

The new `game-settings-v2` migration repairs the exact r1-generated preset pair
once, only when a valid v1 receipt proves the original cleanup was inherited-only.
Comments, edited values, incomplete provenance and custom source files prevent
this repair. Older template cleanup, saved snapshots and rollback remain.

Batch live graphics reads no longer initiate console getter layout discovery.
They use cached SDK numeric accessors under a nonblocking cache lock and report
missing/busy layouts as unavailable. Unverified raw-flags calls are removed.
Name lookup and cached getter calls still occur; this is not a hard latency
guarantee. Single-variable reads retain their previous behavior.

Evidence root: `E:\Coding\wuwa-vr\extracted\graphics-cleanup-r2-20261006`.

- Backend compiled with one BelowNormal worker, memory at 82–84%, over 10 GiB
  available. SHA-256: `c3e19ef50f1b5e9a28b2d09e61743381fd3ba38287f5fafd30ee0486e5eb2c2c`.
- Frozen native patch: `585955b8c94b224f1e33f19945ec7b528ec3272b78131a5d151382720a679dc3`;
  all 128 overlays match the build and reconstruct from the pinned base.
- Injector compiled from the nine-file source overlay over base
  `95d7eee535dda4c59cfe812e4e7942ac05da5541`. Executable SHA-256:
  `6783312467ee7106f6be6a8bda1c55b9b3fcf14f9627cf0b4b3ee5d6051cb872`.
- Graphics migration and profile-upgrade regressions passed in PowerShell 5.1
  and 7, including custom preservation, repeated selection, rollback and a
  post-migration failure restoring the previous state.
- A fixture copied from the observed r1 files passed v2 migration, then the real
  compiled injector's load/UI/timer-write methods left all four files unchanged.
  The form was never shown; actual window timing and injection remain untested.
- Numeric accessor policy: 187 offline checks passed. Independent integration
  review checked all 839 staged manifest entries, nine injector source hashes,
  v1 lineage metadata and the matching built/tested/packaged injector.
- Portable archive: `15658fab20d834b0a8a06d891dbbb8a5df01b36f479f070b40adcff849ccf422`.
- The rebuilt launcher passed 87 core tests, 10 controller groups, 17 updater
  groups and 29 offscreen WPF checks. Its actual installer verified this exact
  archive and retained the selected r2 identity after reopening the isolated
  store (`verify-install-005385625b7340d69b17a3d0701dfc21`).
  Launcher SHA-256: `28a1a5da2e8305fa8ae88c550a1830353adc3525696c416d03a9dc2ffb305a3a`.
  Warnings were the unavailable NuGet vulnerability endpoint and two existing
  unused test override fields; compilation and cached dependency restore passed.
  Final launcher/archive identities are recorded in `delivery.json`.

R2 installation and live/headset acceptance are separate. The current play
session was not modified. When it ends, install r2, check that opening the
injector no longer recreates the preset, and compare the same scene's appearance
and frame rate. The r1 backup is retained as evidence; prior screen-comfort or
cinematic packages remain the useful play rollback choices.

## Historical r1

Candidate `candidate-20261006-graphics-r1`, backend `graphics-20261006`.
Continues the private player-menu and incident-diagnostics preview on
`codex/player-menu-diagnostics`. Public release and production profile are unchanged.

**Runtime follow-up: r1 cleanup acceptance failed.** On 6 October the owner
installed and launched without adjusting graphics. Migration cleared the known
templates, then the bundled injector's Shown/timer path regenerated its level-2
preset before injection. A live snapshot returned 38 values after the client's
8-second timeout (10:11:02 BST); cold SDK getter discovery was expensive. A
follow-up is required. Preserve r1 artifacts and test evidence; do not describe
this revision as fully following game preferences.

## Behavior

The inherited 14-line startup preset asked for many `sg.*` groups at zero. It
conflicted with saved game preferences and did not establish the effective live
quality. The live read on 6 October at 09:44 BST reported `r.ShadowQuality=5`;
the old bridge could not read `sg.ShadowQuality`. Neither observation proves the
visual result in every scene or that both eyes are correct.

This package opts into `game-settings-v1`. When the player selects it with the
game closed, the existing snapshot transaction clears only exact recognized
templates in `user_script.txt`, `cvars_standard.txt` and `cvars_data.txt`, retaining
`r.OneFrameThreadLag=0`. Custom files are preserved byte-for-byte. A receipt
prevents later intentional edits being silently cleaned again. Returning to an
older package and back is covered, including older helpers leaving a stale receipt.
The policy covers these three files; native rendering corrections and other
scripts can still affect renderer settings. There is no new performance preset.

Developer tools adds an English/Chinese **Graphics: saved vs live** panel. Saved
inspection and live reading are separate explicit actions. The new backend reads
a fixed 41-variable inventory including `sg.*` and `r.*`. Missing variables or
unvalidated getters are unavailable, never synthetic zero. Existing writes remain
restricted to `r.*`; the new snapshot endpoint accepts no settings to change.
Raw SDK flags are separate from last-writer provenance, which remains unavailable.

## Evidence and identity

Evidence root: `E:\Coding\wuwa-vr\extracted\graphics-cleanup-20261006`.

- Backend build passed with one BelowNormal worker after the game closed;
  memory stayed at 71–72%, with at least 17.65 GiB available in recorded samples.
  The earlier attempt stopped automatically at 86% and was not packaged.
- Backend SHA-256: `b2d8e1d577c9c5d2c4ef9730643981411f5b21cc0074113cd2c545106e9e69c7`.
- Frozen patch: `4b7130a910ee742d53bd6e0d0ad0587cc4ebe722c71f2188600e59e7d6dcea4b`.
  All 128 overlays match the compiled tree and reconstruct from the recorded base.
- Portable payload: `1623af7c8b923e6a1414811ba69f511d03a9897002c3d3d5e95e2e340099c59e`.
- Exact-template, custom-preservation, same-build and cross-version migration,
  immutable rollback and prior profile-upgrade fixtures passed in Windows
  PowerShell 5.1. Policy checks also passed in PowerShell 7.
- Console read/write policy: 164 offline checks. Python reader: six tests.
  Real page/HTTP tests passed with inert graphics backends, including authentication,
  invalid bodies, failure/retry and literal text rendering. Prior incident tests:
  17 passed. Screenshots do not establish live or headset acceptance.
- Packaging corrects the build runner's inherited PowerShell 7 module path before
  invoking Windows PowerShell 5.1. The shipped helper already did this. Incomplete
  staging attempts were not published or installed.
- Launcher: 87 core tests, 10 controller groups, 17 updater groups and 29 offscreen
  WPF checks passed. The exact compiled launcher installed the exact archive into
  `launcher/native/work/verify-install-5eb8ba52ee0543fa9de2541fd2e60acf` and
  retained selection on reopening. No normal launcher was opened by the tests.
  Build warnings: NuGet vulnerability endpoint unavailable and two existing
  unused test override fields. Cached dependency restore and compilation passed.
- Launcher SHA-256: `6c853658d2998ca0e4d640fdcf9293eb3b8436d08bcfebad144576be19804800`.

The final `delivery.json` records exact launcher/archive hashes and the isolated
installer receipt. No game launch, injection, runtime switch, production profile
write or publication occurred while preparing this candidate.

## Human checks still pending

- [ ] Close other launchers. Open `Ready\WuWa VR.exe`, choose
  `candidate-20261006-graphics-r1` in step 02 and install it with WuWa closed.
- [ ] In Developer tools, inspect saved overrides: inherited entries absent;
  any custom entries remain listed and timing is separate.
- [ ] Play the same outdoor scene and explicitly read live graphics. Both shadow
  values are numbers or individually labelled unavailable with a reason. Compare
  appearance, frame rate and both eyes against the prior package.
- [ ] If appearance or performance regresses, close WuWa and select the previous
  package. Its saved profile is retained. The immediate predecessor artifact is
  `extracted\player-audit-20261004\Ready`; screen-comfort and public cinematic
  rollback choices remain available as appropriate to the user's prior selection.

No claim is made that this fixes Indath's crash, all shadow issues, dialogue
loading stalls, cinematic detection, HUD refresh or the remaining VFX issues.
