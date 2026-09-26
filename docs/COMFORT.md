# Put the view and HUD where you want them

Most settings apply live. A native DLL change needs a restart; adjusting a HUD
or camera option normally does not.

## When an effect differs between eyes

Open **WuWa Controls > Stereo rendering compatibility**. **Use separate-eye
renderer** selects UEVR's Synchronized Sequential / Skip Draw path. **Use native
stereo** returns to the ordinary native path. The restore button restores the
renderer settings from before the comparison. Camera, HUD placement and Xbox
controls stay as you set them. Opening the section changes nothing.

The owner's September 26 comparison produced a **black scene with the HUD still
visible**. Keep native stereo selected for normal play. The 14:09 and later candidates
repairs stale OpenXR depth metadata and rejects depth rectangles outside their
swapchain, but this has not established a fix for the black scene or the native
NPC labels, doubled reflections and one-eye character effects.
See [UEVR's rendering guide](https://docs.uevr.io/#quick-overview-of-rendering-methods).
Both comparison choices keep Native Stereo Fix off. UEVR saves the renderer you
leave selected. The image may pause while its targets rebuild.

Keep the current native mode if the alternative looks worse. No GPU frame
capture or replacement shader is bundled, and neither a menu toggle nor a
successful compile establishes that the one-eye effects are fixed.

## Three HUD arrangements

| Feel | Setting |
| --- | --- |
| Personal HUD that moves with your gaze | Enable follow-view, then set size, distance and vertical offset. |
| Panel placed in front of you | Disable follow-view, sit comfortably, then recenter. |
| HUD at the edges of the 6DOF window | Enable the window, attach the HUD to it and adjust coverage. |

These are still stereo panels with a chosen distance. Following your view is
not a promise of zero eye strain. A closer panel is not automatically easier to
read: increase its physical size as well, and keep important text away from
the extreme edges. The headset is the final check; a flat spectator capture
cannot establish comfortable stereo fusion.

Use **L3 + B** to hide/show the game UI. It also hides menus. The control-recovery
candidate keeps a small red recovery hint visible while UI is hidden; detected
menus get a larger warning. Some game menus do not expose the cursor signal.
Use L3+B or **Show game UI now** in WuWa Controls to restore it.

## Recenter is not world scale

**L3 + A** recenters the view. Position reset can be enabled separately. World
scale changes the relation between physical head movement and game units, so it
can change perceived height and offsets. Keep it steady while tuning first
person. Use camera height and forward offset for camera placement, rather than
maxing world scale to compensate.

The OpenXR Simulator's Home key resets its own simulated pose. UEVR has a
separate origin, and the game has another camera. Resetting one does not reset
all three. In the simulator, release mouse capture, focus its preview, press
Home, then recenter UEVR. A real headset uses its tracking pose instead.

## First person

Use L3+View, then compare walking, sprinting, wing flight and character swaps.
The head can be hidden while the body stays visible, but leaning, animation and
camera placement can still expose the torso. In the 15:39 control-recovery
candidate, use **WuWa Controls > First person** (older builds placed this in
**Camera and gameplay controls > Camera customization**):

| First person motion | What follows you |
| --- | --- |
| Custom / saved behavior | The previous animation and horizon checkboxes. |
| Comfort | Stable height relative to the character, with a level horizon. |
| Animated position + stick pitch | Updated head/neck position after animation, with normal game-camera pitch. |
| Full animation rotation | Character turns and animated bone rotation, including pitch and roll. Experimental; can be intense. |

The last three choices sample after animation, before drawing. Full animation
calibrates rig axes to the pose where you enter first person; enter while
standing neutrally. Choose Animated position for ordinary right-stick aiming.
The owner reported that Animated position + stick pitch now tracks sprinting,
grappling and wing flight well. The 17:30 candidate preserves that path. In first
person, **L3 + D-pad Down** switches full animation on/off, returning to the
previous motion choice. Close UEVR and game menus before using this shortcut.

Under **Full animation: stick override**, choose exact animation or game view
while either stick is active. The owner prefers the latter: animation returns
after 0.4 seconds with both sticks neutral. **Smooth transition between animation
and game view** blends that handover. The 17:30 supplied profile enables it at
0.2 seconds; older saved profiles retain their choice. Reported movement-mode
changes also trigger a blend. Custom flight states that do not expose a change
cannot be detected by this signal. Legacy pitch-only is under **Aiming and
right-stick pitch**. These choices only affect full animation. Its game-view
override now uses the blended view's yaw for eye offsets, so walking backward
does not turn the offset behind the head while the view stays forward. This has
regression checks; game-aim disagreement remains under investigation.

The 20:11 candidate also removes the old camera-angle lag after a handover ends
and starts the return blend from the last displayed game view. It retains game-view
rotation if global headset/controller aim or Decoupled Pitch conflicts with full
animation. The warning beside the motion selector explains how to restore compatible
settings without silently changing your global aim choice. This does not establish
headset-aligned grapple targeting.

**Character visibility** is a separate choice. **Hide head bones; keep body**
keeps the visible body but also removes head geometry from the shadow. Its
camera reconstructs the head from a visible parent bone, which is an
approximation. **Hide body; keep original shadows** leaves head bones intact for
tracking and requests hidden shadows only from components that were visible
shadow casters before the mod hid them. It no longer enables unused equipment
casters, addressing the reported duplicate wing shadows.

**Hide head bones; full shadow copy** keeps the body visible and
uses a hidden, non-colliding pose copy for the original caster's shadow. The
visible head stays hidden. The 15:39 candidate repaired automatic swap handling;
the owner subsequently reported this mode working perfectly. The 17:30 profile
makes it the default. Unsupported rigs can still fall back. Leaving the mode destroys owned copies and
restores the original shadow settings. Visible-body modes can still expose the
torso when leaning or during unusual animations; they cannot promise an
unbreakable first-person illusion.

The previous Custom controls remain available:

- **Follow animated head / neck position** follows the supported rig instead of
  a fixed height. It can introduce animation bobbing.
- **Refresh first-person position before drawing (Custom)** samples that
  existing position later in the frame. The other presets do this automatically.
- **Keep first person horizon level** removes visual pitch. Use the dedicated
  **Use right-stick pitch** button if you want the game's vertical camera motion.
- **Try headset aim** uses UEVR's existing head-aim/control-rotation route. Check
  actual grapple activation as well as the glow/sound of the target.

If the camera drops inside the body after a swap, return to game camera and
back, or swap away/back as the user found helpful. Record the character and
movement state if it repeats; do not save a large permanent height offset to
compensate for a transient rig problem.

## Shortcut sheet

L3+Menu toggles it. Select front placement first to verify visibility; then try
feet placement and adjust width, drop, forward distance and tilt. Its anchor is
your **recentered tracking origin**, not your avatar. A desktop spectator view
may omit compositor layers. UEVR settings can temporarily take priority over
the sheet, so close them to inspect placement. The 17:30 candidate keeps the
sheet visible during manual or automatic mouse mode, including with game UI
hidden. The sheet contains amber mouse and red hidden-UI hints; if you hide the
sheet, standalone recovery notices remain.

## The window

Open **WindowMode → 6DOF Window**, enable it, then use **Recenter Window In Front
Of Me**. Width, height, distance, edge feather, corners and curvature are live.
In **HUD placement**, attach the HUD to the window. **Fill whole frame** with
coverage **1.0** now fills both dimensions. This stretches the UI if the portal
and HUD have different proportions. **Preserve proportions** touches one axis;
**Safe fit** also leaves a margin for rounded and feathered edges. The HUD is a
flat rectangle: use zero curvature and square corners for exact edge alignment.
It is a stereo window into the running scene, not a paused reconstruction.
Passthrough/chroma-key support depends on the streaming/runtime setup and has
not been established for every Quest connection method here.

## Record a problem or a showcase clip

The **17:30 Camera, HUD and privacy** candidate adds **WuWa Controls → Streamer
privacy: cover player IDs**. This optional switch draws opaque black rectangles
over the usual bottom-right UID and the ESC profile-ID row in the extracted UI,
before the VR/portal and mod spectator copies. It starts off. The ESC rectangle
stays on outside menus too, since cursor detection alone misses controller menus;
you can disable that rectangle separately and adjust both rectangles' edges.
Control-profile resets preserve this privacy setting.

Inspect a short gameplay and ESC-menu recording before sharing it. The masks
have GPU pixel checks, but their live placement still needs verification and
other layouts may put an ID elsewhere. They do not redact names, chat, log files,
the launcher or pre-injection screens, and cannot edit recordings already saved.
Both playtest and content-creation recordings use the same masked VR image when
the switch is enabled.

On the new portable launcher, open **Record gameplay** and press **Start
recording** while WuWa is already running through SteamVR. Return focus to WuWa,
then play normally. Stop from the launcher when finished, or it stops after five
minutes. This captures SteamVR's two eye views, not the desktop; there is no
audio. Recording can reduce performance. The first real clips captured about
11 new images per second despite a 30 fps file target. The new GPU resize path
passed synthetic color/encoding checks but needs a live performance comparison.
Choose 30/45/60 fps and 720/1024/1280 pixels per eye; start with 30 / 1024.

**Open recordings** opens the saved folder. `clean-sbs.mp4` is the unannotated
stereo video. `replay.html` shows controller presses and camera state separately;
it can save clean left-eye, right-eye or stereo PNG stills. If the browser blocks
local still export, expand the file-picker help and select that folder's MP4.

On the 17:30 candidate, playtest sidecars can also contain bounded per-eye
`render_stage0` observations for the NPC-label investigation. They record CPU
viewport/depth setup immediately before dispatch, not completed GPU pixels.
They do not change render parameters and remain separate from the video and
camera replay. Content-creation recordings do not enable this diagnostic trace.
Matching is approximate, and stale input is marked unavailable. The new backend
records raw and delivered input, post-animation/game/control camera information,
HMD position/orientation and recent per-eye rendered poses. Choose **Playtest
with camera and Xbox data** for these sidecars. **Content creation: clean video
only** also supports older backends without motion recording.

For showcase footage, hide the shortcut sheet and close UEVR first; the recorder
does not remove game HUD, menus or account identifiers. Keep the original clip
and diagnostic sidecars for debugging, and review any footage before publishing.
Simulator video capture and automatic upload are not included.
