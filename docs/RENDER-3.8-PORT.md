# 3.8 rendering routes (12 November 2026)

Codex restored the HUD and menu on 3.8 (`docs/UI-3.8-PORT.md`). Five rendering
fixes still refused on 3.8: god rays, the mono authored camera, cinematic
native aspect metadata, the far-lighting one-eye rebuild detector and the
custom reflection capture projection. This note covers the first four. The
reflection capture stays refused (see the end).

Captures, file offset = RVA, both outside the repo:

- 3.7 hotfix `data/game-3.7-6ace6377/client-6ace6377-kuro.bin` (taken with mod hooks
  installed: the god-ray pass prologue is a jump there)
- 3.8 `data/game-3.8/client-6ae17e03-kuro.bin` (clean, by Codex)

Method: capstone disassembly, then instruction-shape alignment (difflib) of each
3.7 function against its 3.8 counterpart. Every difference other than a moved
call/jump target or RIP displacement is listed and reviewed.

## View layout

The FSceneView constructor (3.7 `0x54cbb20`, 3.8 `0x5274250`) writes the same set of
`[this+X]` fields for X in `0x2c0..0xda0` in both builds. That set includes `0x2d9`,
`0x2dc`, `0x3e0..0x41c`, `0x760..0x7e0`, `0xc20..0xd48`, the stereo pass at
`0xc90` and the FOV at `0xca4`.

The LOD distance factor `0xfd8` and its neighbours `0xfb0..0xfe8` are also
unchanged. New fields start at `0xff0`, so later fields move:

- `0xff4..0x1150` move by +0x10.
- `0x1df0..0x1e68` move by +0x40 (the render rect `0x1e70` becomes `0x1eb0`, as in Codex's table).

LOD sync (`0x2b8 0x2d0 0x2d4 0xca4 0xca8 0xfd8`) and camera-fade sync (`0xc70`,
`0xc80`) are therefore correct on 3.8 as shipped in Test 24.

## Ports (WuWaRender38.hpp, exact build 6ae17e03)

**Mono authored camera.** GetProjectionData `0x526f1a0` maps to `0x5014d50`.
- Both are `0xc7a` bytes and 728 instructions.
- The stack frame grew by 0x30 above the low locals:
  - `lea rbp,[rsp-0xc20]` became `-0xc50`.
  - `sub rsp` went from `0xd20` to `0xd50`.
  - The saved xmm slots and five high rbp locals moved.
- The rbp−rsp distance stays 0x100, so the locals the `+0x299` mid-hook reads (rbp
  `+0xd0`, `+0xe8`, `+0xfc`, `+0x100`, `+0x104`) are unchanged.
- The hook bytes are identical in both builds.

The FMinimalViewInfo builder `0x4eba980` maps to `0x4bc37d0`. Only another
object's `[rbx+0xa40..0xa48]` moved, by +0x30. Three helpers only moved.

**Cinematic native aspect metadata.** The CalcSceneView piece `0x52677f8` maps to
`0x500d368`.
- Its stack init struct moved from rbp+0x950 to rbp+0x980.
- The init fields `+0x289` and `+0x28c` are unchanged.
- The constructor and LGUI ranges only moved.
- The route's other gate, `wuwa_shadow::compatible()`, already passed on 3.8.

**God rays.** The pass at `0x37abe30` is the same function, with these changes:
- Renderer Views `0x108` became `0x110`.
- `0xca6c` became `0xcacc`.
- `0xc770` became `0xc7d0`.
- RHI vtable `+0x600` became `+0x618`, and `+0x1e8` became `+0x1f0`.

The fields the hook reads from Views[0] are unchanged. A second exact pattern
carries the Views offset. Across both patterns, exactly one hit is required.

**Far-lighting detector.** The view state's volume moved:
- 3.7 forms the address with `mov rsi,[rsi+0x1e88]; add rsi,0x27a8` at `0x3eaf36c`.
- 3.8 uses `mov rdi,[rdi+0x1ec8]; add rdi,0x27b8` at `0x3bf3f40`.

Inside the volume, the compared header fields moved (`+0x29`/`+0x2a`/`+0x2c` became
`+0x2c`/`+0x2d`/`+0x30`). The volume is still `0x2c8` bytes, and its destructor is
unchanged. The destructor does not touch the cascade texture slots, so `+0xd0` is not
proven by code.

The acceptance test is the October measurement repeated in the simulator: a game
CLV refresh must report a one-eye rebuild. As a guard, a slot that is not an
aligned pointer makes the signature unreadable, so the detector stays quiet.

## Not ported: custom reflection capture projection

Its producer and factory changed far beyond relocations (the port tool reports
5725 and 1095 differing bytes), and four mid-hooks depend on registers and stack
slots in those bodies. It stays refused on 3.8. Reflections behave as before that
fix.

## Verification

- `python dev/qa.py unit --cpp --only test_render38` checks:
  - each range against both captures;
  - corrupted bytes, a changed contract and another timestamp;
  - both far-lighting sites;
  - the god-ray patterns, read from the shipped header.
- Live proof is the simulator log:
  - `[WuWaStereoCode] route=Mono authored camera ... reviewed_3_8=5`
  - `Cinematic native aspect metadata ... reviewed_3_8=4`
  - `[WuWaGodRay] god-ray pass hooked`
  - `[WuWaCLV] one-eye rebuild detector: watching (volume at view state +27b8)`
