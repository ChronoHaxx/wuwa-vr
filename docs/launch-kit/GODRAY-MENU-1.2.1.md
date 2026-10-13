# WuWa VR 1.2.1 beta · 13 Oct 16:51 BST — live world behind menus, HUD, god rays and AO

For Wuthering Waves 3.7 on Windows x64. A fix release on top of 1.2.0.

## Fixed

- **Full-screen menus:** the quest log, the Esc menu and the journey menu no
  longer freeze the world into a picture fixed in front of your eyes. The game
  stopped drawing the world behind these menus (a saving on a flat screen); it
  now keeps drawing it, so the world stays in place when you turn your head.
  Behind these menus the graphics card now works as in normal play.
- **Journey (story) menu:** its background picture sits on the floating menu
  panel, behind the menu, instead of filling your view.
- **Squashed HUD after a view switch:** coming back to full VR from the stereo
  screen or mono theatre, the HUD and menus are full size straight away
  instead of squashed until ESC.
- **Storm-sky god rays** (Fogveil Pagoda and other storm skies): the light
  shafts no longer slide against the clouds when you turn your head. The game
  drew them for its own 75° camera instead of each eye's view.
- **Ambient occlusion** (Fixes page, still off by default): with it on, rocks,
  hills and roofs now shade the same in both eyes. The picture is slightly
  softer with it on.

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill). The 1.2.1 fixes (ambient occlusion in both eyes, the storm-sky god rays, the live world behind full-screen menus, the journey picture on the menu panel and the HUD after a view switch) were measured on the OpenXR simulator and played there by the owner; a headset check is still pending. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

## Update

Update the launcher app to **1.2.1**, then choose and install **1.2.1 beta**
in step 02. The exact VR build is **godray-menu-1-2-1**. Updating the app preserves your
selected VR package, so both updates are needed. Close the game and injector
first. Your settings carry over.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Known limits

Many dialogue close-ups sit wider than the 2D shot, and dialogue runs slower than on the screens (a lower render resolution on the menu's Fixes page helps). The HUD may still stay squashed after loading a new area until you open and close ESC. Opening the journey menu can flash one eye for a frame. Scene/dialogue loading stalls remain open. Some NPC lighting, reflections and fog/VFX still need testing. Automatic cinematic switching is experimental and off by default. Epic injection and broad headset compatibility remain unverified.

During one test, a graphics driver reset happened right after three view
switches while recording at 3292 x 3292 per eye on a 10 GB card. If it happens
to you, a lower render resolution reduces the load.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-2-0
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
