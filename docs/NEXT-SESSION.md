# Combined acceptance draft

The saved development candidate is **Stereo, menus and languages · 26 Sep 22:42 BST**
(`stereo-menus-20260926-r3`). Backend SHA-256:
`624eeda38b0e26a66692e68124059cb6eb30daffba4ac27f4b9a99db09316090`.
It is not selected automatically. **Controls and recovery · 26 Sep 15:39 BST**
and earlier checkpoints remain available.

**Rendering work remains open.** This build includes an unverified missing-eye
NPC-label candidate. Doubled head-following preview reflections, one-eye
materials and full-animation game-aim alignment remain unresolved.
This checklist is saved for a combined session; it is not a
request to interrupt gameplay or repeatedly demonstrate unchanged defects.

Use the local launcher, select **22:42**, and verify **Next Apply &
launch** before launching through the official standalone game launcher. Steam
injection previously failed and has not been retested; Epic is untested. Native
DLL changes need a normal game restart. Keep **native rendering** and **Native
Stereo Fix off**; separate-eye rendering previously gave a black scene.

Quest Pro through Virtual Desktop or SteamVR, Xbox pad connected to PC. Current
automated video needs SteamVR; the patched simulator supports still-image batches.
VDXR video capture is not implemented. No countdown or
intermediate replies are needed. Report pass/fail/not tried together, including
the affected character/mode. Game time continues in every camera mode.

1. [ ] **Normal play and full-animation transitions.** L3+View enters first
   person. First check Animated position + stick pitch for ordinary walk,
   sprint, grapple and wing flight. Under **WuWa Controls → First person**,
   Full animation retains **Use game view while either stick moves**. Smoothing
   starts enabled at 0.2 seconds in this supplied profile. Walk backward,
   strafe, turn and release both sticks; check the head offset stays coherent
   and the return to animation blends. Compare takeoff/landing. The movement
   component must report a change for those transitions to trigger a blend.
   Head position should remain immediate. Completed game-view handovers now use
   the current camera angle and matching offsets. L3+D-pad Down returns to the
   previous motion preset. Full animation requires Game aim and Decoupled Pitch
   off; conflicting settings now show a warning and retain game-view rotation.
   The warning's button restores compatible aim settings explicitly. Full-animation
   game-aim disagreement remains open; this guard does not establish a targeting fix.
2. [ ] **Shadows and controller recovery.** Head hiding with a full shadow is
   now the default. Confirm swaps do not require re-toggling it, no unused wing
   shadows appear, and leaving first person restores the character. Check normal
   menus/dialogue, the WuWa scrollbar and Alt-Tab/LB+Y without the UEVR overlay.
3. [ ] **Shortcut sheet and HUD/mouse mode.** Show the sheet with L3+Menu, leave
   page selection automatic, then use L3+LB and release. The sheet should stay
   put, show page 03 and explain how to exit. Hide game UI with L3+B: normal play
   should show no hidden-UI warning. Open ESC/map to see the restore hint. Hide
   the sheet to check the standalone menu warning. Restore UI and exit mouse mode using the same shortcuts. Automatic
   mouse-in-menus stays off by default and is a separate legacy option.
4. [ ] **Streamer privacy and reset.** Enable **Streamer privacy: cover player
   IDs**. Inspect a short gameplay, ESC-menu and character/menu recording: both
   eye views should cover each ID completely. Adjust rectangle edges if needed.
   The upper-left ESC-row mask should disappear in normal play; other overlays
   sharing that render route can also display it. The manual Always option is
   available for an undetected layout. Restore supplied
   controls, then undo a harmless HUD-size change; privacy must stay enabled.
   Names, chat and diagnostic files are outside the masks' scope. Supplied freecam
   style is now Polar fly. If desired, select another Language / WuWa and check
   the shortcut pages; technical help still has English fallbacks.
5. [ ] **One recording and portal check.** Use the launcher's Playtest purpose,
   30 fps / 1024 pixels per eye, to record movement and any visible NPC bubbles,
   translucent character effects and preview reflections encountered naturally.
   The NPC candidate adds a guarded primary-eye pass; CPU observations in
   `motion.jsonl` establish whether it ran, separately from visible labels.
   `clean-sbs.mp4` and `replay.html` still show the video and
   controller/camera comparison separately. Portal HUD coverage, freecam and
   simulator/headset controls remain available. Runtime switching requires the
   game/injector to be closed. Nothing is uploaded automatically.

The next private launcher also provides **Recovery and removal → Compare stereo
rendering (experimental) → Compare current scene**. During this same grouped
session, leave an affected character preview visible, close UEVR settings, start
the comparison and return focus to WuWa. Keep the view still. Five allowlisted
reflection/material settings are compared and restored automatically; no manual
CVar toggling or simulator switch is needed. **Open comparisons → index.html**
shows the before/changed/restored captures. These are composited SteamVR mirrors,
not isolated scene layers or guaranteed simultaneous eye frames. Results remain
private and may contain a UID. This is diagnosis, not a verified reflection fix.
The planar setting gates updates and may leave a cached reflection visible;
an unchanged still image cannot rule out that path. No texture clearing or
scene recreation is performed by this batch.
