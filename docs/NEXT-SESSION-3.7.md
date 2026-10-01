# After the 3.7 update: port, then the fix bench (30 Sep)

Game patch 3.7 changes `Client-Win64-Shipping.exe`. Every WuWa-specific feature (Same Pass,
the state swap, LGUI HUD routes, reflection and CVar helpers, the LOD probe) is pinned to the
old build (PE timestamp `0x6a74963e` plus code checks and function hashes at fixed RVAs), so on
3.7 those features refuse and switch themselves off. Base UEVR stereo should still start. The
exe on disk is packed, so the port needs a capture of the unpacked image from the running game.

## Build for the first launch: `fix-bench-20260930`

Same as `state-swap-20260929`, plus:

- `WuWaModuleCapture.hpp`: `"any_build": true` in the capture request allows a capture of a
  build other than the pinned one. Read-only; the identity is recorded in the metadata.
- Fix bench: `StateSwapTransaction` modes `exchange` (E3), `first_for_both` (the second view
  renders with the first view's state) and `second_for_both` (the reverse). The share modes
  write one field and restore it after the pair, with the same read-back, rollback and
  fault latch as E3. Tests: `test-state-swap.cmd`, 8 cases.
- UEVR menu, WuWa experiments: "Stereo freeze fix bench (60-second windows)". Three mode
  buttons, Stop, status, and verdict buttons that log `[WuWaBench] verdict=...` with the mode.
- `wuwa-test.py state-swap --mode ...` and `lod-inputs --state-swap --state-swap-mode ...`.

## Session order

1. Before launch: write the capture request beside the backend in the build's runtime folder:
   `{"output": "E:/Coding/wuwa-vr/extracted/lgui-live/client-3-7-<date>.bin", "any_build": true}`
   as `wuwa-module-capture.request.json`. Remove it after the capture.
2. Launch `fix-bench-20260930` on 3.7 and get into the world. The log should show the WuWa
   code checks refusing (expected) and the capture finishing.
3. Port: `python launcher\dev\wuwa_port_offsets.py --old extracted\lgui-live\client-inprocess-v3-20260908.bin --new extracted\lgui-live\client-3-7-<date>.bin --report extracted\port-3-7.json`.
   Review every non-`ok` site and every `review: true` site, then rerun with `--apply`.
   Function ranges whose code changed beyond relocated displacements stay `changed`: their
   route stays refused until reviewed by hand.
4. Rebuild, register (`wuwa_register_candidate.py`), relaunch, confirm the checks pass
   (`[WuWaShadow] six constructor/predicate code checks matched`).
5. Far tree: E3 (`Swap the two eye states`), then `Both eyes use the right eye's state`,
   then `Both eyes use the left eye's state`. Press a verdict after each.

| Bench result | Reading | Next |
|---|---|---|
| Swap: right freezes; right's state for both: both sway | State B (views[1]) lacks per-state far data | Narrow which part of state A is needed (borrow ranges), then a permanent fix |
| Swap: left stays frozen; sharing changes nothing | Not the state: target or `Views[0]`-keyed game-thread logic | E4 (target flip) |
| Sharing sways but shows ghosting or flicker | Right direction, too blunt | Borrow only the needed members |

## Port script self-test

`wuwa_port_offsets.py --old <v3 capture> --self-test` maps the 8 Sep image onto itself: 234 of
234 sites (188 RVAs, 38 function ranges, timestamps and image sizes) come back unchanged in
about 85 s. That proves the mechanics, not robustness to a real build change; the 3.7 capture
is the first real test.
