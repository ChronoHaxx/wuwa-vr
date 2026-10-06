# WuWa VR

Free, unofficial Wuthering Waves VR mod, built on praydog's UEVR and community work.

**[Download the Windows installer · beta 1.0.3 · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-06-recovery/WuWa-VR-Setup.exe)** · [Release notes & source](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-recovery) · [简体中文](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html)

Install **WuWa VR**, then follow **01 Game → 02 Install VR → 03 Headset or simulator**.
The small installer downloads the separate VR mod (about **54 MB**) in step 02,
so internet is needed for first installation.
Choose **Launch in VR** and accept Windows permission. Steam starts the selected
game through Steam; with Kuro, press **Play** in its launcher. The installed app offers launcher updates while idle;
VR package updates are in step 02. Updates preserve settings, backups and recordings.

Launcher **1.0.3** adds visible startup recovery and confirmed process stopping
under **Troubleshooting**. Scan, review the process identities, select eligible
launcher workers, then confirm before stopping them. Game, Steam and VR runtime
processes are excluded. The paired **beta-2026-10-06-recovery** VR package adds
startup diagnostics and fresh-PC simulator checks; updating the app alone keeps
the previously selected VR package, so select/install the new package in step 02.

Renderer/injector **steam-20261006** are unchanged. Background checks and isolated
package tests passed; the affected Windows 11 Steam PC and physical headset still
need confirmation. This is a recovery beta, not a claim of universal compatibility.

[Watch / guide](https://chronohaxx.github.io/wuwa-vr/) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues)

[Portable ZIP (advanced fallback)](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-06-recovery/WuWa-VR-Launcher.zip)
uses the older browser launcher: extract everything, then open **WuWa VR Launcher.exe**.
No separate Python installation is needed. [Previous beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-steam).
[Setup](docs/START-HERE.md) · [Xbox shortcuts](docs/CONTROLS.md) · [Recovery](docs/TROUBLESHOOTING.md)

Stereo view, first person, Xbox controls, adjustable HUD, freecam and an optional
6DOF window. Full-animation first-person aiming is still unresolved.

**Use at your own risk.** Unofficial injection can trigger anti-cheat or account
bans. Not affiliated with Kuro Games. [Risk notice](docs/RISK.md).
Owner-PC Steam startup was confirmed in the previous beta. Other-PC recovery,
Epic injection and fresh-PC compatibility remain unverified.

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
