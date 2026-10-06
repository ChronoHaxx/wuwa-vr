# Renderer-startup follow-up, 6 October 2026

Release target: launcher **1.0.5**, `beta-2026-10-06-renderer-startup`,
backend/package build `renderer-startup-20261006`, for WuWa **3.7**.
This beta has compiled and passed the background checks below. Do not describe
the affected PC or headset as working until those trials succeed.

## What the supplied log establishes

The Windows 11 Steam retest of **1.0.4 failed renderer startup**. The injector
verified all three requested DLLs loaded, and UEVR wrote a fresh backend log.
That log repeatedly created and hooked a dummy DirectX 12 swapchain, then
requested another hook attempt. It did not reach real renderer/VR initialization.
The user reported an earlier crash and manually closed later attempts; the
15:25:29 framework shutdown does not establish a crash by itself.

This is a different stage from the legacy-worker problem addressed in 1.0.4.
Neither Windows 11 nor the simulator is established as the cause. A single
`Failed to get type info` line also does not establish the cause. Creating a
dummy swapchain, loading DLLs and writing a log are not proof of a rendered frame.

The copied report said Steam was selected, while its startup transcript followed
the manual-start branch. The production delegate passed the Steam arguments in
isolated tests; no dropped-argument defect was reproduced. New per-attempt mode
and target fields make that distinction visible on the next trial.

## Changes

- Permit bounded DirectX 12/11 probe alternation before the matching real
  presentation callback is reached, instead of repeating only the initial
  graphics probe. Callback lifetime guards retain the original functions while
  removing hooks, so queued callbacks forward without initializing a retired renderer.
- Capture a bounded backend-log head and tail inside each launch attempt, with
  source identity and truncation metadata. Preserve that evidence when the
  shared profile log rotates; never clear the player's log.
- Include backend evidence and effective startup selection in copied diagnostics.
  Repeated graphics-hook retries show that UEVR loaded but has not attached to
  the game renderer. A lone RTTI warning does not trigger that diagnosis, and
  actual renderer initialization clears the retry warning.
- Keep error reporting observational: no automatic abort, reinjection, runtime
  switch, graphics-setting change or game-process termination.
- Preserve already-attributed backend evidence when Steam process identity later
  becomes unreadable or ambiguous. Report that verification gap instead of saying
  the renderer never started; readiness still requires the uniquely matched game.
  Diagnostics record bounded process IDs, counts and path-readability flags.

## Verification status

- **Implemented and fixture-tested:** bounded evidence capture, mode/target
  recording and renderer-retry classification. The targeted PS suite passed
  35 checks, including actual worker-delegate and simulator-runner scripts with
  inert system boundaries. The existing 24 launch-attempt checks and Steam
  identity/dispatch suite also passed. These do not launch or inject into WuWa.
- **Player diagnostics passed:** 25 Python tests, including bounded/redacted
  backend evidence, attempt identity, and propagation of ambiguous Steam-target status.
- **Compiled:** backend SHA256 `901bfc1a5cc2a85209f0b5d861824078388274c02ba1b6ddcfc93b6b09f8728c`.
  One low-priority compiler worker completed at 16:11 BST. A prior attempt stopped
  at the memory guard; it did not leave a compiler running.
- **Background graphics checks passed:** real hardware DX11/DX12 swapchains in
  hidden, isolated test windows, actual PointerHook dispatch/restoration, and
  queued Present/Present1 retirement. These exercise the hook primitive and policy,
  not the full UEVR initialization or a game. Eleven production-policy cases passed.
- **Source reconstruction passed:** the full patch applies to the pinned upstream,
  matches all 131 overlay files and preserves the other indexed source entries.
  Patch SHA256 `4889f6e8346780c832ac4b1c43d1f6c0ad3a2d4c665f1c35feeeb455799bc9c5`.
- **Launcher and packaging passed:** 226 existing checks, including offscreen WPF
  cases; real package installation into an isolated directory; and Velopack
  installer/update packaging. No app was installed into the production location.
  The public Setup executable itself has not been installed on a fresh Windows 11 PC.
- **Package/source identity checked:** archive
  `5022a8052a59317234fbc678f88416f2c50d925b40a89d140b87cc7a8eedce0e`;
  Setup `70945bef83dccedf3a8c41e1b1ba6dcd1d6528d74456f489ed2de143362c2e02`.
  Full hashes and the source checkpoint remain in the release assets.
- Website and public update-feed checks are recorded in the publication receipt
  after deployment; local build success does not establish public availability.
- **Pending:** successful startup on the affected Windows 11 Steam PC, the
  owner's regression check, and physical headset acceptance.

Evidence prepared so far: `extracted/backend-startup-audit-20261006/receipt.json`
and `supplied-log-classification.txt`. The log archive is under
`extracted/steam-init-audit-20261006`; do not publish private diagnostic paths or
assume its launcher log is the backend log.

Native and graphics receipts: `extracted/renderer-startup-20261006/backend-build/build-20261006-160551.json`,
`dxgi-hardware-r2/evidence.json`, `policy-test.log`, and
`source-reconstruction/latest-receipt.json` under the same evidence directory.
Launcher/installer receipts: `build.json`, `verify-install.json`, `pack.json`,
`packed/pack-receipt.json`; final helper test output: `startup-tests-r2.log`.

## Working-PC comparison

The owner's successful Steam run on Windows 10 used the same published 1.0.4
backend as the affected PC, not a private development DLL. The seven exposed
VR flags in the supplied diagnostics matched, and both selected the simulator.
The owner has an RTX 3080; the user reports an AMD RX card on the other PC, with
model and frame-generation state unconfirmed. The local run used real DX12;
the remote log only establishes a DX12 *probe*. No forgotten developer-only
prerequisite or enabling configuration was established. OS, driver, overlays,
actual game graphics API and frame generation remain possible differentiators,
not diagnosed causes. Indath's earlier FEnumProperty report is a separate stage.

The same comparison exposed the Steam observation issue repaired above: the
local renderer initialized even though process verification later failed.
That reporting error does not explain the affected PC's missing renderer frames.

## Update and rollback

1. In step **02 → Versions & updates**, check for updates and update the app to
   **1.0.5**. Restart to update when the game, injector, recording and other
   operations are idle. If the old app cannot update, use the new installer.
2. Explicitly choose **beta-2026-10-06-renderer-startup** in the package list and
   install it. Updating the app preserves the selected VR package; checking for
   updates alone does not replace it. The renderer change requires the new package.
3. Keep the previous package and existing backups for rollback. User settings,
   recordings and logs stay in their current folders. Review stuck processes
   through the existing confirmed-recovery flow before changing packages.

For the affected-PC trial, record the selected mode, exact package, runtime and
whether the game reaches rendering. If it waits again, copy diagnostics before
retrying; do not start another injector. Keep crashes and deliberate game closure
as separate outcomes. A simulator trial does not establish headset acceptance.
