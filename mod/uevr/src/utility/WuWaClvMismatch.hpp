#pragma once
// Far lighting: one-eye rebuild detector (18 Oct 2026, game 3.7 hotfix 6ace6377).
// Each eye's view state keeps its own CLV volume at ViewState+0x27a8 (the rebuild path forms
// that address with `add rsi, 0x27a8` at 0x3eaf373). A game CLV refresh (r.CLV.TriggerRefresh,
// which the game's own scripts can issue) is consumed by the first eye rendered, so only that
// eye's volume replaces its cascade textures (+0xd0..+0x128); the other eye keeps stale far
// lighting, darker in the distance until something refills both. Measured on the simulator:
// one game refresh changed all 11 texture pointers of the first eye and none of the second;
// our refill changed both; a 60 s camera sweep changed neither (both centres moved together).
// Read only: guarded reads once per stereo pair; the decision is MismatchDetector, the refill
// is WuWaClvRefresh.hpp's tick (setting WuWaStereo_RefillFarLighting).
#include <array>
#include <cstdint>
#include <spdlog/spdlog.h>
#include "WuWaClvRefresh.hpp"
#include "WuWaLguiProbe.hpp"
#include "WuWaSelfPort.hpp"

namespace wuwa_clv {
inline constexpr uint32_t volume_offset_rva = 0x3eaf373;
inline constexpr std::array<uint8_t, 7> volume_offset_code{0x48, 0x81, 0xc6, 0xa8, 0x27, 0x00, 0x00};
inline constexpr uintptr_t view_state_offset = 8;        // FSceneView -> view state (constructor-proven)
inline constexpr uintptr_t state_volume_offset = 0x27a8;
inline constexpr uintptr_t volume_textures_offset = 0xd0;
inline MismatchDetector mismatch_detector{};             // game thread

// The volume and its texture pointers folded into one value; 0 when anything is unreadable.
inline uint64_t volume_signature(uintptr_t state) {
    namespace checked = wuwa_lgui_probe::detail;
    uintptr_t volume{};
    std::array<uint64_t, 11> textures{};
    if (!checked::read_field(state, state_volume_offset, volume) || volume == 0 ||
        !checked::read_field(volume, volume_textures_offset, textures)) return 0;
    uint64_t hash = 1469598103934665603ull ^ volume;
    for (const auto texture : textures) hash = (hash ^ texture) * 1099511628211ull;
    return hash ? hash : 1;
}

// Game thread, once per native-stereo eye pair, after on_stereo_frame.
inline void observe_pair(const void* first_view, const void* second_view) {
    auto state = detector_state.load();
    if (state == 0) {
        state = wuwa_self_port::code_matches(volume_offset_rva, volume_offset_code) ? 1 : -1;
        detector_state = state;
        spdlog::info("[WuWaCLV] one-eye rebuild detector: {}", state == 1 ? "watching" : "this game build differs; off");
    }
    if (state != 1 || first_view == nullptr || second_view == nullptr) return;
    namespace checked = wuwa_lgui_probe::detail;
    uintptr_t first_state{}, second_state{};
    checked::read_field(reinterpret_cast<uintptr_t>(first_view), view_state_offset, first_state);
    checked::read_field(reinterpret_cast<uintptr_t>(second_view), view_state_offset, second_state);
    const auto stale = mismatch_detector.observe(pair_sequence.load(), {first_state, second_state},
        {volume_signature(first_state), volume_signature(second_state)});
    if (stale) stale_eye = *stale;
}
} // namespace wuwa_clv
