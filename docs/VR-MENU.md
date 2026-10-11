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
   controllers, diorama, HUD size and distance, record video, recenter.
2. **Test:** only while a plan is loaded (see below).
3. **Camera:** the camera mode, then only the options for that mode: fixed
   distance and height; freecam style, speed, turn speed and collision; first
   person motion, body visibility and level horizon. Then, for every mode:
   world scale, the camera offset (forward / back, right / left, up / down in
   cm, and reset), and recenter.
4. **Cinema:** view, the game's own fix for cutscene black bars (Cinematic:
   Fullscreen in WuWa's graphics settings), close-up match and its closest
   distance, **depth of field (experiment)**, the cinematic scene note,
   automatic cinematic screen and what story scenes use, matched framing.
5. **HUD:** size, distance, **HUD shape (full VR)** (`UI_WuWaHudShape`,
   default 1.33; see below), up / down, left / right, follow head, refresh
   layout, controls cards (on / off, keep open, size, height, distance) and
   view hints.
6. **Comfort:** decoupled pitch (and its HUD adjustment), smooth camera turns
   and tilt, walk by default, L3 + A also resets position.
7. **Fixes:** matched far detail, far lighting, matched framing, and
   **Smoothness**: render resolution (`OpenXR_ResolutionScale` as 60-120 %
   steps, applied live like a view switch) with the live eye size and frame
   rate. Test 3's *Restart VR runtime* is gone: in the headset it made things
   worse, and the 10 Oct recording showed the slowness was not a stuck state
   but full VR's cost (dialogue at 24-30 fps in full VR, about 45 on the
   stereo screen, with 3292 x 3292 px per eye at scale 1.0).
8. **Record:** record video, then **Hide player IDs** directly below
   (`WuWaPrivacy_Mode`: Off, While recording, Always; default While
   recording, because the boxes are approximate and can cover useful HUD), the
   profile-row option, recorder status, frame rate, picture size, telemetry,
   and **Report a problem**. While recording means the launcher's recorder
   reports starting, recording or finishing (polled twice a second, and set at
   once when this menu or the classic one sends the start request); other
   recorders such as OBS need Always. A profile that had test 2's
   `WuWaPrivacy_Enabled=true` and no mode yet starts on Always.
9. **Menu:** language, menu size and distance, laser status, how each input works,
   the classic menu, and the switch back to UEVR's window.

Pages rebuild when a plan step or the camera mode changes; the same page and,
where it still exists, the same row stay selected.

**Why the HUD looks squashed in full VR only.** The log shows the UI pass
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
  stays at least max(closest distance, 30 cm) behind them.
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

**Depth of field** (`WuWaStereo_DepthOfField`, default off). Mono theatre is
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
