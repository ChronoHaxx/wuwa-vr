# After a game update

Wuthering Waves gets a major update about every six weeks. WuWa VR patches game code at fixed
addresses (RVAs) and checks the bytes first. So an update has two possible outcomes.

- **Fail safe (the normal case).** Each native route checks:
  - the game build (PE timestamp);
  - the image size;
  - a hash of every code range it touches.

  On any difference it refuses and logs why. The game still runs in VR, but without that
  fix. Examples: the HUD floats in one eye, the far trees differ again. The Lua scripts work
  by reflection names. Where they use a raw offset (the LGUI scaler cache) they first check
  the class size and the field offset, and switch off if either changed.
- **Breakage without a refusal (rare, and the reason for this page).** A Lua script reads a
  property or class that was renamed. Or the game changes behaviour without changing the code
  we hash.

**Self-port (from 1.2.5).** A hotfix that only moves code no longer needs a release for the
converted routes: the LGUI HUD and menu redirect with its viewport rewrites, and the five stereo
code routes (mono authored camera, same-draw stereo camera, cinematic aspect metadata, completed
cinematic views, custom reflection capture). On a game build other than the reference the plugin
scans the loaded game once at startup (about half a second) for the fingerprints in
`utility/WuWaSelfPortTable.hpp`. It uses a site only on exact evidence, and a code range only when
every byte outside the moved call/jump targets and RIP displacements is unchanged. The log line
`[WuWaSelfPort] game build ... found N/M sites ... refused [...]` says what it found. Those routes
then log `self_ported_from=<reference>`. A route still on `base + RVA` keeps the strict build check
and refuses on any new build. Convert a route by taking every address from
`wuwa_self_port::at()/find()` and checking through `wuwa_code_check::verify_portable()` or
`wuwa_self_port::range_matches()/code_matches()`.

The 3.7 update (30 Sep to 1 Oct 2026) is the worked example used below:
- timestamp 0x6aa96d2d, image 0xad21000;
- the engine's views moved from 0xe8d0 to 0xe940 bytes apart, and view fields moved by
  0x40 to 0x70;
- the port tool mapped 82 RVAs, 64 function ranges and 12 signatures unchanged;
- 37 function ranges changed, and 40 sites were ambiguous or unmapped. All of these needed
  a person;
- the image shrank from 1 GB (3.6, 0x6a74963e) to 181 MB. That is why the routes still
  pinned to 3.6 now log `out-of-image range` or `readable=false`.

## Day 0: what broke (no build)

1. Launch the current release on the updated game, first on the simulator. Get into the world
   and open a menu.
2. Run the session report:
   ```powershell
   # PowerShell, in the repository
   python dev\qa.py session "$env:APPDATA\UnrealVRMod\Client-Win64-Shipping\log.txt" --baseline <last good log>
   ```
   - `game-build` shows the new timestamp, or "unknown image" for a new image size.
   - **Refused routes** is the list of what broke. Each line names the module, for example
     `[WuWaLguiRedirect]`, `[WuWaStereoCode] ... code mismatch`.
   - "verified in the baseline but not here" lists routes that never got checked. Either the
     feature was not used in this session, or its route no longer runs.
3. Go through the **smoke** rows of `qa/matrix.json` by hand (`python dev/qa.py plan --build
   <game version>-day0`). Record what fails. Users will report the same things first.
4. Post a short notice in your own words: what still works, what is off, and that a fix is
   coming. Pinned routes fail safe, so "playable, some fixes off" is the usual state.

## Day 1: port (no headset needed until the end)

1. **Capture the new game.** The backend only writes directly under
   `E:\Coding\wuwa-vr\extracted\lgui-live`; that folder must exist. So create it, write
   `{"output": "E:/Coding/wuwa-vr/extracted/lgui-live/client-<timestamp>-<copy>.bin", "any_build": true}`
   as `wuwa-module-capture.request.json` beside the loaded backend. That is the installed
   version's `app\runtime\Wuthering Waves UEVR - <build>` folder: the launch record's
   `build.json` names it. Then launch.
   - The capture starts about 30 s after the plugin loads; the world need not be loaded.
     The log says `module capture finished: status=complete`.
   - The request is read once per backend load, so a refused request needs a new launch.
   - Remove the request file afterwards. Move the capture and its JSON files to
     `data\game-<version>\`, then delete `extracted\` again.
   - The capture is read-only. The exe on disk is encrypted, so only an in-memory capture works.
   - `launcher\dev\wuwa_merge_captures.py` joins interrupted captures.

   A hotfix also needs this. Every rebuild of the exe changes the PE timestamp and moves code.
   For the 16 Oct 2026 hotfix (0x6ace6377), every one of the 211 sites mapped as `ok`.
2. **Map every pinned address.**
   ```powershell
   python launcher\dev\wuwa_port_offsets.py --old <current capture> --new <new capture> --report ..\data\game-<version>\port-<version>.json
   python launcher\dev\wuwa_offset_table.py --report ..\data\game-<version>\port-<version>.json --old <current> --new <new> --out ..\data\game-<version>\port-<version>-offset-table.json
   ```
   - `ok` sites have identical surrounding bytes.
   - Function ranges are re-hashed only when relocated displacements are the only difference.
   - `changed`, `ambiguous` and `unmapped` sites need a person.
   - The offset table shows struct offsets that moved; in 3.7, the view stride and fields.
3. **Review in priority order.** The most visible first, so that a partial port is already
   useful:
   1. The shared identity: `WuWaCodeCompatibility.hpp`, timestamp and both image sizes
      (Steam and launcher differ).
   2. HUD and menu routes (LGUI redirect, viewport rewrites, scaler).
   3. Same-draw stereo camera and `WuWaStereoBasePose`: one-eye shake comes back without them.
   4. Same Pass, LOD sync, CLV refresh, reflections, AO projection, god rays, cinematic framing.
   5. Diagnostics last. Stage-0 and world labels are pinned off anyway (`known_off` in the
      matrix).

   Apply with `--apply`, then review each `changed` range by disassembly. Re-hash it only after
   an instruction diff shows the change is harmless to us: stack-frame growth, or a field we
   do not read.
4. **Lua and reflection names.** Run the unit tests. Then in the game, check the classes the
   scripts look up:
   - LGUI canvas scaler size and field offsets (`wuwa_hud_refresh.lua`);
   - `UiView_Plot` and `SprPhoto` (story pictures);
   - the movie and plot-skip widgets (automatic cinema).

   `[Plugin] WuWaCinema:` status lines name the signal that is missing.
5. **Regenerate the self-port table** from the new capture, which is now the reference build.
   `utility/WuWaSelfPort.hpp` refuses to compile when the table and
   `WuWaCodeCompatibility.hpp` name different builds.
   ```powershell
   # PowerShell, in the repository (Python with capstone and numpy)
   python launcher\dev\wuwa_self_port_table.py --image <new capture> --header mod\uevr\src\utility\WuWaSelfPortTable.hpp
   python launcher\dev\wuwa_self_port_table.py --image <new capture> --simulate <old capture> --expect ..\data\game-<version>\port-<version>.json
   ```
   The second line runs the plugin's matching on the previous build. Every site it maps must
   agree with the port report (it may also correct one: on 16 Oct it found the LguiRoute slot
   the port tool got wrong). Then update `dev/test_self_port.cpp` to the new captures.
6. Rebuild, check-sums, register the candidate, launch on **both** copies:
   `python dev\qa.py session <log> --baseline <day-0 log>`. Every route the old build
   verified must verify again, and no new error kinds may appear.
   To exercise the in-game scanner on the reference build, put an empty
   `wuwa-self-port-force.txt` beside the loaded backend. Every found site must map to itself
   (`0 moved`), and the converted routes must still verify. Remove the file afterwards.

## Verify and release

1. `python dev\qa.py plan --build <candidate> --since <last release tag>`. Every smoke cell,
   plus every feature whose code the port touched.
2. Headset pass on both copies. Record the results, then `python dev\qa.py report`.
3. Update the `known_off` list in `qa/matrix.json` if a route is deliberately left pinned.
   Give the reason.
4. Release as in [RELEASING.md](RELEASING.md). Name the game version in the notes.

## Keeping the next update cheap

- Every new native route needs:
  - range hashes and a log line on success and on refusal;
  - a matrix row naming its source file in `code`;
  - for diagnostics, an entry in `known_off` once they stop being ported.
- Keep the current capture in the workspace's `data\game-<version>\` folder (outside the
  repository). The next port needs it as `--old`. For 3.7 that is
  `data\game-3.7\client-3-7-merged.bin`; never move it aside in a clean-up.
- Prefer reflection (names) to raw offsets in Lua. Guard any raw offset with a size or layout
  check that switches the feature off.
