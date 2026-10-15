# WuWa VR

Free, unofficial Wuthering Waves VR mod for Windows, built on praydog's UEVR and community work.

**[Download the Windows installer · 1.2.3 beta · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-2-3/WuWa-VR-Setup.exe)** · [Website](https://chronohaxx.github.io/wuwa-vr/) · [Release notes and source](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-3)

Install once, then **01 Choose game → 02 Install VR → 03 Choose headset or simulator → Launch in VR**. Saved choices are remembered for later launches. Steam starts through Steam; Kuro users press Play in the Kuro launcher. The first VR installation needs internet; no separate Python installation is needed.

The launcher app and VR package display their own versions. Update the app to **1.2.3** for this release: when it starts with the game closed, it installs the matching VR package by itself, and **Back to …** in step 02 returns to the one you had. Every published version is listed in Troubleshooting → Versions & updates. Launch requires a valid headset runtime or the simulator belonging to the selected installed VR package.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1 in the headset with the Kuro launcher copy of the game, and an earlier 1.2.3 test build in the headset (SteamVR) with the Kuro launcher copy. The final 1.2.3 test builds were tested by the owner on the OpenXR simulator with both the Steam copy and the Kuro launcher copy: menus, the mouse and the controller cursor in the 16:9 and taller layouts, story pictures and story scenes, the character screen and the translations; on the Steam copy the UI redirect starts and the stereo fixes pass their code checks. The 1.2.3 release build has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

Stereo VR, first person, mono theatre, a stereoscopic screen, portal and diorama shortcuts, adjustable HUD, and optional VR-controller walking. See [controls and limits](docs/CONTROLS.md).

**Use at your own risk:** unofficial injection can trigger anti-cheat or account restrictions. Not approved by Kuro Games. [Risk notice](docs/RISK.md).

[Player guide](docs/START-HERE.md) · [Troubleshooting](docs/TROUBLESHOOTING.md) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues) · [简体中文](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html)

## Source and downloads

- **[Open-source launcher](launcher/native/)** — [MIT licence](launcher/native/LICENSE.txt).
- **[Public mod source, Lua and translations](mod/)** — changed files and pinned patches; [build instructions](mod/BUILD.md). Upstream and community components retain their own licences; the combined mod is not under one open-source licence.
- **[Release files and checksums](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-3)** — installer, portable fallback and matching source export. VirusTotal reports, when available, identify exact file hashes; scans do not certify safety.
- `site/` and `docs/` — website and guides. [Component licences](LICENSE.md) and [credits](CREDITS.md).

[Portable ZIP](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-2-3/WuWa-VR-Launcher.zip) is an advanced fallback using the older browser interface. Extract it completely; it does not self-update the desktop app. [Previous 1.2.2 release](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-2) remains available.

Forks and improvements are welcome where the component licences permit. Please credit the original authors and keep community access free. [Optional Ko-fi support](https://ko-fi.com/chronohax); no paid access or promised support.
