#pragma once

// When the game last received controller input, so the VR mouse cursor can hide as soon as
// the player picks up the controller instead of lingering beside the game's own cursor ring
// (user request 15 Oct 2026). The XInput hook notes the final state the game sees.
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <cstdlib>

#include <Windows.h>
#include <Xinput.h>

namespace wuwa_input_activity {
inline std::atomic<uint64_t>& gamepad_tick() { static std::atomic<uint64_t> value{0}; return value; }

// Any button, a trigger past its threshold or a stick past its dead zone.
inline bool active(const XINPUT_GAMEPAD& pad) noexcept {
    const auto past = [](SHORT x, SHORT y, int zone) { return std::abs(int{x}) > zone || std::abs(int{y}) > zone; };
    return pad.wButtons != 0 || pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
        past(pad.sThumbLX, pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) ||
        past(pad.sThumbRX, pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
}

inline void note(const XINPUT_GAMEPAD& pad, uint64_t now_ms) noexcept {
    if (active(pad)) gamepad_tick().store(now_ms, std::memory_order_relaxed);
}

// GetTickCount64 of the last controller input; 0 if none yet.
inline uint64_t last_gamepad() noexcept { return gamepad_tick().load(std::memory_order_relaxed); }

// And of the last key press or mouse movement the game window received (1.2.7): whichever was used last
// decides whether the cards and notices name controller buttons or keys.
inline std::atomic<uint64_t>& keyboard_tick() { static std::atomic<uint64_t> value{0}; return value; }
inline void note_keyboard(uint64_t now_ms) noexcept { keyboard_tick().store(now_ms, std::memory_order_relaxed); }
inline uint64_t last_keyboard() noexcept { return keyboard_tick().load(std::memory_order_relaxed); }
inline bool keyboard_last() noexcept { return last_keyboard() > last_gamepad(); }

// A PlayStation pad read over HID, and the VR controllers (their pad, the menu's buttons) (1.2.7): the menu and the
// cards show shortcuts on the device used last. Both also reach the game as XInput, so a gamepad tick within half
// a second of theirs is theirs.
inline std::atomic<uint64_t>& playstation_tick() { static std::atomic<uint64_t> value{0}; return value; }
inline std::atomic<uint64_t>& vr_tick() { static std::atomic<uint64_t> value{0}; return value; }
inline void note_playstation(uint64_t now_ms) noexcept { playstation_tick().store(now_ms, std::memory_order_relaxed); }
inline void note_vr(uint64_t now_ms) noexcept { vr_tick().store(now_ms, std::memory_order_relaxed); }

// 0 Xbox pad, 1 PlayStation pad, 2 VR controllers, 3 keyboard and mouse (wuwa_pad3d::Device). Nothing used yet: Xbox.
inline int last_device() noexcept {
    const uint64_t pad = last_gamepad(), ps = playstation_tick().load(std::memory_order_relaxed),
        vr = vr_tick().load(std::memory_order_relaxed), keys = last_keyboard();
    int device = 0;
    uint64_t at = pad;
    const auto take = [&](uint64_t tick, int which) {
        if (tick != 0 && tick + 500 >= at) { device = which; at = (std::max)(at, tick); }
    };
    take(ps, 1);
    take(vr, 2);
    if (keys > at) device = 3;
    return device;
}
} // namespace wuwa_input_activity
