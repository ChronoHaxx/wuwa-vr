#pragma once
#include "WuWaStage0Payload.hpp"
#include <initializer_list>
#include <optional>

namespace wuwa_world_labels {
using Rect = std::array<int32_t, 4>;
using Extent = std::array<int32_t, 2>;
using Pair = std::array<Rect, 2>;

// The game's stage-0 setup consumes these records synchronously and copies
// their graph pointers/rectangles into its deferred pass, never the records.
struct TextureRect { uintptr_t graph{}; Rect rect{}; };
static_assert(sizeof(TextureRect) == 24 && offsetof(TextureRect, rect) == 8);

struct Layout {
    uint32_t views{}, argument_index{}, first_pass{}, second_pass{};
    bool native_fix{}, independent_eyes{};
    int32_t first_early_count{}, first_dispatch_count{};
    Extent game_extent{}, color_extent{}, depth_extent{};
    Rect color_input{}, depth_input{};
    // Both rectangle fields are read from each view. Preserve view order even
    // when Swap Eyes puts view 0 on the right. No scaled/guessed UV conversion.
    Pair rects_2f8{}, rects_1e30{};
    uint32_t argument_view_index{};
};
struct Plan { Pair color{}, depth{}; };

inline bool tiles_attachment(const Pair& pair, const Extent& size) {
    if (!wuwa_lgui_stage0::rectangle_valid(pair[0], size) ||
        !wuwa_lgui_stage0::rectangle_valid(pair[1], size)) return false;
    const auto& left = pair[pair[0][0] < pair[1][0] ? 0 : 1];
    const auto& right = pair[pair[0][0] < pair[1][0] ? 1 : 0];
    if (left[0] != 0 || left[1] != 0 || right[1] != 0 || left[2] != right[0] ||
        left[3] != right[3] || int64_t{left[2]} * 2 != right[2]) return false;
    // Live VDXR receipt: 4618x2487 valid depth area in a 4620x2488
    // allocation. Admit exact coverage or four-pixel allocation padding only;
    // preserve the measured viewport, never expand it into padding.
    const auto exact_or_padded=[](int32_t used,int32_t allocated) {
        return used==allocated || (allocated%4==0 && (int64_t{used}+3)/4*4==allocated);
    };
    return exact_or_padded(right[2],size[0]) && exact_or_padded(right[3],size[1]);
}

inline std::optional<Pair> matching_pair(const Pair& a, const Pair& b,
        const Extent& size, const Rect& input) {
    std::optional<Pair> result;
    for (const auto* pair : {&a, &b}) {
        if (!tiles_attachment(*pair, size)) continue;
        // Keep the existing eye's layout identical, even when view 0's labels
        // enter/leave its frustum. Combined/unknown inputs are observation-only.
        if (input != (*pair)[1]) continue;
        if (result && *result != *pair) return std::nullopt;
        result = *pair;
    }
    return result;
}

inline std::optional<Plan> plan(const Layout& x) {
    if (x.views != 2 || x.argument_index != 1 || x.argument_view_index != 1 || x.first_pass != 2 || x.second_pass != 3 ||
        x.native_fix || x.independent_eyes || x.first_early_count <= 0 ||
        x.first_dispatch_count <= 0 || x.game_extent != x.color_extent) return std::nullopt;
    const auto color = matching_pair(x.rects_2f8, x.rects_1e30, x.color_extent, x.color_input);
    const auto depth = matching_pair(x.rects_2f8, x.rects_1e30, x.depth_extent, x.depth_input);
    if (!color || !depth) return std::nullopt;
    return Plan{*color, *depth};
}
} // namespace wuwa_world_labels
