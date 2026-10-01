# Start playing

**Account risk:** this unofficial mod injects code into the game. Anti-cheat may
detect it, and account restrictions or a ban are possible. Kuro Games has not
approved it. [Read the risk notice](RISK.md) before launching.

## What you need

- Wuthering Waves on Windows, from the official launcher.
- A PC VR headset and its software (SteamVR, Virtual Desktop or similar).
- An Xbox (XInput) controller connected to the PC.

## Install and launch

1. Download **WuWa-VR-Launcher.zip** from the latest beta on
   [Releases](https://github.com/ChronoHaxx/wuwa-vr/releases). Each beta lists
   the game version it supports.
2. Extract the whole ZIP to a simple path such as `C:/Games/WuWa VR`. Python is
   included; keep the `app` and `python` folders beside the EXE.
3. Start your headset software, then open **WuWa VR Launcher.exe**. A page opens
   in your browser.
4. Read the risk notice, tick the box and click **Apply & launch**. Accept the
   Windows prompt, then press **Play** in the game launcher.
5. In game, **L3 + R3** (both stick clicks) or Insert opens UEVR. Our settings
   are under **VR → WuWa Controls**.

The EXE is unsigned, so Windows may warn. Download only from this project's
Releases and compare the published checksum if unsure. Do not disable antivirus
or anti-cheat.

The official standalone launcher is the tested route. The Steam and Epic
versions of the game are untested.

**Simulator or headset:** the launcher's two buttons at the top switch the
system's OpenXR runtime (with the game closed); **Use headset** restores the
previous one. The launcher changes your UEVR profile, not game files, and keeps
backups.

## First minute in game

- The profile starts with **Native Stereo**, **Native Stereo Fix** and **Same
  Pass** on. Keep them on: the stereo fixes depend on them.
- **L3 + B** shows or hides the game UI. A blurred menu with no buttons usually
  means the UI is hidden.
- **L3 + A** recenters while you sit comfortably.
- **L3 + Menu** shows the illustrated shortcut sheet.
- To use HUD/mouse adjustment (L3 + LB), enable **Xbox mouse shortcuts** in
  WuWa Controls and leave **Physical gamepad passthrough** off.

## Update to a new beta

Extract the new ZIP and open its **WuWa VR Launcher.exe**. Each beta's folder is
named with its date and time, so versions sit side by side; delete older folders
when you no longer need them. Settings, backups and logs live in
`%LOCALAPPDATA%\WuWa VR Launcher` and are shared by every version.

## Switch, undo or remove

Close the game before switching builds; the launcher refuses while it runs.

- **Reset this build** restores the build's supplied settings, after a backup.
- **Restore my settings from before WuWa VR** returns your UEVR profile and
  injector choice to how they were before. Restore checks the backup's file
  hashes and stops without changing anything if they do not match.
- **Remove everything:** if the simulator is active, click **Use headset** first.
  Then restore your settings, click **Stop launcher** and delete the WuWa VR
  folder. Delete `%LOCALAPPDATA%\WuWa VR Launcher` too if you no longer need its
  backups and logs.
- **Check package files** (or `Verify-Package.ps1` in the folder) confirms no
  packaged file is missing or changed.

Problems? See [troubleshooting](TROUBLESHOOTING.md).
