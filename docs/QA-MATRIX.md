# Acceptance matrix

Generated from `qa/matrix.json` and `qa/results.jsonl` by `python dev/qa.py report`; do not edit by hand. Process: [QA.md](QA.md).

Legend: ✓ passed (build, date) · ✗ failed · ◐ blocked · ? not yet checked · blank: not applicable. A cell with dimensions (game copy, runtime, input) shows the worst of them. The `log` column is the newest automatic result from `dev/qa.py session` (worst over game copy and runtime).

## Launch

| Feature | load | log |
| --- | --- | --- |
| Install, update and launch into VR (`launch`) | ? | ✓ 1015-1658 10-15 |
| Every game-code check passes on this game build (`game-hooks`) | ? | ✓ 1015-1658 10-15 |

## Stereo

| Feature | full169 | stereo | switch | dialogue | load | log |
| --- | --- | --- | --- | --- | --- | --- |
| Both eyes share one game camera (no one-eye shake or zoom) (`eye-camera`) | ? | ✓ 1015-1658 10-15 | ? | ✓ 1015-1658 10-15 |  | ✓ 1015-1658 10-15 |
| Far trees and foliage match in both eyes (`eye-lod`) | ? | ? |  |  |  |  |
| Far lighting (CLV) matches after teleports (`eye-lighting`) | ? |  |  |  | ? |  |
| Ambient occlusion matches in both eyes (`eye-ao`) | ? |  |  |  |  |  |
| God rays match (pagoda, storm sky) (`eye-godrays`) | ? |  |  | ? |  |  |
| Depth of field (blur) in both eyes; LT + RT, L3 toggles it (`eye-dof`) | ? | ? |  | ? |  |  |
| Fades reach both eyes; no one-eye frame after a load (`eye-fades`) | ? |  |  | ? | ? |  |
| Tree wind moves the same in both eyes (`eye-wind`) — open | ? |  |  |  |  |  |

## Views

| Feature | full169 | stereo | mono | diorama | switch | dialogue | menu | log |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| LT + RT, R3: click full VR / stereo; hold mono and back; repeat while the triggers stay held (`view-chord`) | ? | ? | ? |  |  | ? | ? |  |
| Story videos switch to mono theatre and back (`auto-cinema-video`) |  |  |  |  |  | ? |  | ? |
| Story pictures switch to the stereo screen and back (`auto-cinema-picture`) | ? |  |  |  |  | ? |  | ? |
| Automatic cinema on by default; story scenes keep the current view (`auto-cinema-default`) | ? | ? |  |  |  | ? |  |  |
| Cinematic framing matches between the eyes (letterbox) (`framing`) | ? |  |  |  |  | ? |  |  |
| Diorama: scale, its own camera, and back (`diorama`) |  |  |  | ? | ? |  |  |  |
| Hide characters at your eyes (LT + RT, Y) (`hide-near`) | ? |  |  |  |  | ? |  |  |

## UI

| Feature | full169 | fulltall | stereo | mono | switch | dialogue | menu | character | log |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HUD and menus laid out for the view they are shown in (`ui-layout`) | ? | ? | ? | ? | ✗ | ? | ? |  | ✓ 1015-1658 10-15 |
| Mouse and controller cursor hit what they point at (`ui-cursor`) — open | ? | ✗ | ? | ? | ? |  | ? | ? |  |
| Story scenes in 16:9 with the taller layout (`ui-story-169`) |  | ? |  |  |  | ? |  |  |  |
| Character screen floor reflection stays live (`ui-character`) | ? |  |  |  |  |  |  | ? |  |
| WuWa VR menu: open, settings, defaults, X / Delete reset, save switch (`vr-menu`) | ? |  | ? | ? |  |  |  |  |  |
| Classic UEVR window and emergency keys (`classic-menu`) | ? |  | ? |  |  |  |  |  |  |
| Every text translated in all ten languages (`translations`) | ? |  |  |  |  |  |  |  |  |

## Camera

| Feature | full169 | dialogue |
| --- | --- | --- |
| Game, first person, fixed camera and freecam (`camera-modes`) | ? |  |
| Camera height, L3 + Y / X and L3 + Y + X reset (`camera-height`) | ? |  |
| Close-up match stops behind performers (`close-up`) |  | ? |

## Tools

| Feature | full169 | stereo | load |
| --- | --- | --- | --- |
| Recorder (SBS video) from the WuWa VR menu (`recorder`) | ? | ? |  |
| Restore controls and the profile upgrade (`recovery`) |  |  | ? |

## States

- `full169`: Full VR, 16:9 UI layout (default)
- `fulltall`: Full VR, taller UI layout (16:9 off, or the character screen)
- `stereo`: Stereo screen
- `mono`: Mono theatre
- `diorama`: Diorama
- `switch`: A view switch while it is on screen (full VR / stereo / mono, both ways)
- `dialogue`: Story dialogue or a story cutscene
- `menu`: A full-screen game menu open (journey log, map, Terminal, inventory)
- `character`: The character screen (Resonators)
- `load`: A world load or teleport
