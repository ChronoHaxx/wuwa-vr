#pragma once
#include "WuWaPlanarSnapshot.hpp"
#include <cmath>
#include <optional>

namespace wuwa_reflection_projection {
using wuwa_planar_probe::Matrix;
using wuwa_planar_probe::Rect;
struct Plan { uintptr_t output{}, view{}, state{}; Matrix before{}, after{}; };

inline bool perspective(const Matrix& m) noexcept {
    for (const auto x : m) if (!std::isfinite(x)) return false;
    if (m[0] < .0001f || m[0] > 100.f || m[5] < .0001f || m[5] > 100.f ||
        std::abs(m[8]) > 2.f || std::abs(m[9]) > 2.f || m[14] <= 0.f ||
        std::abs(m[11]-1.f) > .00001f) return false;
    for (const auto i : {1,2,3,4,6,7,12,13,15}) if (std::abs(m[i]) > .00001f) return false;
    return true;
}

// Exact September capture-producer contract. It constructs a symmetric
// projection from atan(1 / mainView.M00), discarding the main eye's M11 and
// optical-centre terms. For the custom screen-UV reflection, keep that eye's
// XY projection. The mirrored camera, clipping/depth, proxy and texture stay
// owned by the game. Other planar effects and multi-view atlases are excluded.
template<class Read>
std::optional<Plan> plan(uintptr_t renderer, uintptr_t view, uintptr_t component,
                         uintptr_t output, Read read) {
    auto field = [&](uintptr_t base, uintptr_t offset, auto& value) {
        return base && offset <= UINTPTR_MAX-base && sizeof(value) <= UINTPTR_MAX-(base+offset) &&
            read(base+offset, value);
    };
    uintptr_t first{}, family{}, state{}, proxy{};
    int32_t count{}, pass{};
    uint8_t custom{}, instanced{}, multiview{};
    std::array<uint8_t,7> modes{};
    Rect rect{};
    float extra_fov{};
    Matrix source{}, capture{};
    if (!field(renderer,0x108,first) || first != view || !view ||
        !field(renderer,0x110,count) || count != 1 ||
        !field(view,0,family) || !family || !field(view,8,state) || !state ||
        !field(view,0xc90,pass) || (pass != 2 && pass != 3) ||
        !field(view,0xfea,modes) || modes[0] || modes[2] || modes[3] ||
        !field(view,0xffe,instanced) || instanced || !field(view,0x1000,multiview) || multiview ||
        !field(view,0x2f8,rect) || rect[0] != 0 || rect[1] != 0 ||
        rect[2] <= 0 || rect[3] <= 0 || rect[2] > 32768 || rect[3] > 32768 ||
        !field(component,0x368,proxy) || !proxy || !field(proxy,0x168,custom) || custom != 1 ||
        !field(component,0x330,extra_fov) ||
        !std::isfinite(extra_fov) || std::abs(extra_fov) > .001f ||
        !field(view,0x320,source) || !field(output,0,capture) ||
        !perspective(source) || !perspective(capture) ||
        std::abs(capture[8]) > .00001f || std::abs(capture[9]) > .00001f) return {};
    // proxy+0x160 is the render thread's published output. The producer clears
    // it before this hook and rendering republishes it later; it cannot gate
    // construction of the new capture or selection of its per-eye history.
    Plan p{output,view,state,capture,capture};
    for (const auto i : {0,5,8,9}) p.after[i] = source[i];
    return p;
}
} // namespace wuwa_reflection_projection
