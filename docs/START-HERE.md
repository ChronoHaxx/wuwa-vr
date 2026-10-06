# Start playing

**Beta for game 3.7 · desktop app 1.0.5.**
[Download WuWa-VR-Setup.exe](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-06-renderer-startup/WuWa-VR-Setup.exe)
and run it to install **WuWa VR** for your Windows user. Open **WuWa VR** from
the Start menu. The small installer does not bundle the VR mod: the first
**Install VR** needs internet to download about **54 MB**. No separate Python
installation is needed.
[简体中文安装与更新指南](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html).

You need your own copy of Wuthering Waves, a Windows PC, a PC VR headset/runtime
and an Xbox/XInput controller connected to the PC. The simulator is an optional
way to inspect the view without a headset; it does not demonstrate headset behaviour.

**Account risk:** this unofficial mod injects into the game. Anti-cheat may
restrict or ban an account. It is not approved by Kuro Games.
[Read the risk notice](RISK.md) before launching.

## Install and launch

1. **01 · Game.** Choose the detected Kuro or Steam installation. Saved choices
   are kept; if several installations are found, choose one. For Steam, browse to
   **Wuthering Waves.exe** if needed, not the inner Shipping executable.
2. **02 · Install VR.** Read and accept the account-risk notice, then install the
   selected VR version. Existing settings and backups are kept.
3. **03 · Headset or simulator.** Start your headset software and check the
   displayed runtime, or deliberately choose the simulator. Select **Launch in VR**,
   accept the Windows permission prompt. Steam opens the selected game through
   Steam; for Kuro, press **Play** in its launcher.

The installer is unsigned, so Windows may warn. Download from this project's
GitHub Releases and compare the published checksum if unsure. Do not disable
antivirus, SmartScreen or anti-cheat. Game injection asks for Windows permission
separately from installing the app for your user.

The owner previously confirmed Steam startup/backend activity on Windows 10.
On a remote Windows 11 Steam PC, 1.0.4 loaded the VR DLL but received no real
game-renderer frame. The 1.0.5 backend adds a startup fallback for that condition;
it does not establish a hardware-brand cause. That PC’s retest, Epic injection and
current headset acceptance remain pending. SteamVR is headset software, separate
from the Steam-store game route.

Choosing a simulator or restoring the headset runtime changes the system OpenXR
runtime and may ask for Windows permission. Close the game and injector first.
Opening or updating the app does not automatically select the simulator. A missing
registration or an old package’s simulator can be replaced through this explicit
choice. If the simulator reports missing Visual C++ 2015–2022 x64 dependencies,
follow the prerequisite message; the launcher does not install them automatically.

## Updates

- Open **02 → Versions & updates → Check updates**.
- **From an older app, including 1.0.4, to desktop app 1.0.5:** choose **Update launcher** to download and verify the update;
  this does not restart the app. When it is ready, close the game and injector,
  stop recording and finish other operations, then choose **Restart to update**
  and confirm. Installing or updating the desktop app preserves your selected VR package.
- **VR package:** in **step 02**, choose **beta-2026-10-06-renderer-startup** and install it,
  or select it if already installed, to use VR build **renderer-startup-20261006**.
  **Explicitly select the new package after the app
  restarts:** Check updates preserves the old selection. Both updates are needed.
  Older installed VR versions remain available for rollback.
- **Rollback, if needed:** close the game/injector and stop recording, then use
  **Troubleshooting → Repair & recovery → Use previous installed version**.
  This changes the VR package, not the desktop app. Keep the old package; its
  renderer-startup problem may return on the affected PC.
- If the old launcher blocks updating, close it and run the new **WuWa-VR-Setup.exe**
  from this website. This updates the app but does not guarantee a stalled worker
  has stopped. Review remaining processes before changing the VR package/runtime.

Updating keeps settings, backups, logs and recordings. The manager uses
`%LOCALAPPDATA%\WuWa VR Manager`; the existing launcher data stays in
`%LOCALAPPDATA%\WuWa VR Launcher`. Do not delete these folders to update.

Uninstall: **Troubleshooting → Prepare uninstall** checks and removes verified
downloads first, with confirmation. Use process recovery for a stalled worker, then
retry cleanup. Windows Apps uninstall also runs a bounded cleanup hook. Active
OpenXR packages, busy/unverified files and user data are retained with reasons in
`%LOCALAPPDATA%\WuWa VR Manager\uninstall-result.txt`; retained-file reports
open after Windows uninstall. Recordings, backups and settings are preserved.

## What this beta changes

The backend now tries the other graphics hook when the first probe receives no
real game frames. Once a renderer is detected, it keeps that renderer. Startup
attempts preserve available **backend.log** evidence, included by **Copy diagnostics**.
If the selected Steam process later becomes unreadable or ambiguous, retained
renderer evidence is not discarded: **TargetUnverified** reports that observation
became inconclusive. Check the actual game view before retrying.

Native build, isolated DXGI callback and background observer checks passed;
packaging and isolated package-installation checks passed. The remote Windows 11 Steam retest and
headset acceptance remain pending. Existing stalled-worker recovery and **Close
launcher only** are retained; closing/reinstalling does not stop background workers.

The **renderer-startup-20261006** VR build retains Steam selection and keeps your game’s
graphics choices. It stops recreating inherited low/medium graphics overrides,
with a one-time cleanup of recognized generated settings; custom edits are kept.
The accepted timing correction remains separate from graphics quality.

Cinematic framing is **on by
default**. The owner confirmed the simulator replay on private build
**screen-comfort-r1**; sampled frames from the latest 68-second recording show
matching letterbox heights. **Headset comfort remains pending.** Earlier accepted
foliage, far indirect-lighting, main Resonators reflection, ultimate-camera and
2D-brightness fixes are retained.

Automatic cinema remains **off by default and unverified**. It did not activate
for the latest reported in-engine scene; a prerendered movie has not been tested.
Long scene/dialogue stalls, HUD-aspect refresh and moving backgrounds behind
flat menus remain open. Manual mono theatre is a menu workaround. Check the actual
view and copied startup diagnostics when the outcome is unclear; do not start a
second injector or assume the remote startup failure is resolved.

**VR → WuWa Controls → Suppress mismatched NPC rim lighting** starts **off**.
It suppresses the toon-rim effect that produced extra bright contours in the
tested NPC scene. It also removes that intended effect from nearby characters
that use it. Turning it off restores the prior value if the workaround still
owns it. The underlying stereo rendering fault is unresolved; this is a
workaround, not a claim that every NPC or scene is fixed.

Weapon/Echo submenu reflections and full-animation first-person aiming remain
open. Keep **Native Stereo**, **Native Stereo Fix** and **Same Pass** on for this
build's stereo fixes.

## First-minute controls and recovery

- **L3 + R3** or Insert opens UEVR settings; custom options are under
  **VR → WuWa Controls**. Close settings before using gameplay shortcuts.
- **L3 + B** shows or hides the game UI. A blurred menu with no buttons can mean
  the UI is hidden. **Show game UI now** is also available in WuWa Controls.
- **L3 + A** recenters. **L3 + Menu** shows the shortcut sheet.
- Fully hold **LT + RT first**, then **click R3** for mono theatre: both eyes see
  the same scene and HUD. For a screen with stereo depth, hold **L3 for 0.8 seconds**
  instead. Release all controls before repeating; close UEVR and HUD/mouse adjustment.
- For a squashed HUD after leaving screen mode, try **Reset HUD aspect** and read
  its result. It can report unavailable; opening and closing ESC has helped when
  the game allows it. It is not a guaranteed dialogue-safe recovery.

See the [controller guide](CONTROLS.md), [comfort settings](COMFORT.md) and
[recovery reference](TROUBLESHOOTING.md). The older browser-launcher instructions
in that reference apply to the portable fallback.

## Startup and process recovery

If a launch stalls, use **Stop waiting** when offered. It cancels the startup
wait, not the running game. **View details** and **Copy diagnostics** retain the
reported stage and failure reason.

Open **Troubleshooting → Stuck launcher processes → Find stuck launcher processes**.
Review the name, PID, role, start time, path and eligibility of each candidate; Windows may request elevation. Nothing is selected
automatically. Select only a verified launcher helper/startup worker, choose
**Stop selected**, then confirm the listed processes. The panel does not terminate
the game, Steam, headset/runtime or injector, or kill by name/process tree.
Runtime/profile changes and active or unknown recording state remain protected.
Review the per-process results, then explicitly choose **Retry connection** when
ready. Recovery does not reconnect or launch the game automatically.
Inconclusive processes may still need manual handling; copy diagnostics and check
what the process belongs to before acting.

## Rollback, repair and removal

Close the game, injector and recorder before switching or repairing VR packages.
Use step 02 to choose an installed older version; repair creates a fresh verified
copy. Personal settings and recordings are not replaced with package defaults.

Before removing the app, restore the headset runtime if you selected the bundled
simulator, and use **Restore my settings from before WuWa VR** in recovery if you
want to undo the profile changes. Then uninstall **WuWa VR** through Windows.
Keep the data folders if you want to retain recordings, backups and settings.

## Portable fallback

[WuWa-VR-Launcher.zip](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-06-renderer-startup/WuWa-VR-Launcher.zip)
is an advanced fallback. Extract the whole archive, keep `app` and `python`
together, and open **WuWa VR Launcher.exe**. It uses the older browser interface;
it does not install or self-update the desktop app. Choose **Apply & launch**,
accept Windows permission, then follow the selected Steam or Kuro launch route.

The [previous 6 October beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-stalled-launch)
remains available. [Release notes and checksums](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-renderer-startup)
identify each download. Versions for older game releases may be incompatible.
