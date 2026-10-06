# Renderer handoff candidate, 6 October 2026

App **1.0.8**, package **beta-2026-10-06-renderer-handoff**, build
**renderer-handoff-20261006**, for game **3.7**. Build and background-test evidence
is separate from affected-PC and physical-headset acceptance.

## What the latest failure establishes

The affected Windows 11 Steam PC still starts flat with **1.0.7**. Its report
verifies all three injection DLLs and one exact game process through a retained
handle. The earlier process-identification problem is not the blocker shown by
this trial. UEVR does not reach renderer or OpenXR initialization.

The first DX12 probe receives one filtered callback. Later DX12 probes own the
observed dummy swapchain slots but receive zero callbacks. DX11 probes receive
61 calls each and reject the rendering chain's DX11 device query with
`E_NOINTERFACE (0x80004002)`. Owning a dummy slot does not prove the game's calls
use that slot. These facts do not establish a Windows 11, GPU-brand, driver or
frame-generation cause.

An isolated retained-dispatch fixture reproduces that diagnostic pattern using
the production hooks: calls through a previously retained callback continue
reaching the wrong probe while the active DX12 dummy slots receive no calls.
That is a reproduced mechanism, not proof of the affected game's exact internal
dispatch implementation. Evidence is under `extracted/retained-dispatch-20261006`.

## Candidate behavior

The repair targets rendering calls that retain an earlier probe callback.
A handoff to DX12 requires the same rendering chain to positively expose a
DX12 device; a failed DX11 query by itself is insufficient. It validates a direct
command queue at the existing known offset, checks that queue's owning device,
and retains COM references. Refused handoffs use the original callable. Review
covered callback lifetime, locking and forwarding; the fixture checks one call
to the original Present per frame and refusal for genuine DX11 or an invalid offset.

The candidate retains the prior window-filter fixes, exact Steam target checks,
per-attempt backend evidence, cancellation and shutdown reporting. A loaded DLL
is not a rendered frame. No scene-rendering, graphics-preset, game-input or
OpenXR runtime change is part of this startup repair.

## Verification state

- The **1.0.7 affected-PC retest failed**; that result remains recorded.
- Both retained-vtable and cached-callback cases: **61/61 DX12 callbacks** after
  repair, with the expected queue and one original Present per frame. The original
  sources reproduced zero DX12 callbacks and 61 wrong-device DX11 callbacks.
- Genuine DX11, invalid-offset refusal, retired forwarding, captured Present1,
  three ordinary hot API switches and final slot restoration: **passed**.
- Diagnostics retention: **28 focused tests passed**.
- Full backend build: **passed**, one worker at BelowNormal priority, completed
  6 October 2026 at 18:47:35 BST. SHA256:
  `b5267ae0478f154c290e514fe6e60d21c88852292e8820e77ff27242bebb3365`.
- Source reconstruction: **passed**, all 136 overlay files match build inputs.
  Patch SHA256: `043f8850d901153c343fb8b20d02b3522d70c834459b9c93243d646b9fc1328b`.
  Reconstructed tree: `d81b96386c52029784136c5c98760d1e08092488`.
- Launcher tests and offscreen WPF checks: **passed**. No game or helper launch.
- Isolated package installation and updater packaging: **passed**. No production
  installation or user-settings change. Portable SHA256:
  `6d534a0c9c118105352a959c86fa3bfbc121565cd09c57438da752429f91e0e1`.
  Setup SHA256: `863bb4aefff506d641f0379a0251c4da2b0dfb89e6ab85810ffa65e7a0f1e220`.
- Local website: **14 release-page and 15 community-page checks passed**, with
  no browser errors or network posts. Public download/update verification follows
  publication and is recorded separately in the release evidence directory.
- Successful startup on the affected PC and physical-headset acceptance:
  **unverified**.

Final receipts belong under `extracted/renderer-handoff-release-20261006`.
The fixture exercises production hooks with real DXGI but stubs the Framework
monitor; it does not initialize the full game renderer or OpenXR. Queue/device
identity rejection was source-reviewed, not tested with a substituted foreign
queue. Supplied personal diagnostics are not publication assets.

## Update and one affected-PC check

When this release is available, update the app to **1.0.8** while idle, then
explicitly select and install **beta-2026-10-06-renderer-handoff** in step 02.
An app update preserves the previous selected VR package. Keep the same game
installation, runtime and graphics settings for comparison. No reinstall or
driver change is needed to try this repair.

- [ ] One launch reaches the simulator/headset's actual stereo view.
- [ ] If it remains flat, Stop waiting ends the wait and Copy diagnostics retains
      the per-attempt hook/device/handoff evidence; no second injector is started.
- [ ] Exit normally and confirm another intentional launch remains available.

Keep older packages and saved backups for rollback. Background validation cannot
substitute for the affected-PC result or physical-headset testing.
