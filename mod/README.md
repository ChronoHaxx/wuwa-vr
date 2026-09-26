# Mod source — 26 Sep 2026 22:42 BST

Matches `stereo-menus-20260926-r3` in the 23:02 beta.

- `uevr/`: browsable copies of modified/new native files.
- `uesdk/`: the two modified SDK files.
- `source-changes/`: complete Git patches against pinned upstreams.
- `lua/`: matching camera/controller scripts.
- `localization/`: editable controls/shortcut-page translations.
- `notices/`: upstream notices, including font licenses.
- `../launcher/dev/`: the packaged launcher and SteamVR recorder source.

`uevr/` is an overlay, not a standalone fork. Follow [BUILD.md](BUILD.md) to
reconstruct a full checkout. The private checkpoint SHA in `checkpoint.json`
records provenance; the public release tag identifies the exported files.

Still open: doubled head-following reflections, eye-dependent materials
(Lynae/Mornye/Iuno) and some NPC labels/bubbles. A recent simulator frame shows
an NPC name in both eyes; that is not a complete headset acceptance pass.

Native changes are not covered by the website's MIT grant. Upstream notices
remain applicable. Credit the authors listed in the root CREDITS.md.
