# Start playing

**Account risk:** this unofficial injector may be detected or blocked by
anti-cheat, and account restrictions or a ban are possible. Use at your own
risk; there is no publisher approval or account-safety guarantee.
[Read the risk notice](RISK.md) before launching.

[Download the experimental ZIP](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/experimental-2026-09-28-1628)
or the [older beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-09-26-230213),
extract it, and open **WuWa VR Launcher.exe**. Keep the `app` and `python` folders
beside it. You need your own game, a Windows PC VR headset/runtime, and an
Xbox/XInput controller connected to the PC. No separate Python installation
is needed.

## Which build?

There are two public packages, and the [README](../README.md) headlines the
first:

- The **[experimental package](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/experimental-2026-09-28-1628)** (asset **WuWa-VR-Launcher-Experimental.zip**) contains **Camera candidate + trigger controls · 28 Sep 16:27**. Select that build in its launcher; the older beta stays initially selected. This is a testing-tools update with camera work carried forward, **not a confirmed foliage or ultimate fix**, and its live acceptance is pending.
- The **[older beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-09-26-230213)** (asset **WuWa-VR-Launcher.zip**) contains **Stereo, menus and languages · 26 Sep 22:42 BST**, from the owner-played 23:02 package; its native files and supplied profile are unchanged.

Which one should you pick? Take the experimental package to test the camera
candidate and trigger controls; take the older beta for the more-settled
baseline. Both share the same gameplay bindings (see [controls](CONTROLS.md)).
Your saved settings are retained; use **Reset this build** for supplied defaults.
Older builds stay in separate releases to keep downloads smaller.

## Install and launch

1. Extract the whole ZIP to a simple path such as `C:/Games/WuWa VR`.
2. Start your headset software, then open **WuWa VR Launcher.exe**.
3. Read the risk notice, choose how you start the game, and click **Apply & launch**.
4. Accept Windows' prompt, then press **Play** in the game launcher.
5. In game, **L3 + R3** (or Insert) opens UEVR settings. Custom options are under
   **VR → WuWa Controls**. Close settings before using gameplay shortcuts.

The EXE is unsigned; Windows may warn. Download from this project's GitHub
Releases and check the published hash if unsure. Do not disable antivirus or
anti-cheat. Injection needs elevation to match the game.

| Game installation | Evidence |
| --- | --- |
| Standalone official launcher | The 23:02 package reached gameplay on the owner's PC |
| Steam game version | An earlier injection attempt failed; current fixes not retested |
| Epic game version | Untested |

SteamVR / Steam Link headset support does not establish Steam-store injection.
The full latest-build headset check and a clean-PC test remain pending.

**Simulator or headset:** use the two buttons at the top of the launcher with
WuWa and the injector closed. This changes OpenXR globally, asks for Windows
permission, and applies next launch. **Use headset** restores the previously
saved runtime. Opening the launcher itself does not change it.

The launcher changes your UEVR profile, not game files. Backups are in the
launcher's data folder under `snapshots`.

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
