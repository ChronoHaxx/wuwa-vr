# WuWa VR 1.2.6 beta · 25 Oct 14:18 GMT — your voice in recordings, far lighting refill, no cutscene bars in full VR

For Wuthering Waves 3.7 on Windows x64, with the game update of 16 October 2026
(also checked with the game's 3.7.16 update). On top of 1.2.5.

## New

- **Your microphone in recordings** (Record page > Microphone, Off by default):
  pick a microphone by name and your voice is mixed into the video's sound with
  the game, kept in sync by the microphone's own clock. Microphone volume goes
  from 0 to 300 %. A microphone that is not plugged in shows as "not connected".
  The same choices are in the WuWa VR app's Recording options. SteamVR only.
- **Separate sound files** (Record page, on by default): next to the video you
  also get game.wav (what your PC plays) and microphone.wav (your voice), each
  alone, for editing.
- **One-eye recordings:** Record page > Video style "Left eye, whole view" or
  "Right eye, whole view": that eye's whole picture as you see it, one frame per
  game frame (no repeated frames), with the headset pose of every frame saved
  for steadying later. SteamVR only.
- **Scene info in recordings** (Record page, on by default): scene.txt next to
  the video lists the quest and its step, dialogue lines, place names, the map
  and your character at video time, so a clip can be found by what happened.
  Your UID is never read or saved.
- **Recordings without the one-hour limit:** they keep going until you stop
  them (or the drive has less than 10 GB free). If SteamVR or the headset
  stops, the video still plays, and a note in the headset says when a
  recording stopped by itself.
- **Controls card above your view:** HUD page > Card place, Below your view (as
  before) or Above your view; Card height is the angle from your line of sight.
- **Refill far lighting now** (Fixes page): refills the far lighting in both
  eyes by hand.

## Fixed

- **Far lighting darker in one eye:** when the game refreshes its far lighting
  outside a loading screen (after a long glide), it rebuilt only one eye's
  copy, so distant hills looked darker in the other eye. WuWa VR now notices
  and refills both eyes (Fixes page, Refresh far lighting automatically).
- **Cutscene bars:** full VR hides the game's letterbox bars above and below
  dialogue and story scenes (Fixes page, on by default). The stereo and mono
  screens keep the game's bars; the game's Graphics setting Cutscene Mode >
  Full Screen removes them there too.
- **First person with the diorama** keeps your view in your character's head
  again, with the world made small around you, instead of showing the headless
  body from 5 m away.
- Long choices in the VR menu (microphone names) fit between the arrows.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1, an earlier 1.2.3 test build and the 1.2.5 test builds in the headset with the Kuro launcher copy of the game, and the 1.2.6 release build in the headset (SteamVR, Bigscreen Beyond, Kuro launcher copy), including the controls card above the view and a recording with the microphone. On the OpenXR simulator, with both the Steam and the Kuro launcher copies and the game's 3.7.16 update: the UI redirect starts and the stereo fixes pass their code checks; first person with the diorama stays in the head; the cutscene bars are hidden in full VR; the far lighting is refilled when the game rebuilds it in one eye. The far lighting refill after a real long glide has not been seen in the headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.6** (the bar at the top offers it; use
Restart to update when idle). When it starts again with the game closed, it
installs **1.2.6 beta** by itself; **Back to 1.2.5** in step 02 returns
to your previous package. The exact VR build is **recording-voice-1-2-6**. Your settings carry over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). Full VR can slow down a lot after switching between full VR and the screens several times in one session; restart the game. The character screen switches to the taller layout so its floor reflection stays live, but on the Weapon, Echoes, Forte and Chain tabs the reflection still freezes. Quest markers sit a little below their target in full VR. Story-video detection for automatic cinema is not yet confirmed on 3.7. At some distances a tree can sway in the wind in one eye only. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Epic injection and broad headset compatibility remain unverified.

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

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-5
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
