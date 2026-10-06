# WuWa VR 1.1.1 beta — Steam resize startup repair

For Wuthering Waves 3.7 on Windows x64.

Steam startup on the owner's PC crashed while Kuro worked with the same 1.1.0
package. The matching crash dump and symbols show runaway recursion in our
DirectX 11 resize callback's early forwarding path. This repair removes the
global resize hook during renderer detection. Resize handling attaches only to
a confirmed DirectX 11 game surface, stores its original callback, and rejects
recursive calls before forwarding or notifying the renderer. It does not patch
Steam's code or require a per-PC graphics preset. The existing DirectX 12
renderer handoff is unchanged.

The launcher also ends its wait when Windows confirms the original Steam game
process exited. Missing or ambiguous process information remains unconfirmed.
Stop waiting allows four seconds for normal cancellation before suggesting
stuck-worker recovery. It does not close the game or automatically kill workers.

## Update

Update the launcher app to **1.1.1**, then choose and install **1.1.1 beta** in
step 02. The exact VR build is **steam-resize-1-1-1**. Updating the app preserves
your selected VR package, so both updates are needed. With game and injector
closed, select **Use bundled simulator** if using the simulator; headset users
keep their valid headset runtime. Then launch once through the desired route.

The desktop app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## Evidence and remaining checks

The previous source reproduces the unsafe global resize change on a real DirectX
12 test surface before its first Present. The repaired production hooks pass
controlled recursion, active/filtered/retired callback, replacement, resize and
renderer-handoff checks on real DXGI surfaces. The full backend and launcher
builds, startup/identity/cancellation tests and isolated package checks are run
for this release. These checks do not reproduce every Steam overlay or driver.

The user confirmed 1.0.10 startup on the previously affected Windows 11 Steam simulator PC, and 1.1.0 through Kuro on the owner's PC. The owner's 1.1.0 Steam route crashed. Version 1.1.1 repairs the resize recursion found in that crash dump; controlled graphics and launcher tests pass, but this repair still needs real Steam and headset testing.

PS4/PS5 USB/Bluetooth shortcuts remain experimental and require physical testing.
Existing graphics preferences, input behavior, simulator and injector are retained.
Cinematic framing remains on; automatic cinema remains experimental and off.
Scene/dialogue stalls, HUD refresh, moving flat-menu backgrounds and some
lighting/reflection/fog differences remain open.

The launcher is MIT open source. Public mod source changes retain their component
licences. The installer is unsigned; checksums and any exact-file VirusTotal
reports are not safety certificates. Unofficial injection can trigger anti-cheat
or account restrictions. The project is not approved by Kuro Games.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-1-1-0
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
