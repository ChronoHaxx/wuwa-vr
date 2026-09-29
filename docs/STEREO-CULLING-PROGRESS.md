# Stereo foliage/prop mismatch: progress and evidence

Updated after the paired far/near captures (PR #2 DLL `8bb228d9…`). **No rendering fix exists
and none is proposed here.** The mismatch is unresolved: at distance the left-eye tree is static
while the right eye's sways; nearer, both have been seen moving; several trees and locations are
affected. "LOD/culling/impostor" remains a hypothesis. This update repairs the paired-eye
diagnostic, which collected nothing in its first real run, and records what the far run and the
later far/near pair showed. **Eye slots:** geometry puts slot 0 on the right and slot 1 on the
left (fact 14). That mapping is **provisional** until verified against the displayed output, so
rows below name slots; "right"/"left" in brackets is the provisional reading.
Windows build, packaging and the one next session: [EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md).

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
| 14 | Slot 0 is the **right** eye, slot 1 the **left**: eye1 sits 6.4 units along −right of eye0 (view matrices and `+0x6ec` agree to 1e-5), and eye0's projection is off-centre right (M[2][0] = −0.245) while eye1's is off-centre left (+0.245) | Provisional: the frozen (left) eye would be slot 1 (pass 3, second submission in the normal order). Not verified against the displayed output; a submission or compositor swap would reverse it |
| 15 | Every state sample is truncated (about 2,430 of 4,096 dwords differ; 1,536 kept). Before-snapshots are complete up to `+0x31dc`; `+0x31dc..+0x4000` is unknown in **all** samples. From about `+0x2800` both sides are constant and nearly all different | Nothing can be said above `+0x31dc`. The objects' sizes are unknown: readable memory does not show where either object ends |
| 16 | Of 1,540 differing state offsets below the horizon, 1,513 differ in every sample in both conditions. Every per-eye position stored in the states that differs between the eyes differs by exactly **6.4** (IPD) at far and near; one scaled copy by 0.64 | No stale per-eye origin (temporal-LOD, previous-view or similar) in any **differing** position field at far. Positions stored identically in both eyes, or above the horizon, are not covered |
| 17 | Slot 1's state window holds 693 dwords whose bytes read as the game's script text, unchanged across both captures (slot 0's: 13). They sit between dwords that match slot 0's; slot 0's values there are also unchanged | Origin unknown. They inflate raw difference counts; they show neither object extent nor what any constructor writes |
| 18 | **One stable far-only difference:** `+0x550` = 2 (slot 0) vs 0 (slot 1) in all 22 far snapshots (before and after), equal at near. Around it (`+0x528..+0x558`) slot 0 holds pointer-like values and small counts (3, 3, 1), identical in all 44 snapshots; slot 1 holds zero there in both conditions | **An unidentified correlation with distance.** Its type, whether it belongs to the view state, and whether rendering reads it are unknown. Equal values were not recorded, so either (A) slot 0 goes 0→2 at far while slot 1 stays 0, or (B) slot 0 stays 2 while slot 1 goes 2→0. The raw snapshots below record this. Nothing writes to it |
| 19 | Small counters (`+0x0d7c`, `+0x208c`, `+0x20dc`; values 25–27) differ between the eyes by ±1; which one differs changes within captures. The right eye's `+0x0d7c`/`+0x0d84` read 0 after the submissions in both conditions | Per-frame counters and transients; no consistent condition link |
| 20 | View region: nothing differs only at far. A 156-dword block (right eye data, left eye zeros) switches on and off per sample (3/11 far, 9/11 near) and never repeats a value. The other near-only view differences are float rounding (1 to about 50 ulp) of coordinates both eyes share; near-only state `+0x298/+0x6a8/+0x72c` are 1-ulp | Per-sample switching and float rounding; no view input that follows distance |
| 21 | Instanced binder: no `lod_branch` rows at far or near. CPU view time constants equal per eye in all same-frame pairs (123 far, 84 near) | Same as facts 9 and 11, now at both positions |

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

**Not rejected:** per-eye view-state history (H1), a constructor-owned view field (H2), a
difference downstream of both (H3), and LOD/culling/impostor selection as the visible outcome.

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
No hook, no game-memory write. Slots are not physical eyes (`physical_eye: unverified`), and the
windows are fixed spans whose extent is unknown (`object_extent: unknown`). Status adds
`raw_snapshots_supported` and a `raw_snapshots` counter block.

## Tests actually run

| Check | Result |
|---|---|
| `check-eye-diff-json.py`: compiles the **verbatim** `pair()`, `struct Record` and serializers with instrumented SRW lock/clock/reader and replays the real call order in 7 scenarios (unassigned phase-1 frame + render lock held at phase 2 + 5 invalid early snapshots; stride 2 from an odd frame; lost phase 2; lock held at phase 1; state changed mid-pair; ring capacity; fixture) | pass with this branch; **fails with merged PR #1** (0 samples, 0 phase-1 and 0 phase-2 rows), reproducing the field result |
| Six planted bugs (exclusive lock back at phase 2, schedule off by one, deferred row dropped, orphans uncounted, per-phase lock misses uncounted, state verification removed) | all caught |
| `test-eye-diff.cpp` (policy, 15 cases) | pass: g++ 13 with ASan+UBSan, clang 18 |
| Lifecycle harness under g++ ASan+UBSan; fixture byte-identical from g++ and clang | pass |
| Whole `WuWaLodProbe.hpp` with stub Windows/safetyhook/spdlog headers, `g++ -fsyntax-only` | no new diagnostic (one pre-existing sign-compare warning); a planted typo in the new status code is reported |
| Python: summarizer (23: truncation horizon, unknown-is-not-equal in `--compare`, unchanged/text tags, provisional eye sides, `--raw` reader) and candidate registration (7) | pass; the compare test fails on the earlier summarizer, and the three registration hash tests fail on the earlier tool |
| Raw snapshots through the verbatim `pair()`: opt-in (0 rows unless requested), every 5th eye-diff pair, 3-pair budget, before/after of one sequence, equal value kept in both slots, unreadable tail reported as a range and zeroed; `test-eye-diff.cpp` block/schedule/ring cases (19 cases total) | pass; 6 planted raw bugs (opt-in ignored, cadence, slots swapped, range off by one, stale words, missing validity) are caught |
| Reconstruction patch: only the two header sections changed (97 sections); all 65 text new-file sections apply to an empty tree and equal the overlays | pass |

**Local, user (Windows):** MSVC build `8bb228d9…` with zero errors; far and near traces in the game
with the lifecycle counters in fact 13. Not run anywhere: overhead measurement, PowerShell
launcher actions in cloud. Cloud checks are g++/clang only.

## Hypotheses (ranked; all untested on WuWa)

1. **A per-eye view-state difference.** The only far-only difference is `+0x550` (fact 18), an
   unidentified correlation: what it is, whether it belongs to the view state, and whether any
   draw reads it are all unknown. Fits facts 1-4 only if it is real state.
2. **A constructor-owned view field differs.** Weakened: no far-only view difference at hand-off
   (fact 20). Fields past the verified `+0x1004` extent are still unproven.
3. **Neither differs at hand-off**; the split is downstream (bindings, uniform payload beyond
   the CPU view, a shader distance gate). Still possible if (1) turns out to be unrelated.

## Next experiments, in order

1. **Absolute values at `+0x528..+0x560` for both slots, far and near (decides A vs B).**
   Implemented as opt-in raw snapshots (below); needs the next far/near session with
   `--raw-snapshots`, then `wuwa_eye_diff_summary.py --raw state:0x528-0x560 far near`. The eye
   diff stream itself is unchanged and still truncates; the raw pairs cover the full windows.
2. **Does anything read `+0x550`?** Only after (1). A read-only watch of which code reads that
   field (a verified hook site, not an arbitrary offset) would join it to the renderer. Without
   that, (1) stays a correlation.
3. **Tree-to-draw join** (unchanged): no capture yet says which draw is the failing tree.
4. Only if (1)-(3) agree: a minimal rendering change, accepted only by a headset comparison at
   the same far and near positions.

Not repeated: CVar switches from fact 5; copying view state between eyes; removing parallax.
Evidence and user recordings stay local; raw captures are not committed.

### How to read a far/near compare now

`wuwa_eye_diff_summary.py --compare far near` compares only up to the lowest known offset
(`compared up to +0x…`), tags values that are `UNCHANGED` in every sample and script `TEXT`,
and prints a provisional slot-to-eye label. A difference is a lead only if it is (a) below the known
horizon, (b) not text, not 1-ulp rounding and not a per-sample flicker, and (c) consistent in
every snapshot of one condition and absent in the other. In this pair only `+0x550` passes.
