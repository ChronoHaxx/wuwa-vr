# Mod source — renderer handoff beta

The release target is
[beta-2026-10-06-renderer-handoff](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-renderer-handoff),
launcher **1.0.8**, backend/package build **renderer-handoff-20261006**, for game
**3.7**. Native compilation, graphics-hook regression checks and packaging verification are pending for this candidate.
The matching `checkpoint.json` and release receipt must identify the final
artifacts before publication.

The affected Windows 11 Steam PC still started flat with 1.0.7 despite verified DLL loading and process identity. Its new logs show rendering calls reaching the wrong graphics probe while the DX12 probe receives no frames. This candidate targets callbacks retained from an earlier probe: a handoff to DX12 is allowed only after that exact rendering chain positively exposes a DX12 device. It retains the window-filter and startup-recovery repairs. The backend build and background tests passed: retained callbacks reached DX12 with a verified device and queue, original presentation ran once per call, and ordinary DX11/DX12 paths and refused handoffs were checked. Successful startup on the affected PC and physical-headset acceptance remain unverified. See [the renderer handoff report](../docs/launch-kit/RENDERER-HANDOFF-20261006.md).

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
