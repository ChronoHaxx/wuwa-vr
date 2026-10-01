# Problems and recovery

## Known issues

- Weapon and Echo submenu reflections pause or sit in the wrong place (deferred:
  the flat game does not rotate the character there either).
- Full-animation first-person aiming can disagree with the game's grapple
  targeting.
- First person has not been checked on every character, outfit or animation.
- Collision in freecam is optional and cannot stop real head movement.
- No publisher approval, account safety or anti-cheat compatibility is claimed.

## Fixes for common problems

| Symptom | What to do |
| --- | --- |
| Nothing opens after double-clicking WuWa VR Launcher.exe | Wait a few seconds and check your browser for a new tab; running the EXE again reopens it. If a file is reported missing, extract the whole ZIP again to a simple folder. Logs: `%LOCALAPPDATA%\WuWa VR Launcher\logs`. |
| "Windows protected your PC" | The launcher is unsigned, so SmartScreen does not recognise it. Continue only if the ZIP came from this project's Releases. Do not disable SmartScreen, antivirus or anti-cheat. |
| Apply reports a missing or changed build file | Click **Check package files**. Extract a fresh copy if anything is missing; security software may have quarantined a DLL. |
| The game starts, but not in VR | Start your headset software first and check the launcher shows it as the OpenXR runtime. Use a simple install path such as `C:\Games\WuWa VR`. |
| **Use headset** does not select SteamVR | It restores the runtime saved before the simulator was enabled, which may be Virtual Desktop. Select SteamVR as the OpenXR runtime in SteamVR's settings. |
| The Windows permission prompt was declined | Nothing started. Close any waiting injector and click **Apply & launch** again. |
| The launcher refuses to switch build or runtime | Close the game, its launcher and any leftover injector first; keep the WuWa VR Launcher page open. |
| A menu is blurred but its buttons are gone | The game UI is hidden: press **L3 + B** or **WuWa Controls → Show game UI now**. |
| Buttons move the mouse or HUD unexpectedly | HUD/mouse adjustment is on (amber notice). Press **L3 + LB** or **Exit mouse mode now**, then release all controls. |
| L3 + LB does nothing | Enable **Xbox mouse shortcuts** and turn **Physical gamepad passthrough** off. Release all buttons after the chord. |
| Menu bumpers/triggers do the wrong thing | Leave HUD/mouse adjustment (L3 + LB). Keep automatic mouse in game menus off. |
| LB + Y fails after Alt-Tab | Close UEVR, release all buttons and click back into the game. |
| Far objects look darker or flatter in one eye | **WuWa Controls → Refill far lighting now**, and report where it happened. |
| Distant trees or props freeze in one eye | Check **Match far-object detail between eyes** is on. |
| An effect or character part shows in one eye only | Check **Native Stereo Fix** and **Same Pass** are on (the supplied profile has them on). |
| The full head shadow is missing after a swap | It retries automatically; if it stays missing, switch visibility mode and back, and report the character. |
| The torso shows during sprint or wing flight | Use **Animated position + stick pitch**. Leaning or unusual animations can still expose it. |
| Full animation stops following turns | Read the warning beside First person motion: full animation needs Game aim with Decoupled Pitch off. |
| The camera drops into the body after a swap | Switch to the game camera and back, or swap away and back. Report the character. |
| The target glows but LB + Y flies instead of grappling | Compare in third person or **Animated position + stick pitch**; full-animation targeting is unresolved. |
| The shortcut sheet is not visible | Toggle **L3 + Menu**, try front placement, recenter and close UEVR. Check in the headset, not the flat window. |
| SteamVR stutters | Note the build, headset software and settings, and report it. |

## Reporting a bug

Use the [feedback form](https://chronohaxx.github.io/wuwa-vr/feedback.html). Include
the build name and time, game version, headset and its software, the character
and place, what you did, and what you expected. For stereo problems say which
eye looks wrong and at what distance; a short side-by-side clip helps. Remove
account IDs and personal paths first, and do not share game files or memory
dumps.
