# Put the view and HUD where you want them

Most settings apply live; only a new mod build needs a game restart. All of
these are in **VR → WuWa Controls** unless noted.

## Stereo settings

Both are on by default; leave them on.

- **Match far-object detail between eyes** keeps distant trees and props at the
  same detail level in both eyes, so they do not freeze in one eye.
- **Match far lighting between eyes** refills the game's lighting volume for
  both eyes after the game starts, after loading screens and after teleports.
  Each refill can cause a short (~0.2 s) hitch. If distant objects ever look
  darker in one eye, press **Refill far lighting now**.

**Stereo rendering compatibility** offers an alternative renderer for
comparison only. It turns Native Stereo Fix off, which breaks the fixes above;
use its **Restore** button afterwards.

## Three HUD arrangements

| Feel | Setting |
| --- | --- |
| Personal HUD that moves with your gaze | Enable follow-view, then set size, distance and vertical offset. |
| Panel placed in front of you | Disable follow-view, sit comfortably, then recenter. |
| HUD at the edges of the 6DOF window | Enable the window, attach the HUD to it and adjust coverage. |

A closer panel is not automatically easier to read: increase its size too, and
keep important text away from the edges. Judge comfort in the headset, not in a
flat capture.

**L3 + B** hides or shows the game UI, including menus. While it is hidden, a
small red hint stays visible; **Show game UI now** also restores it.

## Recenter is not world scale

**L3 + A** recenters the view; position reset is a separate option. World scale
changes how physical head movement maps to game units, so it changes perceived
height. Keep it steady and use camera height and forward offset to place the
camera instead.

In the OpenXR Simulator, its Home key resets only the simulated headset; recenter
UEVR with L3 + A as well.

## First person

**L3 + View** enters first person. Choose the motion in **WuWa Controls → First
person**:

| First person motion | What follows you |
| --- | --- |
| Comfort | Stable height relative to the character, with a level horizon. |
| Animated position + stick pitch | The animated head/neck position, with normal game-camera pitch. Recommended for walking, sprinting, grappling and wing flight. |
| Full animation rotation | Character turns and animated head rotation, including pitch and roll. Experimental and can be intense. |
| Custom / saved behavior | The individual options below. |

Enter first person while standing still: full animation calibrates to that
pose. **L3 + D-pad Down** toggles full animation and back. Its **stick
override** can show the game view while either stick moves, returning to
animation 0.4 s after both are neutral; **Smooth transition** blends that
handover (0.2 s by default). Full animation needs **Game** aim with **Decoupled
Pitch** off; a warning appears beside the motion choice if they conflict. It can
still disagree with the game's grapple targeting.

**Character visibility:**

- **Hide head bones; full shadow copy** (default): the body stays visible, the
  head is hidden, and a hidden copy keeps the full character shadow.
- **Hide head bones; keep body**: the shadow loses its head.
- **Hide body; keep original shadows**: only the shadow remains.
- **Keep entire character visible**.

Leaning or unusual animations can still show the torso. Unsupported rigs fall
back to ordinary head hiding.

**Custom options:** *Follow animated head / neck position* (can bob),
*Refresh first-person position before drawing*, *Keep first person horizon
level*, and the aim buttons *Use right-stick pitch* (game aim) and *Try headset
aim* (UEVR head aim). The aim buttons change global UEVR aim settings.

If the camera drops into the body after a character swap, switch to the game
camera and back.

## Shortcut sheet

**L3 + Menu** toggles it. Start with front placement, then try feet placement and
adjust width, drop, distance and tilt. Its anchor is your recentered tracking
origin, not the character. Close UEVR settings to inspect it: they can cover the
sheet. A flat spectator view may not show it at all.

## The 6DOF window

Open **WindowMode → 6DOF Window**, enable it and use **Recenter Window In Front
Of Me**. Size, distance, edge feather, corners and curvature apply live. In **HUD
placement**, attach the HUD to the window: **Fill whole frame** stretches the HUD
to the window, **Preserve proportions** keeps its shape and **Safe fit** adds a
margin. For exact HUD edges use zero curvature and square corners.

## Record gameplay

**Streamer privacy: cover player IDs** draws black boxes over the UID in the
bottom-right corner and the ID row in the ESC menu, in every view and recording.
It starts off. Other layouts, names and chat are not covered: review footage
before sharing.

In the launcher, open **Record gameplay** and press **Start recording** while the
game runs through SteamVR. Return focus to the game and play; stop from the
launcher or after five minutes. It records both eyes as side-by-side video,
without audio. Start with 30 fps and 1024 pixels per eye; recording costs some
performance.

**Open recordings** shows the folder: `clean-sbs.mp4` is the video and
`replay.html` replays controller and camera data next to it and can save
left-eye, right-eye or stereo stills. **Playtest with camera and Xbox data** adds
those data files; **Content creation: clean video only** records just the video.
For showcase clips, close UEVR and the shortcut sheet first.
