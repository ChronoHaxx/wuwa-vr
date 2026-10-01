# WuWa VR: how it works, how we got here, what comes next

Updated 28 September 2026. This is a beginner's map of the project and an honest handoff, not a claim that stereo rendering is finished.

## Start here tonight

The **28 Sep 16:28 BST experimental package** includes the portable launcher EXE, its runtime folders, the older 26 Sep beta and a newer camera/diagnostic candidate. Extract the whole ZIP; the EXE needs its adjacent folders. You do not need to install Python.

1. Finish and close any existing game/injector session before applying a profile.
2. Open **WuWa VR Launcher.exe**, read the account-risk notice, then select **Camera candidate + trigger controls · 28 Sep 16:27**. The package initially selects the older beta: explicitly choose this candidate to test the new work.
3. Select your headset runtime or the bundled simulator. Start your headset software first. Use the standalone official WuWa launcher; Steam/Epic injection is still unverified.
4. Apply and launch; accept Windows' permission prompt, then press Play and enter gameplay.
5. Keep the supplied settings for the first comparison. Open the in-game shortcut sheet for the authoritative bindings; restore the supplied profile through the launcher if settings become confusing.

The new native DLL passed compilation and input-policy tests. The actual packaged EXE passed integrity, startup and refusal-to-overwrite-a-running-game checks. **This exact candidate still needs live trigger/inventory and closed-game apply/restore acceptance.** It adds testing tools; it does not contain a new foliage repair. It also carries the morning's same-draw camera-base candidate aimed at ultimate-transition desync; the project owner **confirmed on 28 September that the end-of-ultimate camera mismatch is fixed in headset testing for the cases tested**. The public download is an experimental checkpoint, not a stable release. The build label is 16:27; the ZIP was packaged at 16:28.

Unofficial injection carries anti-cheat/account-ban risk. This project is not approved by Kuro Games. Read the included risk and component-license notices. The website/independent guide's MIT license does not relicense UEVR or the whole bundle. Donations are optional and do not buy guaranteed support or future compatibility.

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

Both eyes need the **same instant of game state**, with deliberately different eye positions and projections for depth. Copying one eye's finished image into the other would hide disagreement but destroy proper stereo. The goal is consistent scene decisions with correct separate eye views.

## Timeline: the useful turning points

| When | Work and result | What that established |
|---|---|---|
| Early September | Investigated WuWa's LGUI render paths and ported work to UEVR nightly 01143, commit `4ee5c6b…`. | A pinned starting point; not a promise of tracking the newest upstream nightly. |
| 19–24 Sep | Built native UI redirection, adjustable HUD, first person, shortcut pages, freecam and the optional 6DOF window. Added rollback/checkpoint workflows. | The user found normal UI/gameplay increasingly usable; menu/reflection exceptions remained. |
| 24–26 Sep | Improved moving-character head placement, full-animation/comfort choices, hidden-head shadows, controller recovery, privacy masking, translations and recording. | The user confirmed several individual improvements, including the full shadow copy. This was not an all-character acceptance pass. |
| 26–27 Sep | Published source, website and a compact public beta. Added a portable EXE and clearer setup/recovery. | A shareable package, with attribution and known issues. The older beta supplied Native Stereo Fix off. |
| 27 Sep | The user found **Native Stereo Fix on** repaired affected character materials. Indath suggested disabling one-frame thread lag for jitter; later profiles adopted timing corrections. | Stronger evidence than speculative shader edits. It did not solve every shadow, foliage or camera problem. |
| 27–28 Sep | Reflection work improved the main Resonators page. Weapon/lower submenus still offset/freeze and were deliberately deferred. | Improvement on one page, not a complete reflection fix. |
| 28 Sep morning | Captured divergent game-camera inputs between native eye calls. Added a same-draw camera-base candidate while preserving eye offsets. Repaired an unsafe diagnostic hook using real branch-path tests. | Initially a compiled candidate; the owner subsequently confirmed the ultimate-return fix in headset testing that evening. The old diagnostic hook was unsafe, but a specific crash was not conclusively attributed. |
| 28 Sep afternoon | Agent-controlled menu navigation reached a character trial. Y activated a skill, not an ultimate. The older automation bridge lacked triggers. | Real controller automation works, but that skill clip cannot count as an ultimate test. |
| 28 Sep, 16:00–16:28 | Added bounded LT/RT input and a dormant read-only asset inventory; compiled, packaged, saved source and checked the launcher. | The next tests can be performed by the agent after a restart. These are tools, not another claimed foliage fix. |
| 28 Sep afternoon | Reproduced the same tree moving in both eyes nearby, then left-static/right-moving farther away, even during a zero-IPD retreat. | A repeatable distance-dependent failure. Ordinary eye spacing alone does not explain it. |
| 29–30 Sep | Diagnostic builds swapped view states, render targets, construction pass, eye index and eye pose between the eyes. Game patch 3.7 then broke the HUD; the HUD, menus, ultimate-camera fix and Same Pass were ported to 3.7 by matching the old code's bytes in the new game. | Each test ruled out one cause of the far-tree freeze. The 3.7 HUD, menus and camera fix were confirmed by the owner. |
| 1 Oct | A full field-by-field diff of the two eye views showed the second eye built with the default 90° FOV, so its LOD distance factor was 1.0 instead of 0.836. The second eye now copies the first eye's LOD factor and FOV before rendering. | **Far foliage/prop mismatch fixed**: measured in the simulator (left-eye canopy motion 2% → 67%) and confirmed in the headset. Still open: far lighting/fog differences between the eyes and the Resonators reflection. |

## What works, what remains open

**Observed improvements:** usable real-game UI in several screens; first-person tracking during sprint/grapple/flight reported much better; hidden head with a full character shadow reported working; recovery controls; Native Stereo Fix repairing the reported one-eye character materials; main Resonators reflection improvement. The simulator recordings and those user reports have different scopes—neither proves every headset, character or menu.

**Headset-confirmed on 28 Sep:** the end-of-ultimate camera mismatch is fixed for the owner’s tested cases. This is user acceptance, not a claim that every character or transition has been exhaustively tested.

**Fixed on 1 Oct (game 3.7):** far trees and props now use the same detail level in both eyes; see the next section for the cause.

**Still open:** far objects lit or fogged differently in each eye (they match up close), the Resonators / team-screen reflection position, and the deferred reflection submenus.

## Why a tree looked frozen in one eye

**Answer (1 Oct):** the game built the second eye's view with the default 90° FOV instead of the camera's 75°, so its LOD distance factor was 1.0 instead of 0.836. Far objects counted as further away in that eye and switched to cheaper, non-animated detail levels sooner. Near, both eyes animated; very far, both froze; in between, only one eye froze. The fix copies the first eye's LOD factor and FOV into the second view before rendering. The background below is how the investigation got there.

A distant tree may use a cheaper mesh, a flat impostor, different shader code, or a distance-based wind cutoff. Wind often moves vertices in the shader; the object's CPU transform can stay still. Therefore a static object transform does not prove a frozen animation clock.

The affected tree stays frozen on the left while its neighbor moves in both eyes. Moving closer makes that same target move in both; retreating restores the asymmetry. Setting the eye-position separation to zero throughout the retreat did not prevent it. Recorded main-eye projection scales also match. **A different mesh/shader path or different final shader inputs is a stronger lead than simply “the eyes are six centimetres apart.”** This is a hypothesis, not a confirmed root cause.

Two render-side candidates were found in retained near/far traces. The strongest uses different vertex-factory/shader groups per eye far away, matching groups nearby. It has **not** been matched to the visible tree. Its pointer is valid only as evidence from that recorded process; it is not a permanent asset ID or a UObject to dereference later.

## Experiments already tried: avoid another blind loop

| Experiment/evidence | Result | Limit |
|---|---|---|
| Native Stereo Fix enabled | User reports affected character materials repaired. | Other stereo defects remain. |
| One-frame thread lag disabled | Adopted after Indath's timing suggestion and user testing. | Not proof that all view/animation synchronization is fixed. |
| Cached mesh commands disabled | Target left canopy remained frozen. | Not an exhaustive test of every renderer cache. |
| Submission order reversed | The same physical eye still froze. | Makes simple draw-order explanations less persuasive. |
| Standard foliage LOD/culling and impostor comparisons | No established repair of the target. | Custom paths may bypass those settings; a CVar changing is not proof the target used it. |
| Shadow-quality comparison | No demonstrated foliage repair. | Does not settle the separate lighting issue. |
| Zero-IPD stationary and moving comparisons | Failure persisted; sampled CPU offset outputs coincided, including 399 retreat rows. | Four-call scopes did not satisfy strict two-eye pairing; this does not prove identical final GPU resources. |
| Near/far movement | Same tree fails far, recovers near. | Does not alone distinguish LOD, impostor, shader distance gate or cached state. |
| Main-eye projection comparison | Equal scale factors in retained samples. | Actual viewport dimensions, LOD distance factors and relevant temporal LOD inputs remain unrecorded. Auxiliary views have different projections: grouping by pass number alone creates a false mismatch. |
| Generic View/binding trace | Clocks advance; identified two representation candidates. | Trace is capped and sampled before final binding. Shared buffer addresses do not prove stale contents. |

## Next work, in order

1. **Run the prepared tools once.** Install the 16:28 candidate with the game closed. Confirm its trigger capability and asset-inventory endpoint, then collect the affected scene. The ultimate camera fix is now headset-confirmed; keep an ultimate return as a regression check after future rendering changes. Trigger-bridge acceptance is separate from the owner’s manual ultimate test.
2. **Name the failing asset.** Use the bounded nearby mesh/material inventory, landmark positions and FModel to identify the actual tree and representations. No game asset dump needs to be published.
3. **Join the visible failure to a draw.** Correlate the component/mesh with the relevant render primitive and exact shader permutation. Inspect the selected near/far representation and final View/material/instance inputs. This identity join is not done yet.
4. **Measure the missing LOD inputs.** Actual per-view dimensions, LOD factors and the active temporal-origin branch are still missing. Do not interpret inactive/default trace values as real decisions. One interesting source lead is the original full-width target versus a custom eye-width target; its causal role is unproven.
5. **Make the smallest demonstrated correction.** If representation selection is wrong, fix that policy while retaining per-eye projection and visibility. If the selected path is right but an input is stale/wrong, fix that input's ownership/update. Do not flatten stereo or disable wind/reflections and call it solved.
6. **One grouped acceptance pass.** Test the reproduced tree near/far and another tree, non-tree props, shadows, an ultimate, UI roundtrip, and first-person movement. Keep before/after captures and an easy rollback. Return to reflection submenus afterward.

FModel reveals asset/material structure; it does not prove what a live GPU draw consumed. RenderDoc/PIX or a narrower final-binding capture are possible next tools, **not completed tests**; compatibility, overhead and game/anti-cheat restrictions need evaluation. The NTE/custom-UEVR and community research has not supplied a verified drop-in fix. Avoid copying broad patches without understanding their target build.

## Codebase cheat sheet

Public paths below are a **source overlay**, not a self-contained UEVR fork. `mod/BUILD.md` explains reconstruction against pinned upstream versions. Public release tags identify the exported code; local recovery checkpoint IDs record provenance and may not be reachable in public Git.

| What you want to understand/change | Public path / starting file |
|---|---|
| Portable EXE bootstrap | `launcher/dev/portable/launcher-stub.c` |
| Dashboard, build selection, profile backup/apply and launch | `launcher/dev/wuwa-player.html`, `wuwa_player.py`, `wuwa-launcher.py`, `start-wuwa-build.ps1` |
| Real game HUD/menu extraction | `mod/uevr/src/utility/WuWaLguiRedirect.hpp` |
| Stereo view/camera interception | `mod/uevr/src/mods/vr/FFakeStereoRenderingHook.cpp` |
| One draw's shared game-camera base | `mod/uevr/src/utility/WuWaStereoBasePose.hpp` |
| Mod controls/settings and recorded camera/controller state | `mod/uevr/src/mods/vr/WuWaControlsComponent.cpp`, `mod/uevr/src/mods/VR.cpp` |
| First person, body/head visibility, controller camera behavior | `mod/lua/` — start with `01_WuWaVR_Comfort.lua`, then `02_WuWaVR_PolarControls.lua` |
| Bounded synthetic controller input | `WuWaInputSequence.hpp`, `WuWaInputSequenceBridge.hpp` under native `src/utility/`; `launcher/dev/wuwa_input_sequence.py` |
| Nearby reflected assets, not a renderer modification | `launcher/dev/diagnostics/wuwa_nearby_inventory.lua` and dormant request wrapper |
| LOD/View/mesh diagnostic records | `WuWaLodProbe.hpp`, `WuWaLodSnapshot.hpp`, `WuWaMeshBindingSnapshot.hpp`; Python analyzers in `launcher/dev/` |
| Recorder and replay | `launcher/dev/record-wuwa.py`, `recording-replay.html`, `steamvr-capture/` |
| Translations and website | `mod/localization/`, `launcher/localization/`, `site/` |
| Exact native changes and build pins | `mod/source-changes/`, `mod/checkpoint.json`, `mod/BUILD.md` |

For your private working checkout, `mod/uevr/` corresponds to `upstream/UEVR/`, `mod/lua/` to the packaged profile scripts / `dev/`, and `launcher/dev/` to `dev/`. `runtime/` contains runnable builds; `extracted/` holds frozen packages, recordings and private evidence; `_docs/HANDOFF-next-session.md` is the latest local handoff. Do not upload all of `extracted/`: it contains personal paths, gameplay identifiers and game-derived data.

### Terms without the jargon

| Term | Meaning here |
|---|---|
| Native Stereo / NSF | Stereo rendering mode / an additional UEVR compatibility fix. These are separate switches. |
| View / view family | One rendering camera's data / a group of views. Extra views need not correspond to a headset eye. |
| LOD / culling / impostor | Cheaper distant model / skip invisible work / a cheaper image-like stand-in. |
| WPO / vertex factory | Shader-driven vertex movement, such as wind / the code that supplies geometry inputs to shaders. |
| Uniform buffer | A packet of shader inputs such as matrices, time and camera position. Same address can hold updated data. |
| Temporal history | Previous-frame data used for effects; each eye needs correctly owned history. |
| LGUI / UI quad | WuWa's UI rendering system / the surface or layer presenting UI in VR. We redirect actual game UI, not a Lua recreation of its text. |
| IPD / projection | Eye separation / how 3D coordinates map into each eye's image. |
| Build / profile / runtime | Compiled program / supplied configuration and scripts / the VR implementation connecting the application to its display. |

### Reading, editing and testing efficiently

Start with the player guide and this map, then follow **one feature** from dashboard or setting → implementation → recorder/test. Don't start by reading the entire renderer hook. Small Lua/text changes are easier to understand than native rendering hooks; still keep a backup and follow the script reload/restart rules.

A native `.hpp` change can rebuild many C++ units because headers are included widely. A backend DLL change needs a game restart. Recent build delays also included excessive inner MSVC workers and a locked PDB; the latter was solved by relinking existing objects to a new symbol filename. Compilation passing proves compilation, not correct stereo.

For native builds, use the pinned instructions and keep both outer and MSVC inner parallelism bounded on this PC: `cmake --build out/build/x64-RelWithDebInfo --config RelWithDebInfo --target uevr --parallel 1 -- /p:CL_MPCount=1 /p:MultiProcMaxCount=1`. Keep the matching PDB privately. Do not overwrite a loaded DLL or copy random debug builds into the working profile.

For controller input changes, run the native input-sequence/bridge tests and Python recording/input tests. For the inventory, run its Lua fixtures. For packaging, verify the archive and actual EXE using isolated test AppData. For a rendering change, you still need **a recording of the failing scene**, not just passing unit tests or counters. The input tools use bounded plans, neutral release, focus/physical-takeover checks and explicit requests; ordinary recording does not play the game.

### A useful bug report

Include release/build ID, game version, VR runtime/headset, selected NSF/profile settings, character and location, the exact action, expected result and a short SBS clip showing both eyes. Say which eye fails and when. Keep UID/player names private and review footage before uploading. Use the site's feedback form to prepare a GitHub issue; it does not silently upload private logs or post for you.

For future rendering candidates, one combined regression report is enough: startup/controls, near/far foliage, ultimate return, menu roundtrip, first-person movement. Separate **fixed**, **still broken** and **not tested**. Keep the older 26 Sep public beta (included in this ZIP) for rollback.
