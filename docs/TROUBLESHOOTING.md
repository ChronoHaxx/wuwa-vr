# Known issues and recovery

Status: October 1, 2026. The current public build is **3.7 + reflection + far lighting fix v2 ·
1 Oct 18:17 BST** (beta, game version 3.7). It keeps the controls, HUD and camera
features of the earlier builds; the candidate times below refer to the
checkpoints in [CHECKPOINTS.md](CHECKPOINTS.md). Keep the selected build
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
| Esc is blurred but its buttons are gone | Show game UI with L3+B. In the current build (as in the 24 Sep 23:31 candidate), the red notice is based on the game's menu/cursor signal; Show game UI now is also in WuWa Controls. |
| LB+Y fails after Alt-Tab | Close UEVR, release all buttons and return focus to WuWa. Forward Windows focus events helped in the user's later tests, but a universal fix is not confirmed. Test this separately from first-person grapple aiming. |
| L3+LB does nothing | The current build includes it. Enable Xbox mouse shortcuts and disable Physical gamepad passthrough. Saved preferences can have mouse shortcuts off. Release all buttons after the chord. |
| Menu bumpers/triggers do the wrong thing | Exit HUD/mouse adjustment with L3+LB. Leave automatic mouse in game menus off for native menu navigation. |
| SteamVR periodically stutters | The 3.7 build needs Native Stereo Fix on. On the previous game version the user reproduced stutter with it on; the owner's 1 Oct SteamVR test of the 3.7 build was near perfect apart from a far lighting difference. If it stutters, record the build and headset software. |
| An older build crashes with menu extraction + Native Stereo Fix | Applies to builds for the previous game version: do not repeat that combination there. The 3.7 build's menu route runs with Native Stereo Fix on. |
| NPC name or speech bubble visible in one eye | Not seen by the owner in 3.7 testing (1 Oct): NPC names and enemy health labels looked correct. Report it with the build name if it appears. |
| Resonators / team-screen reflection sits in the wrong place in each eye | Fixed in the 1 Oct 18:17 beta (per-eye mirror projection ported to 3.7). Weapon and Echo submenus still pause or misplace it; deferred, as the flat game does not rotate the character there. |
| Far objects look darker or flatter in one eye | The tested far-object CLV case was fixed in the 1 Oct 18:17 beta: the game's lighting volume is refilled for both eyes after starting, loading screens and teleports, with a short hitch. Some distant NPC/enemy lighting or outlines still differ. If it recurs, capture the same target near/far and before/after WuWa Controls → Refill far lighting now, then report where and which eye. |
| Iuno hair/head, Mornye leg transparency or Lynae effects absent in one eye | On 27 Sep the owner found Native Stereo Fix on repaired the affected character materials; the 3.7 build runs with it on. Report it with the character and build name if it recurs. |
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
