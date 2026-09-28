# Paired-eye diagnostic repair: Windows build, package and one session

This candidate repairs **tooling only**. It changes no rendering, no view field and no game
memory. It keeps the headset-confirmed ultimate camera fix (`WuWaStereoBasePose.hpp` and
`FFakeStereoRenderingHook.cpp` are byte-identical to `main`) and `r.OneFrameThreadLag=0`.
Nothing below proves a foliage fix. Evidence and reasoning are in
[STEREO-CULLING-PROGRESS.md](STEREO-CULLING-PROGRESS.md).

## 1. Build (MSVC, existing tree)

Only two native files changed: `src/utility/WuWaEyeDiff.hpp` and `src/utility/WuWaLodProbe.hpp`.
In the tree that produced `583231192c…` (`E:\Coding\wuwa-vr\upstream\UEVR`):

```powershell
# A separate worktree leaves any unpushed local work in E:\Coding\wuwa-vr untouched.
git -C E:\Coding\wuwa-vr fetch origin claude/jolly-turing-lflai7
git -C E:\Coding\wuwa-vr worktree add E:\Coding\wuwa-vr-eye-diff origin/claude/jolly-turing-lflai7
copy E:\Coding\wuwa-vr-eye-diff\mod\uevr\src\utility\WuWaEyeDiff.hpp  E:\Coding\wuwa-vr\upstream\UEVR\src\utility\
copy E:\Coding\wuwa-vr-eye-diff\mod\uevr\src\utility\WuWaLodProbe.hpp E:\Coding\wuwa-vr\upstream\UEVR\src\utility\
```

If the local `upstream\UEVR` copies of these two files differ from `main` for any other reason,
stop and compare first; the cloud could only see the uploaded snapshot, which matched `main`.

Then rebuild `uevr.vcxproj` exactly as in `relink-unrestricted-command.json` (RelWithDebInfo,
x64, a new unique PDB name). For a fresh tree, `mod/BUILD.md` applies: the regenerated
`uevr-working-tree-full.patch` differs from `main` only in those two file sections.

Local checks (cloud already ran them with g++/clang; MSVC has not):

```powershell
launcher\dev\test-eye-diff.cmd                                   # MSVC policy test
cd launcher\dev; python -m unittest test_eye_diff_summary test_register_candidate
python check-eye-diff-json.py --json-include <dir with nlohmann\json.hpp>   # needs g++; optional on Windows
```

## 2. Package into the usual portable Launcher.exe

Close Launcher.exe and the game. From the portable package folder (the one holding
`Launcher.exe`, `app` and `python`), after copying `launcher\dev\wuwa_register_candidate.py`
into `app\dev\`:

```powershell
python\python.exe app\dev\wuwa_register_candidate.py --app app --backend <built UEVRBackend.dll> --dry-run
python\python.exe app\dev\wuwa_register_candidate.py --app app --backend <built UEVRBackend.dll>
```

It copies the **Camera candidate + trigger controls · 28 Sep** runtime and profile seed
(`camera-trial-controls-20260928-r2`, the profile Codex's entry was identical to), replaces
only `UEVRBackend.dll`, and appends one `candidate` entry (`eye-diff-lifecycle-20260928`).
Existing builds, runtimes and seeds are not modified. The catalog keeps its UTF-8 BOM. Recording
controls and languages are untouched. **Rollback:** pick the earlier build in Launcher.exe, as
usual (Select backs up the live profile first). Recovery → self-check should list the new build
as "backend matches catalog". The older developer `.cmd`/web registration is no longer needed.

## 3. One session: far then near, same tree

Use Launcher.exe → the new candidate → Apply & launch. Stand at a tree where the left eye is
static and the right eye sways. Keep still.

1. **Confirm the failure is present** (headset, and a 10 s clip with the launcher's recording
   controls). Stop recording before tracing, so the recorder does not load the trace.
2. **Far trace, lightweight** (no `--view-uniforms`/`--mesh-bindings`: those slowed the last
   run to about 16 pairs/s and filled their buffers within seconds):
   ```powershell
   python launcher\dev\wuwa-test.py lod-inputs --pid <game pid> --seconds 30 --capture-source steamvr --output <session>\far
   ```
3. Walk in until **both** eyes sway; keep still; repeat into `<session>\near`.
4. Summaries (any Python 3, from the repo):
   ```powershell
   python launcher\dev\wuwa_eye_diff_summary.py <session>\far\lod.jsonl
   python launcher\dev\wuwa_eye_diff_summary.py --compare <session>\far\lod.jsonl <session>\near\lod.jsonl
   ```

**The tooling repair is accepted** only if the far run shows: `lod-inputs.json` →
`end.lod_probe.eye_pair_diff.before_taken ≥ 1` and `written ≥ 2`; `pair` rows with
`"phase":"before_submissions"` in `lod.jsonl`; `pairs.after_submissions.lock_misses` near 0; and
no `LIFECYCLE:` warning from the summarizer. Also note the `frames` value on the
before-submission samples (the repair assumes it is not the assigned frame; stock UE would
show `4294967295`).

Send back: both `lod-inputs.json` files, both summary outputs, the two `lod.jsonl`, the clip,
and one line on what each eye showed at far and near. What each outcome would mean is in the
progress document's table. A clean summary is not a visual fix; only a later rendering change,
compared in the headset at the same far and near positions, can be accepted as one.
