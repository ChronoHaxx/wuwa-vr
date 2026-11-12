# WuWa VR 1.2.7 beta · 12 Nov 12:16 GMT — game 3.8, new VR menu, controls cards, VR setup

For Wuthering Waves 3.8 on Windows x64 (the game update of 11 November 2026).
On top of 1.2.6.

## Game 3.8

- **Works with 3.8:** the HUD and menus on the VR panel, the stereo fixes, god
  rays in the pagoda storm, the far lighting refill, the mono theatre story
  camera and the cinematic letterbox shape are ported to the new game build.
- One fix stays off on 3.8 for now: the per-eye correction for the game's
  custom mirror reflections. Those reflections can sit a little differently in
  each eye until it is ported.

## New

- **New VR menu:** a Home page with tiles, then View, Camera, Comfort,
  Controls, Interface, Graphics and Record (LB / RB change pages). The button
  hints match what you used last: Xbox, PlayStation, keyboard or VR
  controllers. Bigger text in every language, Arabic included. Settings for
  edge cases sit under More settings on each page.
- **VR setup on first launch** ("Welcome to VR"): a few questions set the
  camera, background blur, camera tilt and story scenes, each taking effect at
  once. Run it again from Interface > VR setup.
- **Controls cards with a live 3D controller:** hold L3 and the card shows
  every shortcut with a line to its button, lit as you press it. Hold LT + RT
  for the views card: full VR, stereo screen and mono theatre, with "Now" on
  the one you are in.
- **Keyboard shortcuts:** hold the VR key (the key left of 1; Right Ctrl or Off
  on the Controllers page) and press a key for any L3 shortcut. Hold it half a
  second for the keyboard card. With the freecam on, W A S D, Space / Ctrl,
  Shift and the mouse fly it.
- **The story camera's tilt comes back:** with Decoupled pitch on, story
  scenes, dialogue and ultimates get the game camera's tilt, then your steady
  view returns. The diorama goes back to normal size for an ultimate. Comfort
  page: Level the game camera now.
- **Your UID is hidden** in full VR, the stereo screen and mono theatre, also
  right after a view switch. The profile screen keeps its black box.
- **Recording stops when the game crawls:** Record page > Stop if the game
  slows down (under 5 fps for 20 s by default). You get a beep, a window on the
  desktop and RECORDING STOPPED in the headset. gpu.jsonl next to the video
  logs graphics memory and the game's frame rate.
- **Leaner launcher:** one screen with Game, VR mod and Headset rows. Launch in
  VR lights up four steps (Permission, Press Play, Starting VR, In VR), and a
  row that fails says why. Troubleshooting has Repair, Reset and Save a problem
  report; More options has Open settings folder and every version.
- A new look: Abby in a VR headset, on the launcher and the website.

## Fixed

- A deleted or blank config.txt no longer blocks VR start or runs on UEVR's own
  defaults: WuWa VR puts its supplied one back and keeps every other file.
- Prepare uninstall puts back your UEVR settings from before WuWa VR first.
- Recordings cover the whole HUD for 0.4 s after a view switch and keep the
  privacy boxes while the launcher's status lags, so no frame shows the UID.

The owner played 1.2.0 to 1.2.6 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with Virtual Desktop and a Reality Runner treadmill), and the 1.2.7 work on game 3.8 in the headset (SteamVR, Bigscreen Beyond, Kuro launcher copy): the new VR menu, the controls cards and about 70 minutes of recording, on the test build before the last four 3.8 rendering fixes. This release build ran on the OpenXR simulator with the Kuro launcher copy of 3.8: the UI redirect, the menus and four of the five stereo code routes pass their checks, god rays are hooked, and the far lighting refill caught a game refresh in one eye. The Steam copy had not downloaded 3.8 yet, so it is not checked on 3.8. The story camera tilt in ultimates and the VR setup questions have not been played through in the headset yet. Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.7** (the bar at the top offers it; use
Restart to update when idle). 1.2.7 is made for game 3.8, so the app does
not swap your 3.7 package by itself: select **Update to 1.2.7 beta** in
the VR mod row (Launch in VR offers it too). **Back to previous version** returns
to 1.2.6. The exact VR build is **game-3-8-1-2-7**. Your settings carry over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower Render resolution on the menu's Graphics page helps). Full VR can slow down a lot after switching between full VR and the screens several times in one session; restart the game. The character screen switches to the taller layout so its floor reflection stays live, but on the Weapon, Echoes, Forte and Chain tabs the reflection still freezes. Quest markers sit a little below their target in full VR. On 3.8 the per-eye fix for the game's custom mirror reflections is off until it is ported, so those reflections can sit a little differently in each eye. At some distances a tree can sway in the wind in one eye only. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Epic injection and broad headset compatibility remain unverified.

Turning the 16:9 layout on or off (or, in the taller layout, switching view)
while a journey log chapter or the map is open moves the stick cursor's ring
off its click point until you reopen that screen. Hiding characters at your
eyes works in story close-ups with the game camera in full VR only.

After a future game update that only moves code, the self-port keeps the fixes
on. If the game's code changed, those fixes switch off until a matching WuWa VR
update is out; the game itself keeps working.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-6
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
