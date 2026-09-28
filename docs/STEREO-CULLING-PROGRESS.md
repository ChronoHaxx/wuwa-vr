# Stereo foliage/prop mismatch: progress and evidence

Prepared 28 September 2026 from source only. This candidate contains **no rendering
change** and **no fix**. It adds one read-only measurement so a single local capture can
decide between the remaining explanations. Nothing here has been run in the game.

## Baseline and accepted work

- Public `main` `c68d850` (release `experimental-2026-09-28-1628`, build
  `camera-trial-controls-20260928-r2`). Pins: UEVR `4ee5c6b6`, UESDK `14478dfa`.
- **Accepted (user, real headset, 28 Sep, after publication):** the end-of-ultimate camera
  desynchronisation is fixed for the tested cases. `WuWaStereoBasePose.hpp` and its
  callsite/scope guards are untouched. Older public pages still call it unaccepted.
- Preserved: Native Stereo Fix, its early-primary/same-pass conversion, the scene-frame
  pairing, and the `r.OneFrameThreadLag=0` default. No pass-value or view-field rewrite.
- Out of scope and untouched: reflections, water, UI, packaging.

## What is measured, and what is not

| # | Fact (retained local evidence unless noted) | Consequence |
|---|---|---|
| 1 | Same tree fails far, animates in both eyes near, fails again on retreat | Distance-dependent and history-shaped, not a permanent per-object flag |
| 2 | Persists at zero IPD; 399 CPU eye-offset outputs coincide | Eye separation does not cause it |
| 3 | 676 main constructors / 1776 binder rows have equal main-eye projection scales | Projection scale does not differ (auxiliary views do: never group by pass alone) |
| 4 | Reversing submission order leaves the same physical eye frozen | Rules out "second submission loses" / first-writer-wins caches. Leaves state or inputs owned by the eye |
| 5 | Cached mesh commands, foliage LOD/culling/impostor CVars, shadow quality: no fix | Not those switches, as tried |
| 6 | One primitive picked a different vertex factory per eye far away, the same one near (not yet joined to the visible tree) | Some per-eye selection differs; **which draw is the tree is unknown** |
| 7 | Main-view pairs are only accepted by existing reflection code when `+0xffe`/`+0x1000` are zero | Main views are treated as non-instanced (a gate, not a per-frame measurement) |

Still unmeasured: the tree-to-draw join, per-eye view-state contents, the constructor-owned
LOD factor, final `ViewSize`/`BufferSize` uniforms, actual shader bindings. The 2W×H
(original) versus W×H (NSF) target request is a source asymmetry, not evidence of a
different view rectangle or LOD scale.

## Engine source consulted (stock excerpts, not WuWa's build)

Indath's repo carries stock UE excerpts (`Scenevisibility.cpp.txt`, `SceneView.h.txt`, commit
`787b9ea`). WuWa's engine is modified, so this shows what the stock code *reads*, not what
the shipped game does:

- CPU static-mesh LOD reads the view origin and `LODScale = r.StaticMeshLODDistanceScale *
  View.LODDistanceFactor` (lines 1765-1783, 2165). `LODDistanceFactor` is a plain `FSceneView`
  field, so it is a constructor-owned input.
- Distance-cull fade, HLOD substitution and dithered LOD transitions live in the per-eye
  `FSceneViewState` (`PrimitiveFadingStates`, `HLODVisibilityState`, temporal LOD state;
  lines 622-665, 3440-3450, 4583). Fade is dropped when `bDisableDistanceBasedFadeTransitions`
  is set by a per-state large-camera-movement / stale-render-time test (lines 3396-3407).

With origin and projection scale equal (facts 2 and 3), the stock inputs left to differ are a
constructor-owned view field (hypothesis 2) or the eye's view-state history (1); anything
else has to sit downstream of both (3). This is inference from stock code, not measurement.

## Hypotheses (ranked by fit to the facts above; all untested on WuWa)

1. **Per-eye view-state history diverges** (fade, HLOD, temporal LOD, occlusion-reset flag).
   Fits 1, 2, 3, 4 and the history shape of 1. If true, the state window will hold dwords
   that differ between eyes only in the failing capture, typically clock-like or flag-like.
2. **A constructor-owned view field differs** (`LODDistanceFactor`, camera-cut/fade flags,
   rect) because the two views are built in different contexts (empty family versus one
   view, pass 3 converted later, different target size). Fits 1-4 only if a constant
   asymmetry crosses a threshold at different distances. Cheapest to exclude.
3. **Neither differs at hand-off**; selection or final shader inputs differ downstream
   (bindings, uniform payload beyond the CPU view, buffer-size asymmetry, a shader-level
   distance gate). Least specific. It is the result if both regions come back clean while the
   mesh-binding trace still shows per-eye vertex-factory differences.

Rejected with evidence: eye separation (2), projection scale (3), submission order (4),
instanced stereo for the main views (7). A CVar/cached-command switch (5) is not repeated.

## The change: one read-only measurement

`WuWaEyeDiff.hpp` plus 44 added lines in `WuWaLodProbe.hpp`. Inside an already requested
LOD trace, every 60 family frames it takes the existing before/after-submissions pair
snapshot and, with guarded reads, records **which dwords differ** between (a) the two main
`FSceneView`s (`+0` to `+0x1e40`) and (b) their two view states (`+0` to `+0x4000`). Equal
dwords are omitted; deltas are ascending by offset and capped, with the true total kept.

- No new hook site, no game-memory write, no allocation on the game thread, nothing shared
  or copied between eyes. The existing lease, exclusive lock, code checks and displaced-span
  checks are unchanged. Extra heap only exists once a trace has been requested (about 3 MB).
- Limits stated in the output: the game-thread view is only proven readable to `+0x1004`,
  so higher view offsets are flagged unverified; the view-state extent is unknown, so expect
  pointer noise and read it only across samples; addresses are identifiers, never to be
  dereferenced.
- The **before** snapshot is the trustworthy one: with `r.OneFrameThreadLag=0` the previous frame has
  finished. The **after** snapshot can race with render-thread writes to the view states and show torn
  values, so the summarizer reads *before* by default (`--phase after|both` is opt-in).
- `launcher/dev/wuwa_eye_diff_summary.py` classifies deltas (verified offsets, clock-like
  floats using the trace's own `uniforms` clock, small ints, pointer low-half hints) and
  `--compare failing control` lists dwords that differ only while failing.

## Tests actually run (Linux cloud; not the game, not MSVC)

| Check | Result |
|---|---|
| `test-eye-diff.cpp` policy tests (13 cases) | pass: g++ 13 with ASan+UBSan, clang 18 (also builds under TSan, which adds little to a single-threaded test) |
| 20 planted bugs in `WuWaEyeDiff.hpp` (over-read, wrap, alignment, truncation, stale region state, wrong phase/partner/interval, ring overwrite/flag) | all caught, none survived |
| Probe serializer and the verbatim `pair()` call site compiled with `-Werror` against nlohmann/json 3.11.3 (`check-eye-diff-json.py`) | pass; output equals the committed fixture, and a deliberately renamed key is detected |
| `test_eye_diff_summary.py` (12 cases, incl. native-shaped fixture, clock matching, far-versus-control) | pass |
| Reconstruction patch: only the two affected sections changed (95 others byte-identical); all 71 new-file sections applied to an empty tree and matched the overlays | pass; the six binaries match raw, text matches modulo CRLF |

**Not run:** an MSVC build of the full backend (needs the Windows toolchain and a
reconstructed UEVR tree), any in-game execution, overhead measurement, the modified-file
patch sections against the pinned UEVR checkout (untouched by this change), `node dev/check-*`
(needs `npm install`; unrelated). `manifest.json`, `SHA256SUMS.txt` and `checkpoint.json`
describe the published release and were deliberately not regenerated. Candidate
`uevr-working-tree-full.patch` SHA-256: `29d24f1f31fa527f806be1a616293f648f6a3f884cfbe3cd5dcb4ec44a36ff98`.

## One local capture (visual acceptance stays separate)

Build the backend from `mod/BUILD.md` and test in a separate profile with the game closed.
Then, in one session at the Xuanfang Hold tree:

1. **Failing capture:** stand where one eye freezes the tree. Request a 30 second LOD trace
   with view uniforms **and** mesh bindings. Note the clip time; keep still.
2. **Control capture:** the same tree close enough that both eyes animate; same request.
3. Run `wuwa_eye_diff_summary.py far.jsonl`, then `--compare far.jsonl near.jsonl`; also
   record from the mesh-binding trace whether the visible tree's primitive picks different
   vertex factories per eye at far only.
4. Optional, only if the earlier fade experiments were not already tried: repeat step 1 once
   with `r.DisableLODFade 1` and note whether the freeze remains. It tests fade history
   directly and needs no code.

| Outcome | What it would establish | Next |
|---|---|---|
| State-region dwords differ **only while failing** (clock-like, flag-like) | Hypothesis 1: per-eye history diverges | Identify the field, then find why that eye's history skips or resets; do not share histories |
| View-region unclassified or "unexpected" dwords differ (constantly, or only while failing) | Hypothesis 2: a constructor input differs | Decode the field, then fix its ownership at view construction |
| Both regions show only expected eye geometry, yet vertex factories still differ per eye | Hypotheses 1 and 2 excluded for these windows | Hypothesis 3: capture the full raw uniform payload per eye; no fix is justified yet |
| No records, warnings about invalid or unreadable regions | The window or pairing assumption is wrong | Report the warning text; do not infer anything |

Not established even by a clean result: which draw is the visible tree (the tree-to-draw join
still needs its own step), and any visual improvement. Only a headset comparison of the
failing and control positions can accept a future fix; a passing capture proves neither.
