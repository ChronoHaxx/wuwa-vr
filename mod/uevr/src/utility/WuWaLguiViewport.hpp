#pragma once

// Full VR shows the flat game's 16:9 screen UI.
// Kuro's LGUICanvas sizes the screen canvas from the local player's left-eye
// projection data (GetProjectionData(viewport, left eye) -> constrained view
// rect), where stock LGUI asks the player controller for the window size. In
// full VR that is the eye (2064 x 2208, 3292 x 3292, ...), so the layout, the
// UI projection and the mouse ray all took the eye's shape: story pictures
// were cropped at the sides, the HUD needed the 4:3 "HUD shape" stretch, and
// the cursor picked the wrong item. Three copies read that rect (canvas
// viewport size, the inlined copy in GetProjectionMatrix, and a view-rect
// getter used by the mouse ray and the scaler); the redirect hooks each one
// just after the engine call and replaces the rect with the UI target size,
// which is the window. Live test 14 Oct 2026: whole story pictures, round
// HUD on a 16:9 panel, ray hit under the pointer, no relayout on view switch.
#include <atomic>
#include <cstdint>

namespace wuwa_lgui_viewport {
struct State {
    std::atomic<bool> installed{false};
    // Window size as (width << 32 | height); 0 leaves the game's rect alone.
    std::atomic<uint64_t> size{0};
    std::atomic<uint64_t> rewrites{0};
};
// Read by hook stubs until process exit; never destroyed.
inline State& state() { static auto* value = new State{}; return *value; }

inline bool installed() noexcept { return state().installed.load(std::memory_order_acquire); }
// True while full VR's screen UI is laid out at the window size.
inline bool active() noexcept { return installed() && state().size.load(std::memory_order_acquire) != 0; }

inline void update(bool enable, uint32_t width, uint32_t height) noexcept {
    const bool sane = width >= 64 && height >= 64 && width <= 32768 && height <= 32768;
    state().size.store(enable && sane ? (uint64_t{width} << 32) | height : 0, std::memory_order_release);
}

// The engine call filled a local FIntRect {min x, min y, max x, max y} at rsp + offset.
inline void rewrite(uintptr_t rsp, uint32_t offset) noexcept {
    auto& s = state();
    const auto size = s.size.load(std::memory_order_acquire);
    if (!size) return;
    auto* rect = reinterpret_cast<int32_t*>(rsp + offset);
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) return; // no projection data: keep the game's fallback
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = static_cast<int32_t>(size >> 32);
    rect[3] = static_cast<int32_t>(size & 0xffffffffu);
    s.rewrites.fetch_add(1, std::memory_order_relaxed);
}
} // namespace wuwa_lgui_viewport
