# WuWa VR 1.2.0 beta · 11 Oct 22:49 BST — VR menu, controls cards and VR controllers

For Wuthering Waves 3.7 on Windows x64.

## New VR menu

**L3 + R3** (both stick clicks) now opens the WuWa VR menu instead of UEVR's
desktop window. It works with every input at once:

- **VR controllers:** point the laser and pull the trigger, or use either
  stick and A; B closes, grips switch pages.
- **Gamepad:** D-pad or either stick, A select, B back, LB / RB pages.
- **Mouse and keyboard:** click and scroll, or arrow keys, Enter, Esc, Tab.

Pages: Quick, Camera, Cinema, HUD, Controllers, Comfort, Fixes, Record and
Menu. They change the same settings as before. The Menu page picks the
language, each shown in its own script. **All settings (classic UEVR
menu)** opens UEVR's full window, and turning off **Use this menu when opening
UEVR** brings back UEVR's window on L3 + R3. Move the menu
anywhere (Y on a gamepad, or Menu page → Move menu) and set its size and
distance. On the stereo screen and in mono theatre it stays in front of the
picture.

## Controls cards

No more remembering shortcuts. The L3 card starts pinned low, below the HUD,
with every L3 + button shortcut, and rises in front of you when you turn well
away; **L3 + Menu** hides it or pins it again. Hold **L3** for half a second
to bring it up, or **LT + RT** for the views card (full VR, stereo screen,
mono theatre and blur). Each button lights up as you press it, and shortcuts
that do nothing in the current camera are greyed out. Size, height and
distance are on the HUD page. They replace the old L3 + Menu shortcut sheet.
With VR controllers the card names their buttons. A one-line tip shows on
the first three launches, and a short line after a view switch says how to
go on or come back for the first five switches (HUD page → View hints).

## Every language

The WuWa VR menu, the controls cards, the player guide the launcher opens
and the launcher itself are now in all ten languages: English, Simplified
Chinese, Japanese, Korean, German, Spanish, French, Brazilian Portuguese,
Russian and Arabic. Most were drafted by machine translation, so wording
may be rough in places; corrections from native speakers are welcome.

## VR controllers for walking (optional)

Quest, Index and other OpenXR controllers can act as an Xbox pad for walking
and looking around. They start off each launch: hold the left Menu button for
1 second (or both stick clicks for 1 second on controllers without one). They
can share the game with an Xbox pad or treadmill: both together (default),
last used wins, or VR only. **Controller slot: Automatic** (default) follows
whichever pad is really connected, such as Reality Runner's virtual pad while
the real pad is hidden. All of it is on the new **Controllers** page, with a
live status line. This is a sightseeing layout, not a tested combat layout.

## Treadmill runs

**Treadmill run** (Quick, Controllers or Record page) times your session and
saves your route in the game once a second, with a magenta sync flash in the
desktop view so an OBS video lines up with it. With Reality Runner open, 10
seconds of walking starts a run by itself and 5 minutes without walking ends
it. The run line shows the time, walking time and distance in the game. Hide
player IDs (while recording) also covers your UID during runs.

## Views and cinema

- **LT + RT, then click R3:** next view (full VR, stereo screen, mono
  theatre). **LT + RT, then hold R3 for 0.8 s:** straight back to full VR.
- **LT + RT, then click L3:** blur (depth of field) on or off. Blur is now on by
  default and shows in both eyes at the 2D game's strength (1x), with the game's
  other camera effects (colour grading, vignette) matched in both eyes too.
- The stereo screen uses the game's own lens, and depth of field shows in both
  eyes.
- Full VR dialogue close-ups move in along the game camera when the game sets a
  focus, and follow the camera through the shot (Cinema page → Close-up match).

## Recording and reporting problems

The menu's **Record** page records the headset view through the WuWa VR app.
A recording no longer stops when SteamVR briefly shows something else (it
pauses and carries on), and it continues in 5-minute parts until you stop it,
up to an hour. **Hide player IDs** (default: while recording) puts black boxes
over your UID; the boxes are approximate and can miss in full VR when the HUD
is squashed, so check a clip before sharing it.

## Also

- Render resolution (60-120 %) on the Fixes page, applied live.
- Ambient occlusion is off by default: in VR it shaded rocks, hills and roofs
  differently in each eye. Fixes page → Ambient occlusion brings it back.
- Fixed a likely crash after changing the menu language.
- A settings reference and credits on the website; its controller guide
  follows the controls cards.

The owner played this release in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill) through private tests: the new VR menu with the laser and a gamepad, the view shortcuts, full-VR close-ups that follow the camera without getting stuck, blur in both eyes, from the start of a story scene, the menu in front of the stereo screen, a gamepad taking over at once from a put-down controller, the controls cards and the translated menu and VR controllers and a Reality Runner treadmill working together. The last test's additions (treadmill runs in the VR menu, the Controllers page, ambient occlusion off, the L3 card pinned low) and recording in 5-minute parts still need a headset check. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot, and in full VR the HUD can stay squashed until ESC.

## Update

Update the launcher app to **1.2.0**, then choose and install **1.2.0 beta**
in step 02. The exact VR build is **vr-menu-1-2-0**. Updating the app preserves your
selected VR package, so both updates are needed. Close the game and injector
first. Your settings carry over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

In full VR the HUD can stay squashed after a view switch or a new area until you open and close ESC, many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). Scene/dialogue loading stalls and moving backgrounds behind flat menus remain open; mono theatre can help with menus. Some NPC lighting, reflections and fog/VFX still need testing. Automatic cinematic switching is experimental and off by default. Epic injection and broad headset compatibility remain unverified.

During one test, a graphics driver reset happened right after three view
switches while recording at 3292 x 3292 per eye on a 10 GB card. If it happens
to you, a lower render resolution reduces the load.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-2
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
