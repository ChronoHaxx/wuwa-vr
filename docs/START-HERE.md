# Start playing

**1.1.1 beta · Wuthering Waves 3.7 · Windows x64.**
[Download WuWa-VR-Setup.exe](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-1-1/WuWa-VR-Setup.exe), install it for your Windows user, then open **WuWa VR** from the Start menu. The first VR package installation needs internet. No separate Python installation is needed.

1. **01 · Game.** Choose your Steam or Kuro installation. For Steam, browse to **Wuthering Waves.exe** if necessary; sign into Steam first.
2. **02 · Install VR.** Read and accept the account-risk notice, then install **1.1.1 beta**. This is the VR package; the launcher app has its own version shown separately.
3. **03 · Headset or simulator.** Start your headset software and choose its valid OpenXR runtime, or explicitly select **Use bundled simulator**. Select **Launch in VR** and accept Windows permission. Steam starts through Steam; Kuro users press **Play** in its launcher.

Your choices are saved, so later sessions can go straight to **Launch in VR** when ready. A simulator from an older package or an invalid/missing runtime blocks launch and shows what to fix. Close the game and injector before switching runtimes. Updating the app never changes the system runtime automatically.

The simulator lets you inspect the view without a headset. It does not establish headset comfort or compatibility. See [controller shortcuts](CONTROLS.md) for Xbox and PlayStation support.

**Account risk:** unofficial injection can trigger anti-cheat or account restrictions. Not approved by Kuro Games. [Read the risk notice](RISK.md). The installer is unsigned; download from the project release and check its checksum. Do not disable antivirus, SmartScreen or anti-cheat.

## Updates and versions

**Launcher app** is the installed desktop application. **VR package** contains the game integration, scripts and bundled simulator. The two statuses are separate: an up-to-date app can still be using an older VR package.

- **Update launcher:** download and verify the app update. When game, injector, recording and other operations are idle, select **Restart to update** and confirm.
- **VR package update:** use the package update option or **02 → Versions & updates**, deliberately choose **1.1.1 beta**, then install it. Checking for updates preserves your current choice, including rollback.
- **Rollback:** when idle, use **Troubleshooting → Repair & recovery → Use previous installed version**. This changes the VR package, not the app. If using the simulator, select the restored package's simulator before launching.
- If the app cannot update, close it and run the latest installer from the website. Reinstallation preserves settings and does not stop existing workers.

Older date-and-fix-name packages are historical builds. New releases use a readable version such as **1.1.1 beta**; exact package IDs remain in diagnostics for bug reports.

## Controls and current limits

Open UEVR with **L3 + R3**, then **VR → WuWa Controls**. Both stick clicks are L3/R3 on Xbox and PlayStation.

- Hold both triggers first, then click **R3** for **mono theatre**: the same scene and HUD in both eyes.
- Hold both triggers first, then hold **L3 for 0.8 seconds** for a **stereoscopic screen**.
- Release all controls before repeating, with UEVR and HUD/mouse adjustment closed.

Cinematic framing is on by default and was accepted in a simulator replay. Automatic cinematic switching is experimental, **off by default**, and has not been verified with a real prerendered movie. Keep the manual shortcuts available.

The optional NPC rim-light suppression starts off and also removes intended rim lighting nearby; the **underlying stereo rendering fault is unresolved**. Scene/dialogue stalls, HUD-aspect refresh failures, some reflections/fog and moving flat-menu backgrounds remain open. Manual mono theatre can help with menus. See [troubleshooting](TROUBLESHOOTING.md).

The user confirmed 1.0.10 startup on the previously affected Windows 11 Steam simulator PC, and 1.1.0 through Kuro on the owner's PC. The owner's 1.1.0 Steam route crashed. Version 1.1.1 repairs the resize recursion found in that crash dump; controlled graphics and launcher tests pass, but this repair still needs real Steam and headset testing.

## Recovery, recording and uninstall

Use **Troubleshooting → Copy diagnostics** when launch stalls. **Stop waiting** cancels the startup wait; it does not close the game. **Find stuck launcher processes** lists identities and reasons. Review eligible launcher workers, then explicitly confirm **Stop selected**. Game, Steam, headset/runtime and injector processes are excluded.

**Close launcher only** closes an unresponsive launcher connection while leaving background work untouched. It does not claim that work stopped. Use **Retry connection** after reviewing recovery results. Do not start a second injector to resolve a stalled launch.

**Developer tools** in the footer holds recording and advanced diagnostics. These are optional for normal play.

**Prepare uninstall** checks removable verified downloads and asks for confirmation. Windows Apps uninstall also attempts bounded cleanup. Active runtime files, busy/unverified files, settings, backups and recordings may be retained; the remaining-file report gives exact reasons. Updates preserve user data under `%LOCALAPPDATA%\WuWa VR Manager` and `%LOCALAPPDATA%\WuWa VR Launcher`.

## Portable fallback and source

The [portable ZIP](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-1-1/WuWa-VR-Launcher.zip) uses the older browser interface and does not self-update the desktop app. Extract the whole archive and keep its folders together.

The launcher is MIT open source. The mod's source changes and build instructions are public; upstream components retain their own licences. [Source repository](https://github.com/ChronoHaxx/wuwa-vr) · [component licences](../LICENSE.md).

[Previous 1.0.10 beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-enum-startup) is available for rollback.
