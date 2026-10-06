# Fresh-PC launch and recovery audit — 6 October 2026

This records the original private r2 audit. The user subsequently requested
publication and a visible, confirmed process-recovery panel. That follow-up is
tracked in `STARTUP-RECOVERY-RELEASE-20261006.md`; the private r2 artifacts below
remain preserved and do not contain the later recovery panel.

The public Steam beta failed the user's Windows 11 PC trial. The report shows
repeated startup/runtime lock refusals with no game or injector running, an old
October 4 preflight record, and a refused connection to a previous local helper
port. It does **not** establish a Windows 11 incompatibility or an injected
renderer crash. The original release and evidence are preserved.

## Identity and publication

- Public baseline: `beta-2026-10-06-steam`, launcher 1.0.2, commit
  `493c6407a8d2acee9196beef780f65ef77c646bb`. GitHub main, release target, source
  archive, installer and package digests were checked during this audit.
- Private repair: launcher 1.0.3, `candidate-20261006-startup-recovery-r2`.
  This candidate is not published; the public catalog/feed stay unchanged.
- Renderer remains graphics-r2, SHA256
  `c3e19ef50f1b5e9a28b2d09e61743381fd3ba38287f5fafd30ee0486e5eb2c2c`.
- Injector remains SHA256
  `244104e019a1b6dd5bc63775a6407e56cde61fbb781130643941adc108170384`.
- Evidence and private delivery are under
  `E:\Coding\wuwa-vr\extracted\fresh-pc-launch-audit-20261006`.
  Revision r1 is retained as an intermediate, not the delivery to test.

## Repaired behavior

1. Every new launch has an attempt identity, owner PID/creation time, cancellation
   marker and diagnostic folder before elevation. Early worker/account/catalog
   failures and injector stderr are retained. A retry cannot replace a live
   attempt's state, including the request/worker lock handoff race.
2. Native and web interfaces retain current worker progress after the initial
   request finishes. Stop waiting remains available for a verified pending
   worker, late errors expand details, and duplicate launches/runtime changes
   are disabled. Stale state cannot masquerade as a current failure or authorize
   cancellation of a recycled PID.
3. Connection deadlines cover both headers and response bodies. A transport
   timeout is an actionable error, not an apparent user cancellation. Timed-out
  POSTs are never automatically repeated. The native app suppresses Python's
  fallback error dialogs, so a helper failure cannot hide behind a blocking
  message box instead of reaching the launcher's recovery UI.
4. Closing the native app stops its verified idle local helper. The existing game
   can remain running. Active startup/recording/operations prevent silent exit;
   lost startup status requires reconnection before abandoning its worker.
   No process-name kill or arbitrary lock bypass was added.
5. Simulator selection supports absent first-time OpenXR registration and a
   missing older simulator path, preserves the headset backup, compares state
   again before changing it and restores the prior registration on failure.
   UTF-8 manifests and explicit empty-registration arguments work under PS5.
   Simulator VC++ dependencies are checked by file presence before switching or
   launching, with an actionable missing-prerequisite message.
6. Lock inspection distinguishes access denial from a known occupied mutex.
   A real isolated reproduction showed an **unowned** mutex could previously be
   labelled as a running launch when opening it was denied. This is a proven
   diagnostic defect, not proof that the affected PC has that exact condition.

## Limits and open evidence

- The reported old lock's owner is not identified by the supplied report. The
  public ZIP contains no launch-state, endpoint, settings or historical run
  records; the October 4 state did not originate in that release's packaged files.
  A Windows restart may be needed once to clear an unidentifiable legacy lock.
  The new launcher deliberately does not kill an unverified process.
- Cross-account UAC now fails clearly before using the wrong roaming profile.
  Starting the game as a different Windows user is not supported by this fix.
- VC++ checking establishes file presence, not successful DLL loading or ABI
  compatibility. No redistributable, runtime registration or security setting
  was changed during this audit.
- Indath's gamma-discovery concern is plausible but unconfirmed here. The gamma
  hook and SDK patch are unchanged since the October 4 cinematic release; our
  source guards discovery failure and contains no new hardcoded gamma slot.
  This rendering hook cannot explain a refusal before injection. His exact
  compared hashes/logs are still needed to assess that separate issue.
- English and Simplified Chinese are the compact launcher's implemented language
  packs. They do not determine the game's language.

## Verification and acceptance

Background verification covers real inert PowerShell children, isolated mutexes
and registry substitutes, helper status/cancellation, bounded HTTP handlers,
offscreen WPF interaction and actual package installation into a fixture store.
The native build passed 95 core tests, 10 controller groups, 17 update groups and
36 logged offscreen checks. The offscreen harness's old final count is stale;
individual PASS records are the evidence. Helper recovery tests passed 14 cases;
Steam discovery/settings regression tests passed 6. Runtime tests passed 13
groups; final lock/lifecycle checks passed 21. An additional real integration
check connected the compiled native bridge to the bundled Python helper in an
isolated data/roaming directory, read status and closed it through the native
cleanup path: the actual child PID exited and its receipt was removed. All 842
packaged file hashes and the bundled Python self-check passed. Exact artifact
hashes are recorded with the delivery receipt. No live game/injection, UAC,
microphone, global runtime
change, production profile install or publication was performed.

Human acceptance for the exact r2 delivery is still pending:

- [ ] On the affected PC, select and install the bundled r2 candidate in step 02,
  retaining the saved Steam installation. Existing public beta remains rollback.
- [ ] With the game closed, select the bundled simulator; after accepted UAC,
  the app marks this package's simulator current or gives the precise failure.
- [ ] Launch once; confirm visible stages through Steam/game/injector/first frame.
  If it fails, Copy diagnostics includes this attempt, not only an old record.
- [ ] While startup waits, Stop waiting ends that owned wait and permits a retry;
  it does not close a running game or change the runtime.
- [ ] Close the idle launcher and verify its local helper exits; a running game
  remains untouched. Reopen and confirm saved choices, then test actual headset
  output separately.
