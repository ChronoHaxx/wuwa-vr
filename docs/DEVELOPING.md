# Code and contributions

WuWa VR is an experimental integration built on UEVR and community WuWa work.
Help with rendering, camera behavior, controller comfort, documentation and
translations is welcome. A donation is never required to contribute.

## Find the right starting point

Use the GitHub links on this page to browse the project, search issues or fork
the published repository. The public showcase export contains its website,
guides and community templates. It does not contain a complete buildable UEVR
fork or a downloadable mod. Native changes are preserved in a separate review
bundle while their distribution scope is clarified. Do not mistake the
showcase source for the full mod source.

For the published website: edit the Markdown under `docs/` or translations
under `site/languages/`, then run `npm install` and `npm run build`. The checked-in HTML
also works directly. Follow [CONTRIBUTING.md](../CONTRIBUTING.md) and preserve
the [component license boundaries](../LICENSE.md).

## Mod source map

| Area | Main source in the development checkout | What it controls |
| --- | --- | --- |
| UEVR integration | `src/mods/vr/FFakeStereoRenderingHook.cpp`, `OverlayComponent.cpp`, `src/utility/WuWaLgui*.hpp` | Game UI extraction, stereo hooks and HUD placement |
| In-VR settings | `src/mods/vr/WuWaControlsComponent.cpp` | Xbox shortcuts, first-person settings, warnings and recovery |
| Camera and controller behavior | `dev/02_WuWaVR_PolarControls.lua` | Head binding, animation follow, visibility/shadows and freecam |
| Portal window | `src/mods/WindowMode.cpp` | Optional 6DOF window, adapted from Elliott Tate's work |
| Windows launcher | `dev/wuwa_player.py`, `dev/wuwa-player.html`, `dev/portable/` | Local launcher UI, packaging and startup |
| Recording | `dev/steamvr-capture/record.cpp`, `dev/record-wuwa.py` | SteamVR mirror capture, GPU resizing and separate motion sidecars |
| Stereo comparisons | `dev/wuwa-test.py`, `dev/steamvr-capture/main.cpp` | Bounded graphics leases with restored values and simulator or public SteamVR mirror PNGs; exact SteamVR source-frame pairing is unavailable |

The `src/` paths above are relative to the UEVR checkout, not this website
export. They are a map for collaborating on the mod, not links to missing files.

## Reproducible review checkpoint

The latest prepared native candidate is **Stereo and camera candidate · 26 Sep
20:11 BST**, source tag `wuwa/stereo-camera-2026-09-26-201113`. The review bundle's
manifest records the exact source commit. It retains the guarded missing-eye
label pass and reversible material comparisons. Full-animation game-view
handovers now use current rotation and matching offsets after the blend, and
incompatible global aim settings retain game-view rotation with a visible warning.
Build and component checks passed; the candidate has not been observed in game.
Labels, doubled reflections, one-eye materials and full-animation target alignment
are not claimed fixed. The owner accepted full-shadow head hiding on 15:39.

The native patch is against UEVR
`4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d`. Its review bundle includes the pinned
UE SDK submodule revision, native/SDK patches, the matching Lua sources,
component notices and hashes. It excludes game files, logs, binaries,
recordings and personal profiles. The complete native patch already contains
the portal adaptation; applying a separate portal patch again is incorrect.

## Rendering work that is still open

- Some NPC names and overhead speech bubbles appear in only one eye.
- Character/Resonator previews show two reflections in each eye, and those
  reflections follow the head. Databank previews can show the same problem.
- Iuno hair/head, Mornye translucent legs and Lynae effects have been reported
  missing in the left eye. Chisa's ribbon can leave a silhouette in the right.
- Full-animation first person can disagree with game aiming/target selection.
- Separate-eye rendering previously produced a black scene with the HUD visible.

These observations do not establish one shared cause. The recorded reflection
defect is inside the scene eyes. Existing LGUI eligibility counts do not prove
a missing draw or justify bypassing all view gates. Useful next evidence is
the first failing draw/dispatch and its actual per-eye projection, viewport
and resources. No shader fix for these issues is claimed.

Keep Native Stereo Fix off in the current starting profile: it previously
caused SteamVR stutter, and combinations with experimental menu extraction
have crashed. Preserve an accepted checkpoint before changing rendering hooks.

## A useful report or pull request

State what happened, what you expected, and the shortest reproduction. Include
the build name/time, headset/runtime, controller connection, camera mode,
character/menu, and whether a setting or Alt-Tab changes it. A single-eye or
SBS crop can be useful; label which eye is affected. Review clips for account
IDs before sharing. Do not upload whole logs or memory captures by default.

Search existing issues first. Add new evidence to a matching report instead
of creating another report for the same symptom. The website's feedback form
prepares a draft for you to review and submit on GitHub; it sends nothing by
itself. A GitHub account is required to submit there. Copying the draft also
works for a community discussion.

Small fixes and forks are welcome where each component's terms allow them.
Credit upstream authors and describe what was actually tested. See the
[credits](../CREDITS.md) and [contribution guide](../CONTRIBUTING.md).
