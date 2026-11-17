# WuWa VR 1.2.8 beta · 17 Nov 15:33 GMT — UID hidden in the game, other 3.8 clients

For Wuthering Waves 3.8 on Windows x64. On top of 1.2.7.

## New

- **Your UID and profile ID are hidden in the game itself.** With Hide player
  IDs on (Record page; While recording is the default), the game no longer
  draws the UID in the corner, in any view, and with Also hide the profile ID
  row it no longer draws the ID row in the ESC menu either. Recordings show the
  normal HUD with no black box. A black box comes back only while WuWa VR cannot
  confirm the text is hidden: for up to 2 seconds as a recording starts, or
  after a game update moves the UID. Check a short video before sharing: other
  names and chat are not hidden.
- **Other 3.8 game clients:** 1.2.7's 3.8 fixes worked on one exact game
  build only. On another 3.8 client the HUD lost its 16:9 layout, so the
  minimap, quest list and team status sat outside the view, and the mono
  theatre camera, the cinematic letterbox and the far lighting check were off.
  WuWa VR now finds that code in other 3.8 builds and uses it wherever it is
  unchanged. A game hotfix during 3.8 gets the same treatment.

## Fixed

- **App updates failed with "being used by another process":** programs the
  app started (the game launcher after Play without VR, folders it opened)
  kept its install folder busy. They now start outside it. If an app update
  still does not install, the update bar now says so and names the likely
  cause.

The owner played 1.2.0 to 1.2.6 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with Virtual Desktop and a Reality Runner treadmill), and the 1.2.7 work on game 3.8 in the headset (SteamVR, Bigscreen Beyond, Kuro launcher copy). The 1.2.8 changes ran on the OpenXR simulator with the Kuro launcher copy of 3.8: the UID and the ESC profile ID are hidden in the game with no black box, and the HUD, menus and 3.8 fixes also pass through the path other 3.8 game clients take. They have not been played in the headset yet, and that path has not run on another client yet. The Steam copy had not downloaded 3.8 yet, so it is not checked on 3.8. The story camera tilt in ultimates and the VR setup questions have not been played through in the headset yet. Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.8** (the bar at the top offers it; use
Restart to update when idle). When it starts again with the game closed, it
installs **1.2.8 beta** by itself; **Back to previous version** in the VR mod
row returns to 1.2.7. The exact VR build is **clients-uid-1-2-8**. Your settings carry over.

If the app stays on its old version after the update (1.2.6 to 1.2.7 did for
some players), close the game and the Wuthering Waves launcher, including its
icon in the tray, or restart Windows. Then open WuWa VR and update before
anything else. From 1.2.8 on the app avoids this itself.

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

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-7
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
