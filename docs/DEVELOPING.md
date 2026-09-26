# Code and contributions

WuWa VR is an experimental integration built on UEVR and community WuWa work.
Help with rendering, camera behavior, controller comfort, documentation and
translations is welcome. A donation is never required to contribute.

## Find the right starting point

The [public repository](https://github.com/ChronoHaxx/wuwa-vr) contains the
website, matching native source changes, Lua, language catalogs and launcher.

| Folder | Contents |
| --- | --- |
| [mod/uevr](https://github.com/ChronoHaxx/wuwa-vr/tree/main/mod/uevr) | Modified native files: stereo/UI hooks, camera settings and portal |
| [mod/lua](https://github.com/ChronoHaxx/wuwa-vr/tree/main/mod/lua) | Camera, controller, head hiding and shadow scripts |
| [mod/source-changes](https://github.com/ChronoHaxx/wuwa-vr/tree/main/mod/source-changes) | Full patches against the pinned upstreams |
| [mod/localization](https://github.com/ChronoHaxx/wuwa-vr/tree/main/mod/localization) | Editable controls and shortcut translations |
| [launcher/dev](https://github.com/ChronoHaxx/wuwa-vr/tree/main/launcher/dev) | Portable launcher, capture and SteamVR recording source |

## Reconstruct the release

Read [mod/BUILD.md](https://github.com/ChronoHaxx/wuwa-vr/blob/main/mod/BUILD.md).
The export matches **26 Sep 22:42 BST** in the owner-played **23:02** package.
It is a browsable overlay plus patches, not a complete standalone UEVR fork.
All 62 native paths and two SDK paths were forward-applied and compared with
the saved build sources. That is not a fresh-PC or complete headset check.

The original local checkpoint IDs are preserved for provenance; they are not
commits reachable in the public repository. Public release tags identify the
actual exported files. Keep component notices and credit the original authors.

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
