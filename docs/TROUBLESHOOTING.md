# Known issues and recovery

Status: 6 October 2026. The release candidate is
[beta-1-1-2](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-2):
desktop app **1.1.2**, VR build **steam-guard-1-1-2**, for game **3.7**.
The owner confirmed this release's VR build on the Windows 10 PC where 1.1.1 crashed through Steam: the game reaches VR through Steam and stays open. On that PC the Steam overlay sent the game's first frames back into our DirectX hook in a loop; 1.1.2 breaks the loop and presents the frame. The Windows 11 Steam PC retest and the new launcher window on a real launch are still pending.

Uninstall: **Troubleshooting → Prepare uninstall** checks and removes verified
downloads first, with confirmation. Use process recovery for a stalled worker, then
retry cleanup. Windows Apps uninstall also runs a bounded cleanup hook. Active
OpenXR packages, busy/unverified files and user data are retained with reasons in
`%LOCALAPPDATA%\WuWa VR Manager\uninstall-result.txt`; retained-file reports
open after Windows uninstall. Recordings, backups and settings are preserved.

The dated observations below retain their original checkpoint scope. References
to **WuWa VR Launcher.exe**, a browser dashboard or **Apply & launch** describe
the advanced portable fallback, not the installed desktop app. Start with the
[current setup and recovery steps](START-HERE.md) for that app. Candidate times
refer to the checkpoints in [CHECKPOINTS.md](CHECKPOINTS.md). Keep the selected
build name, timestamp and backend hash with a report; a result against one
checkpoint does not automatically apply to another.

| Symptom | What to do / current finding |
| --- | --- |
| Close keeps refusing, or Stop waiting does not end a launch | Version 1.0.4 refreshes the actual worker state on Close and reports whether cancellation was acknowledged. Use the visible process recovery panel when a verified old worker remains. If the helper still cannot close, the explicit **Close launcher only** confirmation exits the window without terminating background work. Closing the window is not confirmation that the worker stopped. Uninstall/reinstall also does not stop existing workers. |
| Steam or Kuro installation is missing or wrong | Use **01 → Change** to choose the installation. For Steam, select **Wuthering Waves.exe**, not the inner Shipping executable, and keep Steam installed and signed in. Saved choices are preserved; several detected installations require a choice. Steam startup on the affected Windows 11 simulator PC was confirmed for 1.0.10. The current 1.1.2 Steam repair and physical headset use remain pending retest. |
| Updated app but backend/helpers are still old | Update the launcher to **1.1.2** and restart it. Then explicitly select **beta-1-1-2** in step 02 and install it; confirm build **steam-guard-1-1-2**. Check updates keeps the old package selected; updating the app alone does not replace its backend/helpers. If the old app blocks updating, close it and use the new Setup from the website. A stalled worker may still need review. |
| Steam crashes during startup while Kuro works | 1.1.2 breaks the loop in which the Steam overlay sent the game's first frames back into our DirectX hook; the owner confirmed Steam startup with it. 1.1.1 already removed the global resize hook from renderer detection and guards recursive callbacks. The owner's crash dump showed repeated unguarded resize forwarding. Update both the app and VR package; this repair still needs real Steam retesting. Copy diagnostics if it fails. |
| VR DLL loaded, but no VR view or real renderer frames | Loading DLLs is not proof of VR startup. Version 1.0.9 reached DX12 and OpenXR on the affected PC; this candidate retains that dispatch repair. Copy diagnostics includes available **backend.log** from that startup attempt, so the next launch does not erase that evidence. |
| Simulator briefly opens, then the game exits | The affected PC reached OpenXR with 1.0.9 before exiting. This candidate validates enum objects before reading their class metadata and guards cached resize callbacks during renderer recovery. A matching last log line is a lead, not a confirmed crash cause. The 30-second stability check must also pass; seeing a first frame is insufficient. Copy diagnostics before another attempt. |
| TargetUnverified after Steam startup | The selected process could not be continuously identified, after process identity became unavailable. If Windows signals the retained original process handle, the launcher reports that the game exited. Missing, unreadable or ambiguous process information remains unconfirmed. The launcher preserves same-session evidence. Check the actual game view and copy diagnostics before retrying. Exact-process safety checks still apply; this is not permission to target a different game process. |
| Need to undo this VR update | Close the game and injector and stop recording, then use **Troubleshooting → Repair & recovery → Use previous installed version**. Keep an older package installed. This rolls back the VR package, not the desktop app; the old renderer-startup problem may return on the affected PC. |
| Launch spinner, permission accepted, or “already running” with no useful progress | Use **Stop waiting** when offered and allow up to four seconds for cooperative cancellation, then inspect **View details** and **Copy diagnostics**. In **Troubleshooting → Stuck launcher processes**, choose **Find stuck launcher processes** and review names, PIDs, roles, start times, paths and eligibility. Nothing is preselected. **Stop selected** requires confirmation and only accepts verified launcher helper/startup targets; game, Steam, headset/runtime and injector processes are excluded. Runtime/profile changes and active/unknown recordings stay protected. Review each result, then explicitly choose **Retry connection** when ready; this does not launch the game. Inconclusive processes may need manual handling. The updated 1.1.2 recovery flow still needs user testing. |
| Simulator belongs to another package, or no runtime is registered | With game/injector closed, explicitly select **Use bundled simulator** and accept Windows permission. The current package replaces the old registration while preserving any headset backup. If missing CRT files are reported, install Microsoft Visual C++ 2015–2022 Redistributable **x64** from Microsoft and retry. A detected manifest alone is not proof the runtime can load. A live startup/runtime-change lock must settle before switching. |
| Graphics quality changes unexpectedly after launch | This release keeps the game’s graphics choices and removes only recognized inherited/generated overrides once, with a backup and receipt. Custom edits are kept; it no longer writes a forced low or medium preset at startup. Use the game’s graphics menu for quality changes. The timing correction is separate. |
| Unequal cinematic letterbox heights in immersive VR | Cinematic framing is on by default in this release. The owner confirmed the simulator replay of private screen-comfort-r1, and sampled frames from the latest 68-second recording match across eyes. Headset comfort and other scenes remain pending. Keep a manual screen shortcut available if another scene differs. |
| Need a manual screen for a cutscene or menu | Fully hold LT + RT first, then click R3 for mono theatre (same scene and HUD for both eyes), or hold L3 for 0.8 seconds for a stereo screen. Close UEVR and HUD/mouse adjustment; release all controls before repeating. |
| Automatic cinema does not switch | It is experimental and off by default. It did not activate for the latest reported in-engine scene; prerendered movie switching has not been tested. Use the manual shortcut rather than assuming automatic detection works. |
| Long scene or dialogue loading; a flat menu background moves with the headset | Both remain open. Manual mono theatre can help with the moving menu background; it is not an established loading-stall fix. Include the scene, mode and duration in a report. |
| HUD squashes after returning from a screen mode | Try VR → WuWa Controls → Reset HUD aspect and read the result; unsupported schema can still prevent refresh. Opening/closing ESC has helped when available. Do not assume the reset succeeded because the button was pressed. |
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
