#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wuwa_lgui_stage0 {
// CPU capture copied by the inspected stage-0 constructor, not a guessed
// FSceneView/RDG object. The final word is copied but not interpreted.
struct Payload {
    uint64_t renderer{},view{};
    float scale{};
    std::array<int32_t,4> viewport{};
    std::array<int32_t,2> color_extent{},depth_extent{};
    std::array<int32_t,4> color_rect{},depth_rect{};
    uint32_t uninterpreted{};
};
static_assert(sizeof(Payload)==0x58 && offsetof(Payload,viewport)==0x14 &&
    offsetof(Payload,color_extent)==0x24 && offsetof(Payload,depth_extent)==0x2c &&
    offsetof(Payload,color_rect)==0x34 && offsetof(Payload,depth_rect)==0x44);
inline bool rectangle_valid(const std::array<int32_t,4>& r,const std::array<int32_t,2>& size) {
    return size[0]>0 && size[0]<=32768 && size[1]>0 && size[1]<=32768 &&
        r[0]>=0 && r[1]>=0 && r[2]>r[0] && r[3]>r[1] && r[2]<=size[0] && r[3]<=size[1];
}
}
