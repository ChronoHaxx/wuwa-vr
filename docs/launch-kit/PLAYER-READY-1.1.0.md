# WuWa VR 1.1.0 beta — launcher clarity and PlayStation shortcuts

For Wuthering Waves 3.7 on Windows x64.

The user confirmed that 1.0.10 finally starts VR on the previously affected
Windows 11 Steam PC using the bundled simulator. That startup repair is retained.
This update improves the player flow and adds experimental native Sony shortcuts.

## What changed

- Launch requires a valid headset runtime or the simulator from the selected
  installed VR package. A simulator left registered by an older package blocks
  launch with a clear recovery action. Both the UI and startup helpers check it;
  runtime changes still require an explicit choice.
- The app and VR package display separate update status. New releases use a
  readable version such as **1.1.0 beta**. A package-update action deliberately
  selects the newest available beta; checking does not replace a rollback choice.
- DualShock 4 and DualSense shortcuts can read native USB/Bluetooth input when
  no XInput pad is active. Steam Input/XInput takes priority to prevent duplicate
  toggles. Direct input is experimental and has not yet passed physical testing.
- The player guides are shorter. Source, component licences and exact-download
  checks are easier to find. The launcher is MIT open source; the combined mod
  retains its upstream/community licence terms.

## Install or update

Use **WuWa-VR-Setup.exe**, then open WuWa VR. The first VR package download needs
internet. Existing users: update the launcher app to **1.1.0**, then explicitly
select and install **1.1.0 beta** in step 02. An app update keeps your old package
choice. If using the simulator, choose **Use bundled simulator** after the game
and injector are closed. For headset use, start its software and select its runtime.

Steam starts through Steam; Kuro users press Play in the Kuro launcher.
The installed app self-updates while idle. The portable ZIP is an advanced fallback
with the older browser interface and no desktop-app self-update.

## PlayStation controls

Connect a supported Sony controller to the PC. Read the input-source status under
**VR → WuWa Controls → Controller shortcuts**. L3/R3 mean stick clicks.

- L3 + R3: UEVR menu.
- L3 + Cross: recenter. L3 + Circle: hide/show HUD.
- Hold L2 + R2 first, then click R3: mono theatre.
- Hold L2 + R2 first, then hold L3 for 0.8 seconds: stereoscopic screen.

Release controls after connecting, changing source or returning from Alt-Tab.
Direct HID input only adds shortcuts; it does not create a virtual gamepad or
consume buttons from the game's own input. The game can still act on those chord
buttons. Use Steam Input if available for the existing XInput path. Another active
XInput pad also takes priority; unsupported/hidden/exclusively held devices may
need Steam Input. No adaptive triggers, rumble, touchpad or PS-button feature is added.

## Verification and limits

The earlier 1.0.10 startup was user-confirmed. This release's new flow, PlayStation
hardware and headset comfort still need user testing. Automated checks are
component evidence, not a claim of universal hardware compatibility. Source and
artifact identities are recorded in mod/checkpoint.json and SHA256SUMS.txt.

Cinematic framing remains on; automatic cinema remains experimental and off.
Scene/dialogue stalls, HUD-aspect recovery, moving flat-menu backgrounds and some
lighting/reflection/fog differences remain open. Manual mono theatre is available.

No new graphics-quality preset, driver or background service is installed.
Stopping verified launcher workers requires visible selection and confirmation.
Game, Steam, headset/runtime and injector processes are not recovery-kill targets.

The installer is unsigned. VirusTotal reports, when linked, describe exact file
hashes; they are not guarantees of safety or account compatibility. This unofficial
mod is not approved by Kuro Games; injection can trigger anti-cheat/account action.

Previous release: https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-enum-startup
Source: https://github.com/ChronoHaxx/wuwa-vr
Website: https://chronohaxx.github.io/wuwa-vr/
