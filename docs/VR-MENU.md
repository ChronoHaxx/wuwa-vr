# VR menu design

Status: design for the `claude/vr-menu` branch, 10 Oct 2026.

## Goal

One settings menu that is comfortable in the headset and works with every input
at once:

- **VR controllers:** point with a laser and pull the trigger; or either stick,
  A or X to select, B or Y to close, the grips to switch pages.
- **Gamepad (Xbox or PS5):** D-pad or either stick moves, A selects, B goes back,
  LB / RB switch pages.
- **Mouse and keyboard:** click, scroll; arrow keys, Enter, Esc, Tab.

Whatever you used last decides how focus is shown. Nothing needs a mode switch.

The menu is also where playtesting happens. A test plan written on the PC (for
example by Claude) shows its current step in the headset. You answer with big
buttons, start or stop recording, or mark a moment. The result goes back to a
local file on the PC, together with the settings in effect. Nothing leaves the
PC.

## What exists today

- **UEVR's menu:** a desktop-style ImGui window with deeply nested sections. In
  the headset it is drawn on a quad.
- **UEVR's laser pointer** for that quad works on OpenXR, but only while UEVR's
  *Enable motion-controller input* is on. The WuWa profile turns that off, so
  players never get a laser.
- **Gamepad navigation** of the UEVR menu goes through ImGui's own navigation.
- **Developer playtests:** the dev launcher (`launcher/dev/wuwa_player.py`,
  `wuwa_playtest_bridge.py`) runs checklist sessions. The in-game client
  (`WuWaPlaytestControl.hpp`) can save Pass / Fail / Blocked / Not tested
  results and notes, and link the latest recording. Its panel uses dropdowns
  and typed notes, which are hard to use in VR.
- **Recording** is owned by the launcher (`WuWaVideoBridge.hpp`); the game only
  asks for it to start and stop.
- **Test control** (`WuWaTestControl.hpp`): local request/response files in the
  profile folder, used by PC-side test scripts (cvar leases, captures).

## Design

**Separate the view from the settings.** `utility/WuWaVrMenu.*` draws pages of
items from a plain model. Each item is a toggle, a choice, a slider, an action
or a line of text, with getters and setters. The menu knows nothing about
UEVR's mods. This keeps it drawable in `dev/imgui-preview` with sample data, so
the layout can be designed and checked without a headset.

**Its own navigation.** The menu reads discrete moves (up, down, left, right,
accept, back, previous / next page) and the ImGui mouse, rather than ImGui's
navigation. That keeps every input behaving the same:

| Action | Laser / mouse | Gamepad / VR controller buttons | Keyboard |
| --- | --- | --- | --- |
| Focus an item | Point at it | Up / down | Arrow keys |
| Toggle or run | Trigger / click | A (VR: A or X) | Enter / Space |
| Change a choice or slider | Click the arrows, drag the bar | Left / right | Left / right |
| Switch page | Click the tab | LB / RB (VR: grips) | Tab / Shift+Tab |
| Back / close | Close button | B (VR: B or Y) | Esc |
| Back to the setting's default | | X (Xbox, or VR controllers mode) | Delete / Backspace |

**Sticks give one direction at a time.** A push counts only when it is well
past centre and mostly along one axis (`StickReader`), so a slightly diagonal
push down never also changes a value. A held direction lasts until the stick
falls back toward centre. While this menu is open, VR controllers feed it the
analog stick rather than UEVR's per-axis D-pad presses, and holding RT no
longer switches UEVR into its camera-offset mode. The Camera page has the
offset sliders instead.

**Holding left / right speeds up.** A held direction repeats after 0.45 s,
every 0.18 s, then twice as often after 1.5 s and three times after 3 s. On a
slider each move also grows: 2 steps after 1.2 s, 5 after 2 s, 10 after 3 s,
never more than a tenth of the slider's range at once. Choices and toggles
always move one entry, so a held Step row never skips a test step.

**The laser and A never act twice.** In this menu the pointing hand's trigger
clicks where the laser points; A selects the focused row. (UEVR's own mapping
also clicks with A, which toggled a row twice.)

**VR controllers work in the menu even with VR controllers mode off.** Their
sticks and buttons are then read straight from the runtime, only while the menu
is open.

**Readable at arm's length.** Rows are at least 64 px tall at the base size.
Labels are short, and each item's longer explanation sits in a help strip at the
bottom for the focused item. The theme is black and gold, matching the launcher
and the site.

**Every setting says whether it is kept and what its default is (1.2.3).** A
second line in the help strip reads "Default: On. Saved for next time." (the
default in the row's own format: On / Off, the choice's name, or the number
with its unit), adding "X or Delete: back to the default." when the value
differs and the laser is not in use. Rows that last only this launch (diorama,
VR controllers, record video, treadmill run, what happened, Save changes for
next time) read "This launch only: not saved." The `setting_*` helpers fill
`default_text`, `is_default` and `reset` from the setting's own
`default_value()` (UEVR's accessor returned a non-const reference to a const
member and failed to compile once called; `mod/uevr/src/Mod.hpp` carries the
const fix). Custom rows (view, camera, slot, language, menu place) show no
default line.

**Save changes for next time** (Menu page, this launch only, default on).
UEVR saves `config.txt` whenever a menu opens or closes. Off,
`Framework::save_config` writes nothing (`WuWaConfigSave.hpp`, logged "Not
saving config.txt"), so settings can be tried freely and the next launch starts
from the saved ones; the header shows a red NOT SAVING badge (REC takes its
place while recording). Turning it back on saves the current settings at the
next open or close.

**The laser works for everyone.** While the menu is open and an OpenXR
controller is tracked, the pointer and trigger drive the menu, whether or not
*Enable motion-controller input* is on. Both quad hit tests (OpenVR overlay and
OpenXR framework layer) honour this.

**A visible beam (OpenXR).** OpenXR has no laser of its own, so the menu draws
one: a second quad layer, 8 mm wide, from 3 cm ahead of the pointing controller
to the hit point on the menu (or 1.2 m ahead when it points away), turned with
its flat side to the eyes. It samples a 32 x 4 px swatch that the menu draws in
the top-left corner of the UI image (soft at both sides); the menu's own quad
leaves those top rows out at the same pixel scale, and the hit test maps into
the remaining rows. Shown only while this menu is open and only when the menu
quad is placed in stage space (`UI_Framework_FollowView` off, the default).

**Nothing is lost.** *All settings (classic)* opens UEVR's full menu. Every new
page edits the same settings objects as the classic menu, so the two always
agree.

## Pages

Tabs shrink to fit the header; the title hides when they need the room. A red
REC badge with the elapsed time shows while recording.

1. **Quick:** view (full VR, stereo screen, mono theatre), camera, VR
   controllers, diorama, HUD size and distance, record video, treadmill run
   (its clock in place of On), recenter.
2. **Test:** only while a plan is loaded (see below).
3. **Camera:** the camera mode, then only the options for that mode: fixed
   distance and height; freecam style, speed, turn speed and collision; first
   person motion, body visibility and level horizon. Then, for every mode:
   world scale, the camera offset (forward / back, right / left, up / down in
   cm, and reset), the **diorama camera** (its own forward / back, right /
   left and up / down, default 5 m back and centred, and reset), and recenter.
   The diorama multiplies eye separation by its scale, so a camera that suits
   normal play puts the character too close and crosses the eyes there; while
   the diorama is shown (per draw, `Scale::shown()`), `VR::get_camera_*_offset`
   returns `VR_WuWaDioramaForwardOffset` / `RightOffset` / `UpOffset` instead of
   the normal offsets, which stay as they were. Holding RT in the classic menu
   nudges the diorama set while the diorama is on, and RT + B resets it.
4. **Cinema:** view, the game's own fix for cutscene black bars on the
   screens (Graphics > Cutscene Mode: Full Screen), close-up match and its closest
   distance, **depth of field (experiment)**, the cinematic scene note,
   automatic cinematic screen and what story scenes and story pictures use
   (see below), matched framing.
5. **HUD:** size, distance, **16:9 UI layout** (`VR_WuWaUiWindowSize`,
   default on; see below), **Character screen in the taller layout**
   (`VR_WuWaUiCharacterTall`, with 16:9), **Story scenes in 16:9** (`VR_WuWaUiStoryWindowSize`,
   default on; only with the taller layout), **HUD shape (full VR)** (`UI_WuWaHudShape`,
   default 1.33; shown only while the taller layout is in use), up / down, left / right, follow head, refresh
   layout, controls cards (on / off, keep open, size, height, distance) and
   view hints.
6. **Controllers:** VR controllers on / off with a live status line (which
   slot Automatic chose), **Sharing with Xbox / treadmill**, **Controller
   slot** (Automatic first), the button map, then **Treadmill**: the run
   switch, the run line (time, walking time, distance in the game) and **Start
   and end runs automatically** (`WuWaRun_Auto`), and the laser status. Motion
   controls will go here.
7. **Comfort:** decoupled pitch (and its HUD adjustment), smooth camera turns
   and tilt, walk by default, L3 + A also resets position.
8. **Fixes:** matched far detail, **Full ground detail**
   (`WuWaStereo_FullTerrainDetail`, from 1.2.5), **Hide cutscene bars in full
   VR** (`WuWaStereo_HideCutsceneBars`, default on from test 21: with Cutscene
   Mode on letterbox the game draws the bars as two textures,
   `BlackTexture`/`BlackTexture1` in `UiView_BlackFadeScreen_Prefab`;
   `mod/lua/wuwa_cutscene_bars.lua` sets their alpha to 0 in full VR while they
   are letterbox-sized and gives the game's alpha back on the screens),
   **Refresh far lighting automatically** (`WuWaStereo_RefillFarLighting`;
   from test 21b switching it on also refills once) and the action **Refill far
   lighting now** (`wuwa_clv::request()`, the same refill as the classic
   panel button, works with the toggle off), matched framing, **Ambient
   occlusion** (`WuWaStereo_AmbientOcclusion`, default on from 1.2.2; while
   on it renders symmetric eye frustums so both eyes shade alike, about 7 %
   frame time; see REFERENCE.md), and
   **Smoothness**: render resolution (`OpenXR_ResolutionScale` as 60-120 %
   steps, applied live like a view switch) with the live eye size and frame
   rate. Test 3's *Restart VR runtime* is gone: in the headset it made things
   worse, and the 10 Oct recording showed the slowness was not a stuck state
   but full VR's cost (dialogue at 24-30 fps in full VR, about 45 on the
   stereo screen, with 3292 x 3292 px per eye at scale 1.0).
9. **Record:** record video, the treadmill run and its line, then **Hide
   player IDs** directly below
   (`WuWaPrivacy_Mode`: Off, While recording, Always; default While
   recording, because the boxes are approximate and can cover useful HUD), the
   profile-row option, recorder status, frame rate, picture size, telemetry,
   and **Report a problem**. While recording means the launcher's recorder
   reports starting, recording or finishing (polled twice a second, and set at
   once when this menu or the classic one sends the start request) or a
   treadmill run is on; OBS at other times needs Always. A profile that had test 2's
   `WuWaPrivacy_Enabled=true` and no mode yet starts on Always.
10. **Menu:** language, **Save changes for next time**, menu size and
   distance, laser status, how each input works, the classic menu, and the
   switch back to UEVR's window.

Pages rebuild when a plan step, the camera mode or the language changes; the
same page and, where it still exists, the same row stay selected. Live rows
(status lines, the run line) return catalog English or text filled by
`wuwa_ui::format`, and are drawn in the current language.

**Full VR uses the flat game's 16:9 UI (1.2.3).** Kuro's LGUICanvas sizes the
screen canvas from the local player's left-eye projection data, where stock
LGUI asks for the window size, so in full VR the layout, the UI projection and
the mouse ray took the eye's shape: cropped story pictures, the squash below,
and a cursor that picked the wrong item. `WuWaLguiViewport.hpp` replaces that
rect with the window size in its five readers (canvas size, the projection's
inlined copy, the view-rect getter, and the two UI point conversions that
controller presses use), only in full VR with the UI redirect on. The HUD is
then the flat game's 16:9 layout and the slate keeps 1.0; the HUD shape row is
hidden. If the code fingerprints differ (a game update), the hooks stay off and
the behaviour below applies.

The HUD page's **16:9 UI layout** switch turns this off for players who prefer
1.2.2's taller layout (the Terminal fits on one page; the character screen's
floor reflection keeps updating). With the 16:9 layout that reflection stops
updating and can show the previous character until a layout refresh: test 17e
traced it to one game caller of `GetViewportSize` (return RVA 0xe0d438), which
also lays out the 16:9 UI, so giving it the eye size fixes the reflection but
breaks the UI. So **Character screen in the taller layout**
(`VR_WuWaUiCharacterTall`, on by default, test 17g) uses the taller layout while
the character screen is open. Its 3D scene is a streamed UI level
(`UL_UIMap_Character`) about 1.5 km outside the world, and while the screen is
open the camera sits a few metres from that level's `BP_UIShowRoom_C`
`PlanarReflection` (2.4 m measured). After closing, the camera is 2.6 km away,
but the level stays loaded for minutes. The detector reports the camera within 30 m of a
showroom reflection whose level's `OwningWorld` is the current world (sample
bit 16, protocol `lease-v4`), held in a third lease with the story lease's
timing. Switching moves the hooks
and the panel at once and asks for a HUD layout refresh (as after a view
switch), because the game's UI scaler keeps its old size until then. The
taller layout brings back the flatter icons (HUD shape). From test 17m the mouse
and controller buttons line up there too. Only the input conversions take the window
size; the layout keeps the eye shape (REFERENCE.md).
Since test 17c the plugin asks for that refresh whenever the full VR layout in
effect changes, whatever changed it (this switch, a story scene, a steps card).

**Story scenes in 16:9** (on by default) applies only with the taller layout.
Story dialogue and story videos then use 16:9 while they play, so story pictures
show whole. It reuses the automatic cinematic screen's detector
(`wuwa_auto_cinema.lua`), which also runs for this setting with automatic cinema
off (since test 17e; 17c only started it for automatic cinema). The story layout
follows the plot system's own state (`PlotBlueprintFunctionLibrary`
`IsInPerformingPlot`, sample bit 4, protocol `lease-v2`): true for the whole
dialogue with controller or keyboard and mouse, with the game UI hidden and on
the skip summary, false as the plot ends, and the same for a chat with an
overworld NPC (measured 15 Oct on the simulator), so every conversation counts. The
plot-skip widget is the fallback when that query is unavailable. The plot
library is a TypeScript class that loads with the game world, so the detector
looks each class up at most three times per world, 5 s apart, and a new world
(login, teleport) allows three more. Test 17g's detector started at launch and
gave up on the login screen; the lookups also stay out of the 50 ms budget. Automatic
cinema keeps the widget and the story video player only. The plugin
holds the detector's samples in a second lease with the cinema lease's timing:
100 ms to confirm a scene, and the taller layout returns 500 ms after a verified
end. This lease never changes the view.

**Story pictures use** (Cinema page, test 17f; `VR_AutoPicturePresentation`,
default Stereo screen) is what the automatic cinematic screen shows while a 2D
story picture fills the screen, so a player who keeps story scenes in full VR
still gets a screen for the pictures. The picture is the plot view's
full-screen `UITexture` with display name `SprPhoto` (under `UiView_Plot`),
shown with alpha above zero and a `BgScene` texture, and back to alpha 0 with
`T_CommonDefault_UI` afterwards (measured 15 Oct in main story 1-8). The view
is rebuilt for every dialogue, so while the plot runs the detector looks for
it again, 150 widgets per 100 ms tick (the game has about 900), and then reads
only that widget. UEVR's Lua returns an FString with its terminating NUL, so
the name is cut at the first NUL before comparing (17f to 17h never matched). Sample bit 8, protocol `lease-v3`, automatic cinema only:
the story layout ignores it. A story video still wins (mono theatre). The
choice is held for 1.2 s after a picture so the crossfade between two pictures
does not flip the view.

**On by default from 1.2.3 (test 17j).** `VR_AutoCinema` defaults to on and
**Story scenes use** (`VR_AutoStoryPresentation`) to **Keep current view**
(choice 0, formerly "Keep immersive"/"Full VR"). That choice asks for nothing,
so whatever the player has (full VR, stereo screen or mono) stays during story
scenes; only story videos (mono theatre) and story pictures (Story pictures use)
switch. UEVR saves every value, so a profile from before 1.2.3 holds the old
default (off, story scenes on the stereo screen). `VR::on_config_load` turns it
on once for a profile without the `VR_AutoCinemaDefaults` marker (written on
every save) and sets story scenes to Keep current view; a profile that already
had automatic cinema on keeps its choices (`wuwa_auto_cinema::upgrade`, tested
in `dev/test_auto_cinema.cpp`).

**Why the HUD looked squashed in full VR only (before 1.2.3).** The log shows the UI pass
drawn for the square eye (`stage3_fit original=0,0,3292,3292`) and fitted into
the 2560 x 1440 HUD image. WuWa lays its UI out for screens no narrower than
4:3 (it supports 4:3 tablets), so the square eye gets a 4:3 layout, which the
16:9 image widens by (16/9) / (4/3) = 1.33. That is the value testers picked by
eye. The stereo screen and mono theatre render each eye at 2560 x 1440 (16:9),
so nothing is widened there and the slate keeps 1.0 regardless of the setting.

**Why ESC fixes it, and what did not (tests 3i-3k).** The 3j probe showed the
root HUD canvas at the eye size (3292 x 3292) keeping the scale WuWa's one
LGUICanvasScaler worked out for 2560 x 1440 (1.0) until the ESC menu closes,
when it becomes 1.286 (3292 / 2560). Nudging a scaler setting (3i, 3j) did not
make it rescale. Test 3k switched the scaler off and on, but WuWa's LGUI build
has no reflected `SetEnable(bool)` on the scaler, so nothing ran ("HUD refresh
unavailable ... no SetEnable(bool)"). The release keeps the 3j nudge; ESC
remains the fix.

**Layout check in every view (test 17k).** The scaler re-lays out only from the
viewport size it last heard about (`wuwa_hud_refresh.lua`, cache at +0x138 and
+0x148). A menu opened in the taller full VR layout (journey log, 15 Oct) kept
that layout after switching to the stereo screen or mono, where the canvas is
2560 x 1440: the game sent no viewport event, so the drawn menu and the cursor
disagreed (mouse and controller). The once-a-second check (two mismatches in a
row, at most three resyncs a minute) and the follow-ups after a view switch
now compare the cache with the canvas in every view, not only full VR, and
sync it only when they differ.

**Close-up match** (Camera page in Game camera mode, and the Cinema page).
Measured from the 10 Oct recordings: in full VR the eye sits exactly at the
game camera (plus `VR_Camera*Offset`), and its angle matches too. What differs
from the 2D view is the lens. The log's authored cameras use 75-90 degrees in
play but 20-40 in dialogue (mostly 24-27), while the headset shows about 89
degrees per eye. From the same spot, a 25-degree close-up's subject looks about
4.4 times smaller in VR.

Per shot, `02_WuWaVR_PolarControls.lua` moves the eye along the game camera's
view so the subject at distance d sits at d * k^s, where k = tan(fov/2) /
tan(45 degrees) and s is the strength (`WuWaControls_LensMatch`, 0-100 %,
default 75), stopping no nearer than the closest distance
(`WuWaControls_LensNearest`, 5-150 cm, default 10). Strength goes to 150 %
since test 3f (closer than the 2D shot).
- **Subject (test 3g, `WuWaControls_LensSubject` = 0):** the characters inside
  the 2D frame. Every `Engine.Character` (UEVR's class index includes
  subclasses) is tested as a capsule against the game camera's 16:9 frustum;
  hidden actors are skipped. The subject is the person at the game's focus
  distance, else the one nearest the middle of the frame; anyone whose near
  side is 30 cm or more in front of the subject's is foreground, and the eye
  stays at least max(closest distance, 30 cm) behind them. Story performers
  are usually not `Engine.Character`, so with none in frame the skeletal meshes
  are placed by their world `Bounds` (body sized, visible, actor not hidden),
  one per owning actor. Until 16c this source asked `WasRecentlyRendered`,
  which was false for nearly every mesh, so it found nobody and the eye could
  go into a performer (test 15, shot 21).
- **Why:** in test 3f every shot logged `focus=348 trace=none`. The trace
  (Visibility channel) does not hit characters, so in over-the-shoulder shots
  the eye moved 2.6-3 m along the view, through the person at the frame's
  edge. The 3f behaviour stays available as `WuWaControls_LensSubject` = 1.
- **Fallback:** with no person in frame, the game's own depth-of-field focus
  distance when it sets one; otherwise a 5 cm sphere trace along the view,
  which also stops the eye the closest distance short of anything in between.
  A trace-only shot moves at most 1 m (often a far wall).
- **Why the closest distance is a setting:** test 3c used a fixed 60 cm. The
  game's focus distances are mostly 0.7-2.2 m, so at 75 % the 60 cm floor
  already applied, and anything above about 40 % changed nothing.
- **Per shot:** the distance is measured once at each cut (a camera jump of more
  than 50 cm or 10 degrees), so the camera's own moves within a shot carry on
  unchanged.
- **Where it applies:** only lenses of 60 degrees or less, in Game camera mode,
  in full VR.
- **Hide characters at your eyes (test 16, `WuWaControls_HideNearCharacters`,
  default off; LT + RT, then Y):** some close-ups put the game camera just
  behind a head, outside the narrow 2D frame but inside the headset's wide view,
  so it filled one eye (Rover on the Abby balcony: the head 10-20 cm from the
  right eye). In close-ups, at each cut and about once a second while on, the
  script reads up to 24 bones of up to 6 visible skeletal meshes of each
  character near the eyes. Meshes are found by their world `Bounds` (a
  background pass reads 64 a frame and keeps those within 8 m; hidden actors,
  such as party members off the field, and zero-bounds templates are skipped):
  `WasRecentlyRendered` was false for 425 of 428 meshes in a dialogue, people on
  screen included, and reading all of them at once cost 14-18 ms. A
  character with a bone within 30 cm of either eye (after the close-up move) and
  no bone inside the 2D frame (16:9 at the game's FOV, 10 cm margin) leaves the
  main pass whole: every primitive component of its actor, so held props go
  too, while its shadow stays. Anything the game already hid is left alone. It
  comes back when a bone enters the 2D frame, at a cut that no longer qualifies
  it, when the close-up ends or when the option goes off. Props and scenery are
  never hidden. Logged as `[WuWaHide] shot=... hiding|note [cut] at_eyes=
  in_frame= hidden= meshes=near/kept bones= ms (pass n of listed meshes, ms)`
  and `[WuWaHide] shown again (why)`.
- **The note (`WuWaControls_NearHint`, default on):** with hiding off, a
  character found at your eyes at a cut sets `WuWaControls_NearSeen`, and the VR
  menu shows a short note at the top of the view ("Hide characters at your
  eyes", "LT + RT, then Y") for 5 seconds, once per scene (again only after 2
  minutes without one), after the cinematic-scene note if both are due. Full VR
  only. Test 16's note was the low, wide cinematic-note panel and sat in the way
  of the dialogue; the compact one keeps its lines at 24 px or more (head-locked
  at 2 m, 1 px is about 1 mm).
- **Near clip in close-ups (test 15, `WuWaControls_CloseUpNearClip`, 10-50 cm,
  default 10 = off):** the first fix raised the near clip plane in close-ups,
  kept 10 cm short of the game's focus, the centre trace and anyone in the 2D
  frame. Recorded on 15 Oct it sliced Rover's head, hands and the table open
  (a cross-section, with the inside showing), so it is off by default and kept
  for experiments. `utility/WuWaNearClip.hpp` applies it and restores the game's
  10 cm when the request stops or lapses after a second. Logged as `near_clip=`
  on each shot line and `[WuWaNearClip]` on changes; UEVR's own Near Clip Plane
  option, when on, takes precedence.
- **Diagnostics:** every shot is logged as `[WuWaLens] shot=... fov=...
  subject=... (kind) people=in-frame/all foreground=... focus=... trace=...
  dolly=...`, plus `people_error`/`trace_error` when a lookup fails. The menu
  shows the live report (`WuWaControls_LensStatus`), including "held behind the
  person in front".
- **Test 3k (11 Oct), withdrawn:** the subject was measured in the rendered
  scene depth (the middle 35 % of the 2D frame, read back from the eye's depth
  target just after each cut). In the headset the view moved into people and
  into the ground, and depth of field blurred everything because the eye moved
  while the game's focus distance did not. The measured region did not match
  the 2D frame's subject (logged subjects of 15 and 40 cm, and 4.7 m behind a
  near character). The release keeps the 3j behaviour above; the depth code is
  in commit 80dee81 for a later attempt, which must also move the depth-of-field
  focus by the same distance as the eye.
- **Test 3l (11 Oct), the release candidate:** close-ups as in 3j. Blur was
  missing: it had always been off by default (one-eye blur in 1.1.x), and the
  3g-3k plans had switched it on. Switched on by hand, the copy into the second
  eye still never ran, because the search for the post-process block only
  looked in the eye built from the story camera, which no longer holds the
  engine defaults it looks for (130 failed searches in a story replay). The
  search now also tries the other eye, which is built from a default camera,
  and blur in both eyes with all camera effects is on by default: the 3h-3i
  state the owner confirmed ("Both eyes, like 2D", "Eyes match", "Both right").
  Also from 3l: the menu stays in front of the stereo screen and Low fits; a
  gamepad takes over at once from a put-down controller; the VR thumbstick does
  not change the distance in Move menu yet.
- **Limit:** a long lens cannot be matched in full VR. Moving nearer gives the
  subject its 2D size, but the perspective is the headset's wide one: the
  foreground stays big and the background is not compressed. The stereo screen
  (below) is the exact 2D shot in 3D.

**Stereo screen lens (test 3f).** UEVR's screen projection used a fixed 90
degrees (`calculate_stereo_projection_matrix`: "todo, get from
FMinimalViewInfo"). It now uses the primary eye's authored FOV from
`capture_authored_camera` (`VR_WuWaScreenLens`: 0 game lens, 1 the old 90),
so the screen frames every shot like mono theatre; tester: "exactly the
same". The eyes separate along the game camera's right axis, scaled by
tan(fov/2) and `VR_WuWaScreenDepth` (0-3, default 1), so head tilt does not
skew the 3D.

**Camera effects in both eyes (tests 3f-3g).** The game builds the second
stereo view from a default camera: its authored camera reports 90 degrees at
4:3 where the first has the shot's lens (the same cause as the far-LOD
mismatch). That view gets none of the camera's post-process settings, so the
blur showed in one eye only (the right eye, with `swap_eyes`).
`utility/WuWaDofSync.hpp` runs at BeginRenderViewFamily, after both views'
final settings exist:
- **Finding the block:** `FinalPostProcessSettings` is found once by three
  engine defaults (sensor 24.576 mm, min f-stop 1.2, 5 blades) at a single
  offset in the first view, confirmed in the second (`view+0x1150` in this
  build). The search needs a frame without a story camera; it retries every 2 s.
- **Each copy** checks only that both blocks hold plausible camera values. In
  3f each copy re-checked the three defaults, and story cameras change them
  (cine filmback, blade count), so every story shot was skipped silently.
- **What is copied** (`WuWaStereo_EyeEffects`): the depth-of-field fields, or
  every field the engine marks plain old data (numbers, colours, override
  flags, raw texture pointers), merged into byte runs from reflection. Arrays
  such as post-process materials own memory and are never copied.
- **From which view** (`WuWaStereo_DepthOfFieldSource`): first, second, or Auto
  (default; the view with a focus distance when only one has one).
- **Log:** `[WuWaDoF] copy blur|all 1 -> 2 (auto): focus a / b cm, f-stop,
  sensor, blades` every 5 s, or `copy skipped: ...` with the values.

**Depth of field** (`WuWaStereo_DepthOfField`, default on). Mono theatre is
a single view and keeps the blur. The camera script sets the engine's
`ShowFlag.DepthOfField` console variable to 1 (force on) and back to 2 (the
game decides), logging `[WuWaDoF]`. If the variable is missing from this build
the Now line says so. Blur size follows the lens's focal length, so at the
headset's wide view a close-up's blur would almost vanish: in full VR, for
lenses of 60 degrees or less, the f-stop is scaled by k squared (the same k as
close-up match), and the focus distance moves with the close-up dolly. Both
are rewritten every frame from the game's values. Blur strength
(`WuWaStereo_DepthOfFieldStrength`, 0.25-4) divides the f-stop everywhere.

**Menu close no longer presses buttons (test 3e).** Buttons and triggers held
when the menu closes are latched per controller slot and kept from the game
until released, so the B that closes the menu no longer advances dialogue.

**HUD refresh diagnostics (test 3g).** The automatic refresh after a view
switch found no HUD in every 3e/3f switch ("No active HUD with the supported
LGUI schema"), and its schema report was never written. Each refused
`LGUICanvasScaler` now gets a reason, and the status (logged as `WuWaHud:`)
ends with a tally such as `[12 scalers: 9x inactive, 3x render mode 1]`.

**Report a problem** appends one line per report to
`<profile>/wuwa-reports/reports.jsonl`: the kind of problem, the time, the
position in the current video, the view and the main settings. Nothing leaves
the PC; testers share the folder with the video.

**Cinematic scene note.** The cinematic-framing hook records when the game last
drew through an aspect-constrained (letterboxed) camera. When that lasts more
than 1.2 s in full VR, a note appears in view for 8 s with the screen shortcut,
once per cinematic (a cinematic ends after 10 s without one). It uses the
head-locked warning slot, so it shows even with the controls card open.
Setting: `VR_CinemaHint` (default on).

## Guided test plans

A plan is a JSON file at `%APPDATA%\UnrealVRMod\Client-Win64-Shipping\wuwa-steps\plan.json`.
The game reads it at most once a second, and again whenever the file changes.

**While a plan is loaded:**
- **Step card:** its current step is shown where the controls card goes.
- **Test page:** the menu gains one with the step's text and answer buttons, plus:
  - a **Step** row at the top: left / right moves to any step without
    answering (logged as `go`), and earlier answers stay saved;
  - your latest answer to the current step;
  - Skip;
  - Mark this moment;
  - Record video (through the launcher's recorder), with the elapsed time.
- Every setting a step applies is logged as `[WuWaSteps] key = value (reads
  back ...)` in UEVR's log.

**Results file:** every answer and mark is appended to `wuwa-steps\results.jsonl`, each with:
- the time;
- the recorder state;
- the main stereo, screen and HUD settings in use.

Nothing is uploaded.

```json
{
  "version": 1,
  "id": "bars-1",
  "title": "Dialogue black bars",
  "scene": "Any dialogue scene with black bars",
  "steps": [
    {
      "id": "baseline",
      "title": "Baseline",
      "do": "Start a dialogue. Close one eye, then the other.",
      "expect": "Bars in both eyes, or none.",
      "answers": ["No bars", "Left eye only", "Right eye only", "Both eyes"]
    },
    {
      "id": "screen",
      "title": "Stereo screen",
      "settings": {"VR_2DScreenMode": true}
    }
  ]
}
```

**Field rules:**
- **Ids:** letters, digits, `- _ .`, at most 64 characters.
- **Answers:** a step has 1 to 8. A step without answers gets Pass / Fail.
- **`settings`:** maps UEVR config keys (as in `config.txt`, plus `WuWaDiorama_Enabled`) to values.
  - They apply while that step is current.
  - Anything a step changed goes back to its original value once no step needs it.
  - The same happens when the plan finishes, is removed, or is replaced.
  - Unknown keys are ignored.
- **Same `id`:** editing a plan without changing its `id` keeps the current step.
- **Write atomically:** write a temporary file, then rename it.
  - A half-written file is reported on the card.
  - The running plan keeps going.
- **No cvars yet:** console variables can't be set by a plan.
  - For now, the PC side applies them with `launcher/dev/wuwa-test.py` leases while the step is shown.

## Phases

Phases 1 and 2 are built; phase 3 is under way (test 3).

1. **Menu shell.**
   - Model, drawing, navigation for all inputs.
   - Preview scenes.
   - Opening it in the game in place of UEVR's window, with the laser enabled.
   - Quick page bound to real settings.
   - Headset test.
2. **Playtesting in VR.**
   - The Record and test page.
   - A step overlay that stays visible while playing.
   - Test plans Claude can write: checklist steps that point at scenes in a
     test-scene list.
3. **The remaining pages, and Lelouche's requests:**
   - per-view camera distance;
   - a gentler diorama distance;
   - scroll-wheel distance.

   Later: keyboard shortcuts and rebinding.
