# WuWa VR 1.2.4 beta · 17 Oct 00:04 BST — works with the 16 October game update

For Wuthering Waves 3.7 on Windows x64, with the game update of 16 October 2026.
On top of 1.2.3.

## Fixed

- **HUD, menus and stereo fixes after the 16 October game update:** the small
  game update of 16 October (Steam and the Kuro launcher) turned off the 16:9
  HUD and menu layout, the mouse and controller cursor fixes and the stereo
  fixes, because they only switch on for the exact game build they were
  checked against. 1.2.4 is 1.2.3 moved to the new game build, on both the
  Steam copy and the Kuro launcher copy. Nothing else changes: settings,
  controls and features are as in 1.2.3.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1 in the headset with the Kuro launcher copy of the game, and an earlier 1.2.3 test build in the headset (SteamVR) with the Kuro launcher copy. 1.2.4 is 1.2.3 moved to the game update of 16 October. It was tested on the OpenXR simulator with the updated game: on the Kuro launcher copy the HUD and menus in full VR, on the stereo screen and in mono theatre, the mouse, the controller and stick cursors and the character screen; on the Steam copy the UI redirect starts and the stereo fixes pass their code checks. The 1.2.4 release build has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.4** (the bar at the top offers it; use
Restart to update when idle). When it starts again with the game closed, it
installs **1.2.4 beta** by itself; **Back to 1.2.3** in step 02 returns
to your previous package (with the updated game, 1.2.3 keeps its HUD layout and
stereo fixes off). The exact VR build is **game-hotfix-1-2-4**. Your settings carry over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). The character screen switches to the taller layout so its floor reflection stays live, but on the Weapon, Echoes, Forte and Chain tabs the reflection still freezes. Quest markers sit a little below their target in full VR. Story-video detection for automatic cinema is not yet confirmed on 3.7. At some distances a tree can sway in the wind in one eye only. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Epic injection and broad headset compatibility remain unverified.

Turning the 16:9 layout on or off (or, in the taller layout, switching view)
while a journey log chapter or the map is open moves the stick cursor's ring
off its click point until you reopen that screen. Hiding characters at your
eyes works in story close-ups with the game camera in full VR only.

After a future game update, the same fixes switch off again until a matching
WuWa VR update is out; the game itself keeps working.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-3
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
