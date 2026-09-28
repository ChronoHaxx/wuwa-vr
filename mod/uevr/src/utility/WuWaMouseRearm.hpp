#pragma once
#include <cstdint>

namespace wuwa_controls {
// Mouse-mode transitions must not reinterpret a button held to open a menu (or
// a shortcut chord) as a new click. LB/RB/R3 are allowed for HUD/utility mode.
struct MouseRearm {
    bool ready{};
    void reset() noexcept { ready = false; }
    bool accept(uint16_t buttons, int x, int y) noexcept {
        if (ready) return true;
        constexpr uint16_t mouse_buttons = 0x1000 | 0x2000 | 0x4000 | 0x000f;
        if ((buttons & mouse_buttons) == 0 && x > -8000 && x < 8000 && y > -8000 && y < 8000) ready = true;
        return false;
    }
};
}
