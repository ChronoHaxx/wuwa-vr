# PlayStation shortcut input

Direct DualShock 4 and DualSense shortcut support is experimental. Parser and
shared shortcut tests are offline evidence; USB/Bluetooth gameplay acceptance
still requires a physical controller test.

The automatic read-only fallback supports Sony VID `054c` with DualShock 4
products `05c4` / `09cc`, DualSense `0ce6`, and DualSense Edge `0df2`.
It reads one connected controller at a time. It recognizes USB, Bluetooth basic,
and Bluetooth enhanced input reports without enabling enhanced mode or changing
controller state. Other manufacturers, dongles and emulated HID IDs are excluded.

| PlayStation | Existing shortcut vocabulary |
| --- | --- |
| L1 / R1 | LB / RB |
| L2 / R2 | LT / RT (independent analogue triggers) |
| Cross / Circle / Square / Triangle | A / B / X / Y |
| Share (PS4) / Create (PS5) | View |
| Options | Menu |
| L3 / R3 | Click the left / right stick |

The shared Lua state machine handles portal (`L3 + L2`), diorama (`L3 + R2`),
stereo screen (both triggers first, then click `L3`), mono theatre
(both triggers first, then click `R3`), and the other existing WuWa shortcuts.
`L3 + R3` opens/closes UEVR and respects its optional long-press setting.
Direct HID does not add UEVR menu navigation: use existing mapped XInput,
keyboard/mouse, or VR menu controls to navigate it.

Any successfully polled raw XInput pad takes priority, including Steam Input
and an unrelated Xbox pad. Direct input waits until no such poll has occurred
for two seconds, and requires a fresh report (at most 250 ms old). This is a
conservative priority rule; XInput does not identify which HID device produced
its state. The shortcut panel shows the chosen/waiting status. A selected
XInput slot, controller passthrough, disabled shortcuts or focus loss suspends
direct shortcuts. Release all controls after reconnecting or changing sources.

Direct HID reads cannot remove buttons from the game's own HID input, so the
game can also react to a shortcut. A working Steam Input mapping remains the
preferred route for consumed chords. This feature creates no virtual controller,
does not install drivers, and does not send output/feature reports. There is no
rumble, adaptive trigger, PS button, microphone button or touchpad support.

Implementation boundaries:

- `WuWaPlayStationInput.hpp` independently decodes bounded report layouts.
  Enhanced Bluetooth reports require a valid CRC; unknown/truncated reports
  fail without updating the last good pad.
  Windows DS4 Bluetooth's 547-byte transport buffer is normalized to the
  known report payload; enhanced-report CRC covers its first 78 bytes.
- `WuWaPlayStationHid.hpp` owns a joinable background worker, shared read-only
  handles, stop event, and cancelled/drained overlapped reads. Render/Lua calls
  only copy a snapshot. No device enumeration or blocking read occurs there.
  The backend is pinned from its own code address before starting the worker;
  pin failure disables this reader. UEVR exposes no supported backend hot-unload
  path. On confirmed process exit, ownership is released without touching the
  potentially abandoned mutex/thread, following the existing diagnostic policy;
  Windows reclaims it. Normal cleanup outside loader lock cancels/drains and
  joins. A defective HID driver's cancellation latency is not guaranteed bounded.
- `XInputHook.cpp` records successful raw polls before VR synthesis; the Lua
  route change clears pending actions and requires release before rearming.
- `02_WuWaVR_PolarControls.lua` processes HID and XInput through the same
  shortcut state machine. The native HID copy is never returned to the game.

Protocol references (consulted 2026-10-06; no SDL source copied):

- [SDL PS4 input layouts and packet validation](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_ps4.c)
- [SDL PS5 simple/extended layouts and packet validation](https://github.com/libsdl-org/SDL/blob/main/src/joystick/hidapi/SDL_hidapi_ps5.c)
- [SDL Sony USB product identifiers](https://github.com/libsdl-org/SDL/blob/main/src/joystick/usb_ids.h)
- [DS4Windows Windows Bluetooth buffer size and CRC position](https://github.com/CircumSpector/DS4Windows/blob/master/DS4Windows/DS4Library/DS4Device.cs)

Checks: `mod/tests/test_playstation_input.ps1 -OutputRoot <new-folder>`
compiles the production decoder, menu/source policy and Windows reader header.
Its first test is device-free; two separate lifecycle processes then start the
real reader with shared read-only HID access and exercise normal stop and
process-exit owner release. Those do not press controls or send output reports.
This local lifecycle run found no Sony controller, so it does not establish
pending-read cancellation behavior with real controller hardware.
Run `mod/tests/test_playstation_shortcuts.lua`
with Lua 5.4 and `mod/lua/02_WuWaVR_PolarControls.lua` as its argument, plus the
existing `dev/test_portal_shortcut.lua` and `dev/test_mono_theatre.lua` suites.
