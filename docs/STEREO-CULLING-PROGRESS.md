# Stereo foliage/prop mismatch: progress and evidence

Updated after the 29 Sep raw-snapshot far/near pair and the Swap Eyes check. **No rendering fix
exists yet.** At distance the left-eye tree is static while the right eye's sways; nearer, both
move; several trees and locations are affected. **The frozen image is `views[1]`**, the family's
second main view, which WuWa constructs as its secondary eye pass (3) and UEVR renders into the
scene-capture target (section "Which pass is frozen"). Nothing in either view state tracks the
freeze across sessions. The next step is a pair of zero-code switches that split the remaining
hypotheses. **Eye slots:** slot 1 is `views[1]`, shown in the left half with Swap Eyes on (the
correct setting); verified against the output by the swap check (fact 22).
Windows acceptance plan and commands: [EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md).

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
| 28 | Conditions (`--conditions`) of all five traces since 28 Sep: Same Pass on, Early stereo view setup **off** (`early_applied` 0), Swap Eyes on. The four far/near traces read slot passes 2/3 in all 76–80 pair rows of every phase, including before the submissions | `views[1]` is secondary (pass 3) from construction until its own submission. The Early stereo view setup and full-view switches have never been tried on this issue |

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

**Per-view updates `views[1]` could skip at distance** (guessed unless marked):

| Candidate | Status for `views[1]` |
|---|---|
| **Primary-only work on the game thread** between construction and submission: per-view LOD, significance, HLOD/impostor or foliage decisions, registrations, the extra production | **Open, best fit.** `views[1]` is pass 3 for that whole window (proven). Such systems are commonly distance-based and would stay tied to `views[1]` in both orders. Test: E1 |
| Primary-only decision on the render thread | Weak: Same Pass makes `views[1]` primary for its submission (proven). Only a result cached before that call would escape |
| Stereo-eye logic independent of primary/secondary (pass ≠ 0) | Open. Test: E2 (both views as full views) |
| Per-view-state content the secondary state never receives (fact 27), or its history (temporal LOD, occlusion, fading) | Open. Nothing in the compared state tracks the freeze (proven for the fields read, below `+0x20a8`), but slot 0-only content exists. Test: E3 (proposed, writes a pointer) |
| WPO/wind | The CPU view clock is equal per eye (facts 9, 21). WPO reads view uniforms (shared clock) and per-primitive wind data (one per component). A per-eye split needs a per-view input from one of the rows above. GPU side unmeasured |
| Animation update rate (URO, visibility-based ticks) | Unlikely: game-thread, per component, one pose per frame for both views in stock UE. Would need a Kuro per-view animation path (none known) |
| Cached mesh draw commands | Weak: cached per scene and pass, shared by both views; view data comes through the view uniform buffer. Fact 5: disabling via CVar did not fix it |
| The scene-capture target itself | Unlikely: rects and projections match (facts 3, 20). Test: E4 (proposed) |
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

**Local, user (Windows):** MSVC build `8bb228d9…` with zero errors; far and near traces in the game
with the lifecycle counters in fact 13. Not run anywhere: overhead measurement, PowerShell
launcher actions in cloud. Cloud checks are g++/clang only.

## Hypotheses (ranked; all untested on WuWa)

1. **H1: Game-thread setup that runs only for the primary pass.** `views[1]` is pass 3 from
   construction until its submission (fact 28). A distance-based per-view decision or
   registration made there would skip `views[1]` in both orders (fact 22). Test: E1.
2. **H2: Render-thread stereo-eye logic** that keys on "is an eye pass" rather than primary or
   secondary. Test: E2.
3. **H3: The secondary view state lacks something the primary state gets** (fact 27), or holds a
   history that pins far objects. Test: E3, only if E1 and E2 are negative.
4. **H4: The capture target, or constructor-time pass logic.** Lowest prior. E4 for the target;
   no existing switch reaches the constructor.

## Next experiments, in order

E1 and E2 need **no native change**: the registered `eye-diff-raw-20260929` build has both
switches, and each trace now reports whether the switch took effect (`--conditions`). Commands
and the result table: [EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md).

1. **E1: Early stereo view setup on** (UEVR menu, `VR_WuWaEarlyStereoViews`; read on every view
   construction, so it switches live). `views[1]` becomes pass 2 right after construction,
   before game-thread setup. Constructor inputs, state pointers and exposure sharing are
   unchanged. Trace markers:
   - slot passes 2/2 before the submissions;
   - `early_applied` rising;
   - Same Pass `applied` flat, since `PassScope` sees pass 2 and leaves it.

   If the left-eye far tree sways, H1 holds; also look for slot 1 gaining an `external`
   producer. If it stays frozen, H1 is excluded for everything after construction.
2. **E2: `full-views`** (existing, 45 s, restores itself): both views render as pass 0 in both
   submissions. It leaves game-thread setup alone, so it is independent of E1. If the far tree
   sways during the window, H2 holds.
3. **E3, proposed, not implemented:** swap the two views' state pointers (`view+0x8`, verified)
   right after `views[1]` is constructed, for a timed window, restored like the pass scopes. If
   the freeze moves to `views[0]`, the state carries it (H3). This replaces which history each
   eye uses, so each state gets one frame of the other eye at the start and end of the window.
   The codebase does not currently permit that ("not permission to share or replace
   temporal/occlusion history"), so it needs explicit approval and its own review.
4. **E4, proposed:** render `views[1]` to the game target and `views[0]` to the capture target,
   with the display halves flipped. Separates the target from the view. More invasive (it
   changes the NSF target swap, which UEVR notes is performance-sensitive); only if E1–E3 are
   negative.
5. **Fix stage.** If E1 fixes it, the fix already exists as a guarded, experimental option.
   Accept it only after:
   - a headset check at the same far and near positions;
   - a regression pass (both-eye exposure, shadows, reflections, the ultimate camera, frame
     time).

   Only then consider making it the WuWa default, in a separate change.

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
