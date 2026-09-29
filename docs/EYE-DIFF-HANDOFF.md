# Stereo freeze: tools update and Windows acceptance plan (29 Sep)

This round changes **only Python tools and docs**. No native file changed since `4bd6c86`, so
the registered `eye-diff-raw-20260929` build stays as it is: no rebuild, no new registration.
The ultimate camera fix, `r.OneFrameThreadLag=0` and the Launcher.exe recording UI are
untouched. Evidence and reasoning: [STEREO-CULLING-PROGRESS.md](STEREO-CULLING-PROGRESS.md),
section "Which pass is frozen".

The session tests two existing switches that split the remaining hypotheses:

| Step | Switch | What it changes for `views[1]` (the frozen image) |
|---|---|---|
| E1 | UEVR menu → **Early stereo view setup (experimental)** | Primary (pass 2) from construction on, so also during game-thread setup, not only during its own submission |
| E2 | `wuwa-test.py full-views` (45 s, restores itself) | Both views render as full views (pass 0) in both submissions; game-thread setup unchanged |

## 1. Update the tools (no rebuild)

```powershell
git -C E:\Coding\wuwa-vr fetch origin claude/jolly-turing-lflai7
git -C E:\Coding\wuwa-vr-eye-diff-raw checkout --detach origin/claude/jolly-turing-lflai7
git -C E:\Coding\wuwa-vr-eye-diff-raw diff --stat 4bd6c86 HEAD -- mod    # must print nothing
cd E:\Coding\wuwa-vr-eye-diff-raw\launcher\dev
python -m unittest test_eye_diff_summary test_register_candidate        # 33 + 7, all OK
```

Native header hashes are unchanged from the last build:

| File (`src/utility/`) | SHA-256 |
|---|---|
| `WuWaEyeDiff.hpp` | `4aaaf6d295f6c76f5acf950af10682fe3e55873f8505e71795fa2023967c36bf` |
| `WuWaRawSnapshot.hpp` | `241ac2b258decbc8dae7473526e5e70c2818eb0bec33c97824996a0e407d1d26` |
| `WuWaLodProbe.hpp` | `5b092330cfffe232df9927ee8555472dcbc2504f3f0836c3f15dd49f6513057f` |
| `WuWaTestControl.hpp` | `647d8ccb0cebddc920fdf9809b8562c6aaa52893d4e9ef14c482cebe279b647d` |

The commands below use `$t = "E:\Coding\wuwa-vr-eye-diff-raw\launcher\dev"` and
`$s = <session folder>`.

## 2. Session (one tree, about 15 minutes)

Start with Launcher.exe on `eye-diff-raw-20260929`, with Same Pass on, Swap Eyes on and Early
stereo view setup **off**. Stand where the left-eye tree freezes and keep still.

**Launcher recording and `wuwa-test.py` share one control lock and cannot run at the same
time**, so run each step's recording (Record gameplay → Start/Stop recording, 20 s) first, then
its trace.

| # | Position | Do | Headset: note left-eye tree |
|---|---|---|---|
| 1 | far | Record, then `python $t\wuwa-test.py lod-inputs --pid <pid> --seconds 30 --raw-snapshots --view-uniforms --capture-source steamvr --output $s\far-e0` | frozen expected |
| 2 | far | Early stereo view setup **on**, wait 5 s, record, then the same trace to `$s\far-e1` | **sways or frozen?** |
| 3 | far | Early **off**, wait 5 s, watch 20 s | does the freeze return? |
| 4 | far | `Start-Sleep 5; python $t\wuwa-test.py full-views --pid <pid> --capture-source steamvr --output $s\far-e2`, then click into WuWa within 5 s (it stops if WuWa loses focus). Watch during the 45 s window; it cannot be recorded | **sways or frozen?** |
| 5 | near | Early off: record, trace to `$s\near-e0` | both sway expected |
| 6 | near | Early **on**: record, trace to `$s\near-e1`; then Early **off** | both sway expected (no regression) |

In steps 2 and 6, also check for any new eye difference: brightness or exposure, shadows,
reflections, the end-of-ultimate camera, frame time. Leave Early stereo view setup **off** at
the end; it is saved in the profile.

## 3. Check the conditions, then read the result

```powershell
python $t\wuwa_eye_diff_summary.py --conditions $s\far-e0\lod.jsonl $s\far-e1\lod.jsonl $s\near-e0\lod.jsonl $s\near-e1\lod.jsonl
python $t\wuwa_eye_diff_summary.py --layout $s\far-e0\lod.jsonl
python $t\wuwa_eye_diff_summary.py --compare $s\far-e0\lod.jsonl $s\near-e0\lod.jsonl
Get-Content $s\far-e2\comparison.json | Select-String status
```

A step counts only if its live markers match. The `saved settings` line is the profile file and
can lag a menu change, so it does not decide.
- **e0 traces:** `early configured=False`, `pass slot0/slot1 before_submissions: 2/3`.
- **e1 traces:**
  - `early configured=True`, with `applied=` rising from `baseline` to `after`;
  - `pass slot0/slot1 before_submissions: 2/2`;
  - `same-pass applied=` equal in `baseline` and `after`.
- **e2:** `comparison.json` status `captured_and_restored_visual_review_pending` (applied and
  restored cleanly).
- **`--layout`:** says `matching the state extent in use`. A WARNING means a game update moved
  the state layout, and `--compare` must not be trusted until the extent is re-checked.

| E1 far (step 2) | E2 far (step 4) | Reading | Next |
|---|---|---|---|
| sways, and step 3 freezes again | any | **H1**: primary-only game-thread setup skips `views[1]` | Steps 5–6 clean? Then the existing option is the fix candidate. Accept it only after a headset check at far and near, then a later default change. If `--conditions` shows slot 1 gaining an `external` producer, note it |
| frozen | sways | **H2**: render-thread stereo-eye logic | A trace aimed at the pass-gated render path (next round) |
| frozen | frozen | H1 and H2 excluded after construction; the view state (H3) or the target (H4) remain | Decide whether to approve E3, the timed state-pointer exchange (progress doc) |

Nothing here is a fix until the headset check passes at both positions. Send back:
- the six folders, the recordings, and the four summary outputs;
- one line per step: sways or frozen, and which eye.
