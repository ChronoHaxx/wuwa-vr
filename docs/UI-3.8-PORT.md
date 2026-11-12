# 3.8 UI compatibility repair (12 November 2026)

The Kuro 3.8 build has PE timestamp `6ae17e03` and image size `aa5a000`.
Test 23's self-port found 117/151 sites, but the HUD check passed only 20/30
ranges. Its refusal disabled HUD/menu extraction, producing the overlapping
Terminal UI reported by the owner.

The complete read-only capture is in the workspace at
`data/game-3.8/client-6ae17e03-kuro.bin` (178626560 bytes, no holes or read errors).
The reference is `data/game-3.7-6ace6377/client-6ace6377-kuro.bin`.
The port report and HUD/menu instruction diffs are beside the new capture.

## Reviewed scope

`WuWaLgui38.hpp` supplies exact hashes for the 30 HUD, 22 menu and 6 viewport
ranges on this timestamp only. The 3.7 reference and generic self-port matcher
remain unchanged. Unreviewed world-label/stage-0 checks still refuse on 3.8.
Other 3.8 rendering-route refusals are outside this UI repair.

The changed HUD and menu instruction sequences have the same instruction
counts and boundaries as their reference counterparts. Changes include stack
storage growth of 0x40, a uniform-buffer copy growing 0x1740 -> 0x1780,
view-field movement, and the texture release virtual call moving +0x710 -> +0x740.
The hook arguments and pass-constructor payload layout remain unchanged.
The five viewport conversion bodies and the pointer-ray prefix are unchanged
apart from relocated operands.

| Layout | 3.7 | 3.8 |
| --- | --- | --- |
| Renderer views / count | 108 / 110 | 110 / 118 |
| View stride | e940 | e9b0 |
| Early count array | 2164 | 21a4 |
| Dispatch count array | 922c | 928c |
| Depth-bound UI flag | 25ac | 25f4 |
| Render rectangle | 1e70 | 1eb0 |

The setup body at `3e70bc0` establishes the renderer layout and view stride;
its stage-2 counts move 216c -> 21ac and 986c -> 98cc. The arrays retain their
4-byte and 320-byte strides. The depth flag movement appears in both HUD and
menu setup bodies. The low view fields (family, constrained rect and aspect)
are unchanged. World-label predicate diagnostics deliberately report unavailable
on this build until those separate fields are reviewed.

## Manual corrections to the port tool

- HUD execution begins at `3e5a870`; the initial stack-frame changes defeated
  the automatic match. Its six fragments retain their old sizes.
- Menu execution begins at `3e59e00`. Its execute thunk is `3e65620`, which
  adds f0 to RCX and jumps to that body. The suggested `3e64da0` was a false
  match: it adds e8 and jumps to another body. The correct vtable begins at
  `7d3a3b8`, with the execute pointer at +8.
- Two menu fragments at `3e678d0` and `3e6796c` retain their instruction bytes
  apart from relocated operands.
- Pooled-texture AddRef is `4533520`, corroborated by the pooled vtable +38;
  its ambiguous byte match alone was insufficient.

## Verification

`python dev/qa.py unit --cpp --only test_lgui38` checks all 58 ranges against
the real capture, rejects corrupted code and changed source contracts, checks
unknown-build/read-failure refusal, and verifies the menu vtable and jump target.
Runtime, simulator screenshots and headset acceptance must be recorded separately.
