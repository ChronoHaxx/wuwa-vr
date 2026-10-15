#pragma once

// Full VR shows the flat game's 16:9 screen UI.
// Kuro's LGUICanvas sizes the screen canvas from the local player's left-eye
// projection data (GetProjectionData(viewport, left eye) -> constrained view
// rect), where stock LGUI asks the player controller for the window size. In
// full VR that is the eye (2064 x 2208, 3292 x 3292, ...), so the layout, the
// UI projection and the mouse ray all took the eye's shape: story pictures
// were cropped at the sides, the HUD needed the 4:3 "HUD shape" stretch, and
// the cursor picked the wrong item. Five LGUI functions read that rect (canvas
// viewport size, the inlined copy in GetProjectionMatrix, a view-rect getter
// used by the mouse ray and the scaler, and two UI-point <-> pixel conversions
// that controller button presses use); the redirect hooks each one just after
// the engine call and replaces the rect with the window size. The binary has
// two more left-eye readers (0xd56ec0, 0xd577e0): world positions through the
// player camera, sized by the window, independent of the canvas; left alone.
// Test 12 (14 Oct 2026) hooked only the first three: controller buttons in the
// Terminal clicked at eye-sized pixel positions and missed.
// The taller layout (16:9 off, or the character screen) keeps the eye rect for
// the layout, but the mouse still moves over the window and our VR cursor is
// drawn at its window position. The view-rect getter's three callers (3.7) are
// the mouse ray (0xd58550: cursor pixels / rect size -> 0..1 -> ray), the
// scaler's viewport check (0xdd3a80) and a centre-of-screen ray (0xdd94b0), so
// there only the mouse ray gets the window size (test 17l, 15 Oct 2026: the
// mouse divided window pixels by the eye rect and missed in the taller layout).
// Controller presses reach the same ray: the game turns the button's UI point
// into viewport pixels (sites 3 and 4) and casts the ray there, so with the ray
// alone on the window size, Y -> Journey log in the quest menu missed (17l). In
// the taller layout sites 3 and 4 take the window size too (test 17m): every
// pixel conversion then uses one size, as in 16:9; the layout keeps the eye rect.
#include <array>
#include <atomic>
#include <cstdint>

namespace wuwa_lgui_viewport {
struct State {
    std::atomic<bool> installed{false};
    // Window size as (width << 32 | height); 0 leaves the game's rect alone.
    std::atomic<uint64_t> size{0};
    std::atomic<uint64_t> rewrites{0};
    // Window size for the input conversions while full VR shows the taller layout; 0 otherwise.
    std::atomic<uint64_t> pointer_size{0};
    std::atomic<uint64_t> pointer_rewrites{0}; // the mouse ray (site 1, one caller)
    std::atomic<uint64_t> input_rewrites{0};   // UI point -> pixels (sites 3 and 4)
    // Bit n lets site n rewrite (WuWaLguiRedirect.hpp viewport_sites); all five by default.
    // In the taller layout, bit 5 lets the mouse ray and bit 6 the point conversions have the window size.
    // Diagnostics only (VR_WuWaUiWindowSizeSites, never saved): finds which reader a game
    // system depends on (15 Oct: the Resonators floor reflection froze in 16:9 only).
    std::atomic<uint32_t> sites{0x7f};
    // GetViewportSize (site 2) by caller, for the same test: calls per caller (the direct calls
    // below, then any other) and bit n lets caller n have the window size (VR_WuWaUiViewportCallers,
    // never saved). Without site 2 the reflection is live but the layout breaks, so one caller is it.
    std::array<std::atomic<uint32_t>, 9> callers{};
    std::atomic<uint32_t> caller_mask{0x1ff};
};
// Read by hook stubs until process exit; never destroyed.
inline State& state() { static auto* value = new State{}; return *value; }
// Return addresses (RVA) of the eight direct calls to GetViewportSize (0xc668c0) in the 3.7 client.
inline constexpr std::array<uintptr_t, 8> viewport_size_returns{
    0xccd90c, 0xdbb1cf, 0xdd2cb5, 0xdd2d08, 0xdd461f, 0xe0d438, 0x2261c1e, 0x2860cdd};
// Return address (RVA) of the mouse ray's call to the view-rect getter (0xc667c0) in the 3.7 client.
inline constexpr uintptr_t pointer_ray_return = 0xd58615;
// Counts the call and says whether this caller gets the window size.
inline bool allow_caller(uintptr_t return_rva) noexcept {
    uint32_t caller = 8;
    for (uint32_t i = 0; i < viewport_size_returns.size(); ++i) if (viewport_size_returns[i] == return_rva) caller = i;
    auto& s = state();
    s.callers[caller].fetch_add(1, std::memory_order_relaxed);
    return (s.caller_mask.load(std::memory_order_relaxed) >> caller) & 1u;
}

inline bool installed() noexcept { return state().installed.load(std::memory_order_acquire); }
// True while full VR's screen UI is laid out at the window size.
inline bool active() noexcept { return installed() && state().size.load(std::memory_order_acquire) != 0; }

// enable: full VR lays the screen UI out at the window size (16:9). pointer: full VR shows the
// screen UI in the taller layout, so only the mouse ray takes the window size.
inline void update(bool enable, bool pointer, uint32_t width, uint32_t height) noexcept {
    const bool sane = width >= 64 && height >= 64 && width <= 32768 && height <= 32768;
    const auto size = sane ? (uint64_t{width} << 32) | height : 0;
    state().size.store(enable ? size : 0, std::memory_order_release);
    state().pointer_size.store(!enable && pointer ? size : 0, std::memory_order_release);
}

// The engine call filled a local FIntRect {min x, min y, max x, max y} at rect.
inline bool apply(int32_t* rect, uint64_t size) noexcept {
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) return false; // no projection data: keep the game's fallback
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = static_cast<int32_t>(size >> 32);
    rect[3] = static_cast<int32_t>(size & 0xffffffffu);
    return true;
}

inline void rewrite(uintptr_t frame, uint32_t offset, uint32_t site) noexcept {
    auto& s = state();
    const auto size = s.size.load(std::memory_order_acquire);
    if (!size || !((s.sites.load(std::memory_order_relaxed) >> site) & 1u)) return;
    if (apply(reinterpret_cast<int32_t*>(frame + offset), size)) s.rewrites.fetch_add(1, std::memory_order_relaxed);
}

// The input conversions: the view-rect getter (site 1) called by the mouse ray, and the UI point
// -> pixel conversions (sites 3 and 4). The window size in 16:9 as for every reader, and in the
// taller layout too, where the layout readers keep the eye rect.
inline void rewrite_input(uintptr_t frame, uint32_t offset, uint32_t site) noexcept {
    auto& s = state();
    if (s.size.load(std::memory_order_acquire)) return rewrite(frame, offset, site);
    const auto size = s.pointer_size.load(std::memory_order_acquire);
    const uint32_t bit = site == 1 ? 5u : 6u;
    if (!size || !((s.sites.load(std::memory_order_relaxed) >> bit) & 1u)) return;
    if (!apply(reinterpret_cast<int32_t*>(frame + offset), size)) return;
    (site == 1 ? s.pointer_rewrites : s.input_rewrites).fetch_add(1, std::memory_order_relaxed);
}
} // namespace wuwa_lgui_viewport
