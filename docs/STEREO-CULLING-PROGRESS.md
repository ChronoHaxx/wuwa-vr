# Stereo foliage/prop mismatch: progress and evidence

Updated after the 29 Sep pass-split session (E1, E2). **No rendering fix exists yet.** At
distance the left-eye tree is static while the right eye's sways; nearer, both move; several
trees and locations are affected. **The frozen image is `views[1]`**, the family's second main
view, which WuWa constructs as its secondary eye pass (3) and UEVR renders into the
scene-capture target (section "Which pass is frozen"). **The stereo pass is ruled out**, on the
game thread (E1) and the render thread (E2), and `views[1]`'s view-uniform clock does not go
stale (fact 31). Still specific to `views[1]`: its own view state, which never receives the
extra per-frame render that `views[0]`'s state gets, and its capture target (section "What is
still specific to `views[1]`"). The next step, E3, is an opt-in, timed view-state swap that
separates those two. **Eye slots:** slot 1 is `views[1]`, shown in the left half with Swap Eyes
on (the correct setting); verified against the output by the swap check (fact 22).
Windows plan and commands: [EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md).

## Baseline and accepted work

- `main` `1291431` (merged PR #1). Pins: UEVR `4ee5c6b6`, UESDK `14478dfa`.
- **Accepted (user, real headset):** the end-of-ultimate camera desynchronisation is fixed.
  `WuWaStereoBasePose.hpp` and its call sites are untouched by this work.
- **Accepted (user):** `r.OneFrameThreadLag=0` removed the NSF stutter. Kept.
- Main Resonators reflection improved; weapon/lower-submenu offset is deferred. Water looked
  unusual in flat play too; no VR-specific water regression is established. Neither is in scope.
- Local build of PR #1: `UEVRBackend.dll` SHA-256 `583231192c…5f679`, MSVC zero warnings/errors.

## Confirmed facts

Facts 1-7 predate this update; 8-12 come from the 28 Sep far capture (30 s, stationary, far
tree failing; auxiliary buffers truncated). 13-21 come from the far/near pair taken with the
repaired DLL (`8bb228d9…`): two 30 s stationary lightweight traces at one tree, 11 complete
before/after pairs each. Labels come from the user's headset observation (far: left-eye tree
frozen; near: both sway), not from an automated motion measurement.

| # | Fact | Consequence |
|---|---|---|
| 1 | Same tree fails far, animates in both eyes near, fails again on retreat | Distance-dependent and history-shaped |
| 2 | Persists at zero IPD; 399 CPU eye-offset outputs coincide | Eye separation does not cause it |
| 3 | Main-eye projection scales equal (676 constructors / 1776 binder rows) | Not a projection-scale difference |
| 4 | Reversing submission order leaves the same physical eye frozen | Not "second submission loses"; points at state or inputs owned by the eye |
| 5 | Cached mesh commands, foliage LOD/culling/impostor CVars, shadow quality: no fix | Those switches, as tried, are not it |
| 6 | Earlier capture: one primitive picked a different vertex factory per eye far, same near | Some per-eye selection differs; not joined to the visible tree |
| 7 | Main views are only accepted when `+0xffe`/`+0x1000` are zero | Main views treated as non-instanced (a gate) |
| 8 | **Tooling:** PR #1 recorded 0 `eye_pair_diff` and 0 `before_submissions` pair rows; see below | The paired-eye question is still unanswered |
| 9 | CPU view time constants (`game_time`, `real_time`, `delta_time`, previous values) equal between eyes in all 147 same-frame main pairs | One eye's CPU view clock is not frozen (CPU stage only; GPU unproven) |
| 10 | Frame 6870 mesh bindings: 343 primitives in both eyes, 0 vertex-factory **type** differences; 31 differ only in VF **instance**, each unique to its eye with identical section counts and shaders; 20 / 31 primitives seen in only eye 0 / eye 1 | The 31 match per-view VF allocation, not per-eye LOD. Eye-only primitives are unexplained (no bounds recorded; frustum edge is the default reading). One frame only |
| 11 | All 7000 instanced-binder rows in both eyes have `lod_branch=false` | No per-instance LOD transition passed this binder in sampled frames; it is not where a steady per-eye LOD split would show |
| 12 | Eye 0 (**right**) has a second uniform production (236 throttled rows) from a different view object in eye 0's render family that carries **eye 0's view-state pointer**, eye 0's perspective projection and external matrices. Eye 1 has none | Identity unknown. Consistent with a view snapshot such as a shadow-depth view built from the first eye (NSF shares shadows); unverified. Any view that shares eye 0's `FSceneViewState` can write that eye's history. Present at far and near alike (227/223 rows in the pair), so not distance-linked |
| 13 | **Tooling works in the game:** 611/608 pairs, 11/11 scheduled pairs completed, 0 orphans, 0 lock misses and 80 rows in every phase. Before-submission family frame = `4294967295` in every sample | The repair's premise is measured: the frame is unassigned (`UINT_MAX`) before the first submission |
| 14 | Slot 0 is the **right** eye, slot 1 the **left**: eye1 sits 6.4 units along −right of eye0 (view matrices and `+0x6ec` agree to 1e-5), and eye0's projection is off-centre right (M[2][0] = −0.245) while eye1's is off-centre left (+0.245) | Now verified against the output (fact 22): slot 1 is `views[1]`, the frozen left image with Swap Eyes on |
| 15 | Every state sample is truncated (about 2,430 of 4,096 dwords differ; 1,536 kept). Before-snapshots are complete up to `+0x31dc`; `+0x31dc..+0x4000` is unknown in **all** samples. From about `+0x2800` both sides are constant and nearly all different | Nothing can be said above `+0x31dc`. The objects' sizes are unknown: readable memory does not show where either object ends |
| 16 | Of 1,540 differing state offsets below the horizon, 1,513 differ in every sample in both conditions. Every per-eye position stored in the states that differs between the eyes differs by exactly **6.4** (IPD) at far and near; one scaled copy by 0.64 | No stale per-eye origin (temporal-LOD, previous-view or similar) in any **differing** position field at far. Positions stored identically in both eyes, or above the horizon, are not covered |
| 17 | Slot 1's state window holds 693 dwords whose bytes read as the game's script text, unchanged across both captures (slot 0's: 13). They sit between dwords that match slot 0's; slot 0's values there are also unchanged | Origin unknown. They inflate raw difference counts; they show neither object extent nor what any constructor writes. 29 Sep raw rows: a solid run at `+0x1800..+0x1e00`, **below** members both states share (fact 25), so text cannot mark where the state ends |
| 18 | 28 Sep: `+0x550` = 2 (slot 0) vs 0 (slot 1) in all 22 far snapshots, equal at near. Around it (`+0x528..+0x558`) slot 0 holds pointer-like values and small counts (3, 3, 1); slot 1 holds zero there in both conditions | **Did not reproduce** (fact 23): not a distance marker. The slot-0-only block around it is a standing asymmetry between the two states (fact 27). Nothing writes to it |
| 19 | Values 25–27 at `+0x0d7c`, `+0x208c` (and `+0x20dc`) differ between the eyes by ±1 and change within captures | Corrected by the raw rows: `+0x0d7c` and `+0x208c` are the **high halves of heap pointers** at `+0x0d78` and `+0x2088` (e.g. `0x19_cac13340` vs `0x1a_682bccd0`), not counters. Slot 0's `+0x0d78` reads 0 after the submissions (its render work finished first). No condition link |
| 20 | View region: nothing differs only at far. A 156-dword block (right eye data, left eye zeros) switches on and off per sample (3/11 far, 9/11 near) and never repeats a value. The other near-only view differences are float rounding (1 to about 50 ulp) of coordinates both eyes share; near-only state `+0x298/+0x6a8/+0x72c` are 1-ulp | Per-sample switching and float rounding; no view input that follows distance |
| 21 | Instanced binder: no `lod_branch` rows at far or near. CPU view time constants equal per eye in all same-frame pairs (123 far, 84 near) | Same as facts 9 and 11, now at both positions |
| 22 | **29 Sep, Swap Eyes check (user, headset):** captures ran with Swap Eyes on (correct stereo), freeze in the left eye. Swap Eyes off: freeze moves to the right eye and the stereo inverts. In code, Swap Eyes only chooses which half gets the game texture (`views[0]`) and which the scene-capture texture (`views[1]`), `D3D12Component.cpp:210-216`; on puts the capture texture left | **The frozen image is `views[1]`** (slot 1), whatever half it is shown in. Both submission orders render `views[1]` into the capture target, so with fact 4 the freeze stays with that view, its state or its target, not with order |
| 23 | 29 Sep raw rows: `+0x550` = 3 (slot 0) and 0 (slot 1), `+0x554` = 1/0, in every snapshot at far **and** near | The 28 Sep far-only reading (fact 18) is not a distance marker. Rejected |
| 24 | `+0x0e20/+0x0e24` (the 29 Sep far-only difference in the diff stream) is one 64-bit heap pointer; the same address `0x3_faaa2200` sits in slot 1 at far and slot 0 at near (local finding, confirmed from the raw rows) | Not evidence. Rejected |
| 25 | **State layout markers (raw rows, all 8 snapshots of both captures):** both states, allocated separately (`0x383a0aac0`, `0x38972d560`, stable across captures), hold the same game-image pointers at `+0x0` (`0x1677a0b40`), `+0x20`, `+0x1d8`, and `0x16755f188` at **`+0x2058` and `+0x2080`**: one class, a 0x28-byte member repeated (pointer, two heap pointers, an int32) | The shared layout runs to at least **`+0x20a8`**. The summarizer now stops the state compare there (`STATE_EXTENT`, `--layout`). The true `sizeof` is still unknown (how to read it: section "Which pass is frozen") |
| 26 | The 29 Sep far-only `+0x2070/+0x2098` (`0xbe0` vs `0xbd0`) is that member's int32, once per instance; the text next to it (`+0x2074`, `+0x209c`) is the high half of the same 8-byte slot, i.e. padding. 28 Sep: the same field differs at far (`0x640`/`0x470`) **and at near** (`0xbb0`/`0xbe0`) | Inside the shared layout, not heap past the struct. A size-like per-eye value (all multiples of 16) that differs in working and failing captures: not a freeze marker. Rejected |
| 27 | Slot 0's state is used by the extra uniform production of fact 12 in all five traces since 28 Sep (236; 227/223; 241/241 rows); slot 1 has none. Slot 0 alone holds three heap pointers at `+0x528..+0x53c` and 3/1 at `+0x550/+0x554`, where slot 1 holds zeros, in both sessions; `+0x540..+0x54c` (a pointer, then 4, 4) is populated in both | Standing asymmetries between the primary and secondary states, present at far and near. Not distance-linked by themselves; candidates for what the secondary view never receives (H3 below) |
| 28 | Conditions (`--conditions`) of all five traces since 28 Sep: Same Pass on, Early stereo view setup **off** (`early_applied` 0), Swap Eyes on. The four far/near traces read slot passes 2/3 in all 76–80 pair rows of every phase, including before the submissions | `views[1]` is secondary (pass 3) from construction until its own submission. Before 29 Sep afternoon, neither the early-setup nor the full-view switch had been tried on this issue (now facts 29, 30) |
| 29 | **E1, far (29 Sep afternoon):** Early stereo view setup on. The trace shows it took effect: slot passes 2/2 in all 72 pair rows of every phase, `early_applied` 4066 → 4675 over the trace, Same Pass `applied` flat at 20940. Headset: the left-eye far tree **stayed frozen**; no other eye difference noticed | Making `views[1]` primary from construction on changes nothing visible. **Rejected:** primary-only work on the game thread (and anything else keyed on the pass after construction) |
| 30 | **E2, far:** `full-views`. Both views rendered as full views (pass 0) in both submissions: `full_applied` = `full_restored` = 124, cleanly restored. Headset: frozen for the whole window. The command ends the override once its check capture is done, so the window was about 7–8 s (124 pairs at the ~15 fps these traces show), not 45 s as the handoff said | **Rejected:** render-thread logic keyed on "stereo eye pass" versus full view. With fact 29, the pass value is ruled out on both threads |
| 31 | **View uniforms (far-e0, far-e1; 114 consecutive frames each, `view-ub.jsonl`).** At the own-update return, `views[1]`'s final payload has the same game time, real time and delta time as `views[0]`'s in every frame. Both advance every frame, "previous time" equals the last frame's time, and each state's frame index steps by exactly 1 (slot 1's is a constant 73 behind slot 0's in both traces). Family frame and origin shift are equal; planar-reflection and force-velocity flags are 0 for both. The shared view-UB cache makes the same skip/update decisions for both eyes | `views[1]`'s view clock is not stale at far. The payload fields read here contain no wind parameter; wind data in stock UE is per component, not per view. Not measured: per-primitive or wind buffers on the GPU |
| 32 | **The extra "external" production (fact 12) in far-e0/e1:** slot 0 only (225, 235 rows), also with both views at pass 2 (E1). One per frame, on the same render thread, always inside slot 0's render family: the two eyes render in different families in 148/148 and 91/91 frames, and it never shares slot 1's. It usually comes between slot 0's and slot 1's main uniforms. It carries slot 0's view state and fallback origin, a perspective projection close to eye 0's (M[2][0] −0.2455 vs −0.2448) and a **different camera** (another view direction, with a translation). Its matrices come from one persistent object at a fixed address for the whole session | Decided by something other than the pass: the first submission, the player's primary view state, or the first view on the game thread. What it renders is unknown (guess: an auxiliary capture or pass whose output stays with state A). `views[1]`'s state B never gets it |
| 33 | UEVR changes only the family's render-target pointer, view count and view order between the two submissions (`FFakeStereoRenderingHook.cpp`). No family or view flag is set for the scene-capture submission. During each submission the family holds one view, at index 0 | Render-thread "first view / view index" logic cannot single out `views[1]`. Game-thread logic keyed on `Views[0]` can: `views[1]` is index 1 there, and E1 did not change that. The UESDK setter itself (pinned outside this repo) was not read |
| 34 | **E3 and share modes, game 3.7 (30 Sep, headset, far tree, fix bench).** Exchange: 1,861 pairs swapped and restored, 0 skipped; the left-eye far tree stayed frozen and the right swayed. Both views on the right eye's state, and both on the left eye's: the left far tree still froze; the whole image ghosted and shimmered (shared temporal history), not fixable by UEVR's ghosting option | **Rejected: H3, the view state.** The freeze does not travel with the state object and is not cured by giving `views[1]` the working eye's state. Remaining: the scene-capture target, or game-thread logic keyed on `Views[0]` (E4) |
| 35 | **E4 render-target swap, 3.7 (30 Sep, simulator window measured automatically, `wuwa_motion_halves.py`).** Central far-tree canopy, share of pixels that changed over 6 s. Normal: `views[1]` 22%, `views[0]` 75%. Targets swapped (clean run, floor 0%): `views[0]` in the capture target 73%, `views[1]` in the game target **14%** | **Rejected: the scene-capture target.** The freeze follows `views[1]` into the game's own target |
| 36 | **K1-K3 construction, 3.7 (automated bench, ~510 constructions per window).** K1: secondary view constructed with WuWa's primary pass 2; K2: pass 2 with the family's views hidden from the constructor; K3: views hidden only. Clean reruns: `views[1]` canopy stays static in every mode | **Rejected: constructor-time pass and the constructor's multi-view logic** (the value Indath tried, upstream 1, is not a WuWa pass; 2 is) |
| 37 | **E6 eye pose/projection swap, 3.7.** Each pass given the other eye's offset and projection (framing visibly flips). `views[1]` with the right eye's pose: **16%**; `views[0]` with the left eye's pose: 68%. UEVR's Horizontal Projection Override did not change WuWa's framing at all | **Rejected: the left eye's pose and projection.** What remains is outside everything UEVR sets per view: game-side handling of its second eye pass before or outside the view object, or per-eye cached data keyed on that pass |
| 38 | **K4 eye index, 3.7.** The second view carries a per-eye index (init `+0x114` -> view `+0x2ec`, 0 / 1). Constructed as 0 (verified in raw snapshots): the left tree stays frozen | **Rejected: the per-eye index** |
| 39 | **Root cause, 3.7 (1 Oct).** The user saw that further away both eyes freeze: a mid-distance band where only the left eye has already switched. A full field diff of the two views (constant differences included) shows views[0] with the game camera's FOV 75.27 (`+0x2d0/+0x2d4`, copies `+0xca4/+0xca8`) and LOD distance factor 0.836 (`+0xfd8`, copy `+0x2b8`, = FOV/90), and views[1] with the default FOV 90 and factor 1.0. The larger factor makes far objects count as further away, so the second eye drops to cheaper, non-animated LODs sooner | **Cause found.** Fix: before both submissions, copy the first view's LOD factor and FOV fields into the second (`wuwa_shadow::lod_sync`, setting `WuWaStereo_SyncEyeLod`, on by default). Automated measurement at the mismatch distance: left-eye canopy motion 2% -> 67%, matching the right eye's 67%; user confirmed in the simulator |

## Rejected hypotheses and readings

| Rejected | Evidence |
|---|---|
| Eye separation, projection scale, submission order, instanced main views, the tried CVars | Facts 2, 3, 4, 7, 5 |
| "PR #1's empty trace says something about rendering" | It is a tooling failure with a known mechanism (below) |
| "The 60-frame cadence was merely too sparse" | Phase 1 never saw an assigned frame, so no cadence could open; and phase 2 lost the lock (below) |
| "31 per-eye VF instances at far = per-eye LOD" | Fact 10: unique per eye, same sections and shaders: per-view allocation signature |
| "One eye's wind clock is frozen in its CPU view constants" | Facts 9, 21 |
| "One eye's stored view origin is stale at far" (for every position field that differs between the eyes) | Fact 16 |
| "The view objects differ in a distance-linked way at hand-off" (H2, for `+0..+0x1e40`) | Fact 20 |
| "Many more state dwords differ, so the states diverge" | Facts 15 and 17: much of the count is text-like or constant content of unknown origin, not state that tracks distance |
| "The right eye's extra auxiliary view explains the far-only split" | Fact 12: present at far and near alike |
| "`+0x550` marks the distance" (reading A or B) | Fact 23: 3/0 at far and near on 29 Sep |
| "`+0x0e20`, `+0x2070/+0x2098` differ only while failing" | Facts 24, 26: a moving heap pointer; a size field that also differs in a working capture |
| "Text bytes show where the view state ends (about `+0x2070`)" | Facts 17, 25, 26: text sits inside the shared layout and in member padding |
| "Second submission / once-per-frame work skips the frozen eye" | Facts 4 and 22: the freeze stays with `views[1]` in both orders |
| "Primary-only (or any pass-keyed) work on the game thread skips `views[1]`" (H1 of the previous round) | Fact 29 (E1) |
| "Render-thread logic keyed on the stereo eye pass" (H2 of the previous round) | Fact 30 (E2; a 7–8 s window) |
| "`views[1]`'s view-uniform time goes stale at far" | Fact 31 |
| "The extra production follows the primary pass" | Fact 32: still slot 0 only with both views at pass 2 |

**Not rejected:** see "Hypotheses" below. LOD/culling/impostor selection or a per-view WPO gate
remain possible as the visible outcome of any of them.

## Which pass is frozen, and what it could skip (29 Sep)

**Proven (code plus the captures above):**
- The frozen image is **`views[1]`** (fact 22). `views[1]` is:
  - the family's second main view, constructed by the game with **stereo pass 3**. The game's
    predicate (RVA `0x24bbf2d0`, `test [view+0xc90],~2; sete`) treats 0 and 2 as primary, so
    `views[1]` is WuWa's secondary eye;
  - rendered by UEVR into the **scene-capture target** in both submission orders;
  - the owner of the second `FSceneViewState` (`0x38972d560`), separate from `views[0]`'s.
- **Pass timeline, one frame (Same Pass on, Early off):**

  | Stage | `views[1]` pass |
  |---|---|
  | Construction, game-thread view setup, first submission | 3 (pair rows 2/3 before the submissions and after the first one) |
  | Its own `BeginRenderingViewFamily` call (Same Pass `PassScope`, 6234 applications in the far trace) | 2 |
  | After that call (restored) | 3 |

- **Same Pass promotes `views[1]` around its own `BeginRenderingViewFamily` call** (counters
  above), and the freeze persists with it on. Inferred, not verified in WuWa's binary: stock UE
  copies the view for the renderer inside that call, and UEVR's count and target swaps rely on
  the same. If so, a render-thread "primary view only" decision already includes `views[1]`,
  unless its result was cached earlier in the frame.
- **The view object** (`+0..+0x1e40`) shows no far-only difference in either session. **The
  view state** below `+0x20a8` shows none that repeats across sessions (facts 20, 23, 24, 26).
- **Standing asymmetries** (both conditions, both sessions): only slot 0's state receives the
  extra per-frame uniform production, and only slot 0's state holds the pointers and 3/1 values
  in `+0x528..+0x558` (fact 27).

**Per-view updates `views[1]` could skip at distance** (guessed unless marked; status updated after E1/E2):

| Candidate | Status for `views[1]` |
|---|---|
| **Primary-only work on the game thread** between construction and submission: per-view LOD, significance, HLOD/impostor or foliage decisions, registrations, the extra production | **Rejected by E1** (fact 29): primary from construction on changed nothing. Work keyed on `Views[0]` rather than on the pass remains (fact 33) |
| Primary-only decision on the render thread | Weak: Same Pass makes `views[1]` primary for its submission (proven). Only a result cached before that call would escape |
| Stereo-eye logic independent of primary/secondary (pass ≠ 0) | **Rejected by E2** (fact 30) |
| Per-view-state content the secondary state never receives (fact 27), or its history (temporal LOD, occlusion, fading) | **Open, now first** (section below). Nothing in the compared state tracks the freeze (proven for the fields read, below `+0x20a8`), but slot 0-only content and the slot 0-only extra render exist. Test: E3 (implemented) |
| WPO/wind | The CPU view clock is equal per eye (facts 9, 21), and so is the final view-uniform payload's clock, advancing every frame (fact 31). WPO reads view uniforms and per-primitive wind data (one per component). A per-eye split needs a per-view input from the view state. GPU side unmeasured |
| Animation update rate (URO, visibility-based ticks) | Unlikely: game-thread, per component, one pose per frame for both views in stock UE. Would need a Kuro per-view animation path (none known) |
| Cached mesh draw commands | Weak: cached per scene and pass, shared by both views; view data comes through the view uniform buffer. Fact 5: disabling via CVar did not fix it |
| The scene-capture target itself | Open, second: rects and projections match (facts 3, 20), and no flag is set (fact 33). E3 separates it from the state; E4 (proposed) isolates it |
| Constructor-time pass logic | Not covered by any existing switch. Constructor inputs are left untouched since changing the **first** view's constructor pass (2→1) crashed the game (RVA `0x24ac191c`, 24 Sep). Changing `views[1]`'s (3→2) is untried and riskier than E1–E4 |

**View-state size.** The compare stops at `+0x20a8`, a lower bound (fact 25). The earlier
reading of about `+0x2070` came from text bytes, which fact 26 shows sit in member padding. To
read the real `sizeof` from the game binary:
- follow the class pointer at state `+0x0` (`0x1677a0b40`) to its first virtual function. In
  stock UE the base declares its destructor first, so this should be the deleting destructor
  (unverified for Kuro's build). The `mov edx, <size>` before its `operator delete` call is the
  size;
- or find the `new` that allocates the state (`AllocateViewState`) and read its size.

Pass the result as `--state-extent`. Until then, anything above `+0x20a8` stays uncompared.

## What is still specific to `views[1]` (after E1 and E2)

Ranked. "Proven" means shown by code or by the traces above; everything else is a guess.

| # | Factor | Proven | Guessed |
|---|---|---|---|
| 1 | **Its own view state B** (history, caches, per-state registrations) | Tied to `views[1]` in both orders and at every pass (facts 4, 22, 29, 30). Only state A gets the extra per-frame render (facts 27, 32), and only A holds the `+0x528..+0x558` data. Clocks are equal (fact 31) | Far content in B comes from something refreshed only for A (for example the extra render's output kept per state), or B holds history that pins far objects |
| 2 | **Its target: the scene-capture render target** | `views[1]` always renders into it, in both orders (fact 22). UEVR sets no family or view flag for it (fact 33) | Engine or Kuro code that treats a render-target texture differently from the viewport's target |
| 3 | **Game-thread logic keyed on the first view (`Views[0]`)** | `views[1]` is index 1 there; E1 changed only the pass (facts 29, 33) | The extra production, or per-view LOD/culling state, built from `Views[0]` only |
| 4 | **The extra production itself** | Slot 0 only, inside the first submission, not pass-decided (fact 32) | The mechanism behind rows 1 or 3, not a separate cause |
| 5 | Constructor-time pass logic | Untouched by E1/E2 | Low: every later pass-keyed effect is already ruled out |

Rows 1 and 2 separate cleanly by moving only the state. That is E3 (below). Rows 3 and 4 show up
in E3's trace as a side result: which slot the extra production is tagged with while the states
are exchanged.

## Why PR #1 collected nothing, and the repair

The NSF path calls `pair()` three times per game frame: phase 1 before the first submission
(issues a sequence token), phase 3 after the first submission, phase 2 after both. Two gates
failed independently:

1. **Phase-1 frame gate.** Sampling and phase-1 rows required the family frame to be on an
   interval (`% 60 == 0`, `% 30 < 4`). At phase 1 that frame is not yet the one the renderer
   assigns. Evidence: 453 consecutive validated pairs (sequence 19→472 exactly tracks frames
   6870→7323), every on-interval pair produced its phase-3 row (64/64), yet no phase-1 row
   passed the same filter for the same sequences. The phase-1 value itself was never recorded;
   stock UE initialises `FrameNumber` to `UINT_MAX` (`% 60 = 15`) and assigns it in
   `BeginRenderingViewFamily`, which would explain it (guess until the new trace shows it). So
   `Sampler::take` never scheduled a pair. The unit tests passed because they modelled phase 1
   with an assigned frame.
2. **Phase-2 lock.** Phase 2 took the callback lock with `TryAcquireSRWLockExclusive` just after
   both submissions, while render-thread callbacks hold it shared almost continuously. Only 2 of
   64 on-interval pairs produced a phase-2 row. The aggregate `lock_misses` (478) matches about
   one miss per pair, but it was not split per site, so this is strongly supported, not proven.

**Repair (read-only, no new hook, no rendering or game-memory change):**
- Schedule by the probe's own validated pair sequence (`(sequence-1) % 60 == 0`), never by
  frame. Invalid early snapshots issue no sequence; frame strides cannot alias it away. A pair
  starts only if both snapshots fit the ring; a lost phase 2 is counted as an orphan.
- Only phase 1 writes shared state (the eye states the callbacks match), so only phase 1 takes
  the lock exclusively. Phases 3 and 2 take it shared and **verify** the published states
  instead of rewriting them. A small guard serialises the sampler between `pair()` callers.
- The phase-1 pair row is held and emitted with the first later snapshot of the same sequence
  whose assigned frame is on the row interval.
- Status adds per-phase `calls / rows / lock_misses / invalid` and sampler
  `before_seen / before_taken / after_taken / orphaned / ring_full`. Samples record
  `frames_read_at`. The summarizer prints these from `lod-inputs.json` and names an old DLL.
- Unchanged: memory bounds (128 samples, about 3 MB after a trace is requested), guarded reads,
  no file I/O on the game or render thread, read windows and capacities.

## Raw snapshots (opt-in)

`lod_probe` request field `raw_snapshots` (`wuwa-test.py lod-inputs --raw-snapshots`). For every
5th eye-diff pair, 3 pairs per trace at most (about 0, 15 and 30 s into a 30 s trace at the last
capture's rate), both slots' view (`+0..+0x1e40`) and view-state (`+0..+0x4000`) windows are
copied in full, before and after the submissions: every dword, equal ones included, with a
read-validity bit. Rows are `type: eye_pair_raw` in `lod.jsonl` with sequence, phase, frames,
object addresses, `readable_dwords`, `unreadable` byte ranges and `bytes_hex` (memory order).
Six fixed ring slots (about 300 KB, allocated with the probe); filled in place by `pair()` on
the game thread under its guard with the same guarded reads; written only by the control thread.
No hook, no game-memory write. The rows say `physical_eye: unverified`, written before fact 22.
Slot 1 is `views[1]`, and the summarizer prints which half shows it. The windows are fixed spans
(`object_extent: unknown`). The summarizer reads the state only below `STATE_EXTENT` (`+0x20a8`,
fact 25) unless `--state-extent` is given. Status adds `raw_snapshots_supported` and a
`raw_snapshots` counter block.

## Tests actually run

| Check | Result |
|---|---|
| `check-eye-diff-json.py`: compiles the **verbatim** `pair()`, `struct Record` and serializers with instrumented SRW lock/clock/reader and replays the real call order in 7 scenarios (unassigned phase-1 frame + render lock held at phase 2 + 5 invalid early snapshots; stride 2 from an odd frame; lost phase 2; lock held at phase 1; state changed mid-pair; ring capacity; fixture) | pass with this branch; **fails with merged PR #1** (0 samples, 0 phase-1 and 0 phase-2 rows), reproducing the field result |
| Six planted bugs (exclusive lock back at phase 2, schedule off by one, deferred row dropped, orphans uncounted, per-phase lock misses uncounted, state verification removed) | all caught |
| `test-eye-diff.cpp` (policy, 15 cases) | pass: g++ 13 with ASan+UBSan, clang 18 |
| Lifecycle harness under g++ ASan+UBSan; fixture byte-identical from g++ and clang | pass |
| Whole `WuWaLodProbe.hpp` with stub Windows/safetyhook/spdlog headers, `g++ -fsyntax-only` | no new diagnostic (one pre-existing sign-compare warning); a planted typo in the new status code is reported |
| Python: summarizer (33: truncation horizon, unknown-is-not-equal in `--compare`, unchanged/text tags, provisional eye sides, `--raw` reader; **new:** state extent in `--compare`/`--raw`, truncation above the extent is not a gap, `--layout` markers and stride bound, `--conditions` settings/passes/producers/display halves) and candidate registration (7) | pass; 8 planted summarizer bugs (no extent skip, no horizon cap, raw not limited, stride ignored, truncation miscounted, one-slot markers, swapped display halves, `--state-extent` ignored) are all caught |
| Raw snapshots through the verbatim `pair()`: opt-in (0 rows unless requested), every 5th eye-diff pair, 3-pair budget, before/after of one sequence, equal value kept in both slots, unreadable tail reported as a range and zeroed; `test-eye-diff.cpp` block/schedule/ring cases (19 cases total) | pass; 6 planted raw bugs (opt-in ignored, cadence, slots swapped, range off by one, stale words, missing validity) are caught |
| Reconstruction patch: only the two header sections changed (97 sections); all 65 text new-file sections apply to an empty tree and equal the overlays | pass |
| **E3 state swap.** `test-state-swap.cpp`, 6 cases under g++ ASan+UBSan and clang: swap and restore; refuse without writing (null, same, overlapping, unreadable, shared state); roll back a failed second write; a write that does not stick; never overwrite a foreign value; no write when already restored | pass; 4 planted bugs (no read-back, restore clobbers a foreign value, shared state allowed, no rollback) are caught |
| Lifecycle harness `state_swap_window`: the verbatim `pair()` with the states exchanged before phase 1 and restored after phase 2 | pass (no refusals, every scheduled pair completed, swapped identities recorded); with the swap moved after phase 1 it fails 4 expectations (150 refused pairs per later phase, 3 orphans), so the placement is required. Merged PR #1 sources now fail 32 |
| `WuWaShadowPass.hpp` (scope, helpers, status) and `WuWaTestControl.hpp` (op, conflict checks) with stub headers, `g++ -std=c++23 -fsyntax-only`, working tree versus `HEAD` | no new diagnostic; a planted typo in the new op is reported |
| `wuwa-test.py lod-inputs --state-swap` and `state-swap` against a fake backend (`test_state_swap_runner.py`, 7): window opened before the trace and closed after it, restore verified, closed on failure, refused on an old backend or while a window is open, untouched without the flag | pass; 4 planted runner bugs are caught |
| Python totals | summarizer 34, registration 7, runner 7: pass; pyflakes clean |
| Reconstruction patch: `WuWaStateSwap.hpp` added; `WuWaShadowPass.hpp`, `WuWaTestControl.hpp` and `FFakeStereoRenderingHook.cpp` regenerated (99 sections). The upstream original of the latter was rebuilt by reverse-applying its old section (blob `148f143` matches) | all 67 text new-file sections apply to an empty tree and equal the overlays; the FFake section turns upstream into the new file exactly |

**Local, user (Windows):** MSVC build `8bb228d9…` with zero errors; far and near traces in the game
with the lifecycle counters in fact 13. Not run anywhere: overhead measurement, PowerShell
launcher actions in cloud. Cloud checks are g++/clang only.

## Hypotheses (ranked; all untested on WuWa)

The pass hypotheses (H1: game-thread, H2: render-thread) were rejected by E1 and E2 (facts 29,
30).

1. **H3: The view state carries it.** State B lacks something state A gets every frame (facts
   27, 32), or holds history that pins far objects. Test: E3.
2. **H4: The capture target**, or game-thread logic keyed on `Views[0]` (fact 33). Test: E3
   separates H3 from these; E4 then separates the target from the index.

## Next experiments, in order

E1 and E2 are done (facts 29, 30), both negative. Commands for E3 and the result table:
[EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md).

1. **E3: view-state swap (implemented; approved as opt-in, timed and auto-restoring).**
   - **Switch:** WuWa test control op `state_swap` (0..60 s; 0 ends it), driven by
     `wuwa-test.py lod-inputs --state-swap` (trace) or `wuwa-test.py state-swap --seconds N` (opens
     the window and exits, so Launcher.exe can record it). No UI, no profile setting; off unless
     requested.
   - **What it does:** for each verified main pair, the two views exchange their view-state
     pointers (`view+0x8`, constructor-proven) before the LOD probe's first snapshot and the
     first submission. Both are restored when the NSF function returns. Each eye renders with
     the other eye's state object. Nothing is copied between the states and nothing is shared.
     This relies on the renderer copying each view inside its submission call, the same
     inference Same Pass relies on. The trace proves the swap on the probe side; only the
     headset shows the render side.
   - **Guards:** the verified build and pair, both views in the family, first pass 2 and second
     3 or 2. It never runs with another pass or order test. A failed write, read-back or
     restore disables all writes in the module. Restore never overwrites a value it did not
     write.
   - **Cost:** at the start and end of the window each state gets one frame of the other eye
     (a brief TAA or occlusion hiccup).
   - **Readout:**

     | Headset, far | Extra production tagged | Reading |
     |---|---|---|
     | **Right** eye freezes, left sways | slot 1 (stays with state A) | H3: the state carries it, and the extra render follows state A |
     | Right eye freezes | slot 0 | H3 through history; the extra render follows the first submission |
     | Left stays frozen | either | Not the state: H4 (target, or `Views[0]` on the game thread). Next: E4 |

     The trace proves the swap happened: `--conditions` shows slot 0 carrying the state slot
     1 carries without it ("states exchanged"), with applied = restored.
2. **E4, proposed:** render `views[1]` to the game target and `views[0]` to the capture target,
   with the display halves flipped. It separates the target from `Views[0]`-keyed logic. It is
   more invasive (it changes the NSF target swap, which UEVR notes is performance-sensitive),
   so only if E3 says "not the state".
3. **Fix stage:** only after E3/E4 point at one mechanism, and accepted only by a headset check
   at the same far and near positions. A swap is a diagnostic, not a fix: it moves the freeze,
   it cannot remove it.

Not repeated: CVar switches from fact 5; copying view state between eyes; removing parallax.
Evidence and user recordings stay local; raw captures are not committed.

### How to read a far/near compare now

`wuwa_eye_diff_summary.py --compare far near` compares the state only below the state extent
(`+0x20a8`) and the lowest known offset (`compared up to +0x…`), and counts what lies above.
It tags values that are `UNCHANGED` in every sample and script `TEXT`, and prints which display
half shows which slot. A difference is a lead only if it is:
- (a) below both limits;
- (b) not text, not a pointer's high half, not 1-ulp rounding and not a per-sample flicker;
- (c) consistent in every snapshot of one condition and absent in the other;
- (d) repeated in another session.

28 Sep `+0x550` and 29 Sep `+0x0e24` and `+0x2070/+0x2098` pass (a)–(c) and fail (d).
