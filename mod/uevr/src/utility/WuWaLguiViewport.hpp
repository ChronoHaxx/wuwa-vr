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
// two more left-eye readers (0xd573c0, 0xd57ce0): world positions through the
// player camera, sized by the window, independent of the canvas; left alone.
// Test 12 (14 Oct 2026) hooked only the first three: controller buttons in the
// Terminal clicked at eye-sized pixel positions and missed.
// The taller layout (16:9 off, or the character screen) keeps the eye rect for
// the layout, but the mouse still moves over the window and our VR cursor is
// drawn at its window position. The view-rect getter's three callers (3.7) are
// the mouse ray (0xd58a50: cursor pixels / rect size -> 0..1 -> ray), the
// scaler's viewport check (0xdd3f80) and a centre-of-screen ray (0xdd99b0), so
// there only the mouse ray gets the window size (test 17l, 15 Oct 2026: the
// mouse divided window pixels by the eye rect and missed in the taller layout).
// Controller presses reach the same ray: the game turns the button's UI point
// into viewport pixels (sites 3 and 4) and casts the ray there, so with the ray
// alone on the window size, Y -> Journey log in the quest menu missed (17l).
// Test 17m gave sites 3 and 4 the window size too, but the left-stick cursor
// (journey log chapters, the map) is the game's script: it reads the canvas
// viewport size once when the screen opens (caller 6 below, the script binding)
// and moves the pointer in those canvas pixels. In the taller layout that is
// the eye rect, so the window-sized ray clicked below the cursor ring (17m,
// 15 Oct 2026: ring on a quest card, A did nothing). Test 17n kept the canvas
// (eye) rect for everything, as 17k did, and converted the mouse where LGUI's
// ProcessInput reads it, but the game compares that position with the raw
// mouse elsewhere: controller input then looked like mouse movement and the
// game switched to keyboard and mouse (hints changed, the d-pad wheel would not
// stay open). Since test 17o the mouse is left alone and the pointer ray picks
// the rect per pointer: the mouse (pointer 0 in pointer mode, window pixels)
// gets the window size; the stick cursor (navigation mode) and the controller
// presses (their own pointer, at the canvas pixels of sites 3 and 4) keep the
// canvas rect.
// The script's size goes stale if the layout or view changes while such a
// screen is open (the ring is then offset by half the size difference until
// the screen is reopened); nothing here can refresh it.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace wuwa_lgui_viewport {
struct State {
    std::atomic<bool> installed{false};
    // Window size as (width << 32 | height); 0 leaves the game's rect alone.
    std::atomic<uint64_t> size{0};
    std::atomic<uint64_t> rewrites{0};
    // Window size while full VR shows the taller layout (the mouse's ray); 0 otherwise.
    std::atomic<uint64_t> pointer_size{0};
    std::atomic<uint64_t> pointer_rewrites{0}; // the pointer ray for every pointer (site 1, bit 5)
    std::atomic<uint64_t> input_rewrites{0};   // UI point -> pixels (sites 3 and 4, bit 6)
    std::atomic<uint64_t> mouse_rewrites{0};   // the pointer ray for the mouse (site 1, bit 7)
    // The game's own rect size, as the sites read it before any rewrite: (w << 32 | h), 0 until seen.
    std::atomic<uint64_t> eye_size{0};
    // Bit n lets site n rewrite (WuWaLguiRedirect.hpp viewport_sites); all five by default.
    // In the taller layout, bit 5 gives the pointer ray for every pointer and bit 6 the point
    // conversions the window size (test 17m; off since 17n), and bit 7 the mouse's ray (17o).
    // Diagnostics only (VR_WuWaUiWindowSizeSites, never saved): finds which reader a game
    // system depends on (15 Oct: the Resonators floor reflection froze in 16:9 only).
    std::atomic<uint32_t> sites{0x9f};
    // GetViewportSize (site 2) by caller, for the same test: calls per caller (the direct calls
    // below, then any other) and bit n lets caller n have the window size (VR_WuWaUiViewportCallers,
    // never saved). Without site 2 the reflection is live but the layout breaks, so one caller is it.
    std::array<std::atomic<uint32_t>, 9> callers{};
    std::atomic<uint32_t> caller_mask{0x1ff};
    // The return addresses below in the running game build (set_live_returns; 0 = not found).
    std::array<std::atomic<uint32_t>, 8> live_returns{};
    std::atomic<uint32_t> live_pointer_ray_return{0};
};
// Read by hook stubs until process exit; never destroyed.
inline State& state() { static auto* value = new State{}; return *value; }
// Return addresses (RVA) of the eight direct calls to GetViewportSize (0xc66dc0) in the 3.7 client.
inline constexpr std::array<uintptr_t, 8> viewport_size_returns{
    0xccde0c, 0xdbb6cf, 0xdd31b5, 0xdd3208, 0xdd4b1f, 0xe0d938, 0x22620ae, 0x285c43d};
// Return address (RVA) of the pointer ray's call to the view-rect getter (0xc66cc0) in the 3.7 client.
inline constexpr uintptr_t pointer_ray_return = 0xd58b15;
// LGUIPointerEventData (3.7 reflection): inputType (byte; 0 pointer, 1 navigation) and pointerID.
inline constexpr uint32_t event_input_type = 0x40, event_pointer_id = 0x44;
// The pinned return addresses in the running build: lookup(reference RVA) gives the live RVA or 0.
template<class Lookup> inline void set_live_returns(Lookup lookup) noexcept {
    auto& s = state();
    for (size_t i = 0; i < viewport_size_returns.size(); ++i)
        s.live_returns[i].store(lookup(uint32_t(viewport_size_returns[i])), std::memory_order_relaxed);
    s.live_pointer_ray_return.store(lookup(uint32_t(pointer_ray_return)), std::memory_order_release);
}
inline bool pointer_ray_caller(uintptr_t return_rva) noexcept {
    const auto live = state().live_pointer_ray_return.load(std::memory_order_acquire);
    return live != 0 && return_rva == live;
}
// Counts the call and says whether this caller gets the window size.
inline bool allow_caller(uintptr_t return_rva) noexcept {
    uint32_t caller = 8;
    for (uint32_t i = 0; i < viewport_size_returns.size(); ++i)
        if (const auto live = state().live_returns[i].load(std::memory_order_relaxed); live != 0 && live == return_rva) caller = i;
    auto& s = state();
    s.callers[caller].fetch_add(1, std::memory_order_relaxed);
    return (s.caller_mask.load(std::memory_order_relaxed) >> caller) & 1u;
}

inline bool installed() noexcept { return state().installed.load(std::memory_order_acquire); }
// True while full VR's screen UI is laid out at the window size.
inline bool active() noexcept { return installed() && state().size.load(std::memory_order_acquire) != 0; }

// enable: full VR lays the screen UI out at the window size (16:9). pointer: full VR shows the
// screen UI in the taller layout, so the mouse's ray takes the window size.
inline void update(bool enable, bool pointer, uint32_t width, uint32_t height) noexcept {
    const bool sane = width >= 64 && height >= 64 && width <= 32768 && height <= 32768;
    const auto size = sane ? (uint64_t{width} << 32) | height : 0;
    state().size.store(enable ? size : 0, std::memory_order_release);
    state().pointer_size.store(!enable && pointer ? size : 0, std::memory_order_release);
}

// Remembers the game's rect size (every site reads the same rect), for the log.
inline void record(const int32_t* rect) noexcept {
    if (rect[2] <= rect[0] || rect[3] <= rect[1]) return;
    state().eye_size.store(uint64_t(uint32_t(rect[2] - rect[0])) << 32 | uint32_t(rect[3] - rect[1]),
        std::memory_order_relaxed);
}

// The mouse: pointer 0 in pointer mode, whose position LGUI's ProcessInput takes from the window.
inline bool is_mouse(const uint8_t* event_data) noexcept {
    if (!event_data) return false;
    int32_t id{};
    std::memcpy(&id, event_data + event_pointer_id, sizeof(id));
    return event_data[event_input_type] == 0 && id == 0;
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

// The UI point -> pixel conversions (sites 3 and 4): the window size in 16:9 as for every reader;
// in the taller layout only with bit 6 (17m's behaviour, a diagnostic since 17n).
inline void rewrite_input(uintptr_t frame, uint32_t offset, uint32_t site) noexcept {
    auto& s = state();
    if (s.size.load(std::memory_order_acquire)) return rewrite(frame, offset, site);
    const auto size = s.pointer_size.load(std::memory_order_acquire);
    if (!size || !((s.sites.load(std::memory_order_relaxed) >> 6) & 1u)) return;
    if (apply(reinterpret_cast<int32_t*>(frame + offset), size)) s.input_rewrites.fetch_add(1, std::memory_order_relaxed);
}

// The view-rect getter (site 1) called by the pointer ray for event_data's pointer: the window size
// in 16:9 as for every reader. In the taller layout the mouse's position is window pixels, so its
// ray takes the window size (bit 7); the stick cursor and controller presses are canvas pixels and
// keep the rect. Bit 5 gives every pointer the window size (17m's behaviour).
inline void rewrite_pointer_ray(uintptr_t frame, uint32_t offset, const uint8_t* event_data) noexcept {
    auto& s = state();
    if (s.size.load(std::memory_order_acquire)) return rewrite(frame, offset, 1);
    const auto size = s.pointer_size.load(std::memory_order_acquire);
    const auto sites = s.sites.load(std::memory_order_relaxed);
    const bool mouse = ((sites >> 7) & 1u) && is_mouse(event_data);
    if (!size || (!mouse && !((sites >> 5) & 1u))) return;
    if (apply(reinterpret_cast<int32_t*>(frame + offset), size))
        (mouse ? s.mouse_rewrites : s.pointer_rewrites).fetch_add(1, std::memory_order_relaxed);
}
} // namespace wuwa_lgui_viewport
