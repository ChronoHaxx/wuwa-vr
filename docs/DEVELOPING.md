# Code and contributions

The repository holds the website, the launcher and the mod's source changes. It
is an **overlay** on pinned upstream versions, not a standalone UEVR fork:
[mod/BUILD.md](https://github.com/ChronoHaxx/wuwa-vr/blob/main/mod/BUILD.md)
explains how to rebuild. Each release tag records the exact source of that
download.

| Folder | Contents |
| --- | --- |
| `mod/uevr` | Native changes: stereo, UI, camera, game-version checks |
| `mod/lua` | Camera, controller, head-hiding and shadow scripts |
| `mod/localization` | In-game control and shortcut-sheet translations |
| `launcher/dev` | Portable launcher, recorder and test/measurement tools |
| `site`, `docs`, `dev` | Website, player docs and their generators |

The [explainer](understanding.html) has a file-by-file map, the stereo fixes and
how they were found. When changing rendering, keep each eye's own projection and
history; never copy one eye's finished image into the other. Check a rendering
change with a capture of the affected scene, not only a successful build.

Build natively with bounded parallelism on modest PCs:
`cmake --build out/build/x64-RelWithDebInfo --config RelWithDebInfo --target uevr --parallel 1 -- /p:CL_MPCount=1 /p:MultiProcMaxCount=1`.
A new DLL needs a game restart.
