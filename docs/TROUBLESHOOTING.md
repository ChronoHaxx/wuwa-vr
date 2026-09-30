# Known issues and recovery

Status: September 30, 2026. The current public builds are **Camera candidate +
trigger controls · 28 Sep 16:27** (experimental) and **Stereo, menus and
languages · 26 Sep 22:42 BST** (older beta); the candidate times below refer to
the checkpoints in [CHECKPOINTS.md](CHECKPOINTS.md). Keep the selected build
name, timestamp and backend hash with a report. A result against one checkpoint
does not automatically apply to another.

| Symptom | What to do / current finding |
| --- | --- |
| Nothing opens after double-clicking WuWa VR Launcher.exe | Wait a few seconds and check your browser for a new tab. Running the EXE again reopens the existing page. If a message says a file is missing, extract the whole ZIP again to a normal folder. Logs: `%LOCALAPPDATA%\WuWa VR Launcher\logs`. |
| "Windows protected your PC" | The launcher is new and unsigned, so SmartScreen does not recognise it. Continue only if the ZIP came directly from the project owner. Do not disable SmartScreen, antivirus or anti-cheat. |
| Apply reports a missing or changed build file | Choose **Check package files**. Extract a fresh copy of the ZIP if any file is missing or changed; security software may have quarantined a DLL. |
| The game starts, but not in VR | Check the launcher shows your headset software as the OpenXR runtime and that it was running first. Unicode paths passed launcher/profile tests, but real injector startup from them is untested; use a simple path such as `C:\Games\WuWa VR` for that comparison. |
| Use headset does not select SteamVR | It restores the runtime saved before enabling the simulator, which may be Virtual Desktop. For the grouped Steam Link/SteamVR test, select SteamVR as the active OpenXR runtime through its settings. |
| Windows permission prompt was declined or missed | Nothing was started. Close any waiting injector from the taskbar, then choose **Apply & launch** again. |
| Esc is blurred but its buttons are gone | Show game UI with L3+B. In both current builds (as in the 24 Sep 23:31 candidate), the red notice is based on the game's menu/cursor signal; Show game UI now is also in WuWa Controls. |
| LB+Y fails after Alt-Tab | Close UEVR, release all buttons and return focus to WuWa. Forward Windows focus events helped in the user's later tests, but a universal fix is not confirmed. Test this separately from first-person grapple aiming. |
| L3+LB does nothing | Both current public builds include it. Enable Xbox mouse shortcuts and disable Physical gamepad passthrough. Saved preferences can have mouse shortcuts off. Release all buttons after the chord. |
| Menu bumpers/triggers do the wrong thing | Exit HUD/mouse adjustment with L3+LB. Leave automatic mouse in game menus off for native menu navigation. |
| SteamVR periodically stutters | Keep Native Stereo Fix off; the user reproduced stutter with that checkbox on. This does not establish every possible streaming/GPU cause. |
| An older build crashes with menu extraction + Native Stereo Fix | Do not repeat that combination. Use the preserved current menu route with Native Stereo Fix off. |
| NPC name or speech bubble visible in one eye | The 19:04 and 20:11 candidates added a guarded per-eye label pass, included in both current builds. It has not been verified in game. Unknown target layouts are left unchanged; this is still an open rendering issue. |
| Two reflections per eye in character/Echo previews | Open rendering issue in the scene layer. Planar and SSR CVars were already zero in a measured session; turning them off again is not a demonstrated solution. |
| Iuno hair/head, Mornye leg transparency or Lynae effects absent in one eye | Open stereo-material defects. The reported head-following preview reflections are tracked separately; a shared shader cause has not been established. |
| Full head shadow only works after toggling per character | Automatic retries and reused-component recovery are included. The owner reported the 15:39 head-bones/full-shadow mode working; it is the supplied default in 17:30 and later. If it recurs, record the character/build and check the active-copy status. |
| Buttons unexpectedly move the mouse or HUD | L3 + LB exits manual adjustment; release all controls. The control-recovery candidate has an always-visible amber notice and an Exit mouse mode now button above its settings. |
| First-person torso appears during sprint/wing flight | The owner reported improved tracking with Animated position + stick pitch; later candidates preserve that path. Full-animation offsets have additional component-tested repairs. Physical headset displacement or some animations can still expose the torso. |
| Full animation stops following turns or loses head yaw | In 20:11, check the warning beside First person motion. Full animation requires Game aim and Decoupled Pitch off; incompatible settings temporarily use game-view rotation. Use the warning's button only if you want to change those global settings. |
| First-person camera drops after swapping | Rare user-reported issue. Return to ordinary camera and back, or swap away/back. Note which character and motion triggered it. |
| Target glows but LB+Y flies instead of grappling | First compare ordinary third person and Animated position + stick pitch at the same target. Full-animation headset targeting remains unresolved. Stock headset aim conflicts with full-animation rotation in 20:11 and triggers the game-view fallback; switching it on is not a confirmed grapple fix. |
| Cannot see the sheet | Toggle L3+Menu; try front placement, recenter, and close UEVR. Feet placement uses tracking origin. Check in the headset/compositor view, not only the spectator window. |
| Dashboard refuses a build/runtime change | Close WuWa, the game launcher and leftover injector normally. Keep the WuWa VR Launcher page open to make the change. The process guard protects loaded files. |
| Development workspace: Start WuWa VR Launcher.cmd seems silent | Check for the existing browser tab. The bootstrap checks Python and reports an error rather than silently relying on a developer username. Startup logs are local under extracted/build-manager/bootstrap. |
| Simulator Home does not restore first person | Home resets the simulated headset pose; UEVR still has a separate origin. Recenter UEVR with L3+A too. |

## What is not implemented or established

- Global time pause / frozen-time walkaround is not implemented.
- Collision is optional and experimental, and cannot constrain real head movement.
- First-person support has not been checked on every character, outfit or animation.
- No claim of publisher approval, account safety or anti-cheat compatibility.
- No claim that simulator screenshots establish headset comfort or stable Steam Link performance.

For the remaining reflection/material defects, **Recovery and removal → Compare
stereo rendering (experimental) → Compare current scene** can collect the five
reversible comparisons in one SteamVR session. Leave the affected scene still,
close UEVR settings, start the comparison and return focus to the game. It restores
each setting before continuing and saves a local gallery. It is a diagnostic tool,
not an established fix; the images may contain account IDs. No upload occurs.

## Useful bug report

Record the checkpoint and hash, runtime/headset, controller connection, camera
mode, relevant option states and shortest repeatable steps. For stereo problems,
describe each eye separately. Include whether returning to normal camera or
restoring UI changes the result. Remove account IDs, personal paths and private
Discord information before posting logs or screenshots. Do not share game
binaries, memory captures or extracted assets.
