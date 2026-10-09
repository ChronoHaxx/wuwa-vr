# Credits and provenance

The independent clean-video recorder uses Valve's OpenVR public mirror API,
Microsoft Windows Media Foundation and nlohmann/json. The private package carries
OpenVR and nlohmann/json notices. Camera/controller telemetry comes from the
modified backend and is stored separately from the footage. Recording does not
grant redistribution rights over captured game assets or player identifiers.

This project brings existing community work together with local WuWa fixes and
testing. Credit does not imply endorsement, partnership or permission to
redistribute another project's files.

| Contributor / project | Contribution and scope |
| --- | --- |
| [praydog and UEVR contributors](https://github.com/praydog/UEVR) | The VR injection framework, stereo rendering, tracking, compositor/UI and plugin/Lua foundations. Local backend base: `4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d`. |
| [mirudo2's WuWa UI project](https://github.com/mirudo2/WuWa-UI-Fix-for-UEVR) | Original game UI/profile work, gamepad helpers and community distribution. The preserved UI script credits polar as the original author and SannpoKun for modifications. Local reference base: `3ec30b7beb538254be1007ad7d5d8594dde885b4`. |
| SannpoKun | UI-script modifications credited in the preserved community source. The original authorship and subsequent adaptations remain distinct. |
| polar | Original `Freecam_by_polar.lua` and the sensible Xbox camera/mouse layout on which this project's controls are based. Attribution is to the file/community handle; it is not an assertion that all handles identify the same person. |
| [mirudo2's Custom UEVR Injector](https://github.com/mirudo2/Custom-UEVR-Injector) | The custom injector used in the local launch flow. |
| [Indath](https://github.com/Indath/UEVR-Wuthering-Waves) | WuWa-specific UEVR work this project builds on: Native Stereo Fix running for WuWa since July 2026; the LGUI draw and pass hook points in his September 8 build, which showed where the HUD could be captured for VR (this project's HUD route began by observing those exact points); the frame-timing suggestion to turn off the one-frame thread lag, adopted in these profiles; and his modified July UI script and profile, studied at the start. |
| endlessfalls | Community initialization compatibility work; the supplied post described `find_uobject` / `SkipUObjectArrayInit`, explicitly not a Native-vs-Synced-Stereo fix. No broader authorship claim is made. |
| [Elliott Tate: UEVR-6DOF-Window](https://github.com/elliotttate/UEVR-6DOF-Window) | Optional stereo window implementation adapted from `fb31341e860b15e116a15123820c95f044ff0a0f`, whose UEVR base differs from ours. |
| [OpenXR Simulator contributors](https://github.com/elliotttate/OpenXR-Simulator) | Headset-free development runtime. Local input/capture fixes are kept separately from the UEVR backend. |
| [LGUI](https://github.com/liufei2008/LGUI) | Public source used as reference to understand UI render paths. It is not asserted to be the exact version shipped with WuWa and is not bundled as a game asset. |
| [Kanit](https://github.com/cadsondemak/kanit) (Cadson Demak) and [SUITE](https://github.com/sun-typeface/SUITE) (Sunn) | Website heading and button typefaces, the open-licence fonts Wuthering Waves itself ships. Subset WOFF2 copies under the SIL Open Font License 1.1; see `LICENSES/fonts/`. Not covered by this project's MIT grant. |
| ChronoHaxx | Project direction, integration, game/headset testing and this workspace's controls, HUD, camera, diagnostic and packaging changes. |

## Tools and artwork

Development uses AI-assisted coding with Codex and Claude (reviews, and the
September 25 launcher package and website work), alongside human testing.
Model output and passing checks do not replace live acceptance. The Xbox-style controller illustration was generated using image
generation; button placement was visually checked and shortcut labels are
rendered by code. Xbox is a Microsoft trademark; the diagram is not an official
Microsoft product asset or endorsement.

Showcase game images are actual OpenXR Simulator scene-layer captures from
the local September 24 session. They are labeled as development captures,
not promotional renders or headset performance evidence. Wuthering Waves game
content and trademarks remain with their respective owners, including Kuro
Games. No game files or extracted meshes/textures are included in the site.
The launcher-page image is an actual render of the local launcher in a sample
state, not a game capture.

The private launcher package bundles a copy of [CPython](https://www.python.org/)
under the Python Software Foundation licence, plus a small original launcher
stub. Its `notices` folder lists every component's terms.

## Licenses are component-specific

This project's original website, guides and explicitly listed independent
work use the [MIT license](LICENSE.md). Forks and contributions to those parts
are welcome. Please keep free access and give credit; requests against paid
repacks/paywalls are community preferences, not additional MIT conditions.

The pinned [UEVR backend LICENSE](https://github.com/praydog/UEVR/blob/4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d/LICENSE)
states **All rights reserved**. The `include` directory has a separate MIT
license covering that directory; it does not license the whole backend.
The WuWa UI reference checkout has no top-level license file. The local
OpenXR Simulator checkout has an MIT license, whose notice must accompany
copies covered by it. Other dependencies have their own notices.

No blanket license or redistribution permission is inferred from public GitHub
access, a Discord attachment or this credit list. The website/guide can be
reviewed separately while binary/source redistribution scope is clarified.
Original notices are retained in the private recovery kit. See
[release preparation](docs/RELEASING.md) for the remaining publication work.
