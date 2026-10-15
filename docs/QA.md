# Quality: what we check, when, and how

WuWa VR has to keep working across many states (full VR, stereo screen, mono theatre,
diorama, menus, story scenes), two game copies (Steam and the Kuro launcher), two runtimes
(simulator and headset) and three kinds of input. The game changes about every six weeks.
This page is the process. The data lives in two files:

- [`qa/matrix.json`](../qa/matrix.json): every feature, the states it must work in, how it is
  checked, and which source files it depends on.
- `qa/results.jsonl`: one line per checked cell (feature × state × copy/runtime/input), with
  the build, who checked it and when. Append-only; the newest line for a cell wins.

[`QA-MATRIX.md`](QA-MATRIX.md) is the grid generated from both. `python dev/qa.py --help` lists
the commands.

## Four kinds of check

| Kind | Runs | Catches | Example |
| --- | --- | --- | --- |
| `unit` | Offline, seconds, no game | Logic regressions in Lua and C++ helpers | `dev/test_view_chord.lua`, `dev/test_hud_refresh.lua` |
| `invariant` | Read from the UEVR log of **any** play session | Broken game hooks, an unshared eye camera, layout loops, cinema switching | `python dev/qa.py session <log.txt>` |
| `simulator` | Scripted run on the OpenXR simulator | Eye mismatches you can measure in pixels | Per-eye motion in the lantern shot (test 17j) |
| `manual` | A person in the game; the owner in the headset decides | Comfort, feel, anything a script cannot see | "Journey log fills the screen after a view switch" |

Invariants are the cheapest way to see what broke: every session you play produces them, at no
extra cost. When a fix is easy to state as a log line ("both eyes reused the shared camera"),
add that line to the code and an invariant to `qa.py`.

## Before a candidate goes to the headset

1. Unit tests and `python dev/i18n/check_all.py` pass.
2. `python dev/qa.py plan --build <id> --since <last tested commit> --out <candidate>\CHECKLIST.md`.
   The checklist has:
   - every cell of a feature whose code changed;
   - the smoke cells;
   - anything that failed last time or was never checked.
   It goes in the candidate folder next to `TEST THIS.txt`.
3. Simulator pass on both game copies: `python dev/qa.py session <log> --baseline <last good log>`.
   A non-empty "Refused routes" list or new error kinds stops the candidate.
4. In the headset, go through the checklist. Record each cell:
   `python dev/qa.py record ui-layout switch pass --build <id> --by owner --note "journey log, 3 switches"`.
5. `python dev/qa.py session <headset log> --build <id> --record` adds the invariants.
   `python dev/qa.py report` refreshes the grid.

## Release gate

A release ships when:
- every **smoke** cell passed on the release build, on both copies;
- no cell is ✗ without a written decision (known issue in the release notes, or a fix);
- the session report from both copies shows no refused routes beyond the known-off list.

[RELEASING.md](RELEASING.md) has the packaging steps. 1.2.1 shipped broken on Steam because
only the launcher copy was checked. The `copy` dimension exists to stop that.

## Fixing any "different in one eye" problem

Every eye mismatch so far has had one of six causes ([REFERENCE.md §1.4](REFERENCE.md)).
Here is a method that works for any of them, including ones we have not seen yet.

**The rule.** The second eye must be built from the same game inputs as the first. It may
differ only by its own eye position and projection. Each fix restores that rule for one input.
None of them makes the second eye look like the first after the fact.

1. **Pin the state.** Write down which views show it (full VR / stereo screen / mono), the
   scene, and whether it moves.
   - Only on the stereo screen: one of our own switches is off there. That was test 17j: the
     shared camera was disabled on the 2D screen.
   - Also in mono: it is in the game, not in the eyes.
2. **Measure, don't eyeball.** Record both eyes at the same moment: the simulator SBS capture,
   or the launcher recorder in the headset.
   - Motion: compare the per-eye motion (phase correlation over the same box in each eye). In
     test 17j it was 10.0 vs 10.1 px/frame once fixed. Before the fix, one eye moved and the
     other did not.
   - Static: compare the brightness of the two eyes over aligned crops.
3. **Diff the inputs.** `wuwa-test.py lod-inputs` records `eye_pair_diff`: every field that
   differs between the two eyes' views and view states. `wuwa_eye_diff_summary.py` sorts the
   fields into clocks, flags and pointers. Compare a failing capture with a near or control one.
4. **Swap tests.**
   - First toggle `NativeStereoFixSwapEyes` (REFERENCE.md §1.4). If the problem moves to the
     other physical eye, it follows render order. If it stays, it follows that eye's
     projection or data.
   - Swap the two eyes' view states (`wuwa-test.py state-swap`). If it moves, it lives in the
     per-eye state: cause 2 or 3.
   - IPD 0 with an asymmetric headset profile isolates cause 6.
5. **Name the cause, then use its standard fix:**

| Cause | Fix shape | Example |
| --- | --- | --- |
| 1 Default input on the second view | Copy the first eye's value | LOD factor (`lod_sync`) |
| 2 Cache filled for one eye only | Refresh both states | CLV one-frame refill |
| 3 Temporal history per state | Reset or share the history | Ruled out for AO |
| 4 Work done once per frame | Run it per view | Cinematic framing |
| 5 Game data read at different moments | Read once, share by construction | `WuWaStereoBasePose` |
| 6 Asymmetric projection in a screen-space pass | Symmetric projection while it runs | AO, god rays |

6. **Lock it in** so the bug cannot come back silently:
   - A log line that proves the fix is active, e.g. `[WuWaStereoBase] action=reused`.
   - An invariant in `qa.py session` that fails when the line is missing or says "refused".
   - A matrix row, or a new state on an existing row, with the scene that showed it.
   - A unit test for any logic.
   - A REFERENCE.md symptom entry naming the cause.

Every fix must also hold in **every view**. Test 17j existed because the shared camera was
written for full VR and switched off on the stereo screen. When a fix has an "only in view X"
condition, write down why. Add the other views to its matrix row so they get checked.

## Adding a feature

Add its row to `qa/matrix.json` in the same commit:
- `states`;
- `dims` if copy, runtime or input matter;
- `how`, in one or two sentences;
- `checks`;
- `code` globs, so `plan` knows when to retest it;
- `smoke: true` if a broken version should block a release.

`python dev/qa.py check` validates the file. `dev/test_qa.py` tests the tool.

## After a game update

See [GAME-UPDATE.md](GAME-UPDATE.md).
