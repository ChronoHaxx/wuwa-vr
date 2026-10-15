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
#include <array>
#include <atomic>
#include <cstdint>

namespace wuwa_lgui_viewport {
struct State {
    std::atomic<bool> installed{false};
    // Window size as (width << 32 | height); 0 leaves the game's rect alone.
    std::atomic<uint64_t> size{0};
    std::atomic<uint64_t> rewrites{0};
    // Bit n lets site n rewrite (WuWaLguiRedirect.hpp viewport_sites); all five by default.
    // Diagnostics only (VR_WuWaUiWindowSizeSites, never saved): finds which reader a game
    // system depends on (15 Oct: the Resonators floor reflection froze in 16:9 only).
    std::atomic<uint32_t> sites{0x1f};
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

inline void update(bool enable, uint32_t width, uint32_t height) noexcept {
    const bool sane = width >= 64 && height >= 64 && width <= 32768 && height <= 32768;
    state().size.store(enable && sane ? (uint64_t{width} << 32) | height : 0, std::memory_order_release);
}

// The engine call filled a local FIntRect {min x, min y, max x, max y} at frame + offset.
inline void rewrite(uintptr_t frame, uint32_t offset, uint32_t site) noexcept {
    auto& s = state();
    const auto size = s.size.load(std::memory_order_acquire);
    if (!size || !((s.sites.load(std::memory_order_relaxed) >> site) & 1u)) return;
    auto* rect = reinterpret_cast<int32_t*>(frame + offset);
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) return; // no projection data: keep the game's fallback
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = static_cast<int32_t>(size >> 32);
    rect[3] = static_cast<int32_t>(size & 0xffffffffu);
    s.rewrites.fetch_add(1, std::memory_order_relaxed);
}
} // namespace wuwa_lgui_viewport
