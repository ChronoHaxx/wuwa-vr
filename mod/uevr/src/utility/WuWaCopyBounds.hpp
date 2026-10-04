#pragma once

#include <cstdint>

namespace wuwa_copy_bounds {
struct Texture {
    uint32_t dimension{};
    uint64_t width{};
    uint32_t height{}, depth_or_array{}, mips{}, samples{}, quality{}, format{};
};
struct Box { uint32_t left{}, top{}, front{}, right{}, bottom{}, back{}; };
struct Offset { uint32_t x{}, y{}, z{}; };

// DXGI format-family IDs. Same-family bit copies include the existing
// UNORM <-> sRGB presentation path and typed/typeless depth resources.
constexpr uint32_t family(uint32_t f) noexcept {
    if (f >= 1 && f <= 4) return 1;
    if (f >= 5 && f <= 8) return 5;
    if (f >= 9 && f <= 14) return 9;
    if (f >= 15 && f <= 18) return 15;
    if (f >= 19 && f <= 22) return 19;
    if (f >= 23 && f <= 25) return 23;
    if (f >= 27 && f <= 32) return 27;
    if (f >= 33 && f <= 38) return 33;
    if (f >= 39 && f <= 43) return 39;
    if (f >= 44 && f <= 47) return 44;
    if (f >= 48 && f <= 52) return 48;
    if (f >= 53 && f <= 59) return 53;
    if (f >= 60 && f <= 64) return 60;
    if (f >= 70 && f <= 72) return 70;
    if (f >= 73 && f <= 75) return 73;
    if (f >= 76 && f <= 78) return 76;
    if (f >= 79 && f <= 81) return 79;
    if (f >= 82 && f <= 84) return 82;
    if (f == 87 || f == 90 || f == 91) return 87;
    if (f == 88 || f == 92 || f == 93) return 88;
    if (f >= 94 && f <= 96) return 94;
    if (f >= 97 && f <= 99) return 97;
    return f;
}
constexpr bool compatible(const Texture& src, const Texture& dst) noexcept {
    return src.dimension == dst.dimension && src.dimension >= 2 && src.dimension <= 4 &&
        src.samples && src.samples == dst.samples && src.quality == dst.quality &&
        src.format && family(src.format) == family(dst.format);
}
constexpr bool whole(const Texture& src, const Texture& dst) noexcept {
    return compatible(src, dst) && src.width && src.height && src.depth_or_array && src.mips &&
        src.width == dst.width && src.height == dst.height && src.depth_or_array == dst.depth_or_array &&
        src.mips == dst.mips;
}
constexpr bool fits(uint64_t offset, uint64_t size, uint64_t limit) noexcept {
    return size && size <= limit && offset <= limit - size;
}
constexpr bool region(const Texture& src, const Texture& dst, const Box& box, Offset offset = {}) noexcept {
    if (!compatible(src, dst) || !src.mips || !dst.mips || !src.depth_or_array || !dst.depth_or_array ||
        box.right <= box.left || box.bottom <= box.top || box.back <= box.front) return false;
    const uint64_t width = uint64_t(box.right) - box.left;
    const uint64_t height = uint64_t(box.bottom) - box.top;
    const uint64_t depth = uint64_t(box.back) - box.front;
    const auto src_depth = src.dimension == 4 ? src.depth_or_array : 1U;
    const auto dst_depth = dst.dimension == 4 ? dst.depth_or_array : 1U;
    if (!fits(box.left, width, src.width) || !fits(box.top, height, src.height) ||
        !fits(box.front, depth, src_depth) || !fits(offset.x, width, dst.width) ||
        !fits(offset.y, height, dst.height) || !fits(offset.z, depth, dst_depth)) return false;
    // Multisampled resources require a full subresource copy with no offset.
    if (src.samples > 1 && (box.left || box.top || box.front || offset.x || offset.y || offset.z ||
        width != src.width || height != src.height || depth != src_depth ||
        src.width != dst.width || src.height != dst.height || src_depth != dst_depth)) return false;
    return true;
}
} // namespace wuwa_copy_bounds
