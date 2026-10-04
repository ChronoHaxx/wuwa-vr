#pragma once

#include <array>
#include <cstddef>

namespace wuwa_screen_eye_order {

// The 2D intermediates retain source order: [0] is the game's first half,
// [1] is the NSF capture (or the game's second half without NSF). OpenXR UI
// and UI_RIGHT are eye-specific layers, so apply the normal NSF composite's
// eye assignment when selecting them. OpenVR's mono overlay does not use this.
constexpr std::array<std::size_t, 2> openxr_sources(
    bool native_stereo_fix, bool swap_eyes, bool afr, bool capture_available) noexcept {
    // Use the actual AFR mode, including duplicate presents for which the
    // component's per-frame is_afr flag is false. Missing captures retain the
    // ordinary side-by-side fallback instead of reversing unrelated content.
    const bool swap = native_stereo_fix && swap_eyes && !afr && capture_available;
    return {swap ? 1U : 0U, swap ? 0U : 1U};
}

} // namespace wuwa_screen_eye_order
