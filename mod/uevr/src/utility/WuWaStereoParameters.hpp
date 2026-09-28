#pragma once
#include "WuWaPlanarSnapshot.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace wuwa_stereo_parameters {
using namespace wuwa_planar_probe;
inline bool valid_rect(const Rect& r) {
    return r[0] >= 0 && r[1] >= 0 && r[2] > r[0] && r[3] > r[1] &&
        r[2] <= 32768 && r[3] <= 32768;
}
// Equal, adjacent SBS views, with physical ordering independent of eye index.
inline bool tiled(const Rect& a, const Rect& b, uint32_t width, uint32_t height) {
    if (!valid_rect(a) || !valid_rect(b) || !width || !height || width > 32768 || height > 32768) return false;
    const auto& left = a[0] < b[0] ? a : b;
    const auto& right = a[0] < b[0] ? b : a;
    return left[0] == 0 && left[1] == 0 && right[1] == 0 &&
        left[2] == right[0] && right[2] == int32_t(width) && left[3] == int32_t(height) && right[3] == int32_t(height) &&
        left[2] == right[2] - right[0];
}
inline bool serial_pair(const Snapshot& s) {
    if (!s.family.valid || !s.family.value || !s.view_count.valid || s.view_count.value != 2 ||
        !s.views[0].valid || !s.views[1].valid || !s.views[0].value || !s.views[1].value ||
        s.views[0].value == s.views[1].value) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (!s.view_families[i].valid || s.view_families[i].value != s.family.value ||
            !s.passes[i].valid || s.passes[i].value != int32_t(2 + i) ||
            !s.view_rects[i].valid || !s.view_modes[i].valid ||
            !s.instanced[i].valid || !s.multiview[i].valid || s.instanced[i].value || s.multiview[i].value) return false;
        // These three flags exclude the capture views rejected by the game's
        // Kuro planar-texture binding at 0x23617e8d..0x23617ea9.
        const auto& mode = s.view_modes[i].value;
        if (mode[0] || mode[2] || mode[3]) return false;
    }
    const auto& a = s.view_rects[0].value;
    const auto& b = s.view_rects[1].value;
    return tiled(a, b, (std::max)(a[2], b[2]), (std::max)(a[3], b[3]));
}
template<class Read> inline Snapshot renderer_pair(uintptr_t renderer, Read read) {
    Snapshot s{}; Field<uintptr_t> array{};
    read_field(read,renderer,0x108,array); read_field(read,renderer,0x110,s.view_count);
    if (!array.valid || !array.value || !s.view_count.valid || s.view_count.value != 2 ||
        array.value > UINTPTR_MAX-0xe8d0) return s;
    for (size_t i=0;i<2;++i) {
        const auto view=array.value+i*0xe8d0;
        s.views[i]={view,true};
        read_field(read,view,0,s.view_families[i]);
        read_field(read,view,0xc90,s.passes[i]);
        read_field(read,view,0x2f8,s.view_rects[i]);
        read_field(read,view,0xfea,s.view_modes[i]);
        read_field(read,view,0xffe,s.instanced[i]);
        read_field(read,view,0x1000,s.multiview[i]);
    }
    s.family=s.view_families[0];
    return s;
}
inline Vector scale_bias(const Rect& r, uint32_t width, uint32_t height) {
    return {float(r[2]-r[0]) / (2.0f*width), -float(r[3]-r[1]) / (2.0f*height),
        float(r[0]+r[2]) / (2.0f*width), float(r[1]+r[3]) / (2.0f*height)};
}
inline bool close(const Vector& a, const Vector& b) {
    for (size_t i=0; i<4; ++i) if (!std::isfinite(a[i]) || std::abs(a[i]-b[i]) > 0.00001f) return false;
    return true;
}
inline bool projection(const Matrix& m) {
    for (auto x : m) if (!std::isfinite(x)) return false;
    return std::abs(m[0]) > 0.00001f && std::abs(m[5]) > 0.00001f;
}
struct Block {
    std::array<Matrix,2> matrices{};
    std::array<Vector,2> scale_biases{};
    std::array<float,2> bounds{};
    int32_t stereo{};
};
static_assert(sizeof(Block) == 0xac);
static_assert(offsetof(Block, stereo) == 0xa8);
struct Plan { Block before{}, after{}; uint32_t eye{}; };
inline std::optional<Plan> plan(const Snapshot& s) {
    if (!serial_pair(s) || !s.proxy_target.valid || !s.proxy_target.value || s.proxy_target.value != s.candidate_target ||
        !s.target_extent.valid || !s.proxy_stereo.valid || s.proxy_stereo.value > 1 ||
        !s.output_stereo.valid || s.output_stereo.value != s.proxy_stereo.value ||
        !s.proxy_rects[0].valid || !s.proxy_rects[1].valid || !s.bounds.valid) return {};
    const auto eye = s.view == s.views[0].value ? 0u : s.view == s.views[1].value ? 1u : 2u;
    if (eye > 1 || !s.stereo_pass.valid || s.stereo_pass.value != int32_t(2+eye)) return {};
    const auto [width,height] = s.target_extent.value;
    if (!tiled(s.proxy_rects[0].value,s.proxy_rects[1].value,width,height)) return {};
    // A reflected atlas with a different view order cannot be inferred from eye
    // numbers. Refuse it instead of silently swapping a correct reflection.
    if ((s.proxy_rects[0].value[0] < s.proxy_rects[1].value[0]) !=
        (s.view_rects[0].value[0] < s.view_rects[1].value[0])) return {};
    for (size_t i=0; i<2; ++i) {
        if (!s.proxy_matrices[i].valid || !projection(s.proxy_matrices[i].value) ||
            !s.output_matrices[i].valid || !s.scale_bias[i].valid ||
            !std::isfinite(s.bounds.value[i]) || s.bounds.value[i] <= 0 || s.bounds.value[i] > 1.001f) return {};
    }
    const auto uv0 = scale_bias(s.proxy_rects[0].value,width,height);
    const auto uv1 = scale_bias(s.proxy_rects[1].value,width,height);
    if (!close(s.scale_bias[0].value,uv0)) return {};
    if (s.proxy_stereo.value) {
        if (s.output_matrices[0].value != s.proxy_matrices[0].value ||
            s.output_matrices[1].value != s.proxy_matrices[1].value || !close(s.scale_bias[1].value,uv1)) return {};
    } else if (s.output_matrices[0].value != s.proxy_matrices[eye].value ||
               !close(s.scale_bias[1].value,Vector{})) return {};
    Plan p{}; p.eye=eye;
    for (size_t i=0; i<2; ++i) {
        p.before.matrices[i]=s.output_matrices[i].value;
        p.before.scale_biases[i]=s.scale_bias[i].value;
    }
    p.before.bounds=s.bounds.value; p.before.stereo=s.output_stereo.value;
    p.after=p.before;
    // Emit the game's existing non-instanced parameter shape, selecting BOTH
    // the matrix and atlas coordinates for this view. No proxy/view/texture is
    // modified. Each later eye call computes its own independent parameters.
    p.after.matrices[0]=s.proxy_matrices[eye].value;
    p.after.matrices[1]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    p.after.scale_biases[0]=eye ? uv1 : uv0;
    p.after.scale_biases[1]={}; p.after.stereo=0;
    return p;
}
} // namespace wuwa_stereo_parameters
