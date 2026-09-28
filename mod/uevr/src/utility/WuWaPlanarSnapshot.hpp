#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wuwa_planar_probe {
// Layout of the exact September client, independently gated by the hook.
// No engine method calls, object ownership, or writes to these addresses.
template<class T> struct Field { T value{}; bool valid{}; };
using Rect = std::array<int32_t, 4>;
using Matrix = std::array<float, 16>;
using Vector = std::array<float, 4>;
struct Snapshot {
    uintptr_t view{}, proxy{}, parameters{}, candidate_target{};
    Field<uintptr_t> family{}, view_array{}, proxy_target{};
    Field<int32_t> view_count{}, stereo_pass{}, output_stereo{};
    Field<uint8_t> proxy_stereo{};
    std::array<Field<uintptr_t>, 2> views{};
    std::array<Field<int32_t>, 2> passes{};
    std::array<Field<uintptr_t>, 2> view_families{};
    std::array<Field<Rect>, 2> view_rects{};
    // Capture/instancing state is needed before selecting serial-eye parameters.
    std::array<Field<std::array<uint8_t, 7>>, 2> view_modes{}; // +0xfea..0xff0
    std::array<Field<uint8_t>, 2> instanced{}, multiview{};
    Field<Rect> view_rect{}, unscaled_rect{};
    std::array<Field<Rect>, 2> proxy_rects{};
    std::array<Field<Matrix>, 2> proxy_matrices{}, output_matrices{};
    std::array<Field<Vector>, 2> scale_bias{};
    Field<std::array<float, 2>> bounds{};
    Field<std::array<uint32_t, 2>> target_extent{};
};

template<class Read, class T>
inline void read_field(Read& read, uintptr_t object, uintptr_t offset, Field<T>& field) {
    field = {};
    if (object && object <= (std::numeric_limits<uintptr_t>::max)() - offset)
        field.valid = read(object + offset, field.value);
    if (!field.valid) field.value = {};
}

template<class Read>
inline Snapshot snapshot(uintptr_t view, uintptr_t proxy, uintptr_t output,
                         uintptr_t extent_address, uintptr_t candidate_target, Read read) {
    Snapshot s{};
    s.view = view; s.proxy = proxy; s.parameters = output; s.candidate_target = candidate_target;
    read_field(read, view, 0, s.family);
    read_field(read, view, 0xc90, s.stereo_pass);
    read_field(read, view, 0x2f8, s.view_rect);
    read_field(read, view, 0x1e30, s.unscaled_rect);
    if (s.family.valid) {
        read_field(read, s.family.value, 0, s.view_array);
        read_field(read, s.family.value, 8, s.view_count);
        if (s.view_count.valid && s.view_count.value >= 1 && s.view_count.value <= 2 && s.view_array.valid) {
            for (int32_t i = 0; i < s.view_count.value; ++i) {
                read_field(read, s.view_array.value, i * sizeof(uintptr_t), s.views[i]);
                if (s.views[i].valid) {
                    read_field(read, s.views[i].value, 0, s.view_families[i]);
                    read_field(read, s.views[i].value, 0xc90, s.passes[i]);
                    read_field(read, s.views[i].value, 0x2f8, s.view_rects[i]);
                    read_field(read, s.views[i].value, 0xfea, s.view_modes[i]);
                    read_field(read, s.views[i].value, 0xffe, s.instanced[i]);
                    read_field(read, s.views[i].value, 0x1000, s.multiview[i]);
                }
            }
        }
    }
    // The null/default branch initializes only output_stereo. Reading other
    // output fields there would report uninitialized caller-stack bytes.
    read_field(read, output, 0x138, s.output_stereo);
    read_field(read, proxy, 0x160, s.proxy_target);
    if (!s.proxy_target.valid || !s.proxy_target.value) return s;
    read_field(read, proxy, 0x1c, s.proxy_stereo);
    read_field(read, extent_address, 0, s.target_extent);
    read_field(read, output, 0x130, s.bounds);
    for (size_t i = 0; i < 2; ++i) {
        read_field(read, proxy, 0x100 + i * 0x10, s.proxy_rects[i]);
        read_field(read, proxy, 0x80 + i * 0x40, s.proxy_matrices[i]);
        read_field(read, output, 0x90 + i * 0x40, s.output_matrices[i]);
        read_field(read, output, 0x110 + i * 0x10, s.scale_bias[i]);
    }
    return s;
}
} // namespace wuwa_planar_probe
