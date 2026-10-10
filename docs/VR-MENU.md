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
longer switches UEVR into its camera-offset mode.

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
   person motion, body visibility and level horizon. Then world scale, reset
   camera offset and recenter for every mode.
4. **Cinema:** view, the game's own fix for cutscene black bars (Cinematic:
   Fullscreen in WuWa's graphics settings), the cinematic scene note,
   automatic cinematic screen and what story scenes use, matched framing.
5. **HUD:** size, distance, **HUD shape** (`UI_WuWaHudShape`, the HUD's height
   relative to its texture, for a squashed-looking HUD), up / down, left /
   right, follow head, refresh layout, shortcut sheet.
6. **Comfort:** decoupled pitch (and its HUD adjustment), smooth camera turns
   and tilt, walk by default, L3 + A also resets position.
7. **Fixes:** matched far detail, far lighting, matched framing, and
   **Restart VR runtime** (sets the runtime's reinitialize flag: for stutter
   that stays after a view-mode switch, or a stuck SteamVR dashboard).
8. **Record:** record video, then **Hide player IDs** directly below (now on
   by default, under the new key `WuWaPrivacy_HideIDs`, so existing profiles
   start covered too), the profile-row option, recorder status, frame rate,
   picture size, telemetry, and **Report a problem**.
9. **Menu:** menu size and distance, laser status, how each input works,
   the classic menu, and the switch back to UEVR's window.

Pages rebuild when a plan step or the camera mode changes; the same page and,
where it still exists, the same row stay selected.

**Report a problem** appends one line per report to
`<profile>/wuwa-reports/reports.jsonl`: the kind of problem, the time, the
position in the current video, the view and the main settings. Nothing leaves
the PC; testers share the folder with the video.

**Cinematic scene note.** The cinematic-framing hook records when the game last
drew through an aspect-constrained (letterboxed) camera. When that lasts more
than 1.2 s in full VR, a note appears in view for 8 s with the screen shortcut,
once per cinematic (a cinematic ends after 10 s without one). It uses the
head-locked warning slot, so it shows even with the shortcut sheet on.
Setting: `VR_CinemaHint` (default on).

## Guided test plans

A plan is a JSON file at `%APPDATA%\UnrealVRMod\Client-Win64-Shipping\wuwa-steps\plan.json`.
The game reads it at most once a second, and again whenever the file changes.

**While a plan is loaded:**
- **Step card:** its current step is shown where the shortcut sheet goes.
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
