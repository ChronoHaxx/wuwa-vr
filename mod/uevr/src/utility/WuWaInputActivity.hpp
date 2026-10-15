#pragma once

// When the game last received controller input, so the VR mouse cursor can hide as soon as
// the player picks up the controller instead of lingering beside the game's own cursor ring
// (user request 15 Oct 2026). The XInput hook notes the final state the game sees.
#include <atomic>
#include <cstdint>
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
} // namespace wuwa_input_activity
