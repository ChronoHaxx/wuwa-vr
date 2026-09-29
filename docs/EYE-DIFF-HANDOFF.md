# Stereo freeze: E3 view-state swap, build and local plan (29 Sep)

E1 (Early stereo view setup) and E2 (full views) were both negative: the stereo pass is ruled
out. E3 tests the ranked top two causes: the frozen view's **own view state** versus its
**capture target** (progress doc, "What is still specific to `views[1]`").

**What E3 does.** For a timed window only, the two main views exchange their view-state
pointers every frame. Each eye then renders with the other eye's state object. The pointers
are restored after each frame's submissions and when the window ends.
- **Off unless asked:** available only through the WuWa test control (`state_swap`, 0..60 s).
  No menu entry and no profile setting.
- **No state copying or sharing:** only the two pointers are exchanged.
- **Fails safe:** a failed write or restore stops all writes in that module for the rest of
  the session.
- **Untouched:** the ultimate camera fix, `r.OneFrameThreadLag=0` and Launcher.exe's recording UI.

**Cost:** at the start and end of the window, each state gets one frame of the other eye, so
expect a brief blur or pop.

## 0. This machine (differences from the previous handoff)

- **Checkout:** `E:\Coding\wuwa-vr-eye-diff-raw` is a partial clone of PR #2's branch (made
  earlier with `git clone --filter=blob:none --branch claude/jolly-turing-lflai7 <repo URL>`). Your
  `E:\Coding\wuwa-vr` checkout does not track GitHub and is not touched.
- **Python:** the PATH Python is 3.7 without Pillow. Use scoop Python everywhere:
  `$py = "$env:USERPROFILE\scoop\apps\python\current\python.exe"`. Check that `& $py -c "import PIL"`
  prints nothing.
- **Capture source:** there is no SteamVR (Oculus / Virtual Desktop), so leave `--capture-source`
  out. The default `simulator` source works. `steamvr-capture.exe` and `openvr_api.dll` are
  needed only for `--capture-source steamvr`.
- **Focus:** commands stop if WuWa loses focus. Start each one after `Start-Sleep 20`, then
  click into the game.

```powershell
$t  = "E:\Coding\wuwa-vr-eye-diff-raw\launcher\dev"
$s  = "E:\Coding\wuwa-vr\extracted\pr2-state-swap-20260929"   # local evidence only, never committed
$py = "$env:USERPROFILE\scoop\apps\python\current\python.exe"
```

## 1. Update and test the tools

```powershell
git -C E:\Coding\wuwa-vr-eye-diff-raw pull --ff-only
cd $t
& $py -m unittest test_eye_diff_summary test_register_candidate test_state_swap_runner   # 34 + 7 + 7
.\test-state-swap.cmd      # MSVC: "state swap: 6 cases passed"
.\test-eye-diff.cmd        # MSVC: 19 cases, unchanged
```

## 2. Build (native change: rebuild required)

Copy five files into the local tree. First check that each one you replace still has its **old**
hash. If not, the local tree has other changes: stop and send the diff instead of overwriting.

| File | Old SHA-256 (last build) | New SHA-256 |
|---|---|---|
| `src\utility\WuWaStateSwap.hpp` | new file | `6be3d00c10928e01aadad7dd8536d230797e96e125c6584afdf3a27eac45e4de` |
| `src\utility\WuWaShadowPass.hpp` | `9e0ff79d1ef2775c5f4d1c06ecb46720d02e7be4dd8b388051e5f98c84b59a32` | `9ea8fa329a4efab5c6e3f0041c3ac8a8ee119b6872bc866d9a77767a4ad268b3` |
| `src\utility\WuWaTestControl.hpp` | `647d8ccb0cebddc920fdf9809b8562c6aaa52893d4e9ef14c482cebe279b647d` | `9f57a9a519c420e4331aeb66bbd641be0a5e26b1fe4985ac91f028d613755a10` |
| `src\mods\vr\FFakeStereoRenderingHook.cpp` | `5b82d18e1f678ff1cb01025a3ab83c568f721e55b87c378158b353b30c227e17` | `0f64cb47ccabc63ef61b1cc5fa1a8d240a6459da51ad39486f45013786ec1df0` |
| `src\utility\WuWaEyeDiff.hpp`, `WuWaRawSnapshot.hpp`, `WuWaLodProbe.hpp` | unchanged (hashes as in the last build) | unchanged |

```powershell
$from = "E:\Coding\wuwa-vr-eye-diff-raw\mod\uevr"; $to = "E:\Coding\wuwa-vr\upstream\UEVR"
foreach ($f in 'src\utility\WuWaShadowPass.hpp','src\utility\WuWaTestControl.hpp','src\mods\vr\FFakeStereoRenderingHook.cpp') {
  "{0}  {1}" -f (Get-FileHash "$to\$f").Hash, $f          # must equal the Old column
}
foreach ($f in 'src\utility\WuWaStateSwap.hpp','src\utility\WuWaShadowPass.hpp','src\utility\WuWaTestControl.hpp','src\mods\vr\FFakeStereoRenderingHook.cpp') {
  Copy-Item "$from\$f" "$to\$f"; "{0}  {1}" -f (Get-FileHash "$to\$f").Hash, $f   # must equal the New column
}
```

Rebuild `uevr.vcxproj` as before (RelWithDebInfo x64, new unique PDB name). No CMake change: the
new header is included by `WuWaShadowPass.hpp`. Then:

```powershell
& $py -c "import sys;d=open(sys.argv[1],'rb').read();m=[b'state_swap_supported',b'state swap applied',b'view-state swap window',b'eye_pair_raw'];print({k.decode():k in d for k in m})" <built UEVRBackend.dll>
Copy-Item "$t\wuwa_register_candidate.py" <package>\app\dev\     # Launcher.exe closed
& $py <package>\app\dev\wuwa_register_candidate.py --app <package>\app --backend <dll> --id state-swap-20260929 --dry-run
& $py <package>\app\dev\wuwa_register_candidate.py --app <package>\app --backend <dll> --id state-swap-20260929
```

All four markers must be `True`. In Launcher.exe → Recovery → self-check, the new build must show
"backend matches catalog". Keep `eye-diff-raw-20260929` for rollback.

## 3. Session (far tree, one game session, about 10 minutes)

Launch `state-swap-20260929` with Same Pass on, Swap Eyes on and Early stereo view setup off.
Stand where the left-eye far tree freezes and keep still. Addresses identify the two states only
within one game session, so don't restart between steps.

| # | Do | Headset: far tree, each eye |
|---|---|---|
| 1 | `Start-Sleep 20; & $py $t\wuwa-test.py lod-inputs --pid <pid> --seconds 30 --view-uniforms --output $s\far-e0` | left frozen, right sways (baseline) |
| 2 | `Start-Sleep 20; & $py $t\wuwa-test.py lod-inputs --pid <pid> --seconds 30 --view-uniforms --state-swap --output $s\far-e3` | **which eye is frozen now?** Watch the whole 30 s |
| 3 | After the command ends, watch 20 s | does the freeze return to the left? |
| 4 | Recorded copy of step 2: `& $py $t\wuwa-test.py state-swap --pid <pid> --seconds 60`, then at once Launcher.exe → Record gameplay → Start, 20–30 s, Stop. The window ends by itself at 60 s (or run it again with `--seconds 0`) | same question, now on video |
| 5 | Optional, near position: repeat step 2 to `$s\near-e3` | both should sway |

`state-swap` releases the control lock when it exits, so the Launcher recording can run inside
the window. The `lod-inputs --state-swap` trace holds the lock, so it cannot be recorded.

## 4. Check, then read

```powershell
& $py $t\wuwa_eye_diff_summary.py --conditions $s\far-e0\lod.jsonl $s\far-e3\lod.jsonl
Get-Content $s\far-e3\lod-inputs.json | Select-String '"status"|"error"'
```

The swap counts only if all of these hold:
- `states exchanged: ...far-e0... carries the states of ...far-e3... the other way round`;
- in `far-e3`: `state swap: requested; applied A -> B, restored B; faulted=False`, with B > A and
  restored equal to applied;
- slot passes still `2/3` (E3 moves only the state);
- `lod-inputs.json` status `captured_inputs_visual_review_pending`, with no error.

The external production's tag is the `uniform producers: slot… external` line in far-e3,
compared with far-e0.

| Headset, far (step 2 or 4) | External production in far-e3 | Reading | Next |
|---|---|---|---|
| **Right** eye frozen, left sways, and step 3 returns it to the left | slot 1 | The view state carries it, and the extra per-frame render follows state A (the fresh one) | Find what that render updates in state A and why B never gets it (a trace of that render's pass) |
| Right eye frozen | slot 0 | The state carries it through its own history; the extra render follows the first submission | A history reset for B only, as a timed diagnostic |
| **Left** still frozen | either | Not the state: the capture target, or game-thread logic keyed on the first view | E4 (target exchange), or a reversed-order trace to see where the extra render goes |

A swap only moves the freeze: it is a diagnostic, not a fix. Send back:
- the folders, the recording and the `--conditions` output;
- one line per step: which eye was frozen.

Leave everything at the defaults afterwards. Nothing in this session changes the profile.
