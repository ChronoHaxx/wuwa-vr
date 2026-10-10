# WuWa VR reference: symptoms, causes and settings

Started 9 October 2026, game 3.7, UEVR base `4ee5c6b`. This is the page to read
**before** building an experiment. It covers everything that can go wrong in WuWa
under UEVR, not only what has been fixed: what you see, what in Unreal or UEVR
causes that kind of problem, which existing settings control it, what the
community has found, and where this project stands.

Twice a one-line setting beat weeks of custom work: turning off the one-frame
thread lag fixed jitter, and the far-foliage bug was a plain field-of-view
mismatch. So each entry lists settings to try first, in order.

**Status words.** **Fixed** (with date and evidence), **Workaround** (avoids the
symptom, removes something), **Open** (understood or not, no fix), **Untested**
(a reasoned lead nobody has tried). An unproven idea is labelled as a guess.

**Updating it.** When you test something, add one line to the entry: date, build,
setting, result. A failed test is worth recording; it stops the next person
repeating it.

## Part 1. How the pieces fit

### 1.1 One frame in Unreal

Each frame passes through three stages:

- **Game thread:** gameplay, camera, animation and UI layout.
- **Render thread:** turns the frame into views and draw lists.
- **GPU:** draws them.

By default the game thread may run one frame ahead of the render thread
(`r.OneFrameThreadLag 1`). In VR that lets the headset pose and the game camera
come from different frames, which shows up as judder. The profile ships
`Engine_r.OneFrameThreadLag=0` in `cvars_data.txt` (Indath's suggestion,
27 Sep). The cost is less overlap between threads, so CPU-heavy scenes lose
some frame rate.

### 1.2 Views, families and view state

| Unreal type | What it is | Why it matters here |
| --- | --- | --- |
| `FSceneView` | One camera's matrices and settings for one render | Each eye is one view. |
| `FSceneViewFamily` | The views rendered together in one `BeginRenderingViewFamily` call | Code that runs "once per family" may run for only one eye. |
| `FSceneViewState` | Memory a view keeps across frames: temporal AA history, AO history, eye adaptation, occlusion history, Kuro's CLV lighting cache | Each eye has its own. Anything stored here can drift apart between the eyes. |

Indath's "a2/a3 family" remark (9 Oct) uses decompiler names: `a1`, `a2`, `a3`
are a function's unnamed arguments, and "family" is the view family. He meant:
find the function that draws the overlay and use its arguments to tell which
eye it is drawing for.

### 1.3 How UEVR makes two eyes

UEVR replaces the engine's stereo interface (`FFakeStereoRenderingHook`) so
Unreal believes a headset is attached. The rendering method decides how the two
eyes are produced.

| Setting | Shipped | What happens (from UEVR source) |
| --- | --- | --- |
| `RenderingMethod` = Native Stereo | yes | Unreal's own stereo path. One family holds two views, drawn side by side into a double-wide target (headset width x 2). Both eyes share one game tick. |
| `NativeStereoFix` | **on** | UEVR splits the family. The renderer runs twice per frame with one view each; eye one goes to the normal target and eye two to UEVR's scene-capture target. For other games UEVR decrements the scene's frame counter before the second pass so motion vectors match; for WuWa, `wuwa_scene_frame` rewinds it instead. This fixed materials that drew in only one eye. The cost: everything the renderer does per frame now happens twice, and game code that assumes one render per frame can misbehave on the second pass. |
| `NativeStereoFixSamePass` | on | Upstream marks the second view as a primary pass, so Unreal does not skip "secondary eye" work. WuWa numbers its eye passes 2 and 3, and changing them at construction crashed the game on 24 Sep, so the upstream edit is skipped. Instead `wuwa_shadow::PassScope` sets the second view's two pass fields (`+0xc90` and its copy `+0x1a0`) from 3 to 2 **only around that view's own submission**, then restores them. WuWa's primary-view test counts passes 0 and 2, so the second eye now gets its own shadow setup (missing shadows in one eye, fixed 20 Sep). `WuWaEarlyStereoViews` is a separate, experimental attempt (off). |
| `NativeStereoFixSwapEyes` | on | Renders the eyes in the opposite order. This is why a one-eye bug shows in the **right** eye in our profile and the **left** eye in Indath's build. |
| `RenderingMethod` = Synchronized Sequential | no | Each eye is a whole frame. On the second frame the game skips its tick (Skip Tick) or its draw (Skip Draw, the default), so both eyes show one game moment. That halves the frame rate per eye. **The community's default from May 2024 into 2026**, because native stereo crashed: lobotomy traced that to the game's SDK process checking the resolution (2025). UEVR's FPS overlay counts both eye frames, so it reads double in this mode (markmon, Apr 2025). Not tried in this project. |
| `RenderingMethod` = Alternating/AFR | no | Eyes alternate frames with the game ticking in between, so moving things differ between eyes. `GhostingFix` applies only here; in Jan 2025 lobotomy found it crashed WuWa at start-up. |
| `ExtremeCompatibilityMode` | off | A broad fallback. markmon and lobotomy (Dec 2025) describe what it costs: it switches off much of UEVR, renders alternate frames, and shows the game window's own image stretched to the headset. It hides some UI flicker and shadow differences, at about half the resolution. A workaround, not a fix. Several WuWa candidates require it off. |

Indath reported two more limits (1 Oct 2026):
- **Instanced stereo:** the game hard-codes Unreal's instanced stereo off. The
  `vr.InstancedStereo` setting exists in the executable, but by his account
  switching it does nothing.
- **Without the Native Stereo Fix:** the second eye is only a back-buffer copy,
  not a real second view.

So every proper per-eye fix goes through the Native Stereo Fix.

### 1.4 The six causes of "different in one eye"

Every per-eye bug so far fits one of these causes. Name the cause first; it tells
you what to measure.

| # | Cause | Where we hit it | What to check |
| --- | --- | --- | --- |
| 1 | A per-view input left at its default for the second view | 1 Oct: the second eye was built with the default 90° FOV, not 75°, so its LOD factor was 1.0, not 0.836 | Diff every field of both views ([EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md)) |
| 2 | A per-view-state cache filled for only one eye | 1 Oct: Kuro's Cascade Lighting Volume (far indirect light) | Swap the eyes' view states; force the cache to refresh |
| 3 | Temporal history kept per view state | Suspected for AO (open). It was also seen in Synchronized Sequential in Oct 2025, so it is not caused by the Native Stereo Fix. | Turn off only the temporal part and see whether the eyes agree |
| 4 | Work done once per frame, or only for the first view | 4 Oct: cinematic letterbox framing; suspected for the dialogue bars (open) | Does the problem move when you swap eye order? |
| 5 | Game-thread data read at different moments for each eye | 28 Sep: camera base during ultimate returns (`WuWaStereoBasePose`) | Log the camera inputs for both eyes in one frame |
| 6 | Screen-space effects with asymmetric eye projections | Community: Symmetric horizontal projection fixed UI parts drawn in one eye (polar, Nov 2024), and fixed many one-eye post-process artifacts in native stereo (SannpoKun, Dec 2025) | Projection overrides (section 1.5) |

**First test for any one-eye problem: toggle `NativeStereoFixSwapEyes`.** If the
problem moves to the other physical eye, it follows render order: causes 1 to 4.
If it stays with the same physical eye, it follows that eye's projection or data:
causes 5 or 6. This is an inference from how the passes work, not a test we have
run on every bug.

### 1.5 Projection, field of view and distance

- **Eye frustums are asymmetric**, wider towards the outside. UEVR passes each
  eye's field of view from the VR runtime to Unreal.
- `HorizontalProjectionOverride` and `VerticalProjectionOverride` offer Default,
  Symmetric and Mirror. Symmetric widens each eye to a symmetric frustum and
  renders extra pixels; that can help effects that assume symmetry.
  `GrowRectangleForProjectionCropping` is the related crop option.
- **WuWa picks detail by field of view.** The help text beside
  `r.Kuro.DistanceLODBaseFOV` reads "DistanceToCamera *= (FOV / BaseFOV)". A view
  with a wider FOV therefore counts every object as further away and picks
  cheaper detail sooner. This is the formula behind the 1 Oct far-foliage bug:
  75 / 90 ≈ 0.83, against the 0.836 LOD factor we measured. Related settings:
  `r.Kuro.EnableKuroDistanceLOD`, `r.Kuro.DistanceLODFOV` and Unreal's
  `r.ViewDistanceScale.FieldOfView*`. A headset's view is wider than the game's
  75°, so far detail can drop sooner than on a monitor. These are untested knobs
  for pop-in.
- **Scale and near plane:** `WorldScale` (with `vr.WorldToMetersScale` in the
  game) and `CustomZNear` with `EnableCustomZNear`.

### 1.6 The HUD and menus

- WuWa draws its UI with **LGUI**, a third-party Unreal UI plugin that Kuro has
  modified, not UMG/Slate. UEVR's built-in UI capture therefore misses it.
  `WuWaLguiRedirect` and `WuWaLguiMenuRedirect` send LGUI's draws into UEVR's UI
  texture. The hook points were first observed in Indath's 8 Sep build.
- **Two different sizes** (from UEVR source):
  - The eye target is headset width x 2 by headset height.
  - The UI texture is the **desktop back-buffer size**, which is the game window.
  - When the window or swapchain is rebuilt (`Framework::on_reset`), UEVR
    recreates the UI texture at the new size and logs `UI size changed,
    recreating`.
- **The VR panel's shape:** `UI_Size` is the quad's **height**; its width follows
  the UI texture's aspect. If LGUI lays out for one aspect and the texture has
  another, the HUD looks squashed or stretched.
- The executable also contains Kuro's own aspect logic,
  `UUISizeControlByAspectRatio`. That is a name only; its behaviour is unknown.
- **The game sizes its own VR buffer.** In August 2024 lobotomy found this line
  in the game's log at 1080p: "Resizing VR buffer to 2040 by 2116". He also saw
  the whole image stretched vertically. The game reacts to resolution changes
  itself, so treat window and swapchain resizes as risky.
- **The UI layout resolution matters for gameplay, not just looks.** Indath
  (7 Oct 2026) saw Rebecca's ultimate target wrongly when the LGUI canvas was
  sized to one eye instead of a normal 4K screen.

How the UI has been handled over time:
- **2024:** 2D screen mode, plus a mod that hides the HUD.
- **Feb 2025 onwards:** mirudo2's Lua UI fix moved the UI into the world as an
  object. It worked visually, but the button mappings were offset and it needed a
  gamepad-to-mouse plugin.
- **Sep 2026:** Indath and this project both moved to capturing LGUI's draws into
  UEVR's UI texture.

### 1.7 Input

- **The pad's path:** Windows XInput, then this project's hooks (passthrough,
  slot filter, VR-controller mixer), then UEVR (L3+R3 opens settings; controller
  emulation), then WuWa.
- **Re-polling:** Unreal only polls a slot that reported "disconnected" again
  after a `WM_DEVICECHANGE` message. That is why the Xbox pad once had to be on
  first. VR controllers test 2 fixes it.
- **VR controllers** use OpenXR action bindings. SteamVR keeps the Index system
  button for itself, hence the stick-chord toggle.

### 1.8 Where settings live, and testing without a new build

| Place | Holds | Applied |
| --- | --- | --- |
| `config.txt` in the profile | UEVR and WuWa options (`VR_*`, `UI_*`, `WindowMode_*`, `OpenXR_*`) | On load; saved when the UEVR menu closes |
| `cvars_data.txt`, `cvars_standard.txt` | Cvars UEVR holds at a value. We ship only `Engine_r.OneFrameThreadLag=0`. | Re-applied every engine tick |
| `user_script.txt` | One console command per line, e.g. `r.Kuro.Letterbox 0`; `#` and `;` start comments | Once, on the first engine tick after the profile loads |
| UEVR settings → CVars → **Spawn Native Console** | The game's own console (the `~` key) | Live |
| `launcher/dev/wuwa-test.py console <name> --value <v>` | Development harness | Live |

A cvar changing value does not prove the code you care about reads it; WuWa's
custom paths bypass some standard ones. Judge by the picture in both eyes.

UEVR's CVars panel has a fixed list. It includes `r.AmbientOcclusionLevels`,
`r.DefaultFeature.AmbientOcclusion`, `r.OneFrameThreadLag`, `r.MotionBlurQuality`,
`r.DepthOfFieldQuality` and `r.SceneColorFringeQuality`. Players change these
from there. "The AmbientOcclusion cvar, -1 to 0" in community reports is almost
certainly `r.AmbientOcclusionLevels`: it is in the panel, and -1 (automatic) is
its default. For one tester (Oct 2025), a panel change only took effect after a
UEVR update, so confirm the value actually changed.

The executable contains about 6,150 cvar names, 881 of them Kuro-specific.
`launcher/dev/wuwa_cvar_inventory.py` lists them with the strings stored beside
them; the help text is usually the one just before the name. Run it after each
game patch. Keep the output private; it is extracted from the game.

## Part 2. Settings index

Values are what the 1.1.2 profile ships. Defaults that are not in `config.txt`
come from the code.

| Setting | Shipped | What it does | Touch it when |
| --- | --- | --- | --- |
| `RenderingMethod` | Native Stereo | Section 1.3 | Comparing Synchronized Sequential for a per-eye bug |
| `NativeStereoFix` | on | Section 1.3 | Required by most WuWa fixes; off only to compare |
| `NativeStereoFixSamePass` | on | Section 1.3 (skipped for WuWa) | — |
| `NativeStereoFixSwapEyes` | on | Eye render order | First test for one-eye bugs |
| `SynchronizationMode` | Very Late | When UEVR samples the headset pose relative to the frame | Judder that the thread-lag cvar does not fix |
| `WuWaNativeFrameTiming` | on | Keeps OpenXR frame numbers in step when the Native Stereo Fix renders twice | Timing comparisons only |
| `UncapFramerate` | on | Removes the game's frame cap so the headset rate can be reached | — |
| `DisableHZBOcclusion` | on | Turns off Unreal's previous-frame depth culling, which culls wrongly for a second eye | Objects flicker at the edge of one eye |
| `DisableInstanceCulling` | on | Turns off GPU instance culling | Same |
| `DisableHDRCompositing` | on | Turns off Unreal's HDR UI compositing | HUD colour or brightness looks wrong |
| `DisableBlurWidgets` | on | Hides UI blur widgets | Blurred menu backgrounds |
| `GhostingFix` | off | AFR-only history fix | Only with Alternating/AFR |
| `HorizontalProjectionOverride`, `VerticalProjectionOverride` | Default | Section 1.5 | Screen-space effects wrong at eye edges |
| `OpenXR_ResolutionScale` | 1.0 | Eye resolution multiplier | Performance |
| `WorldScale` | 1.0 | Size of the world relative to you | Scale feels wrong. Third-person players in 2024 preferred 0.7–0.875 |
| `UI_Size`, `UI_Distance`, `UI_FollowView`, `UI_Y_Offset` | 1.8, 2.27, off, -0.46 | VR panel height, distance, head-follow, height offset | HUD placement |
| `CinematicFramingFix` | on | Shares the first eye's cutscene framing with the second | Unequal letterbox heights |
| `MonoTheatreMode`, `2DScreenMode`, `AutoCinema` | off | Manual and automatic screen modes | Cutscene fallbacks |
| `WuWaStereoBasePose` | on | One game camera pose for both eyes per frame | Ultimate-return camera |
| `WuWaLguiRedirect`, `WuWaLguiMenuRedirect` | on, on | HUD and menus into the VR panel | HUD missing or in the eye images |
| `WuWaKuroWaterStereo`, `WuWaStereoTranslucency`, `WuWaWorldLabelsStereo` | off | Earlier candidates for Kuro materials, translucency and world labels | Only if those symptoms return; they need specific setting combinations |
| `WuWaHideKuroReflections` | off | Removes custom reflections (workaround) | Comparisons only |
| `WuWaGamepadPassthrough`, `WuWaGamepadSlot`, `WuWaForwardWindowFocus` | off, all, on | Input isolation and focus forwarding | Input conflicts |
| `Compatibility_SkipUObjectArrayInit` and other `Compatibility_*` | off | UEVR start-up compatibility switches; endlessfalls' initialization work used `find_uobject` / `SkipUObjectArrayInit` | Start-up crashes only |
| `WindowMode_*` | off; plane 12 x 8 | The 6DOF window (portal) | Portal shape |
| `r.OneFrameThreadLag` | 0 | Section 1.1 | Must stay 0 |
| `SnapturnJoystickDeadzone` | 0.200001 | A leftover from mirudo2 and markmon's UI fix (Feb 2025): their gamepad-to-mouse plugin read 0.200001 as "menu mode". Snap turn is off and that plugin ships disabled, so it does nothing in our build. | Leave it, or reset to 0.2 when cleaning the profile |

## Part 3. Symptoms

### Rendering: one eye differs

**Character materials missing in one eye (Iuno's hair, Mornye's legs, Lynae's effects).**
**Fixed**, 27 Sep: turning the Native Stereo Fix on repaired them (owner test).
The older candidates `WuWaKuroWaterStereo` and `WuWaStereoTranslucency` stay off.

**Far trees and props frozen, or at a different detail level, in one eye.**
**Fixed**, 1 Oct (cause 1). The second eye now copies the first eye's LOD factor
and FOV. Measured in the simulator; confirmed in the headset. Kuro's distance
LOD scales distance by FOV / BaseFOV (section 1.5), so a default 90° second eye
saw everything as further away. If it returns, check the eye FOVs first, then the
`r.Kuro.DistanceLOD*` values.

**Far objects darker or flatter in one eye.**
**Fixed**, 1 Oct (cause 2). Kuro's Cascade Lighting Volume keeps indirect light
per view state, and its refresh filled only the first eye. The mod now pulses
`r.CLV.RefreshEveryFrame` for one frame after starts, loading screens and
teleports. Manual control: WuWa Controls → Refill far lighting now. Related
cvars: `r.CLV.TriggerRefresh`, `r.CLV.UpdateEveryFrame`.

**Shading on rocks and hills differs per eye (ambient occlusion).**
**Open**, and older than this project.
- **Reports:** Lelouche first reported it on 12 Oct 2025, in Septimont, using
  Synchronized Sequential. They reported it again on 9 Oct 2026 in our build, in
  Rinascita.
- **Workaround:** both times, setting AO from -1 to 0 in UEVR's CVars panel
  removed it. That is `r.AmbientOcclusionLevels` (section 1.8), which turns
  screen-space AO off entirely. It also confirms the fault is in screen-space AO.
- **Likely cause:** 3 (AO history kept per eye, or shared between eyes that take
  turns) or 2 (a Kuro AO cache). Because it happens in Synchronized Sequential
  too, the Native Stereo Fix is not the cause.
- **Which AO runs:** `r.AmbientOcclusion.Method` selects SSAO (0), GTAO (1) or
  XeGTAO (2). Read its value first; only that method's settings matter.
- **What to test:** `launcher/dev/wuwa_ao_batch.py` (branch `claude/ao-eye-mismatch`)
  flips 21 AO cvars one at a time while you stand at the rocks, and scores how
  different the eyes are:
  - Kuro: `r.AOKuroGhostFix`, `r.AOKuroDownsampleMethod`;
  - Unreal: `r.GTAO.TemporalFilter`, `r.GTAO.SpatialFilter`,
    `r.AmbientOcclusion.Denoiser.TemporalAccumulation`, `r.XeGTAO.Denoise`, and
    others.
  - The list also includes `r.UsingDynamicCacheForKuroGTAOSpatialX`; its help
    text says it is a MediaTek phone hint, so expect no effect on PC.
- **The fix we want:** the one setting that makes the eyes agree with AO still on.

**Distant NPC rim light or specular differs.**
**Open**. NPC rim suppression is an optional workaround that also removes
intended nearby rim light.

**Reflections on the Resonators and team screens.**
**Fixed** 1 Oct (per-eye mirror projection). The weapon and Echo submenus are
**open** and deferred.

**Camera jumps differently in each eye at the end of an ultimate.**
**Fixed** 28 Sep (cause 5, `WuWaStereoBasePose`). Owner-confirmed in the headset.

**Shadows missing in one eye (walls, pillars, bridges).**
**Fixed** 20 Sep (game 3.6), ported to 3.7. WuWa treats the second view (pass 3)
as secondary, so under the Native Stereo Fix it never got the shadow setup.
`PassScope` presents it as pass 2 only around its own submission (section 1.3,
Same Pass).
- **How it was isolated:** `sg.ShadowQuality 0` hides the mismatch.
- **Community history:** players saw the same in native stereo: "shadows partly
  missing in the right eye" (Jul 2026). The common workaround was shadows on
  Low; Lelouche notes "Low means off in this game" (Aug 2026). Indath's build
  had flicker and darkness with a similar patch. On 6 Oct 2026 he traced that to
  a gamma vtable his build did not always find.
- **Other relevant settings:** `r.Shadow.CSM*`,
  `r.kuro.EnableKuroCustomShadowDepthPass`, `r.Kuro.CharacterShadow.*` and
  `r.kuro.EnableSequenceShadowFix`.

**NPC names or speech bubbles in one eye.**
**Fixed** 26 Sep in this project. Earlier, in native stereo, players saw NPC
names in only one eye from a distance (Dec 2025). `WuWaWorldLabelsStereo` is an
older candidate, for the setup without the Native Stereo Fix only.

**Whole right eye black in native stereo.**
**Not reported in our build.**
- **Community workaround:** turn off **volumetric fog** in the game's graphics
  settings (SannpoKun and Ryebread, Nov–Dec 2025).
- **Indath's builds:** the right eye also went black with the Native Stereo Fix
  on. He fixed it in Jul 2026 by reverting a scene-view/D3D12 code path. One eye
  went black again after a game patch in Aug 2026.

**Effects visible in only one eye (skill motion blur, glints, some particles).**
**Open** in part. Reports so far:
- **Skill motion blur** shows in the right eye only in native stereo (Lelouche,
  Dec 2025 and Jul 2026).
- **Lucilla's hat light effect** shows in one eye (Jun 2026).
- **Bright butterfly meshes** in Seven Hills flicker in the right eye even in
  Synchronized Sequential (SannpoKun, Dec 2025).
- **Our status:** we still see "some materials one eye only" (26 Sep).
- **Try first:** Symmetric horizontal projection (cause 6). Then check whether
  the effect moves with Swap Eyes.

### Cutscenes and letterbox

**Letterbox bars of different heights in each eye.**
**Fixed** 4 Oct (cause 4, `CinematicFramingFix`). Owner-confirmed in a simulator
replay; headset comfort is still to check. The game's letterbox is kept.

**Black bars in one eye during dialogue (Hsin scenes).**
**Open**, reported 9 Oct. A different problem from the one above: the bars stay
with the HUD hidden. They appear in our right eye (eyes swapped) and in the left
eye in Indath's build (not swapped). So they follow render order, not the
physical eye: cause 4. That is an inference from the two reports.
1. Try `r.Kuro.Letterbox 0`. The help text stored beside it in the executable
   reads "0: engine default 1: add custom postprocess letterbox". Use the console
   during the dialogue, or `user_script.txt`.
2. If that removes the bars from both eyes, decide whether to ship it.
3. Indath's alternative is to find the overlay draw and filter it by view family.

Mono theatre is the workaround.

**Wanting no letterbox bars at all.**
**Untested**: `r.Kuro.Letterbox 0` again. `r.Kuro.LetterboxMobile` is
mobile-only. Some bars may be drawn by the UI rather than by post-processing.

**Long scene or dialogue loading stalls.** **Open.**
- **Indath's build:** loads stalled at 10% with the Native Stereo Fix on. He
  switched the Fix off during loading screens and back on afterwards (Jul 2026),
  then added "suspend the Native Stereo Fix during menu loads" (Sep 2026).
- **Untested here:** whether doing the same shortens our stalls.

**Automatic cinema does not switch.** Experimental and off by default.

**Prerendered movie cutscenes are black (sound and subtitles play).**
**Untested in our build.**
- **What the community found:**
  - With mirudo2's Lua UI fix, flat movies were black in VR and in 2D mode.
    Turning the Lua script off fixed them (Lelouche and markmon, Nov 2025). The
    script was moving the movie's UI layer.
  - Separately, movies were black on DX12 but fine on DX11 with a Mar 2025 UEVR
    build (markmon).
- **What to test:** the game's opening movie, with `WuWaLguiRedirect` on and then
  off. To see the opening again, switch to another server region, as players did.
### HUD and menus

**HUD missing, or drawn into the eye images instead of the panel.**
LGUI redirect (section 1.6). Each game patch can break the hook points: 3.7 needed
a byte-matched port (29–30 Sep). Expect it after every patch.

**GUI squashed, about a third shorter than it should be.**
**Open**, reported 9 Oct. Mechanism from source (section 1.6):
- the UI texture is the size of the desktop window;
- the panel's width follows that texture's aspect;
- the texture is recreated when the window or swapchain is rebuilt;
- LGUI may keep laying out for the old size.

Indath's hint matches this: the UI is sometimes sized from one eye and sometimes
from the full render resolution, especially after a swapchain rebuild.

It is not unique to our build:
- **Native stereo:** in Jun 2025 lobotomy reported "weird UI scaling with
  vertical stretch" in native stereo.
- **Indath's capture:** his Sep 2026 LGUI capture showed "somewhat squished
  looking UI", and he found "no way to resize the UI anywhere in the code
  pipeline".

So the fix is probably the LGUI canvas size, not the panel shape.
- **What to test:** note the game window size. Check `backend.log` for
  `UI size changed, recreating` at the moment it squashes. Try ESC open and close.
- **Not verified:** whether WuWa Controls → Reset HUD aspect works. Do not suggest
  it to players until it is.

**A flat menu's background moves with your head.**
**Open**. Mono theatre is the workaround.

**ESC menu blurred with its buttons gone.**
Use L3+B (show game UI).

**The UEVR menu ignores the Xbox pad while VR controllers are on.**
**Fixed** in VR controllers test 2 (unreleased, 9 Oct): every controller steers
the menu at once.

### Comfort, timing and performance

**Judder or jitter.**
**Fixed**: `r.OneFrameThreadLag 0` plus `WuWaNativeFrameTiming`.
- **Who suggested it:** Indath suggested it here (27 Sep 2026). markmon had
  already turned it off in all his UEVR games for performance (Nov 2025).
- **If it returns:**
1. Confirm `cvars_data.txt` still has the line.
2. Then compare `SynchronizationMode` settings.

**Periodic SteamVR stutter.**
Needs the Native Stereo Fix on in 3.7; the 1 Oct SteamVR test was smooth.

**Low frame rate.**
- First try `OpenXR_ResolutionScale` and the game's quality settings.
- **DX11 or DX12:**
  - DX12 has better lighting and effects (November, polar). DX11 runs faster
    (markmon, Feb 2025).
  - Some old maps crashed on loading under DX12 (SannpoKun, Dec 2025).
  - Players turned ray tracing off for stability. SannpoKun's native launcher set
    `r.RayTracing.LoadConfig=1` in `Engine.ini` and forced DX11.
- **The game caps by hardware name:** it limits settings and frame rate by
  matching CPU and GPU names against a list (lobotomy, Apr 2025). UEVR uncaps the
  frame rate anyway.
- Temporal upscalers (`r.NGX.DLSS.*`, `r.XeSS.*`, FSR) keep history per view
  (cause 3). **Untested in VR**; compare both eyes before trusting them.
- Frame generation (`r.Streamline.DLSSG.Enable`) and Reflex predictive rendering
  are flat-screen features. **Untested**; we expect problems, so keep them off.

**Motion blur, depth of field, colour fringing or vignette feel uncomfortable.**
**Untested for WuWa.** Candidate settings:
- `r.MotionBlurQuality 0` (and `vr.AllowMotionBlurInVR`);
- `r.DepthOfFieldQuality 0`;
- `r.SceneColorFringeQuality 0`;
- `r.Tonemapper.Quality` (its levels include the vignette).

UEVR's CVars panel exposes most of these. The game's own settings may override
them.

**Far detail pops in too soon.**
**Untested**, and every option costs performance:
- `r.ViewDistanceScale`;
- `r.StaticMeshLODDistanceScale`;
- `r.SkeletalMeshLODBias`;
- the `r.Kuro.DistanceLOD*` settings (section 1.5).

**Objects flicker at the edge of one eye.**
UEVR already disables HZB occlusion. The game also has `vr.RoundRobinOcclusion`
("round-robin occlusion queries for stereo rendering", off by default). It
saves work by updating each eye's occlusion on alternate frames, so we would
expect it to make edge flicker worse, not better. **Untested.**

### Start-up and crashes

**Steam crashes at start-up.** **Fixed** in 1.1.2: the Steam overlay was sending
the first frames back into our DirectX hook in a loop.

**The game crashes as soon as UEVR injects, after a game patch.**
This has happened after several patches.
- **Game 3.0 (Dec 2025):**
  - The first workaround was `Compatibility_SkipUObjectArrayInit`, sometimes with
    `ExtremeCompatibilityMode` (drav, Jan 2026). It breaks `find_uobject`, and
    with it every Lua UI fix.
  - The real fix was endlessfalls' UEVR change (Jan 2026):
    `FEnumProperty::update_offsets` uses `FName::to_string_no_numbers()` instead
    of `to_string()`. The no-numbers version checks memory safely before reading,
    which avoids a hard crash. Submitted upstream as
    [praydog/UESDK#1](https://github.com/praydog/UESDK/pull/1).
- **Game 3.7 (Sep 2026):** Indath hit a crash at the same function and fixed it
  by validating UEnum candidates before use; the bad offset was `0x80`. The 1.1.x
  backend of this project also validates enum objects before reading their class
  metadata.

Copy diagnostics first. If `log.txt` ends in `FEnumProperty::update_offsets`, it
is this family of crash.

**"Out of video memory" when entering newer areas (game 3.3).**
**Community report, May–Jun 2026.**
- **Lelouche:** turning on `Compatibility_SceneView` gave a picture briefly, then
  this crash. Without it the screen stayed black.
- **zelesf:** the error went away for them after a later Kuro patch.

Not seen in our 3.7 build.

**Crash when leaving menus or submenus.**
- **markmon:** crashed about every 15 minutes when leaving menus (2025).
- **Indath:** fixed a submenu crash in his build (14 Sep 2026).
- **Photo mode:** taking a screenshot crashed with mirudo2's profile (2025).
- **Our build:** report it with the build name if it appears.

**After a game patch, the HUD, menus or camera fixes stop working.** The native
hooks match bytes in the game, and those bytes move. Re-port them; see 29–30 Sep
in [UNDERSTANDING-WUWA-VR.md](UNDERSTANDING-WUWA-VR.md).

### Input

**The pad does nothing until it is reconnected.** **Fixed** in VR controllers
test 2 (section 1.7).

**Index controllers have no Menu button.** Hold both stick clicks for one second
(test 2).

**LB+Y fails after Alt-Tab.** Players found a simple fix: **press Alt once**
after returning to the game (SannpoKun and Ryebread, Dec 2025). The game probably
still thinks Alt is held from the Alt-Tab, so LB+Y reads as a different combo.
That is a guess. Forwarding Windows focus events also helped; a universal fix is
not confirmed.

**A treadmill pad and an Xbox pad fight.** Use the slot filter or the VR
controllers' sharing style.

### Requests, not bugs

- **A closer diorama distance.** Planned: closer steps.
- **A thinner portal.** The "vignette" a tester mentioned is the L3 + LT portal.
  Planned: a Cinema shape preset; today, use `WindowMode_PlaneWidth`,
  `WindowMode_PlaneHeight` and `WindowMode_LockAspect`.

## Part 4. Community history

**Source:** the full Flat2VR Modding "Wuthering Waves" thread: 1,761 messages
from 23 May 2024 to 9 Oct 2026, exported on 9 Oct 2026 and kept privately by the
project owner. This is a summary, not a copy. Names are the display names used in
the thread; mirudo2 posts as polar.

| When | Who | What they found or made | Here |
| --- | --- | --- | --- |
| May 2024 | Sky Yuki, Drinking Herta | First reports. UEVR only injects very early because of the anticheat (Anti-Cheat Expert). Native stereo crashes; Synchronized Sequential works. The HUD is unusable. World scale about 0.7 feels right. | — |
| May 2024 | polar (mirudo2) | Custom UEVR Injector for early injection. praydog asked that UEVR not be bundled into other downloads, and that sources be public. | Our launch flow uses the Custom UEVR Injector; our backend source is public |
| Aug–Sep 2024 | lobotomy | The UI is LGUI (not UMG/Slate), and game logic runs on Tencent's Puerts (TypeScript). The game logs its own VR buffer size. LOD and shadows behave "as if you're a 100 m tall giant". | LGUI capture since Sep 2026; FOV/LOD mismatch fixed 1 Oct 2026 |
| Nov 2024 | polar | Profile that switches to 2D screen mode for UI. Symmetric projection fixes UI drawn in one eye. | Cause 6 |
| Feb–May 2025 | polar, markmon | WuWa UI Fix for UEVR (Lua): UI placed in the world, a closer camera, L3 combos (L3+A recentres the UI). markmon wrote the gamepad-to-mouse plugin for menus. Release 1.2 (May 2025) added freecam and a screenshot plugin. | Our control layout descends from this. `win_screenshot.dll` and a disabled `gamepad_to_mouse.dll`, both built by polar, ship in our profile. |
| Apr 2025 | jumpomaster | Native stereo with the Stereo Fix worked in Feb 2025 UEVR nightlies (shadows fixed in the 10 Feb build). Later nightlies blacked out the right eye. | — |
| Jun–Jul 2025 | lobotomy | Native stereo runs once the anticheat's SDK process is not scanning the resolution. First person works with a near-clip mask of about 20. | — |
| Oct 2025 | Lelouche | AO differs per eye on rocks (Septimont, Synchronized Sequential). Turning AO off fixes it. | Open |
| Nov 2025–Jan 2026 | SannpoKun | Profile 1.0 to 1.2, built on polar's: smoother UI attachment, a 2D/VR toggle (Back+RB), first-person mode, a native/sequential switch, volumetric fog off for native. | The project owner started from SannpoKun's 1.2 package in Mar 2026 |
| Dec 2025 | SannpoKun, Ryebread, kousakayou | Alt fixes LB+Y after Alt-Tab. Symmetric projection fixes post-process artifacts. NPC names in one eye at a distance. | Above |
| Dec 2025–Jan 2026 | drav, Lelouche, endlessfalls | Start-up crash after game 3.0. Workaround: `SkipUObjectArrayInit`. Fix: the `FEnumProperty` change, [praydog/UESDK#1](https://github.com/praydog/UESDK/pull/1). | Credited (endlessfalls) |
| Jan 2026 | markmon | Set the game's Flat2VR listing to "Works Poorly", because the UI was unworkable. | Lelouche suggested "Works Well" once the one-eye bars are fixed (9 Oct 2026) |
| Jul–Oct 2026 | Indath | Native Stereo Fix build (26 Jul), GitHub fork (1 Sep), LGUI hook (8 Sep), the one-frame thread lag suggestion (27 Sep), UEnum crash fix (30 Sep). Ported this project's LGUI capture (7 Oct). | Credited |
| Sep–Oct 2026 | ChronoHax (this project) | Native stereo with the Fix; LGUI capture; fixes for NPC names, ultimate camera, far LOD, far lighting and one-eye shadows, shared in the thread. | — |
| 9 Oct 2026 | Lelouche | AO again (Rinascita), GUI squashed, one-eye dialogue bars, closer diorama, thinner portal | Open, above |

## Part 5. Before writing code

1. Find the symptom here and name its cause (section 1.4).
2. Toggle `NativeStereoFixSwapEyes` and see whether the problem moves.
3. Try the listed settings live: console, UEVR CVars panel or `user_script.txt`.
4. For an eye mismatch, diff the two views field by field before any pipeline
   experiment ([EYE-DIFF-HANDOFF.md](EYE-DIFF-HANDOFF.md)).
5. Search the Discord archive and Part 4 for anyone who has seen it.
6. Only then write a hook, and record the result back here.
