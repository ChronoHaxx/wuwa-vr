# Paired-eye diagnostics: local build, checks and next session

Diagnostic tooling only: read-only, no rendering change, no write to game memory (including
`+0x550`). `FFakeStereoRenderingHook.cpp` and `WuWaStereoBasePose.hpp` are unchanged, so the
headset-confirmed ultimate camera fix and `r.OneFrameThreadLag=0` stay as they were. Evidence
and open questions: [STEREO-CULLING-PROGRESS.md](STEREO-CULLING-PROGRESS.md).

## 1. Build (MSVC, existing tree)

Native files on this branch (SHA-256 of the committed CRLF files):

| File (`src/utility/`) | Change since `8bb228d9` build | SHA-256 |
|---|---|---|
| `WuWaEyeDiff.hpp` | none | `4aaaf6d295f6c76f5acf950af10682fe3e55873f8505e71795fa2023967c36bf` |
| `WuWaRawSnapshot.hpp` | **new** | `241ac2b258decbc8dae7473526e5e70c2818eb0bec33c97824996a0e407d1d26` |
| `WuWaLodProbe.hpp` | raw snapshots, status keys | `5b092330cfffe232df9927ee8555472dcbc2504f3f0836c3f15dd49f6513057f` |
| `WuWaTestControl.hpp` | forwards `raw_snapshots` | `647d8ccb0cebddc920fdf9809b8562c6aaa52893d4e9ef14c482cebe279b647d` |

```powershell
git -C E:\Coding\wuwa-vr fetch origin claude/jolly-turing-lflai7
git -C E:\Coding\wuwa-vr worktree add E:\Coding\wuwa-vr-eye-diff-raw origin/claude/jolly-turing-lflai7
foreach ($f in 'WuWaEyeDiff.hpp','WuWaRawSnapshot.hpp','WuWaLodProbe.hpp','WuWaTestControl.hpp') {
  Copy-Item "E:\Coding\wuwa-vr-eye-diff-raw\mod\uevr\src\utility\$f" "E:\Coding\wuwa-vr\upstream\UEVR\src\utility\$f"
  (Get-FileHash "E:\Coding\wuwa-vr\upstream\UEVR\src\utility\$f").Hash   # compare with the table
}
```

If a hash differs only because of line-ending conversion, `git -C E:\Coding\wuwa-vr-eye-diff-raw
status` must still be clean. Rebuild `uevr.vcxproj` as before (RelWithDebInfo x64, new unique PDB
name). No CMake change: the new header is included by `WuWaLodProbe.hpp`. A fresh tree can use
`mod/BUILD.md`; the reconstruction patch differs only in those three sections.

## 2. Local tests (worktree)

```powershell
cd E:\Coding\wuwa-vr-eye-diff-raw\launcher\dev
.\test-eye-diff.cmd                                        # MSVC: 19 policy cases incl. raw block/schedule/ring
python -m unittest test_eye_diff_summary test_register_candidate   # 23 + 7
python check-eye-diff-json.py --json-include <dir with nlohmann\json.hpp>   # optional, needs g++
```

## 3. Diagnostic markers, before any far/near session

1. **In the DLL** (string literals compiled in):
   ```powershell
   python -c "import sys;d=open(sys.argv[1],'rb').read();m=[b'eye_pair_raw',b'raw_snapshots_supported',b'validated pair sequence',b'frames_read_at'];print({k.decode():k in d for k in m})" <built UEVRBackend.dll>
   ```
   All four must be `True`.
2. **Registration** into the portable package (Launcher.exe closed; copy
   `launcher\dev\wuwa_register_candidate.py` into `app\dev\` first):
   ```powershell
   python\python.exe app\dev\wuwa_register_candidate.py --app app --backend <dll> --id eye-diff-raw-20260929 --dry-run
   python\python.exe app\dev\wuwa_register_candidate.py --app app --backend <dll> --id eye-diff-raw-20260929
   ```
   The earlier `eye-diff-lifecycle-20260928` entry can stay for rollback. Hash corrections in
   this version: a re-run hashes the installed runtime backend (not just its presence), the
   base runtime must match its own catalog hash before it is copied, and a failed copy check
   removes the half-made folders. Launcher.exe → Recovery → self-check should list the new
   build as "backend matches catalog".
3. **Live, short** (game running on the new build, anywhere):
   ```powershell
   python launcher\dev\wuwa-test.py lod-inputs --pid <pid> --seconds 8 --raw-snapshots --capture-source steamvr --output <tmp>\raw-smoke
   python launcher\dev\wuwa_eye_diff_summary.py --raw state:0x550 <tmp>\raw-smoke\lod.jsonl
   ```
   Expect in `lod-inputs.json` → `end.lod_probe`: `raw_snapshots_supported: true`,
   `raw_snapshots.requested: true`, `taken ≥ 2`, `written == taken`, `dropped: 0`,
   `orphaned: 0`; and the summarizer printing `slot0=… slot1=…` rows (values, not `--`).
   Without `--raw-snapshots`, `raw_snapshots.requested` must be `false` and no `eye_pair_raw`
   rows appear.

## 4. Far/near session (same tree, same order as last time)

Confirm on the headset first (far: left-eye tree frozen; near: both sway), keep still, then:

```powershell
python launcher\dev\wuwa-test.py lod-inputs --pid <pid> --seconds 30 --raw-snapshots --capture-source steamvr --output <session>\far
python launcher\dev\wuwa-test.py lod-inputs --pid <pid> --seconds 30 --raw-snapshots --capture-source steamvr --output <session>\near
python launcher\dev\wuwa_eye_diff_summary.py --raw state:0x528-0x560 <session>\far\lod.jsonl <session>\near\lod.jsonl
python launcher\dev\wuwa_eye_diff_summary.py --compare <session>\far\lod.jsonl <session>\near\lod.jsonl
```

The `--raw` table answers only which slot's `+0x550` differs between positions (reading A or B
in the progress document). It does not identify the field, show that rendering reads it, or
map slots to physical eyes. Send back both folders (about 13 MB each) and the two outputs.
