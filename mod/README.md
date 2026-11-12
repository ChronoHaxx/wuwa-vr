# Mod source — 1.2.7 beta

The release target is
[beta-1-2-7](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-7),
launcher **1.2.7**, backend/package build **game-3-8-1-2-7**, for game
**3.8**. Source reconstruction, launcher checks, isolated package installation, updater packaging and website checks passed. Headset results and open checks are below.
The matching `checkpoint.json` and release receipt must identify the final
artifacts before publication.

The owner played 1.2.0 to 1.2.6 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with Virtual Desktop and a Reality Runner treadmill), and the 1.2.7 work on game 3.8 in the headset (SteamVR, Bigscreen Beyond, Kuro launcher copy): the new VR menu, the controls cards and about 70 minutes of recording, on the test build before the last four 3.8 rendering fixes. This release build ran on the OpenXR simulator with the Kuro launcher copy of 3.8: the UI redirect, the menus and four of the five stereo code routes pass their checks, god rays are hooked, and the far lighting refill caught a game refresh in one eye. The Steam copy had not downloaded 3.8 yet, so it is not checked on 3.8. The story camera tilt in ultimates and the VR setup questions have not been played through in the headset yet. Many dialogue close-ups still sit wider than the 2D shot. See [the 1.2.7 release notes](../docs/launch-kit/GAME-3.8-VR-MENU-1.2.7.md).

Update the app, then explicitly install the new VR package in step 02: the app
update preserves the existing selection. Keep the previous package for rollback.
Graphics follow game preferences instead of inherited low/medium startup overrides.
The candidate retains the existing rendering
changes and includes portal, diorama and 2D-screen shortcuts, HUD recovery, and
optional walking, optical-hand and menu-comfort features. See the
[controller reference](../docs/CONTROLS.md) for controls and their limits.
Optional experimental features still need physical-headset acceptance; public
availability does not establish acceptance for every scene or device.

Cinematic framing now defaults on: the user confirmed matching letterboxing
in an immersive simulator quest replay. Headset comfort and other scenes remain
unverified. Mono theatre is available with LT + RT, then R3. Automatic cinematic
switching stays off and unverified; its delayed Lua module-loading failure is
repaired, but neither story recognition nor prerecorded playback is accepted.
Long dialogue loading, unsupported HUD refresh and selective flat-menu handling
remain open. The source includes transition-copy guards and bounded diagnostics.

`uevr/` and `uesdk/` are changed-file overlays for the pinned upstreams.
Historical evidence: the 3 October reconstruction refresh passed forward
application to the clean upstream index and comparison of its 103 native overlay
files, including lighting code missing from the older patch. That dated receipt
describes the earlier checkpoint; `checkpoint.json` identifies this release.
Follow [BUILD.md](BUILD.md); `lua/` contains the matching scripts.
Localization, component licences and attribution remain included.

Start with [the beginner code map, timeline and next experiments](../docs/UNDERSTANDING-WUWA-VR.md).
Published packages remain available at their release tags. Do not identify this
working tree as the old 28 September experiment. `checkpoint.json` records the
current release identity, build evidence and acceptance limits.

Earlier headset results for the ultimate camera, 2D brightness and far-object
fixes apply to the tested cases. Optional NPC rim suppression starts **off** and
also removes intended nearby character rim lighting. Stationary simulator
comparisons support the workaround; its headset acceptance is pending and the
underlying stereo-lighting fault remains unresolved. Weapon/Echo reflection
submenus and full-animation first-person aiming remain separate open issues.
Native changes retain upstream license terms; the combined mod is not blanket MIT.
