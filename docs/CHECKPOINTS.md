# Saved checkpoints

All times are **BST (UTC+01:00), 24 September 2026**. The dashboard preserves
runtime hashes and saved profiles; these are distinct from headset acceptance.

| Saved | ID | Backend SHA-256 | Source tag |
| --- | --- | --- | --- |
| 17:09 | playable-20260924 | fa0eba4d0002364cd8fbc736a5251eb553b1882d8de1b2ddf743ab637803d569 | wuwa/playable-2026-09-24 |
| 20:56 | first-person-20260924 | 1f477d71191cea15be3d08582d177daf51103218fad4c4677c3b6e087d364999 | wuwa/first-person-2026-09-24 |
| 22:37 | camera-tested-20260924 | 84f1579560b03a0eded29f9d6f54a5a913ebb8b272ae6b66094345061887a18e | wuwa/camera-tested-2026-09-24-2237 |
| 23:00 | hud-input-20260924 | 72a08bd3f42fbc26d0cc1b00b942dfefddf312ca90aaf6f5ec54facdf346d5be | wuwa/hud-input-candidate-2026-09-24-2300 |
| 23:31 | menu-camera-qol-20260924 | 39810b377a4fb70781332177c9e3f054b8fc2103dcebc84bdf11545e9e73c1d7 | wuwa/menu-camera-qol-2026-09-24-2331 |

The first two source-history commits were reconstructed later from preserved
artifacts; they were not original commits made at those save times. The tags
preserve source and patches, not a claim of a clean independently reproduced
build. Older September 20 labels use verified **build** times (17:23 / 18:33)
because the later reconstructed profiles have different dates.

## 25 September 2026, BST (UTC+01:00)

| Saved | ID | Backend SHA-256 | Source tag |
| --- | --- | --- | --- |
| 01:22:07 | accessible-menu-20260925 | 33a9e7f5ce4fd302e4ff2f35305ba23c7332903b787d842c6a4164805f6b9a97 | wuwa/accessible-menu-2026-09-25-012207 |
| 22:49:13 | camera-binding-20260925 | 09edbe295b55e9a78e2d87e70e8c974d347ccc0e5cb2f8ea9e10a30f8c0ac188 | wuwa/camera-binding-2026-09-25-224913 |

The accessible-menu candidate reorganizes the custom UEVR settings page and
supplies visible UI/shortcut defaults. It inherits the 23:31 candidate's
rendering and gameplay behavior. Game/headset acceptance is still pending.
The previous native builds above are unchanged.
The 22:49 candidate repairs two reproduced camera binding cases: a replaced
main mesh and calibration before animation when late refresh is selected.
It also records passive main/captured-view data for the NPC-label investigation.
This does not establish a live camera, grapple or stereo-label fix. Its late
option and Native Stereo Fix remain off by default; all earlier tags remain.
Use the timestamped catalog and manifest rather than a folder called "latest".

## 26 September 2026, BST (UTC+01:00)

| Saved | ID | Backend SHA-256 | Source tag |
| --- | --- | --- | --- |
| 01:17:50 | comfort-runtime-20260926 | db0566a38ba364b7f756360a722460ba0cedf4cd2c8720fff4a7bbcf17830f51 | wuwa/comfort-runtime-2026-09-26-011750 |
| 01:53:38 | combined-playtest-20260926 | e20e02610087d81b19821e734efbb82007de0e1ef166969dc0ef586cbee2d13d | wuwa/combined-playtest-2026-09-26-015338 |
| 14:09:29 | motion-shadow-20260926 | f7044a277c8aae7354b3b07b0ddcfabbda86259bbc6194622c4ebb7a624e9175 | wuwa/motion-shadow-2026-09-26-140929 |
| 15:39:51 | control-recovery-20260926 | 02ac1fbad19af0bb518197952a4b4be1953c5f4da1808d703699d570f444716d | wuwa/control-recovery-2026-09-26-153951 |
| 17:30:42 | playability-20260926 | f6c73f82acbaabab14a02fa39fa657f105c8204e0d25c6f17f1652466fe3b8ff | wuwa/playability-2026-09-26-173042 |
| 18:03:24 | playability-20260926-r2 | 22066ebd46cd105a17c0bdd66f6c81796d8d6d290134acbefbc804b7794acded | wuwa/playability-2026-09-26-180324 |
| 19:04:02 | stereo-labels-20260926 | 1f034f2b58e7cdbf384f749f79c68ad9f9094e4fc8b1611808715a1f42dc5efe | wuwa/stereo-labels-2026-09-26-190402 |

18:03 preserves the 17:30 camera/profile and restores the common HUD/MOUSE
heading. Its source commit also records the later portable relocation and Ko-fi
changes. 19:04 adds a guarded missing-eye label candidate and typed material
comparisons. Both compiled and passed component checks; neither has new headset
acceptance. Reflections and one-eye materials remain unresolved. Their source
commits were saved after these runtime timestamps; earlier artifacts are unchanged.

01:17 restores portable simulator/headset switching, adds a visible WuWa menu
scrollbar, motion presets with post-animation sampling, and an optional hidden
body with a full character shadow. 01:53 retains that batch and adds a reversible
native/separate-eye renderer comparison plus continuing passive stereo samples.
The owner subsequently reported good default first-person sprint, grapple and
wing tracking in 01:53, but duplicate equipment shadows, full-follow drift and
stick issues, portal margins, and a black scene in the separate-eye comparison.
That is partial acceptance, not a pass for the whole build.

14:09 preserves the accepted default camera path and adds filtered shadow
casters, opt-in head-shadow copies, full-follow stick override and L3+D-pad Down,
full-frame portal sizing and clean SteamVR video with separate camera/controller
replay. OpenXR depth metadata is reinitialized and bounds checked; the black-scene
comparison is not yet proven repaired. Native shader/label/reflection repairs
remain open. The owner subsequently liked full animation with the game-view
stick override and reported working head shadows after a manual re-toggle per
character. The recordings establish real SteamVR capture, with only about 11
new captured frames per second; camera handover and stereo defects remain.
This is partial acceptance, not a complete pass.

15:39 adds automatic shadow retries/flag restoration, optional smooth handover,
the First person section, persistent recovery hints and supplied-profile reset
with undo. The launcher adds 30/45/60 fps and resolution choices; the recorder
resizes on the GPU and new debug recordings include head/control/rendered poses.
Offline checks pass; live performance and this candidate's grouped headset
acceptance remain pending. Native labels/reflections/materials and full-follow
aim alignment are unchanged. Source receipts record actual commit/tag creation.

The owner subsequently accepted the 15:39 full-shadow head-hiding behavior and
requested it as the default. Other recordings showed backward offset desync,
disabled smoothing and shortcut help disappearing in mouse mode.

17:30 makes that shadow mode the supplied default, aligns full-follow/game-view
offsets during backward movement, and enables 0.2-second stick and reported
movement-state handovers. It retains shortcut help during mouse/hidden-UI modes
and adds optional player-ID masks. Bounded label-stage recording hooks are
diagnostic only. Native build, camera tests, offscreen D3D11/D3D12 masks and sheet
renders passed; new game/headset acceptance is pending. NPC labels, doubled
reflections, one-eye materials and full-animation game aim remain unresolved.

## Launcher tools — 26 September 19:43:24 BST

Tag `wuwa/steamvr-comparisons-2026-09-26-194324` adds a focus-waited, automatically restored SteamVR graphics
comparison batch and full-resolution per-eye/SBS PNG capture. This is a
tools checkpoint; the 19:04 renderer DLL and its supplied profile are unchanged.
Offline component/launcher checks passed; headset capture and the remaining
stereo defects are unverified. Public mirrors cannot prove exact source-frame
pairing. See `_docs/steamvr-comparisons-2026-09-26.md` for the evidence limits.

## Stereo and camera — 26 September 20:11:13 BST

Tag `wuwa/stereo-camera-2026-09-26-201113` saves `stereo-camera-20260926`, backend
`3c8acd6bfb3312044ef1ef717a263f2ffe7d3bcb6cc32c16f5fb69769082f762`. It retains the missing-eye NPC-label
candidate and adds current game-view handover rotation/offset repairs plus
an incompatible-aim warning and fallback. Component checks and build passed;
it is not installed or headset-tested. Reflections, one-eye materials and
full-animation headset targeting remain open. Earlier builds are preserved.

## Guide and package — 26 September 20:36:18 BST

Tag `wuwa/beta-guide-2026-09-26-203618` records updated setup, controls and recovery instructions.
This is a documentation checkpoint: the 20:11 native runtime, supplied
profiles and comparison helpers are unchanged. The same grouped headset
acceptance remains pending; it is not a new rendering variant.

## Stereo, menus and languages — 26 September 22:42:55 BST

Tag `wuwa/stereo-menus-2026-09-26-224255` preserves `stereo-menus-20260926-r3`, backend
`624eeda38b0e26a66692e68124059cb6eb30daffba4ac27f4b9a99db09316090`. NPC label geometry guards now accept the
measured swapped eyes and padded depth allocation. Hidden-UI warnings
require a detected menu; the upper-left privacy box defaults to the
ESC/overlay route. Polar fly is the supplied/recovery freecam default.
Ten editable language catalogs and embedded fonts cover mod UI/shortcuts
with partial draft translations. Native build, source reconstruction,
four C++ checks and 180 software-rendered layouts passed. The earlier
live simulator batch still shows doubled reflections; the typed planar
comparison now needs this new backend. New behavior is not installed or
game/headset-accepted. No previous checkpoint was overwritten.
