#pragma once

// Keeps the mouse inside the game window while it is in front.
// The flat game confines the cursor to its window; in VR the window is the
// desktop view, and with the game's menus open the mouse could slip off its
// edge onto another monitor (owner, 15 Oct 2026). While the setting is on and
// the window is in front, the cursor is clipped to the window's client area
// every frame, which also wins back a clip the game or another program reset.
// When the window loses focus, or the setting goes off, the clip is released,
// but only if it is still the one set here: another program's clip, or the
// game's own gameplay lock, is never touched.
#include <windows.h>
#include <cstdint>

namespace wuwa_cursor_clip {
enum class Action { none, clip, release };

inline bool valid(const RECT& r) noexcept { return r.right > r.left && r.bottom > r.top; }
inline bool same(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

struct State {
    bool ours{};        // the system clip is the one set here
    RECT clipped{};     // what was set
    uint64_t retaken{}; // clipped again after something else changed it while in front
};

// want: the setting is on and the game window is in front. window: its client area on the
// screen. current: the system clip now (GetClipCursor). Updates state for the action.
inline Action decide(State& s, bool want, const RECT& window, const RECT& current) noexcept {
    if (want && valid(window)) {
        if (same(current, window)) return Action::none;
        if (s.ours && same(s.clipped, window)) ++s.retaken;
        s.ours = true;
        s.clipped = window;
        return Action::clip;
    }
    if (!s.ours) return Action::none;
    s.ours = false;
    return same(current, s.clipped) ? Action::release : Action::none;
}

// The window's client area in screen pixels; empty when minimised or unknown.
inline RECT client_on_screen(HWND window) noexcept {
    RECT client{};
    POINT origin{};
    if (window == nullptr || !GetClientRect(window, &client) || !ClientToScreen(window, &origin)) return RECT{};
    return RECT{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
}

// One step: returns the action taken.
inline Action update(State& s, bool enabled, HWND window) noexcept {
    const bool front = window != nullptr && GetForegroundWindow() == window;
    const auto area = front ? client_on_screen(window) : RECT{};
    RECT current{};
    if (!GetClipCursor(&current)) return Action::none;
    const auto action = decide(s, enabled && front, area, current);
    if (action == Action::clip) ClipCursor(&s.clipped);
    else if (action == Action::release) ClipCursor(nullptr);
    return action;
}
} // namespace wuwa_cursor_clip
