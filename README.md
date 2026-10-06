# WuWa VR

Free, unofficial Wuthering Waves VR mod for Windows, built on praydog's UEVR and community work.

**[Download the Windows installer · 1.1.1 beta · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-1-1/WuWa-VR-Setup.exe)** · [Website](https://chronohaxx.github.io/wuwa-vr/) · [Release notes and source](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-1)

Install once, then **01 Choose game → 02 Install VR → 03 Choose headset or simulator → Launch in VR**. Saved choices are remembered for later launches. Steam starts through Steam; Kuro users press Play in the Kuro launcher. The first VR installation needs internet; no separate Python installation is needed.

The launcher app and VR package display their own versions. Update both to **1.1.1** for this release; updating the app preserves your existing package choice. Older packages remain available for deliberate rollback. Launch requires a valid headset runtime or the simulator belonging to the selected installed VR package.

The user confirmed 1.0.10 startup on the previously affected Windows 11 Steam simulator PC, and 1.1.0 through Kuro on the owner's PC. The owner's 1.1.0 Steam route crashed. Version 1.1.1 repairs the resize recursion found in that crash dump; controlled graphics and launcher tests pass, but this repair still needs real Steam and headset testing.

Stereo VR, first person, mono theatre, a stereoscopic screen, portal and diorama shortcuts, adjustable HUD, and optional VR-controller walking. See [controls and limits](docs/CONTROLS.md).

**Use at your own risk:** unofficial injection can trigger anti-cheat or account restrictions. Not approved by Kuro Games. [Risk notice](docs/RISK.md).

[Player guide](docs/START-HERE.md) · [Troubleshooting](docs/TROUBLESHOOTING.md) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues) · [简体中文](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html)

## Source and downloads

- **[Open-source launcher](launcher/native/)** — [MIT licence](launcher/native/LICENSE.txt).
- **[Public mod source, Lua and translations](mod/)** — changed files and pinned patches; [build instructions](mod/BUILD.md). Upstream and community components retain their own licences; the combined mod is not under one open-source licence.
- **[Release files and checksums](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-1)** — installer, portable fallback and matching source export. VirusTotal reports, when available, identify exact file hashes; scans do not certify safety.
- `site/` and `docs/` — website and guides. [Component licences](LICENSE.md) and [credits](CREDITS.md).

[Portable ZIP](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-1-1/WuWa-VR-Launcher.zip) is an advanced fallback using the older browser interface. Extract it completely; it does not self-update the desktop app. [Previous 1.1.0 release](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-0) remains available.

Forks and improvements are welcome where the component licences permit. Please credit the original authors and keep community access free. [Optional Ko-fi support](https://ko-fi.com/chronohax); no paid access or promised support.
