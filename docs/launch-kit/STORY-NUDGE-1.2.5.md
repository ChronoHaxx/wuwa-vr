# WuWa VR 1.2.5 beta · 18 Oct 18:16 BST — story camera nudge, recordings with sound, full ground detail

For Wuthering Waves 3.7 on Windows x64, with the game update of 16 October 2026.
On top of 1.2.4.

## New

- **Story camera nudge:** in story and dialogue shots in full VR, hold L3 and
  press Y to move nearer or X to move back (L3 + Y + X resets; keyboard
  Page Up / Page Down / Home). The nudge follows the game's own camera through
  the shot and resets at the next cut; the Cinema page can keep it for the whole
  scene. The controls card shows the amount.
- **Recordings with sound:** what your PC plays, as a stereo track (on by
  default; Record page). Windows 10 cannot record one program alone, so voice
  chat and music playing at the time are in it too.
- **Showcase video:** Record page, Video style "Showcase: one eye, steady,
  16:9" (SteamVR). Steadied and level with where you look (Showcase view height
  tunes it), 720p up to 2160p.
- **Bigger recordings on SteamVR:** 1536 and 2048 px per eye.
- **Desktop window HUD** sits where you see it in the headset (Record page;
  "Flat, like a monitor" is the old look).
- **Full ground detail** (Fixes page, on by default): in full VR the game drew
  the ground near you in less detail than in the flat game, so stone steps and
  ledges were covered by dirt and your feet sank in. Full VR now keeps the
  ground near you as detailed as the flat game; the stereo and mono screens
  keep the game's own setting.
- **Self-port:** after a small game update that only moves code around,
  WuWa VR finds its fixes in the new game build by itself, so the 16:9 HUD
  layout and the stereo fixes stay on. If the game's code really changed, they
  still switch off until a WuWa VR update.
- **Launcher:** Troubleshooting → Repair & recovery → Remove old versions.

## Fixed

- **First VR frame:** no black right eye or frozen left eye for the first
  seconds after VR starts, in full VR and on the stereo screen.
- First person, the fixed camera and freecam stay through story and NPC
  dialogue with keyboard and mouse, as they do on a controller.
- The streamer privacy box covers the ESC terminal's User ID in the 16:9
  layout too.
- The steady desktop window is off by default (it made the desktop window
  lag behind the headset); the Record page has a switch and a Steadiness slider.
- Freecam shows only the settings of the flying style you picked.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1 and an earlier 1.2.3 test build in the headset with the Kuro launcher copy of the game, and the 1.2.5 test builds (story camera nudge, showcase video, recordings, desktop HUD, first VR frame) in the headset (SteamVR) with the Kuro launcher copy. The 1.2.5 release build was tested on the OpenXR simulator with both the Steam and the Kuro launcher copies: the UI redirect starts and the stereo fixes pass their code checks; full ground detail turns on in full VR and back off on the stereo screen. The stereo screen start-up fix is checked by a unit test only. The 1.2.5 release build has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.5** (the bar at the top offers it; use
Restart to update when idle). When it starts again with the game closed, it
installs **1.2.5 beta** by itself; **Back to 1.2.4** in step 02 returns
to your previous package. The exact VR build is **story-nudge-1-2-5**. Your settings carry over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). Black bars above and below dialogue in full VR come from the game's Graphics setting Cutscene Mode: set it to Full Screen (this also removes them on the screens). Full VR can slow down a lot after switching between full VR and the screens several times in one session; restart the game. The character screen switches to the taller layout so its floor reflection stays live, but on the Weapon, Echoes, Forte and Chain tabs the reflection still freezes. Quest markers sit a little below their target in full VR. Story-video detection for automatic cinema is not yet confirmed on 3.7. At some distances a tree can sway in the wind in one eye only. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Epic injection and broad headset compatibility remain unverified.

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

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-4
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
