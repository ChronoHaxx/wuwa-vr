# WuWa VR

Free, unofficial Wuthering Waves VR mod, built on praydog's UEVR and community work.

**[Download the Windows installer · beta 1.0.6 · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-06-window-startup/WuWa-VR-Setup.exe)** · [Release notes & source](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-window-startup) · [简体中文](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html)

Install **WuWa VR**, then follow **01 Game → 02 Install VR → 03 Headset or simulator**.
The small installer downloads the separate VR mod (about **54 MB**) in step 02,
so internet is needed for first installation.
Choose **Launch in VR** and accept Windows permission. Steam starts the selected
game through Steam; with Kuro, press **Play** in its launcher. The installed app offers launcher updates while idle;
VR package updates are in step 02. Updates preserve settings, backups and recordings.

Update both the app to **1.0.6** and the step 02 package to
**beta-2026-10-06-window-startup** / build **window-startup-20261006**.
Updating the app preserves the selected VR package; explicitly select/install
the new package after restarting. Existing recovery, cancellation, **Close
launcher only** and guarded process stopping remain under **Troubleshooting**.
Game, Steam and VR runtime processes are excluded from confirmed recovery stops.

The affected Windows 11 Steam PC still failed to reach VR in 1.0.5. This follow-up repairs an unchecked DirectX 12 window lookup: it queries the supported interface and falls back to the base swapchain description when needed, while retaining window validation. Steam process checks now use limited access and retain verified identity only while the same process handle is alive. Repeated log lines are compacted so startup transitions remain visible. These are compatibility repairs and better evidence, not a confirmed fix on that PC; its retest and headset acceptance remain pending.

[Watch / guide](https://chronohaxx.github.io/wuwa-vr/) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues)

[Portable ZIP (advanced fallback)](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-06-window-startup/WuWa-VR-Launcher.zip)
uses the older browser launcher: extract everything, then open **WuWa VR Launcher.exe**.
No separate Python installation is needed. [Previous beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-renderer-startup).
Keep older packages for **Troubleshooting → Repair & recovery → Use previous
installed version**. Close the game/injector and stop recording first. This rolls
back the VR package, not the desktop app; the old renderer-startup issue may return.
[Setup](docs/START-HERE.md) · [Xbox shortcuts](docs/CONTROLS.md) · [Recovery](docs/TROUBLESHOOTING.md)

Stereo view, first person, Xbox controls, adjustable HUD, freecam and an optional
6DOF window. Full-animation first-person aiming is still unresolved.

**Use at your own risk.** Unofficial injection can trigger anti-cheat or account
bans. Not affiliated with Kuro Games. [Risk notice](docs/RISK.md).
Owner-PC Steam startup was confirmed in the previous beta. Other-PC recovery,
Epic injection and fresh-PC compatibility remain unverified.

Uninstall: **Troubleshooting → Prepare uninstall** checks and removes verified
downloads first, with confirmation. Use process recovery for a stalled worker, then
retry cleanup. Windows Apps uninstall also runs a bounded cleanup hook. Active
OpenXR packages, busy/unverified files and user data are retained with reasons in
`%LOCALAPPDATA%\WuWa VR Manager\uninstall-result.txt`; retained-file reports
open after Windows uninstall. Recordings, backups and settings are preserved.

## Source

- [Native changes, Lua and translations](mod/): browsable files plus pinned patches.
- [Reconstruct the native source](mod/BUILD.md).
- [Launcher and recording helpers](launcher/dev/).
- `site/` and `docs/`: website and visual guide. Edit the sources, then `npm install`
  and `npm run build`. Pages deploys from `site/` when a reviewed site change reaches `main`, or by the
manual GitHub workflow.

Small fixes, forks and feedback welcome under the applicable component terms.
Credit the [original authors](CREDITS.md). The combined mod is **not blanket MIT**;
our independent website/guide work is MIT and upstream notices remain in effect.
Please keep access free; this is a community request, not an added MIT condition.

[Optional Ko-fi support](https://ko-fi.com/chronohax). No obligation, paid access,
promised updates or lifetime support.
