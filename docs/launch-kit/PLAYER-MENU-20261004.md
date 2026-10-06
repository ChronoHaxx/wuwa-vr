# Private player-menu and incident-diagnostics preview

## Current scope decision — 6 October 2026

The owner has deferred the proposed helmet/visor HUD redesign. Do not implement
it or expand simulator development for it as part of current work. No helmet-HUD
implementation was made; existing head-following placement remains available.
Prioritize established bugs and avoid repeated speculative headset test cycles.

Before revisiting the redesign, establish a repeatable support process for
headsets the owner does not have: record device/runtime/connection combinations,
use a shared short test procedure and diagnostic bundle, and distinguish
simulator checks from device-confirmed results. Unknown combinations remain
unverified. This does not authorize contacting testers or publishing claims.

Research limitation: simulator profiles cover preset FOV/resolution/nominal IPD
and flat quad composition, not optics or personal fit. Cylinder composition is
unimplemented and unadvertised, so curved HUD fidelity cannot be accepted there.
This is another reason not to treat a simulator preview as headset acceptance.

## Historical private preview

Candidate `candidate-20261004-player-menu-r1`, backend `player-menu-20261004`.
Based on public cinematic release at `a3018d1`; not published or installed into
the owner's live profile. Existing rendering fixes, configuration keys and saved
preferences are retained. Physical headset acceptance remains pending.

## Changes

- WuWa Controls presents Immersive VR, Stereo screen (3D depth), and Mono theatre
  (flat 2D) together. Automatic cutscene switching is explicitly unverified.
- Everyday camera/first-person controls remain accessible. Legacy stereo
  comparisons, script status and technical corrections are collapsed into
  advanced sections. No rendering fix was removed.
- Optional Quest walking reports real arming, neutral-release, focus, slot and
  passthrough blockers. Fresh tracking alone no longer implies input delivery.
  Xbox remains the default gameplay control; this is not full motion combat.
- The developer helper keeps five minutes of bounded recent status/log samples
  in RAM and offers **Save recent diagnostics** after an incident. It must be
  running before the incident; it does not capture retrospective video, audio or
  controller history. Saving is explicit and nothing is uploaded.
- English and Simplified Chinese labels cover the changed menu and helper.

## Verification

- Native backend build passed with one low-priority compiler worker. All 127
  source overlays match the compiled tree and reconstruct from the frozen patch.
- Walking input: 161 checks. Auto-cinema: 13 groups. Mono theatre: 14 groups,
  including 43 fixtures. These are offline tests, not device acceptance.
- Incident helper: 17 module/API tests, 10 existing playtest service tests and
  three headless page/HTTP/collector tests covering stale or missing data,
  invalid input, failures and retry. No live game was used.
- Launcher: 87 core tests, 10 controller groups, 17 update groups and 29 offscreen
  WPF checks. The exact compiled launcher installed the exact candidate archive
  into an isolated test store and retained selection after reopening.
- Build warnings: NuGet vulnerability service unavailable; existing two unused
  test-override fields. Dependency restore/build completed from existing cache.

## Settings audit and limits

The local report contains all 214 saved UEVR options, 32 saved game graphics
preferences, startup commands, frozen CVars and the feature inventory. It is a
saved-state audit with clearly dated runtime evidence, not a fresh live query.

Game preferences save shadow quality 3, while the inherited startup script asks
for `sg.ShadowQuality 0`. Earlier renderer telemetry read `r.ShadowQuality 5` in
a different run. These are different layers/times, so the current effective
quality is unconfirmed. No graphics preferences were changed during this audit.
The retained shadow-correction hook reported enabled/ready with no pair faults;
that does not prove every scene visually correct or reproduce another user's
dark-shadow problem.

Open: scene/dialogue loading stalls; unsupported HUD refresh on some game UI;
flat-menu background motion; rare prerendered-movie recognition; selected VFX;
physical acceptance of walking/hand demos. Indath's screenshot describes a
pre-render backend crash near FEnumProperty, not a launcher UI failure. The
source suggests enum-pointer validation deserves investigation, but no exact
crash cause has been established or patched here.

## Private review files

Local artifact directory: `E:\Coding\wuwa-vr\extracted\player-audit-20261004`.
`settings-and-features.html` is the searchable inventory; `delivery.json` records
package/source/build identities. `Ready\WuWa VR.exe` includes the offline
candidate; `Ready\TEST THIS.txt` provides one grouped optional review checklist.
Close another launcher and the game before explicitly selecting/installing this
candidate in step 02. Keep the previous cinematic or screen-comfort package for
rollback. No fresh-PC, live injection or headset success is implied by the
isolated installer test.
