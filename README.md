# WuWa VR

Free, unofficial Wuthering Waves VR mod, built on praydog's UEVR and community work.

**[Download the beta · 1 Oct 18:17 BST · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-01-1817)** · [60-second explainer and beginner guide](https://chronohaxx.github.io/wuwa-vr/understanding.html)

One build, **3.7 + reflection + far lighting fix v2 · 1 Oct 18:17 BST**, tested by the owner:
the HUD, menus and ultimate-camera fix work on game version 3.7; far trees, props
and far lighting now match between the eyes; Resonators and team-screen
reflections sit in the right place. Still open: weapon and Echo submenu
reflections. The ZIP is about 51 MB.

[Watch / guide](https://chronohaxx.github.io/wuwa-vr/) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues)

Extract **WuWa-VR-Launcher.zip**, then open **WuWa VR Launcher.exe**.
Keep its companion folders together. No Python installation is needed.
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
