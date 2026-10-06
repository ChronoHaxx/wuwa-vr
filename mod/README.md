# Mod source — 1.1.1 beta

The release target is
[beta-1-1-1](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-1),
launcher **1.1.1**, backend/package build **steam-resize-1-1-1**, for game
**3.7**. Source reconstruction, launcher checks, isolated package installation, updater packaging and website checks passed. The previous 1.0.10 startup was user-confirmed; new controller and headset acceptance remain pending.
The matching `checkpoint.json` and release receipt must identify the final
artifacts before publication.

The user confirmed 1.0.10 startup on the previously affected Windows 11 Steam simulator PC, and 1.1.0 through Kuro on the owner's PC. The owner's 1.1.0 Steam route crashed. Version 1.1.1 repairs the resize recursion found in that crash dump; controlled graphics and launcher tests pass, but this repair still needs real Steam and headset testing. See [the resize startup report](../docs/launch-kit/STEAM-RESIZE-1.1.1.md).

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
