# WuWa VR 1.2.2 beta · 14 Oct 03:50 BST — HUD after view switches, ambient occlusion, Steam copy, package updates

For Wuthering Waves 3.7 on Windows x64. A fix release on top of 1.2.1.

## Fixed

- **HUD after the stereo screen or mono theatre:** going back to full VR could
  leave the HUD and dialogue squashed until you opened and closed ESC. The
  1.2.1 fix only worked when the headset's eye image was 2560 pixels wide.
  WuWa VR now gives the game's HUD layout the headset's view size on any
  headset, in the open world and in dialogue, and checks once a second in
  full VR, so a HUD the game rebuilds is caught too. Thanks to Lelouche for
  the reports.
- **Stereo screen with the game UI hidden (L3 + B):** the picture went black.
  The game picture now stays in both panels.
- **Steam copy of the game:** the HUD and menus were stuck to your eyes
  instead of sitting on the VR panel, and several stereo fixes (mono
  theatre's camera, the stereo camera, the reflection projection) were off.
  The Steam copy is the same game build as the copy from Kuro's launcher,
  but its program file is 20 KB smaller, and WuWa VR checked for one exact
  size. It now accepts this build at any size; every piece of game code it
  hooks must still match exactly before anything is hooked, as before.
  Thanks to Indath for the report.
- **Ambient occlusion turned on in UEVR's cvar list**
  (`r.AmbientOcclusionLevels`): it now gets the both-eyes fix as well, not
  only AO turned on through the Fixes page.

## Changed

- **Ambient occlusion is on by default.** Soft contact shadows in corners and
  under objects, with both eyes rendered from a symmetric view so rocks, hills
  and roofs shade the same in each eye. It is switched on once for settings
  saved before 1.2.2; if you turn it off on the menu's Fixes page, it stays
  off. On the test PC it cost about 2 ms per frame (7%).
- **The launcher updates the VR package by itself.** When the app starts with
  the game closed, it installs a newer public package for the same game
  version. The package you had stays installed, and **Back to …** in step 02
  returns to it. Test builds are never replaced. Step 02 now shows one Update
  button; the full version list and the developer tools are under
  Troubleshooting.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill) and played 1.2.1 in the headset with the Kuro launcher copy of the game. 1.2.2 was checked on the OpenXR simulator with both the Steam copy and the Kuro launcher copy: on both, the UI redirect starts, the stereo fixes pass their code checks, and the HUD comes back at the right size after the stereo screen and mono theatre, in the open world and in dialogue (on the Kuro copy also across nine simulated headsets). 1.2.2 has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.2** (the bar at the top offers it; use
Restart to update when idle). When it starts again with the game closed, it
installs **1.2.2 beta** by itself; **Back to 1.2.1** in step 02 returns
to your previous package. The exact VR build is **steam-identity-1-2-2**. Your settings carry
over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). In full VR the mouse or controller cursor in menus can pick a spot away from where it points, more so toward the edges; a fix is planned for 1.2.3. Opening the journey menu can flash one eye for a frame. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Automatic cinematic switching is experimental and off by default. Epic injection and broad headset compatibility remain unverified.

During one test, a graphics driver reset happened right after three view
switches while recording at 3292 x 3292 per eye on a 10 GB card. If it happens
to you, a lower render resolution reduces the load.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-1
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
