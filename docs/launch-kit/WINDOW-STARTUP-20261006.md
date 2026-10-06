# Window-startup compatibility follow-up, 6 October 2026

Release: **1.0.6**, **beta-2026-10-06-window-startup**, build
**window-startup-20261006**, game **3.7**.

## What the 1.0.5 retest established

The supplied Windows 11 Steam diagnostics show the correct 1.0.5 app and
renderer-startup package. Steam dispatch, exact target verification and loading
all three injection DLLs succeeded. UEVR still did not reach renderer/OpenXR
initialization. The previous release therefore did **not** solve this PC's
startup failure. Its new probe fallback ran, giving more useful evidence.

There was one shipping process, with its path later unreadable through the
PowerShell/.NET module query. The worker was alive with a fresh heartbeat. This
was neither evidence of a duplicate game nor a stale worker. The route recorded
both requested and effective Steam dispatch, so dropped Steam arguments are not
the cause of this attempt.

Both probes hooked the same recorded DXGI Present slot. DX11 reached Framework
but had no DX11 device; DX12 did not produce a real-renderer initialization marker.
The source showed an unchecked DX12 `GetHwnd` call before window filtering, while
DX11 used `GetDesc().OutputWindow`. A failed/null result silently rejects a frame.
The copied log is sampled and lacked this gate's results: this is a concrete
compatibility gap and strong investigation lead, **not proof of the remote cause**.
The GPU brand, Windows version and frame-generation state are not diagnosed causes.
Indath's earlier FEnumProperty failure occurred at a different stage.

## Repairs

- Query a supported `IDXGISwapChain1` interface before `GetHwnd`; fall back to a
  successful base `GetDesc` window when the first lookup fails or is null. Keep
  the normal window filter. No foreground-window guessing or null-window bypass.
  Use the same resolver for Present and resize callbacks.
- Log a bounded number of window-selection/rejection and device-query results
  before Framework initialization. No per-frame log flood.
- Read Steam executable paths with `PROCESS_QUERY_LIMITED_INFORMATION`, avoiding
  a dependency on stronger module-reading permission. Exact selected-path checks
  remain. Retain verified identity only while its original synchronized process
  handle remains alive; never trust a PID alone. If synchronization is denied,
  query afresh and do not cache. Handle retention is capped at 32 per worker.
- Compact repeated backend lines while preserving distinct errors and graphics
  startup transitions in copied diagnostics. Reads and output remain bounded,
  paths redacted, and saved attempt logs take precedence over a later game run.

The injector, graphics preferences, accepted scene-rendering changes and recovery
controls are retained. No production profile/runtime was changed and no live game
was launched or injected during these background checks.

## Evidence

- **24 isolated native checks passed:** production resolver with real hidden
  hardware HWND/legacy/composition swapchains, forced lookup failures, invalid
  outputs and diagnostic budget. No game, Present or backend was run by this test.
- **18 real Win32 process checks passed:** restricted child denies module reading
  while the limited query succeeds; later denial of new handles, limited-only
  access, exact path rejection, exit and stale-handle behavior are covered.
- **35 startup/backend checks and the existing Steam suite passed.** System
  boundaries are inert; this is not a Steam launch on the affected PC.
- **27 Python checks passed**, including repeated/interleaved error floods,
  backend evidence, redaction and attempt selection.
- **Native compilation passed** with one low-priority compiler worker and memory
  guard. Backend SHA256:
  `5e7423ada531817047516cc9a5db31c6322d839ebfb79c18d0fa1188c1424d17`.
- **Source reconstruction passed:** all 132 overlay files match the reconstructed
  pinned-base patch; other indexed source entries are preserved. Patch SHA256:
  `cdad82b9349ed77a2d5b7c49be5e9e19805521e069406e3f41517f6cb7cf100a`.
- **226 launcher checks passed**, including offscreen WPF. Real package install
  into an isolated directory and Velopack installer/update packaging passed.
- **25 website and 15 community-page checks passed**, no browser errors.
- Archive SHA256:
  `4006be72786670c087b2a3d88cad8e257b5dadc92df54d58b74f569abfd901ca`.
  Setup SHA256:
  `8c4835b1d21bec3be19e25f2817d817074307e8a0a659977246148af58aa3f20`.
- Publication and live updater receipts are recorded after deployment. Local
  build/package checks alone do not establish public availability.

Evidence root: `extracted/window-startup-20261006`, with `hardware/evidence.json`,
`backend-build/build-20261006-164021.json` and
`source-reconstruction/latest-receipt.json`. Restricted-process receipt:
`extracted/renderer-startup-20261006/limited-process-identity/f02d35479c6b45ca889879063314d41a/receipt.json`.
Startup receipt is under that root's `limited-query-startup/3e5415be623245d3be8a8fa45582efb4`.
These paths are local evidence; supplied personal diagnostics are not release assets.
Package and app receipts: `stage-receipt.json`, `build.json`, `verify-install.json`,
`pack.json` and `packed/pack-receipt.json` under the evidence root. The native
launcher was not installed into the production user directory during these checks.

## Update and remaining acceptance

Update the app to **1.0.6** when idle, then explicitly select and install
**beta-2026-10-06-window-startup** in step 02. Updating the app preserves the
existing package selection. Keep older packages/backups for rollback.

One affected-PC startup trial remains necessary. If it fails, copied diagnostics
should show whether the actual chain was rejected, selected or failed its device
query. Stop waiting and copy evidence before retrying; do not start another
injector. A successful remote launch and physical headset acceptance are pending.
