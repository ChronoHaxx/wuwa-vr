# Window-filter startup repair, 6 October 2026

Candidate app **1.0.7**, package **beta-2026-10-06-window-filter**, build
**window-filter-20261006**, for game **3.7**. Publication and affected-PC
acceptance are separate from the checks below.

## Failure being addressed

The affected Windows 11 Steam PC still starts flat in the latest 1.0.6 retest.
The injector verifies openxr_loader.dll, LuaVR.dll and UEVRBackend.dll; native
backend execution then repeatedly tries DX12 and DX11 without initializing the
renderer. An absent injector window is expected: the launcher hides it and its
configured auto-close runs after loading. This is not an absent injection.

The first DX12 callback has a valid GetHwnd result and is filtered before the
API retry. Old process-global diagnostic deduplication hides whether later
attempts receive or repeatedly reject the same window. DX11 callbacks have no
matching DX11 device. This does not establish the game's actual graphics API,
an AMD driver fault, a frame-generation cause, or the same FEnumProperty crash
previously reported by another tester.

## Repairs

- Reproduced and repaired the window-filter worker exiting on an empty queue.
  It now waits for new work until explicitly stopped.
- Reproduced and repaired the filter holding its mutex across a window-title
  query, allowing message reentry to deadlock rendering. Queries run outside
  the mutex with a bounded timeout. Unknown-window fallback and known helper
  exclusions remain; tested shutdown does not leave a detached worker.
- Each graphics-hook attempt logs bounded callback/filter/device counts, both
  HWND lookup results, and slot ownership at retirement. Identical rejection
  events in a later attempt are no longer hidden by a process-wide dedup cache.
- Normal 60-frame graphics warmup no longer becomes a misleading DX11 error.
  Actual initialization errors remain visible. Same-attempt backend shutdown
  ends waiting; uncertain process enumeration does not assert a game exit.
  Cancellation is checked before and during the settling interval.

The injector, simulator, accepted scene-rendering changes and graphics settings
are retained. No game launch, injection, production profile/runtime replacement
or remote-machine operation is part of the background checks.

## Verification and limits

Evidence is under `extracted/renderer-filter-20261006` and
`extracted/window-filter-release-20261006` on the development machine.

- Original production WindowFilter reproduces both worker and locking defects.
  Repaired source passes five isolated behavior/teardown cases.
- Production startup and dispatch paths pass 71 checks; 24 attempt-lifecycle
  checks and 28 Python recovery/diagnostic checks pass.
- Real DXGI PointerHook tests cover API replacement and callback retirement.
  The additional exact-hook fixture uses actual production hooks/filter and
  built utility libraries; only Framework's mutex is a stub. This does not
  execute the game's full Framework, OpenXR initialization or headset rendering.
- Native build, source reconstruction, package installation, app packaging and
  publication each retain separate receipts with exact identities.

These are reproduced bug repairs, not proof of the affected machine's root
cause. Successful startup on that PC and physical-headset acceptance remain
unverified. Source build strings inherited from the pinned upstream are not the
package identity; use the release hashes and source reconstruction receipt.

## One affected-PC check

Update the app to **1.0.7** when idle. In step 02 select and install
**beta-2026-10-06-window-filter**. Updating the app alone preserves the old
package selection. Keep the same graphics/runtime settings for comparison.

- [ ] One Steam launch reaches the simulator/headset's actual stereo view.
- [ ] If it stays flat, Stop waiting finishes and Copy diagnostics includes the
      per-attempt graphics counts. No second injector or reinstall is needed.

Older packages and saved backups remain available for rollback. No success on
the affected PC is claimed until the user reports that result.
