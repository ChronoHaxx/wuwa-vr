# WuWa VR 1.1.2 beta · 7 Oct 22:07 BST — Steam startup fix

For Wuthering Waves 3.7 on Windows x64.

## Steam startup

1.1.1 still crashed at startup through Steam on the owner's Windows 10 PC. The
logs and crash dump showed the cause: the game's first frame arrived while the
mod was still checking the window, and the Steam overlay's hook on DirectX sent
that frame straight back into ours, over and over, until the game ran out of
stack. 1.1.2 guards every path that hands a frame on: if a frame comes back
round, it restores the real DirectX entry and presents the frame once. Normal
frames are untouched, including the DirectX 11 to 12 handoff used on the
Windows 11 PC.

The owner confirmed this release's VR build on the Windows 10 PC where 1.1.1 crashed through Steam: the game reaches VR through Steam and stays open. On that PC the Steam overlay sent the game's first frames back into our DirectX hook in a loop; 1.1.2 breaks the loop and presents the frame. The Windows 11 Steam PC retest and the new launcher window on a real launch are still pending.

## New launcher window

- Ten languages: English, 简体中文, 日本語, 한국어, Español, Português (Brasil),
  Français, Deutsch, Русский and العربية (right to left). A first start follows
  your Windows display language.
- Step progress: a check when a step is done, an outline on the next one.
- Black and gold look to match the website; app updates appear as a bar only
  when one exists, and the app version sits in the footer.

## Update

Update the launcher app to **1.1.2**, then choose and install **1.1.2 beta** in
step 02. The exact VR build is **steam-guard-1-1-2**. Updating the app preserves your
selected VR package, so both updates are needed. Close the game and injector
first. Then launch once through your usual route.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Evidence and remaining checks

A test that reproduces the overlay loop in its own process fails on 1.1.1's
hooks in all 7 cases and passes all 12 cases with the guard. Real-DirectX hook
checks pass with no guard warnings on normal paths. The backend in this release
is the exact file the owner ran through Steam.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.
Existing graphics preferences, input behavior, simulator and injector are retained.
Scene/dialogue stalls, HUD refresh, moving flat-menu backgrounds and some
lighting/reflection/fog differences remain open.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-1
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
