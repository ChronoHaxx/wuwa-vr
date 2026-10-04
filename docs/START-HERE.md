# Start playing

**Beta for game 3.7 · desktop app 1.0.1.**
[Download WuWa-VR-Setup.exe](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-04-cinematic/WuWa-VR-Setup.exe)
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

1. **01 · Game.** Check the detected official game launcher, or choose its location.
2. **02 · Install VR.** Read and accept the account-risk notice, then install the
   selected VR version. Existing settings and backups are kept.
3. **03 · Headset or simulator.** Start your headset software and check the
   displayed runtime, or deliberately choose the simulator. Select **Launch in VR**,
   accept the Windows permission prompt, then press **Play** in the official
   game launcher.

The installer is unsigned, so Windows may warn. Download from this project's
GitHub Releases and compare the published checksum if unsure. Do not disable
antivirus, SmartScreen or anti-cheat. Game injection asks for Windows permission
separately from installing the app for your user.

The standalone official launcher is the tested route. Steam/Epic game injection
and installation on a fresh PC remain unverified. SteamVR headset support does
not establish support for the Steam-store game version.

Choosing a simulator or restoring the headset runtime changes the system OpenXR
runtime and may ask for Windows permission. Close the game and injector first.
Opening or updating the app does not automatically select the simulator.

## Updates

- Open **02 → Versions & updates → Check updates**.
- **Desktop app:** choose **Update launcher** to download and verify the update;
  this does not restart the app. When it is ready, close the game and injector,
  stop recording and finish other operations, then choose **Restart to update**
  and confirm. Installing or updating the desktop app preserves your selected VR package.
- **VR package:** in **step 02**, choose **beta-2026-10-04-cinematic** and install it,
  or select it if already installed, to use **cinematic-20261004**. Older installed VR versions remain available for rollback.

Updating keeps settings, backups, logs and recordings. The manager uses
`%LOCALAPPDATA%\WuWa VR Manager`; the existing launcher data stays in
`%LOCALAPPDATA%\WuWa VR Launcher`. Do not delete these folders to update.

## What this beta changes

This release uses backend **cinematic-20261004**. Cinematic framing is **on by
default**. The owner confirmed the simulator replay on private build
**screen-comfort-r1**; sampled frames from the latest 68-second recording show
matching letterbox heights. **Headset comfort remains pending.** Earlier accepted
foliage, far indirect-lighting, main Resonators reflection, ultimate-camera and
2D-brightness fixes are retained.

Automatic cinema remains **off by default and unverified**. It did not activate
for the latest reported in-engine scene; a prerendered movie has not been tested.
Long scene/dialogue stalls, HUD-aspect refresh and moving backgrounds behind
flat menus remain open. Manual mono theatre is a menu workaround.

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

## Rollback, repair and removal

Close the game, injector and recorder before switching or repairing VR packages.
Use step 02 to choose an installed older version; repair creates a fresh verified
copy. Personal settings and recordings are not replaced with package defaults.

Before removing the app, restore the headset runtime if you selected the bundled
simulator, and use **Restore my settings from before WuWa VR** in recovery if you
want to undo the profile changes. Then uninstall **WuWa VR** through Windows.
Keep the data folders if you want to retain recordings, backups and settings.

## Portable fallback

[WuWa-VR-Launcher.zip](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-04-cinematic/WuWa-VR-Launcher.zip)
is an advanced fallback. Extract the whole archive, keep `app` and `python`
together, and open **WuWa VR Launcher.exe**. It uses the older browser interface;
it does not install or self-update the desktop app. Choose **Apply & launch**,
accept Windows permission, then press **Play** in the official game launcher.

The [previous 4 October launcher beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-04-launcher)
remains available. [Release notes and checksums](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-04-cinematic)
identify each download. Versions for older game releases may be incompatible.
