#pragma once
#include "WuWaWorldLabelStereo.hpp"

namespace wuwa_water_stereo {
struct Layout {
    bool serial_pair{};
    int32_t feature_level{};
    std::array<int32_t, 2> draws{};
    std::array<uint32_t, 2> flags{};
    wuwa_world_labels::Pair draw_rects{};
    wuwa_world_labels::Extent target_extent{};
};
enum class Refusal { none, family, feature_level, eligibility, draw_count, target_layout };
inline Refusal check(const Layout& x) {
    if (!x.serial_pair) return Refusal::family;
    // Only the desktop branch whose deferred draw contract was inspected.
    if (x.feature_level < 2 || x.feature_level > 5) return Refusal::feature_level;
    if ((x.flags[0] | x.flags[1]) & 2) return Refusal::eligibility;
    if (x.draws[0] < 0 || x.draws[0] > 100000 || x.draws[1] <= 0 || x.draws[1] > 100000)
        return Refusal::draw_count;
    if (!wuwa_world_labels::tiles_attachment(x.draw_rects, x.target_extent)) return Refusal::target_layout;
    return Refusal::none;
}
// The second pass must preserve the first pass's pixels. If the first eye has
// no draws, the second is the first writer and keeps the original clear.
inline uint8_t load_action(bool first_wrote) { return first_wrote ? 1 : 2; }
} // namespace wuwa_water_stereo
