# WuWa VR

Free, unofficial Wuthering Waves VR mod for Windows, built on praydog's UEVR and community work.

**[Download the Windows installer · 1.2.0 beta · game 3.7](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-2-0/WuWa-VR-Setup.exe)** · [Website](https://chronohaxx.github.io/wuwa-vr/) · [Release notes and source](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-0)

Install once, then **01 Choose game → 02 Install VR → 03 Choose headset or simulator → Launch in VR**. Saved choices are remembered for later launches. Steam starts through Steam; Kuro users press Play in the Kuro launcher. The first VR installation needs internet; no separate Python installation is needed.

The launcher app and VR package display their own versions. Update both to **1.2.0** for this release; updating the app preserves your existing package choice. Older packages remain available for deliberate rollback. Launch requires a valid headset runtime or the simulator belonging to the selected installed VR package.

The owner played this release in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill) through private tests: the new VR menu with the laser and a gamepad, the view shortcuts, full-VR close-ups that follow the camera without getting stuck, blur in both eyes, from the start of a story scene, the menu in front of the stereo screen, a gamepad taking over at once from a put-down controller, the controls cards and the translated menu and VR controllers and a Reality Runner treadmill working together. The last test's additions (treadmill runs in the VR menu, the Controllers page, ambient occlusion off, the L3 card pinned low) and recording in 5-minute parts still need a headset check. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot, and in full VR the HUD can stay squashed until ESC.

Stereo VR, first person, mono theatre, a stereoscopic screen, portal and diorama shortcuts, adjustable HUD, and optional VR-controller walking. See [controls and limits](docs/CONTROLS.md).

**Use at your own risk:** unofficial injection can trigger anti-cheat or account restrictions. Not approved by Kuro Games. [Risk notice](docs/RISK.md).

[Player guide](docs/START-HERE.md) · [Troubleshooting](docs/TROUBLESHOOTING.md) · [Report an issue](https://github.com/ChronoHaxx/wuwa-vr/issues) · [简体中文](https://chronohaxx.github.io/wuwa-vr/l/zh-Hans.html)

## Source and downloads

- **[Open-source launcher](launcher/native/)** — [MIT licence](launcher/native/LICENSE.txt).
- **[Public mod source, Lua and translations](mod/)** — changed files and pinned patches; [build instructions](mod/BUILD.md). Upstream and community components retain their own licences; the combined mod is not under one open-source licence.
- **[Release files and checksums](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-0)** — installer, portable fallback and matching source export. VirusTotal reports, when available, identify exact file hashes; scans do not certify safety.
- `site/` and `docs/` — website and guides. [Component licences](LICENSE.md) and [credits](CREDITS.md).

[Portable ZIP](https://github.com/ChronoHaxx/wuwa-vr/releases/download/beta-1-2-0/WuWa-VR-Launcher.zip) is an advanced fallback using the older browser interface. Extract it completely; it does not self-update the desktop app. [Previous 1.1.2 release](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-2) remains available.

Forks and improvements are welcome where the component licences permit. Please credit the original authors and keep community access free. [Optional Ko-fi support](https://ko-fi.com/chronohax); no paid access or promised support.
