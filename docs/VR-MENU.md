# VR menu design

Status: design for the `claude/vr-menu` branch, 10 Oct 2026.

## Goal

One settings menu that is comfortable in the headset and works with every input
at once:

- **VR controllers:** point with a laser and pull the trigger.
- **Gamepad (Xbox or PS5):** D-pad or left stick moves, A selects, B goes back,
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

| Action | Laser / mouse | Gamepad | Keyboard |
| --- | --- | --- | --- |
| Focus an item | Point at it | Up / down | Arrow keys |
| Toggle or run | Trigger / click | A | Enter / Space |
| Change a choice or slider | Click the arrows, drag the bar | Left / right | Left / right |
| Switch page | Click the tab | LB / RB | Tab / Shift+Tab |
| Back / close | Back button | B | Esc |

**Readable at arm's length.** Rows are at least 64 px tall at the base size.
Labels are short, and each item's longer explanation sits in a help strip at the
bottom for the focused item. The theme is black and gold, matching the launcher
and the site.

**The laser works for everyone.** While the menu is open and an OpenXR
controller is tracked, the pointer and trigger drive the menu, whether or not
*Enable motion-controller input* is on.

**Nothing is lost.** *All settings (classic)* opens UEVR's full menu. Every new
page edits the same settings objects as the classic menu, so the two always
agree.

## Pages (first version)

1. **Quick:**
   - VR controllers on / off and sharing;
   - stereo screen and mono theatre;
   - diorama;
   - HUD size and distance;
   - recenter.
2. **View and comfort:**
   - first person;
   - fixed camera distance;
   - snap turn;
   - vignette.
3. **Graphics fixes:** the per-eye fixes players are asked to try (AO, letterbox, LOD).
4. **Record and test:**
   - record / stop;
   - mark a moment;
   - the current playtest step with Pass, Fail, Blocked and preset answers.
5. **All settings (classic).**

## Guided test plans

A plan is a JSON file at `%APPDATA%\UnrealVRMod\Client-Win64-Shipping\wuwa-steps\plan.json`.
The game reads it at most once a second, and again whenever the file changes.

**While a plan is loaded:**
- **Step card:** its current step is shown where the shortcut sheet goes.
- **Test page:** the menu gains one with the step's text and answer buttons, plus:
  - Skip and Previous;
  - Mark this moment;
  - Record video (through the launcher's recorder).

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
