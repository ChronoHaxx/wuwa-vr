# Stereo foliage/prop mismatch: progress and evidence

Updated 28 September 2026 (after the first paired-eye capture). **No rendering fix exists and
none is proposed here.** The mismatch is unresolved: at distance the left-eye tree is static
while the right eye's sways; nearer, both have been seen moving; several trees and locations are
affected. "LOD/culling/impostor" remains a hypothesis. This update repairs the paired-eye
diagnostic, which collected nothing in its first real run, and records what that run did show.
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
tree failing; one run, no near control; auxiliary buffers truncated, so not exhaustive).

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
| 12 | Eye 0 has a second uniform production (236 throttled rows) from a different view object in eye 0's render family that carries **eye 0's view-state pointer**, eye 0's perspective projection and external matrices. Eye 1 has none | Identity unknown. Consistent with a view snapshot such as a shadow-depth view built from the first eye (NSF shares shadows); unverified. Any view that shares eye 0's `FSceneViewState` can write that eye's history |

## Rejected hypotheses and readings

| Rejected | Evidence |
|---|---|
| Eye separation, projection scale, submission order, instanced main views, the tried CVars | Facts 2, 3, 4, 7, 5 |
| "PR #1's empty trace says something about rendering" | It is a tooling failure with a known mechanism (below) |
| "The 60-frame cadence was merely too sparse" | Phase 1 never saw an assigned frame, so no cadence could open; and phase 2 lost the lock (below) |
| "31 per-eye VF instances at far = per-eye LOD" | Fact 10: unique per eye, same sections and shaders: per-view allocation signature |
| "One eye's wind clock is frozen in its CPU view constants" | Fact 9 |

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

## Tests actually run (Linux cloud; not MSVC, not the game)

| Check | Result |
|---|---|
| `check-eye-diff-json.py`: compiles the **verbatim** `pair()`, `struct Record` and serializers with instrumented SRW lock/clock/reader and replays the real call order in 7 scenarios (unassigned phase-1 frame + render lock held at phase 2 + 5 invalid early snapshots; stride 2 from an odd frame; lost phase 2; lock held at phase 1; state changed mid-pair; ring capacity; fixture) | pass with this branch; **fails with merged PR #1** (0 samples, 0 phase-1 and 0 phase-2 rows), reproducing the field result |
| Six planted bugs (exclusive lock back at phase 2, schedule off by one, deferred row dropped, orphans uncounted, per-phase lock misses uncounted, state verification removed) | all caught |
| `test-eye-diff.cpp` (policy, 15 cases) | pass: g++ 13 with ASan+UBSan, clang 18 |
| Lifecycle harness under g++ ASan+UBSan; fixture byte-identical from g++ and clang | pass |
| Whole `WuWaLodProbe.hpp` with stub Windows/safetyhook/spdlog headers, `g++ -fsyntax-only` | no new diagnostic (one pre-existing sign-compare warning); a planted typo in the new status code is reported |
| Python: summarizer (16) and candidate registration (4) | pass |
| Reconstruction patch: only the two header sections changed (97 sections); all 65 text new-file sections apply to an empty tree and equal the overlays | pass |

Not run: MSVC, the game, overhead measurement, PowerShell launcher actions.

## Hypotheses (ranked; all untested on WuWa)

1. **Per-eye view-state history diverges** (fade, HLOD, temporal LOD, occlusion). Fits 1-4.
   Fact 12 gives a concrete way it could be one-sided: an extra view that shares eye 0's state.
2. **A constructor-owned view field differs** (`LODDistanceFactor`, camera-cut/fade flags, rect).
3. **Neither differs at hand-off**; the split is downstream (bindings, uniform payload beyond
   the CPU view, a shader distance gate).

## Next experiments, in order

1. **The one session** in [EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md): lightweight far and near
   traces at the same tree, failure confirmed in the headset first.

   | Outcome of `--compare far near` | Establishes | Next |
   |---|---|---|
   | State dwords differ only while failing (clock-/flag-like) | H1 | Name the field; test whether fact 12's view writes it; never share histories between eyes |
   | View-region unclassified/unexpected dwords differ | H2 | Decode the field; fix ownership at construction |
   | Only expected eye geometry differs in both regions | H1 and H2 excluded for these windows | H3: needs the tree-to-draw join (2) before any fix |
   | `LIFECYCLE:` warnings or no samples | Tooling still wrong | Report counters; infer nothing |

2. **Tree-to-draw join (missing measurement for any rendering patch).** No capture yet says
   which draw is the failing tree, so no per-eye draw difference can be attributed. The
   nearby-mesh inventory records the component, asset, bounds and instances but deliberately
   no render proxy. Joining needs one verified, read-only render identity for that component
   (its scene proxy, or bounds recorded in the mesh-binding rows). Not implemented; it must be
   code-checked for this build, not an arbitrary offset.
3. Only if (1) and (2) point at the same field or draw: a minimal rendering change, accepted
   only by a headset comparison at the same far and near positions.

Not repeated: CVar switches from fact 5; copying view state between eyes; removing parallax.
Evidence and user recordings stay local; raw captures are not committed.
