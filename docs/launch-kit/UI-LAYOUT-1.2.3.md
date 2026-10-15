# WuWa VR 1.2.3 beta · 15 Oct 23:07 BST — 16:9 menus and HUD, VR mouse cursor, automatic cinema

For Wuthering Waves 3.7 on Windows x64. On top of 1.2.2.

## Fixed

- **Menu cursor in full VR:** the HUD and menus are now laid out at the flat
  game's 16:9, so round icons are round, story pictures are whole, and the
  mouse and controller hit what they point at (the game laid them out for
  each eye's shape). Controller buttons in the Terminal work. A **16:9 UI
  layout** switch on the HUD page brings back the taller 1.2.2 layout,
  where the mouse, controller shortcuts and the left-stick cursor now work too.
  Thanks to Indath for tracing the game's UI canvas size.
- **Fades reach both eyes:** login, loading and story-cut fades no longer
  leave one eye black or flash it white, and the one-eye frame after a world
  load is held back.
- **Classic UEVR window:** opens in front of you and stays open (also over
  the stereo screen); Back returns to the WuWa VR menu. Emergency keys:
  **Ctrl+Shift+End** opens UEVR's classic window and **Ctrl+Shift+Home** the
  WuWa VR menu. Thanks to Lelouche for the report and the idea.
- **Close-up move into performers:** in some over-the-shoulder story shots
  the close-up move put your eye inside the person in front. It now stops
  30 cm behind story performers.
- **One eye shaking on the stereo screen** in story shots with camera shake:
  both eyes now get the same game camera there too.
- **Menus after a view switch** keep their layout, so the mouse and the
  controller cursor still hit after switching to the stereo screen or mono
  theatre with a full-screen menu open.
- **The mouse stays in the game window** while the game is in front, so it no
  longer slips onto another monitor; Alt-Tab frees it. **Keep the mouse in the
  game window** on the Comfort page turns this off.
- **Restore controls from this launch** (classic menu) and its Undo work
  again (they failed with "Invalid profile value").
- **Translations:** every text in the WuWa VR menu is in all ten languages,
  including units, menu places, report kinds and the live status lines.
- **Launcher:** Launch in VR offers a waiting update once per version. The
  Player guide and Controls are updated for 1.2.3; Controls opens the
  guide's controls section in your language.

## New

- **VR mouse cursor:** keyboard-and-mouse players see an arrow on the game UI
  where the mouse is. It hides 10 seconds after the mouse stops, or as soon
  as you use the controller.
- **Simpler view shortcut:** hold **LT + RT** and click **R3** to switch
  between full VR and the stereo screen (click again to switch back). **Hold
  R3** (0.8 s) for mono theatre, and hold again to return.
- **Hide characters at your eyes** (off by default; hold **LT + RT**, then
  press **Y**): in story close-ups the headset's wider view can show a head
  right at your eye that the flat shot leaves out. With this on, that
  character is hidden whole while it is there; anyone the flat shot shows
  stays. While it is off, a small note above the HUD names the shortcut, 3
  times in all.
- **Automatic cinema on by default:** story videos switch to mono theatre and
  the 2D pictures some story scenes show switch to the stereo screen, then
  your view comes back. Story scenes and conversations keep the view you are
  in. Settings from older versions get it switched on once.
- **Story scenes in 16:9** (HUD page, on): with the taller layout, every
  conversation and story video switches to 16:9 while it plays, so story
  pictures show whole.
- **Character screen in the taller layout** (HUD page, on): with the 16:9
  layout, the character screen switches to the taller layout while it is
  open, so its floor reflection keeps showing the current character.
- **Diorama scale** (Quick page, 2x to 10x) and its own **Diorama camera**
  (Camera page, 5 m back by default). Thanks to Lelouche for the request.
- **The WuWa VR menu shows each setting's default** and whether it is saved;
  **X** (Xbox) or **Delete** puts the setting back to its default. **Save
  changes for next time** (Menu page) lets you try settings without saving.
- **Camera height reset:** in first person and the fixed camera, **L3 + Y +
  X** together puts the camera height back to its default.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1 in the headset with the Kuro launcher copy of the game, and an earlier 1.2.3 test build in the headset (SteamVR) with the Kuro launcher copy. The final 1.2.3 test builds were tested by the owner on the OpenXR simulator with both the Steam copy and the Kuro launcher copy: menus, the mouse and the controller cursor in the 16:9 and taller layouts, story pictures and story scenes, the character screen and the translations; on the Steam copy the UI redirect starts and the stereo fixes pass their code checks. The 1.2.3 release build has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.3** (the bar at the top offers it; use
Restart to update when idle). When it starts again with the game closed, it
installs **1.2.3 beta** by itself; **Back to 1.2.2** in step 02 returns
to your previous package. The exact VR build is **ui-layout-1-2-3**. Your settings carry
over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). The character screen switches to the taller layout so its floor reflection stays live, but on the Weapon, Echoes, Forte and Chain tabs the reflection still freezes. Quest markers sit a little below their target in full VR. Story-video detection for automatic cinema is not yet confirmed on 3.7. At some distances a tree can sway in the wind in one eye only. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Epic injection and broad headset compatibility remain unverified.

Turning the 16:9 layout on or off (or, in the taller layout, switching view)
while a journey log chapter or the map is open moves the stick cursor's ring
off its click point until you reopen that screen. Hiding characters at your
eyes works in story close-ups with the game camera in full VR only.

During one test, a graphics driver reset happened right after three view
switches while recording at 3292 x 3292 per eye on a 10 GB card. If it happens
to you, a lower render resolution reduces the load.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-2
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
