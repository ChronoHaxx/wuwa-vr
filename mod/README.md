# Mod source — 1.2.5 beta

The release target is
[beta-1-2-5](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-5),
launcher **1.2.5**, backend/package build **story-nudge-1-2-5**, for game
**3.7**. Source reconstruction, launcher checks, isolated package installation, updater packaging and website checks passed. Headset results and open checks are below.
The matching `checkpoint.json` and release receipt must identify the final
artifacts before publication.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1 and an earlier 1.2.3 test build in the headset with the Kuro launcher copy of the game, and the 1.2.5 test builds (story camera nudge, showcase video, recordings, desktop HUD, first VR frame) in the headset (SteamVR) with the Kuro launcher copy. The 1.2.5 release build was tested on the OpenXR simulator with both the Steam and the Kuro launcher copies: the UI redirect starts and the stereo fixes pass their code checks; full ground detail turns on in full VR and back off on the stereo screen. The stereo screen start-up fix is checked by a unit test only. The 1.2.5 release build has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot. See [the 1.2.5 release notes](../docs/launch-kit/STORY-NUDGE-1.2.5.md).

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
