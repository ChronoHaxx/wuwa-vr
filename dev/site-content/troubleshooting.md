# Known issues and recovery

What was tested for each release, and its open limits, are in that release's
[release notes](https://github.com/ChronoHaxx/wuwa-vr/releases). This page
collects symptoms and what to do.

Start with the launcher's own fixes: **Troubleshooting** at the bottom of the
launcher lists **Repair mod files**, **Reset mod settings** and **Save a problem
report** (a zip of your logs and settings on the desktop, without your Windows
user name). **More options** holds the controller check, launch readiness,
diagnostics, the settings folder, installed versions, stuck launcher processes
and **Prepare uninstall**.

Uninstall: **Troubleshooting → More options → Prepare uninstall** puts your UEVR
settings back to how they were before the mod and removes verified downloads,
with confirmation; then remove WuWa VR in Windows Settings → Apps. Recordings and
backups are kept. Anything the launcher could not remove is listed, with the
reason, in `%LOCALAPPDATA%\WuWa VR Manager\uninstall-result.txt`.

| Symptom | What to do |
| --- | --- |
| The game starts, but not in VR | Check that the launcher's **Headset** row shows your headset software as the OpenXR runtime and that it was running first, then **Launch in VR** again. |
| The Windows permission prompt was declined or missed | Nothing was started. Close any waiting injector from the taskbar, then choose **Launch in VR** again. |
| The Steam or Kuro installation is missing or wrong | Use **Change** in the launcher's **Game** row. For Steam, select **Wuthering Waves.exe**, not the inner Shipping executable, and keep Steam signed in. |
| The VR mod row still shows the old version after an app update | Restart the launcher with the game closed: it installs the matching VR package by itself. Or use the update button on the **VR mod** row. |
| Need to undo a VR update | Close the game, then **Troubleshooting → More options → Use previous installed version**. This rolls back the VR package, not the app. |
| The launcher uses a lot of disk space | **Troubleshooting → More options → Remove old versions** keeps the version in use and the previous one, and says how much it frees first. Settings, backups and recordings stay. |
| A launch hangs, or the launcher says it is already running | Use **Stop waiting** when offered, then **Save a problem report**. **Stuck launcher processes** (under More options) lists helper processes with their names and start times; **Stop selected** asks first and never touches the game, Steam or your headset software. |
| The simulator belongs to another package, or no runtime is registered | With the game closed, select **Use bundled simulator** and accept the Windows prompt. If missing CRT files are reported, install the Microsoft Visual C++ 2015–2022 Redistributable **x64** and retry. |
| **Use headset** does not select SteamVR | It restores the runtime saved before the simulator, which may be Virtual Desktop. Set SteamVR as the OpenXR runtime in SteamVR's own settings. |
| A cutscene or menu looks wrong in full VR | Hold LT + RT, then click R3 for the stereo screen (click again for full VR), or hold R3 for 0.8 seconds for mono theatre (hold again to go back). |
| A story video does not switch screens by itself | **Automatic cinematic screen** is on the WuWa VR menu's **View** page; its status line says what it saw. The shortcut above works meanwhile. |
| Long scene or dialogue loading; a flat menu background moves with the headset | Both are known. Mono theatre helps with the moving background. Include the scene, view and how long it took in a report. |
| The HUD or menus look squashed or out of place | WuWa VR menu → **Interface** → **Refresh HUD layout**. Opening and closing ESC also lays the HUD out again. |
| ESC is blurred but its buttons are gone | The game UI is hidden: **L3 + B** (or the VR key + H) shows it again. |
| Buttons move the mouse or the HUD | HUD / mouse mode is on: **L3 + LB** (or the VR key + M) returns to game controls. Release all buttons afterwards. |
| The controls card does not show | Hold L3 for a full half second, or press L3 + Menu to keep it open, and check **Controls cards** on the **Interface** page. The card shows in the headset, not in the desktop window. |
| SteamVR stutters | Keep **Native Stereo Fix** on. If it still stutters, note the build and headset software in a report. |
| Far objects look darker or flatter in one eye | Lighting is refilled for both eyes after loading screens and teleports; **Refill far lighting now** on the **Graphics** page does it on demand. Report where, and which eye, if it stays. |
| A character effect shows in one eye only | Keep **Native Stereo Fix** on, and report the character and build if it persists. |
| The first-person torso shows while sprinting or gliding | **Animated** motion tracks best. Moving your head away from your body can still show it. |
| The first-person view drops after swapping characters | Switch to the game camera and back, and note the character in a report. |
| Simulator **Home** does not restore first person | Home resets the simulated headset only; recenter with **L3 + A** too. |
| Portable ZIP: nothing opens after double-clicking WuWa VR Launcher.exe | Look for a new browser tab; running it again reopens the page. If a file is missing, extract the whole ZIP again to a normal folder. |
| "Windows protected your PC" | The installer is not code-signed, so SmartScreen may warn. Continue only for a download from the project's GitHub release. Never disable SmartScreen, antivirus or anti-cheat. |

## Not implemented

- A global time pause or frozen-time walkaround.
- Collision is optional and experimental, and cannot stop real head movement.
- First person has not been checked on every character, outfit or animation.
- No claim of publisher approval, account safety or anti-cheat compatibility.

## Useful bug report

Use **Save a problem report** in the launcher, and add the headset and runtime, the
controller, the view and camera mode, and the shortest steps that repeat the problem.
For stereo problems, describe each eye separately. Remove account IDs and personal
paths before posting logs or screenshots. Do not share game binaries, memory captures
or extracted assets.
