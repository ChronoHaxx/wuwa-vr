#pragma once
#include "WuWaStereoParameters.hpp"

// Read-only interpretation of the verified September view/uniform layout.
// A spatial half is NOT an OpenXR eye identity: eye swapping is independent.
namespace wuwa_reflection_layout {
using namespace wuwa_planar_probe;
// Bounded observer slots keyed by view state, never by screen position. NSF can
// render both eyes at x=0 on separate targets. Callers provide synchronization.
template<size_t Count> struct StateSamples {
    static_assert(Count>0);
    struct Ticket { size_t slot{}; uintptr_t state{}; uint64_t generation{}; };
    struct Entry { uintptr_t state{}; uint64_t generation{}, next{}, touched{}; };
    std::array<Entry,Count> entries{};
    uint64_t generation{}, evictions{};
    std::optional<Ticket> take(uintptr_t state,uint64_t now) {
        if (!state) return {};
        size_t slot=Count;
        for(size_t i=0;i<Count;++i) if(entries[i].state==state) {slot=i;break;}
        if(slot==Count) {
            for(size_t i=0;i<Count;++i) if(!entries[i].state) {slot=i;break;}
            if(slot==Count) {
                slot=0;
                for(size_t i=1;i<Count;++i) if(entries[i].touched<entries[slot].touched) slot=i;
                ++evictions;
            }
            entries[slot]={state,++generation,0,now};
        }
        auto& entry=entries[slot]; entry.touched=now;
        if(now<entry.next) return {};
        entry.next=now<=UINT64_MAX-1000 ? now+1000:UINT64_MAX;
        return Ticket{slot,state,entry.generation};
    }
    bool owns(const Ticket& ticket) const {
        return ticket.slot<Count && entries[ticket.slot].state==ticket.state &&
            entries[ticket.slot].generation==ticket.generation;
    }
};
struct View { size_t family_index{}, spatial_half{}; bool single{}; Rect rect{}; };
inline std::optional<View> observation_view(const Snapshot& s) {
    if (!s.family.valid || !s.family.value || !s.view_count.valid ||
        (s.view_count.value != 1 && s.view_count.value != 2) || !s.stereo_pass.valid) return {};
    if (s.view_count.value == 2 && !wuwa_stereo_parameters::serial_pair(s)) return {};
    for (size_t i=0; i<size_t(s.view_count.value); ++i) {
        if (!s.views[i].valid || s.views[i].value != s.view || !s.view ||
            !s.view_families[i].valid || s.view_families[i].value != s.family.value ||
            !s.passes[i].valid || s.passes[i].value != s.stereo_pass.value ||
            (s.passes[i].value != 2 && s.passes[i].value != 3) ||
            !s.view_modes[i].valid || !s.instanced[i].valid || !s.multiview[i].valid ||
            s.instanced[i].value || s.multiview[i].value || !s.view_rects[i].valid) continue;
        const auto& flags=s.view_modes[i].value;
        const auto& r=s.view_rects[i].value;
        if (flags[0] || flags[2] || flags[3] || !wuwa_stereo_parameters::valid_rect(r) || r[1] != 0) continue;
        const auto width=r[2]-r[0];
        if (r[0] != 0 && r[0] != width) continue;
        return View{i, size_t(r[0] != 0), s.view_count.value == 1, r};
    }
    return {};
}

struct Uniforms {
    Field<Vector> view_min{}, view_size_inv{}, buffer_size_inv{};
    bool consistent{};
};
template<class Read> inline Uniforms uniforms(uintptr_t buffer, const Rect& rect, Read read) {
    Uniforms out{};
    // Named native View accessors and both inspected shader models agree.
    read_field(read,buffer,0x860,out.view_min);
    read_field(read,buffer,0x870,out.view_size_inv);
    read_field(read,buffer,0x8a0,out.buffer_size_inv);
    if (!out.view_min.valid || !out.view_size_inv.valid || !out.buffer_size_inv.valid ||
        !wuwa_stereo_parameters::valid_rect(rect)) return out;
    for (const auto& f:{out.view_min,out.view_size_inv,out.buffer_size_inv})
        for (float v:f.value) if (!std::isfinite(v)) return out;
    for (size_t axis=0;axis<2;++axis) {
        const auto size=out.view_size_inv.value[axis], extent=out.buffer_size_inv.value[axis];
        if (out.view_min.value[axis] != float(rect[axis]) || size != float(rect[axis+2]-rect[axis]) ||
            extent < float(rect[axis+2]) || extent > 32768 ||
            std::abs(size*out.view_size_inv.value[axis+2]-1.f) > 0.0001f ||
            std::abs(extent*out.buffer_size_inv.value[axis+2]-1.f) > 0.0001f) return out;
    }
    out.consistent=true;
    return out;
}

// Describe an observed atlas, including bounded allocation padding. This does
// not infer eye ordering, select a texture or write a shader parameter.
inline bool padded_sbs(const std::array<Rect,2>& rects, const std::array<uint32_t,2>& extent) {
    if (!extent[0] || !extent[1] || extent[0]>32768 || extent[1]>32768 ||
        !wuwa_stereo_parameters::valid_rect(rects[0]) || !wuwa_stereo_parameters::valid_rect(rects[1])) return false;
    const auto& a=rects[rects[0][0] < rects[1][0] ? 0:1];
    const auto& b=rects[rects[0][0] < rects[1][0] ? 1:0];
    return a[0]==0 && a[1]==0 && b[1]==0 && a[2]==b[0] && a[2]==b[2]-b[0] && a[3]==b[3] &&
        b[2]<=int32_t(extent[0]) && a[3]<=int32_t(extent[1]) &&
        int32_t(extent[0])-b[2]<=3 && int32_t(extent[1])-a[3]<=3;
}
} // namespace wuwa_reflection_layout
