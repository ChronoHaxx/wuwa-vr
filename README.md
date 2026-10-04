# WuWa VR

Free, unofficial Wuthering Waves VR mod, built on praydog's UEVR and community work.

**[Download the Windows installer · beta 1.0.0 · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-04-launcher/WuWa-VR-Setup.exe)** · [Release notes & source](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-04-launcher) · [简体中文](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html)

Install **WuWa VR**, then follow **01 Game → 02 Install VR → 03 Headset or simulator**.
The small installer downloads the separate VR mod (about **54 MB**) in step 02,
so internet is needed for first installation.
Choose **Launch in VR**, accept Windows permission, then press **Play** in the
official game launcher. The installed app offers launcher updates while idle;
VR package updates are in step 02. Updates preserve settings, backups and recordings.

Backend **npc-rim-20261004** retains the owner-reported 2D brightness and ultimate
camera fixes. Its optional NPC rim suppression starts **off** and also removes
intended rim lighting on nearby characters. The underlying stereo fault remains
unresolved, and the new toggle still needs physical-headset checking.

[Watch / guide](https://chronohaxx.github.io/wuwa-vr/) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues)

[Portable ZIP (advanced fallback)](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-2026-10-04-launcher/WuWa-VR-Launcher.zip)
uses the older browser launcher: extract everything, then open **WuWa VR Launcher.exe**.
No separate Python installation is needed. [Previous beta](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-01-1817).
[Setup](docs/START-HERE.md) · [Xbox shortcuts](docs/CONTROLS.md) · [Recovery](docs/TROUBLESHOOTING.md)

Stereo view, first person, Xbox controls, adjustable HUD, freecam and an optional
6DOF window. Full-animation first-person aiming is still unresolved.

**Use at your own risk.** Unofficial injection can trigger anti-cheat or account
bans. Not affiliated with Kuro Games. [Risk notice](docs/RISK.md).
Steam/Epic game injection and fresh-PC compatibility are unverified.

## Source

- [Native changes, Lua and translations](mod/): browsable files plus pinned patches.
- [Reconstruct the native source](mod/BUILD.md).
- [Launcher and recording helpers](launcher/dev/).
- `site/` and `docs/`: website and visual guide. Edit the sources, then `npm install`
  and `npm run build`. Pages deploys from `site/` via the manual GitHub workflow.

Small fixes, forks and feedback welcome under the applicable component terms.
Credit the [original authors](CREDITS.md). The combined mod is **not blanket MIT**;
our independent website/guide work is MIT and upstream notices remain in effect.
Please keep access free; this is a community request, not an added MIT condition.

[Optional Ko-fi support](https://ko-fi.com/chronohax). No obligation, paid access,
promised updates or lifetime support.
