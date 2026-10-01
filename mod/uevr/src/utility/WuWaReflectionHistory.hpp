#pragma once
#include "WuWaReflectionProjection.hpp"

namespace wuwa_reflection_history {
// Submission order, supplied explicitly by the NSF caller. Completed WuWa
// views both say PRIMARY after Same Pass, and both have rectangle x=0.
// Neither field can recover eye identity here.
struct Eye {
    uintptr_t main_state{}, component{};
    uint32_t ordinal{};
    bool active{};
};
inline thread_local Eye current{};

template<class Read>
bool accepts_factory(const Eye& eye, uintptr_t component, uintptr_t frame,
                     uintptr_t stack, uintptr_t expected_caller, uint64_t index, Read read) {
    if (!eye.active || !eye.main_state || !eye.component || eye.ordinal > 1 ||
        component != eye.component || index != 0 || !expected_caller ||
        stack > UINTPTR_MAX-0x660 || frame != stack+0x100) return false;
    uintptr_t caller{};
    int32_t count{};
    // The factory's exact prologue and call-site contract (3.7: RVA 0x3f89ac0,
    // return address at frame+0x558, view count at rsp+0x78; was +0x548 / +0x50). This is the custom
    // reflection producer, not an arbitrary SceneCapture using the factory.
    return read(frame+0x558,caller) && caller==expected_caller &&
        read(stack+0x78,count) && count==1;
}

// Decide only whether to take the existing game's reflected-view refresh.
// The hook changes no matrix, view count, proxy, allocation or history pointer.
template<class Read>
bool needs_single_view_refresh(uintptr_t main_renderer, uintptr_t capture_renderer,
                               uintptr_t proxy, uintptr_t plane, Read read) {
    auto field=[&](uintptr_t base,uintptr_t offset,auto& value) {
        return base && offset<=UINTPTR_MAX-base && sizeof(value)<=UINTPTR_MAX-(base+offset) &&
            read(base+offset,value);
    };
    using namespace wuwa_reflection_projection;
    uintptr_t main{},capture{},state{};
    int32_t main_count{},capture_count{},pass{};
    uint8_t custom{},instanced{},multiview{};
    std::array<uint8_t,7> main_modes{},capture_modes{};
    std::array<float,4> reflection_plane{};
    Matrix main_projection{},capture_projection{},view_matrix{};
    if (main_renderer==capture_renderer ||
        !field(main_renderer,0x110,main_count) || main_count!=1 ||
        !field(capture_renderer,0x110,capture_count) || capture_count!=1 ||
        !field(main_renderer,0x108,main) || !field(capture_renderer,0x108,capture) || main==capture ||
        !field(main,8,state) || !state || !field(main,0xc90,pass) || (pass!=2 && pass!=3) ||
        !field(main,0xfea,main_modes) || main_modes[0] || main_modes[2] || main_modes[3] ||
        !field(capture,0xfea,capture_modes) || capture_modes[0]!=1 || capture_modes[3]!=1 ||
        !field(main,0xffe,instanced) || instanced || !field(main,0x1000,multiview) || multiview ||
        !field(proxy,0x168,custom) || custom!=1 || !field(plane,0,reflection_plane) ||
        !field(main,0x320,main_projection) || !field(capture,0x320,capture_projection) ||
        !perspective(main_projection) || !perspective(capture_projection) ||
        !field(main,0x3e0,view_matrix)) return false;
    for (const auto value:reflection_plane) if (!std::isfinite(value)) return false;
    const auto norm=reflection_plane[0]*reflection_plane[0]+reflection_plane[1]*reflection_plane[1]+
        reflection_plane[2]*reflection_plane[2];
    if (std::abs(norm-1.f)>.001f) return false;
    for (const auto value:view_matrix) if (!std::isfinite(value)) return false;
    // Capture projection must already belong to this eye. Allow small temporal
    // jitter in the optical centre; do not accept another eye's projection.
    for (const auto i:{0,5}) if (std::abs(main_projection[i]-capture_projection[i])>.0001f) return false;
    for (const auto i:{8,9}) if (std::abs(main_projection[i]-capture_projection[i])>.01f) return false;
    return true;
}
} // namespace wuwa_reflection_history
