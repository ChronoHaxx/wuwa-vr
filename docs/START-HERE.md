# Start playing

**Account risk:** this unofficial injector may be detected or blocked by
anti-cheat, and account restrictions or a ban are possible. Use at your own
risk; there is no publisher approval or account-safety guarantee.
[Read the risk notice](RISK.md) before launching.

This guide describes the **WuWa VR Launcher private test package**: a ZIP
containing a folder and `WuWa VR Launcher.exe`. There
is no public download yet. You need your own Wuthering Waves installation, a PC
VR headset whose software provides OpenXR (tested: Quest Pro through Steam Link
and SteamVR) and an Xbox/XInput controller connected to the PC.

## Which build?

| Build | Status | Choose it when |
| --- | --- | --- |
| Camera + acro checkpoint · 24 Sep 22:37 BST | Owner-tested in a headset; the launcher's default | You want the version that has actually been played |
| Stereo and camera candidate · 26 Sep 20:11 BST | Compiled and component-tested; **game/headset test pending** | You are doing the current combined playtest, including camera handovers and the NPC-label candidate |
| Earlier 26 Sep candidates | Preserved comparison builds; not a complete headset pass | You need to compare a reported regression with a specific saved revision |

Each build keeps its own saved settings. You can switch while the game is
closed. See [controls](CONTROLS.md) for the differences between them.
Package manifests identify the exact contents; not every historical build is
included in every ZIP. The full checkpoint list remains available in the guide.
Reflections, one-eye materials and full-animation headset targeting remain open
in the current candidate. Use the [combined checklist](NEXT-SESSION.md) once.

## Install and launch

**Game version:** the standalone official launcher is the previously working
route. The new portable launcher package has **not yet completed a real game
launch or headset test**. Detecting an installation does not verify injection.

| Game installation | Injection evidence |
| --- | --- |
| Standalone official launcher | Previously worked in the development setup; portable-package launch awaiting testing |
| Steam version | Previous injection attempt failed; not retested with the current fixes |
| Epic version | Untested |

SteamVR and Steam Link are the headset connection in our tested setup. That
does not establish support for the **Steam store version** of the game.

1. Extract the **whole** ZIP to a normal folder, for example
   `C:\Games\WuWa VR`. It cannot run from inside the ZIP window. A path with
   only English letters is safest; other characters have not been tested with
   the injector.
2. Start your headset software. For the combined Quest Pro test, connect through
   Steam Link and use SteamVR as the active OpenXR runtime. Check the runtime
   shown at the top of the launcher page after opening it.
3. Double-click **WuWa VR Launcher.exe**. Your browser opens a page served only
   on your PC (`http://127.0.0.1:…`). Windows may say it protected your PC,
   because the launcher is new and unsigned. Only continue if you received the
   ZIP from the project owner directly. Do not disable SmartScreen, antivirus
   or anti-cheat.
4. On the page: read the risk notice and tick the box, choose a build and how
   you start the game (prefer the standalone official launcher; manual
   Steam/Epic startup is experimental), then press
   **Apply & launch**.
5. Windows asks for administrator permission, because the game itself runs as
   administrator and the injector has to match it. Choose **Yes**, then press
   **Play** in your chosen game launcher. The page shows progress. It
   cannot press these protected buttons for you.
6. In the game, press **L3 + R3** (or Insert) for UEVR settings. Most custom
   features live in **VR → WuWa Controls**. Close settings before using the
   gameplay shortcuts. Release all controller buttons after changing modes.

The launcher never changes game files. Before its first change it backs up your
UEVR profile to `%LOCALAPPDATA%\WuWa VR Launcher\snapshots`.

**Simulator or headset:** the top of the page has **Use headset** and **Use
simulator** buttons. Use them with the game and injector closed. They ask for
Windows permission and change OpenXR globally for other PC apps too, taking
effect on the next launch. **Use headset** restores the runtime saved before
the simulator was enabled; that may be Virtual Desktop or another runtime,
not necessarily SteamVR. Opening the launcher alone does not switch it.

## First-minute recovery

The candidate's supplied profile starts with game UI, Xbox shortcuts, Xbox
mouse shortcuts and the shortcut sheet enabled. The 22:37 build keeps the
settings it was saved with: game UI and sheet on, Xbox mouse shortcuts off.
Both start in the game camera; experimental collision is off.

- **L3 + B** shows the game UI. A blurred menu with no buttons can simply mean
  the UI is hidden. The candidate also has **Show game UI now** in WuWa Controls.
- Enable **Xbox mouse shortcuts** in WuWa Controls before using mouse/HUD
  adjustment. Leave **Physical gamepad passthrough** off to use the mod's
  shortcuts; passthrough deliberately bypasses them.
- Leave **Native Stereo Fix → Enabled** off. Keep **Native Stereo** as the
  rendering method; those are different settings.
- Start in the ordinary game camera, with the portal and freecam off. Press
  **L3 + A** while sitting comfortably to recenter. Increase HUD size separately
  from world scale.

## Make it comfortable

**VR → WuWa Controls** holds the Xbox shortcuts, camera and HUD controls, with
**Optional experiments and diagnostics** last in the candidate.
**WindowMode → 6DOF Window** controls the portal.
**LuaLoader → Script UI → WuWa VR comfort controls** contains extra camera/HUD
bookmarks and resets.

For a personal HUD, choose follow-view and adjust panel size and distance
together. For a portal, enable **Attach HUD to window** and adjust coverage.
For a stationary panel, turn follow-view off and recenter where you want it.
The sheet's feet position uses the recentered tracking origin, not the game
character's feet. See [comfort settings](COMFORT.md).

## Switch, undo or remove

UEVR saves its normal settings when the game exits normally. Close the game
before switching builds; the launcher refuses while it is running.

- **Reset this build** (under Recovery on the launcher page) replaces that
  build's settings with the ones it was supplied with, after a backup.
- **Restore my settings from before WuWa VR** returns your UEVR profile and
  injector choice to how they were before the launcher first changed them. The
  mod's settings stay in a backup, so applying the build later brings them back.
- **To remove everything:** if the simulator is active, use **Use headset**
  first and confirm the displayed runtime no longer points to this package.
  Then restore your settings, choose **Stop launcher**, and delete the WuWa VR
  folder. Delete `%LOCALAPPDATA%\WuWa VR Launcher` too if
  you no longer need its backups and logs. UEVR's own log files in its profile
  folder are left for you to inspect or delete.
- **Check package files** on the page, or `Verify-Package.ps1` in the folder,
  confirms that no packaged file is missing or changed.

Restore checks the backup's recorded file hashes before changing your active
settings. If files are missing or changed, it stops and keeps your current
settings. Backups from older packages without a recorded file inventory are
kept for manual recovery; they cannot be verified automatically. Keep those
backups rather than deleting them or treating their presence as proof that
recovery succeeded.

In the development workspace, **Start WuWa VR Launcher.cmd** opens the fuller
diagnostic dashboard instead. It needs Python 3.10+ and is not the player
package. A private recovery archive is a verified copy of one checkpoint, not
an installer.

If something fails, use [troubleshooting](TROUBLESHOOTING.md). For the next
candidate, [one short sequence](NEXT-SESSION.md) covers the important changes
without repeated timed tests.
