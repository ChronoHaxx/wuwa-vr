# WuWa VR: how it works, how we got here, what comes next

Updated 1 October 2026, for game version 3.7. How the mod works, what was broken in stereo and why, and where to find things in the code. To install and play, see the [player guide](https://chronohaxx.github.io/wuwa-vr/guide.html); for known issues, its [problems section](https://chronohaxx.github.io/wuwa-vr/guide.html#recovery).

## A two-minute mental picture

WuWa creates a normal game world and camera. **UEVR** intercepts parts of Unreal's rendering so a VR runtime can receive two eye views plus headset tracking. Our changes adapt WuWa's unusual rendering paths, extract its real UI, and add camera/controller conveniences.

```text
Portable launcher --> apply selected profile --> injector --> WuWa + UEVRBackend.dll
                                                          |
                         Lua: camera, body, controls <-----+
                                                          |
                      native hooks: scene + real game UI --+
                                                          v
                                    OpenXR / SteamVR --> two eyes / headset
                                                          |
                              optional recorder --> video + diagnostic sidecars
```

The browser dashboard is a **local controller for installed software**. A website alone cannot inject the DLL into the game. OpenXR is an interface; the simulator and headset runtimes are implementations of that interface. Switching a runtime does not rebuild the mod. A downloaded DLL cannot replace code already loaded into a running game: a restart is necessary.

With **Native Stereo Fix**, UEVR asks the game to render the scene twice per frame, once per eye. Both renders need the **same game decisions** (which detail level, which lighting, which reflection), with deliberately different eye positions and projections for depth. Most of October's fixes were places where WuWa made a decision for the first eye only. Copying one eye's finished image into the other would hide those differences but destroy proper stereo, so each fix corrects the decision instead.

## Timeline

| When | What happened |
|---|---|
| Early Sep | Studied WuWa's UI rendering (LGUI) and started from UEVR nightly 01143. |
| 19–26 Sep | Built the real-game HUD in VR, first person, freecam, the 6DOF window, Xbox shortcuts, recovery tools, translations and recording. Published the first public beta and this website. |
| 27 Sep | Native Stereo Fix turned out to repair one-eye character materials; it became the basis for later work. Indath's tip to disable one-frame thread lag fixed jitter. |
| 27–28 Sep | First per-eye reflection fix for the Resonators screen. Fixed the brief eye mismatch after ultimates (confirmed in headset). |
| 28–30 Sep | Chased distant trees that froze in one eye with a series of diagnostic builds; each ruled out one cause. |
| 30 Sep | Game patch 3.7 broke the HUD. HUD, menus, the ultimate camera fix and Same Pass were ported to 3.7. |
| 1 Oct, 11:30 | Found the far-tree cause (second eye built with the default 90° FOV) and fixed it; confirmed in headset. |
| 1 Oct, 18:17 | Fixed far lighting (lighting volume filled for one eye only) and ported the Resonators/team-screen reflection fix to 3.7. |

## Where things stand

**Working on game 3.7:** stereo view and head tracking; the real game HUD and menus in VR; first person, freecam and the 6DOF window; Xbox shortcuts; the end-of-ultimate camera fix; far trees, props and lighting matching between the eyes; Resonators and team-screen reflections.

**Open:** weapon and Echo submenu reflections, and full-animation first-person aiming. Far lighting after a long walk with no loading screen has not been measured. Not yet ported to 3.7 and not known to cause visible problems: the shared scene-frame counter and the water stereo diagnostics.

## Three stereo bugs, explained

Each of these made the two eyes disagree about something the game decides once per view.

**Far trees froze in one eye (LOD).** A distant object switches to cheaper versions of itself (levels of detail, LOD); the cheapest ones skip wind animation. The game built the second eye's view with the default 90° field of view instead of the camera's 75°, so its LOD distance factor was 1.0 instead of 0.836: far objects counted as further away in that eye and switched sooner. Near, both eyes animated; very far, both froze; in between, only one eye froze. **Fix:** before both eyes render, the second view takes the first view's LOD factor and FOV. Setting: *Match far-object detail between eyes*.

**Far objects were darker in one eye (lighting).** WuWa lights distant objects partly with a Cascade Lighting Volume (CLV): camera-centred cascades of stored indirect (bounce and sky) light. Each eye keeps its own copy in its *view state*, the per-eye memory the renderer keeps between frames. When the game rebuilds the volume (start, loading, teleport), only the eye rendered first gets the far cascades filled. **Fix:** after the game starts, after loading screens and after teleports, the mod asks for one refill in a frame that renders both eyes. A refill causes a ~0.2 s hitch, so automatic refills are at least 20 s apart. Setting: *Match far lighting between eyes*, plus a *Refill far lighting now* button.

**Character-screen reflections slid outward (projection).** The Resonators and team screens render a mirror image of the character. WuWa's reflection code rebuilds the mirror camera with a centred, symmetric projection; a headset eye's projection is off-centre, so each eye's reflection slid outward and was too large. **Fix:** the mirror capture keeps each eye's own projection, each eye gets its own reflection history, and the mirror camera is refreshed for each eye.

### How they were found

- **Diff everything, including what never changes.** The LOD cause sat in raw view snapshots for days; an earlier comparison had filtered out "constant" differences, and the FOV difference is constant.
- **Swap, then bisect.** Swapping the eyes' view states showed the lighting problem lived in per-eye state. Switching one render setting at a time (with automatic restore) and scoring the left/right brightness gap of far objects narrowed ~40 settings to indirect light, then to the CLV and its refresh.
- **Measure instead of asking.** The simulator's window can be captured and scored: motion of a far canopy for the LOD bug, an eye-aligned brightness gap for the lighting bug. The owner only had to stand still.
- **Ask about distance and angle early.** The owner's observation that the mismatch appears only in a middle distance band was the decisive clue for the LOD bug.

## Game updates

Game patches move code and data. The mod refuses features whose code no longer matches byte for byte, so a patch disables them rather than crashing. Porting compares a capture of the old game with the new one: each hooked function is found again by its surrounding bytes and callers, data offsets are translated from aligned instructions, and anything that changed beyond moved addresses is reviewed by hand. Patch 3.7 moved the view object from 0xe8d0 to 0xe940 bytes and shifted several view fields.

## Codebase cheat sheet

Public paths below are a **source overlay**, not a self-contained UEVR fork. `mod/BUILD.md` explains reconstruction against pinned upstream versions. Release tags identify the exported code.

| What you want to understand/change | Public path / starting file |
|---|---|
| Portable EXE bootstrap | `launcher/dev/portable/launcher-stub.c` |
| Dashboard, build selection, profile backup/apply and launch | `launcher/dev/wuwa-player.html`, `wuwa_player.py`, `start-wuwa-build.ps1` |
| Real game HUD/menu extraction | `mod/uevr/src/utility/WuWaLguiRedirect.hpp` |
| Stereo view/camera interception | `mod/uevr/src/mods/vr/FFakeStereoRenderingHook.cpp` |
| Far LOD fix, Same Pass, view-state swap diagnostics | `mod/uevr/src/utility/WuWaShadowPass.hpp` |
| Far lighting fix (CLV refill) | `mod/uevr/src/utility/WuWaClvRefresh.hpp` |
| Character-screen reflection fix | `mod/uevr/src/utility/WuWaReflectionCapture.hpp`, `WuWaReflectionHistory.hpp`, `WuWaReflectionProjection.hpp` |
| Game-version checks | `mod/uevr/src/utility/WuWaCodeCompatibility.hpp`, `WuWaCodeCheck.hpp` |
| Mod controls/settings | `mod/uevr/src/mods/vr/WuWaControlsComponent.cpp` |
| First person, body/head visibility, controller camera behaviour | `mod/lua/`, starting with `01_WuWaVR_Comfort.lua`, then `02_WuWaVR_PolarControls.lua` |
| In-game test control (timed render-setting switches, captures, refills) | `mod/uevr/src/utility/WuWaTestControl.hpp`; client `launcher/dev/wuwa-test.py` |
| Stereo measurement tools | `launcher/dev/wuwa_light_batch.py`, `wuwa_clv_check.py`, `wuwa_motion_halves.py`, `wuwa_bench_run.py` |
| Porting to a new game version | `launcher/dev/wuwa_port_offsets.py`, `wuwa_offset_table.py`, `wuwa_merge_captures.py` |
| Recorder and replay | `launcher/dev/record-wuwa.py`, `recording-replay.html`, `steamvr-capture/` |
| One-build beta packaging | `dev/package-beta.py` |
| Translations and website | `mod/localization/`, `launcher/localization/`, `site/`, `dev/build-site.cjs` |
| Investigation log | `docs/STEREO-CULLING-PROGRESS.md` |

### Terms without the jargon

| Term | Meaning here |
|---|---|
| Native Stereo / NSF | Stereo rendering mode / an additional UEVR compatibility fix that renders each eye separately. These are separate switches. |
| View / view family / view state | One rendering camera's data / a group of views rendered together / the per-view memory kept between frames (histories, caches). |
| LOD / culling / impostor | Cheaper distant model / skip invisible work / a cheaper image-like stand-in. |
| Indirect light / CLV | Light bounced off surfaces and from the sky / WuWa's Cascade Lighting Volume, which stores it around the camera. |
| WPO / vertex factory | Shader-driven vertex movement, such as wind / the code that supplies geometry inputs to shaders. |
| Uniform buffer | A packet of shader inputs such as matrices, time and camera position. |
| LGUI / UI quad | WuWa's UI rendering system / the surface or layer presenting UI in VR. We redirect actual game UI, not a recreation. |
| IPD / projection | Eye separation / how 3D coordinates map into each eye's image. |
| Build / profile / runtime | Compiled program / supplied configuration and scripts / the VR implementation connecting the application to its display. |

### Working on the code

Follow **one feature** from its setting to its implementation and test, rather than reading the whole renderer hook. A native header change can rebuild many files, and a new DLL needs a game restart. Passing compilation proves compilation, not correct stereo: check rendering changes with a capture of the affected scene, scored automatically where possible. Build steps and contribution notes are on the [developers page](https://chronohaxx.github.io/wuwa-vr/developers.html).
