#pragma once
#include "WuWaCodeCompatibility.hpp"
#include "WuWaViewUbTrace.hpp"
#include <algorithm>
#include <array>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <limits>

// Passive CPU mesh-command construction evidence, not GPU draw/readback proof.
// At 23670b30 the material bindings already exist; the VF callback has not run.
namespace wuwa_mesh_binding {
using wuwa_lod::Field;
// Do not hook the three-byte call at b35: a five-byte jump there overwrites
// b38, which is an incoming branch target for both native null checks.
// This single five-byte store ends before the call and that shared join.
constexpr uint32_t site_rva=0x400edc0, hook_revision=2;
constexpr std::array<uint8_t,5> site_bytes{0x4c,0x89,0x6c,0x24,0x20};
inline bool valid_hook_span(std::span<const uint8_t> bytes) noexcept {
    return bytes.size()==site_bytes.size() && std::equal(bytes.begin(),bytes.end(),site_bytes.begin());
}
constexpr size_t max_resources=32, capacity=2048;
constexpr std::array<wuwa_code_compatibility::Range,4> code_ranges{{
    {0x400ed00,1308,0x4de252864e0381c6ULL},
    {0x115dd30,564,0x753322f75cf58070ULL},
    {0x3c3d0c0,741,0xc5f5b5b0f1d79611ULL},
    {0x486a520,610,0x5f5e712754fe83f3ULL}}};
struct Registers {uintptr_t rsp{},rcx{},rdx{},r8{},r9{},r10{},r13{},r14{},rdi{},rsi{};};
struct Resource {
    Field<uint16_t> slot{},parameter_size{};
    Field<uint64_t> name_hash{},raw_entry{},registry_generation{};
    wuwa_view_ub_trace::Buffer buffer{};
    bool collection_slot{},matches_view_own{},entry_consistent{},complete{};
};
struct Record {
    uint64_t tick_ms{},sequence{},loss_before{};
    uint32_t thread{};
    int eye{-1};
    wuwa_view_ub_trace::Identity view{},view_after{};
    uintptr_t shader{},vf_type{},vf_parameters{},vertex_factory{},element{},scene{},dispatch{};
    Field<uintptr_t> primitive{},mesh{},shader_element_data{},bindings{},binding_layout{},binding_data{},vf_name{};
    Field<uint32_t> code_index{},target_bits{},first_instance{},element_flags{},layout_count{},name_count{},collection_count{};
    Field<uint8_t> binding_mode{};
    Field<uint32_t> tracking_enabled{};
    std::array<Resource,max_resources> resources{};
    uint32_t resource_count{};
    bool bounds_valid{},metadata_complete{},anchors_consistent{},complete{};
};
template<class Read,class T> void get(Read& read,uintptr_t base,uintptr_t offset,Field<T>& out) {
    wuwa_lod::field(read,base,offset,out);
}
// Verified memory-image relative-array encoding. Negative displacement uses
// arithmetic shift. Check overflow rather than wrapping into a readable page.
template<class Read> Field<uintptr_t> array_pointer(uintptr_t at,Read& read) {
    Field<uintptr_t> encoded{};get(read,at,0,encoded);
    if(!encoded.valid || !(encoded.value&1)) return encoded;
    const auto displacement=static_cast<intptr_t>(encoded.value)>>1;
    if(displacement>=0) {
        if(uintptr_t(displacement)>UINTPTR_MAX-at) return {};
        return {at+uintptr_t(displacement),true};
    }
    const auto magnitude=uintptr_t(-(displacement+1))+1;
    if(magnitude>at) return {};
    return {at-magnitude,true};
}
template<class Read> Record snapshot(const Registers& c,uint32_t frame_offset,
                                    uintptr_t image_base,Read read) {
    Record r{};
    r.shader=c.r13;r.vf_type=c.rdi;r.vf_parameters=c.rdx;r.vertex_factory=c.rsi;
    r.element=c.r14;r.scene=c.r8;r.dispatch=c.r10;
    if(!r.shader || r.shader>UINTPTR_MAX-0xe0 || !c.rsp || c.rsp>UINTPTR_MAX-0xf0)return r;
    r.view=wuwa_view_ub_trace::identity(c.r9,frame_offset,read);
    get(read,c.rsp,0xc8,r.primitive);get(read,c.rsp,0xd0,r.mesh);
    get(read,c.rsp,0xe0,r.shader_element_data);get(read,c.rsp,0xe8,r.bindings);
    get(read,r.shader,0xc4,r.code_index);get(read,r.shader,0xc0,r.target_bits);
    get(read,r.element,0x58,r.first_instance);get(read,r.element,0x64,r.element_flags);
    get(read,r.vf_type,8,r.vf_name); // Native type constructor stores its name here.
    get(read,r.bindings.value,0,r.binding_layout);get(read,r.bindings.value,8,r.binding_mode);
    get(read,r.bindings.value,0x10,r.binding_data);
    get(read,r.binding_layout.value,8,r.layout_count);
    get(read,r.shader,0x98,r.name_count);get(read,r.shader,0xd8,r.collection_count);
    get(read,image_base,0x9a8c5c8,r.tracking_enabled);
    Field<uintptr_t> registry{};get(read,image_base,0x9610488,registry);
    const auto layout=array_pointer(r.binding_layout.value,read);
    const auto names=array_pointer(r.shader+0x90,read);
    const auto name_slots=array_pointer(r.shader+0xa0,read);
    const auto collections=array_pointer(r.shader+0xd0,read);
    r.bounds_valid=r.bindings.valid && r.bindings.value && r.binding_layout.valid && r.binding_layout.value &&
        r.binding_data.valid && r.binding_data.value && layout.valid && layout.value &&
        r.layout_count.valid && r.layout_count.value<=max_resources &&
        r.name_count.valid && r.name_count.value<=max_resources &&
        r.collection_count.valid && r.collection_count.value<=max_resources &&
        (!r.name_count.value || (names.valid && names.value && name_slots.valid && name_slots.value)) &&
        (!r.collection_count.value || (collections.valid && collections.value)) &&
        r.binding_mode.valid && r.tracking_enabled.valid;
    if(!r.bounds_valid) return r;
    r.metadata_complete=true;
    const bool tracked=r.binding_mode.value && r.tracking_enabled.value;
    for(uint32_t i=0;i<r.layout_count.value;++i) {
        auto& b=r.resources[i];++r.resource_count;
        get(read,layout.value,4*i,b.slot);get(read,layout.value,4*i+2,b.parameter_size);
        get(read,r.binding_data.value,8*i,b.raw_entry);
        for(uint32_t n=0;n<r.name_count.value && b.slot.valid;++n) {
            Field<uint16_t> slot{};get(read,name_slots.value,2*n,slot);
            if(!slot.valid)r.metadata_complete=false;
            if(slot.valid && slot.value==b.slot.value) {
                get(read,names.value,8*n,b.name_hash);
                r.metadata_complete &= b.name_hash.valid;break;
            }
        }
        for(uint32_t n=0;n<r.collection_count.value && b.slot.valid;++n) {
            Field<uint16_t> slot{};get(read,collections.value,2*n,slot);
            if(!slot.valid)r.metadata_complete=false;
            if(slot.valid && slot.value==b.slot.value) b.collection_slot=true;
        }
        Field<uintptr_t> object{};
        if(b.raw_entry.valid && !tracked) object={uintptr_t(b.raw_entry.value),true};
        else if(b.raw_entry.valid && b.raw_entry.value && registry.valid && registry.value) {
            const auto offset=16*(b.raw_entry.value&0xffff);
            get(read,registry.value,offset+8,b.registry_generation);
            if(b.registry_generation.valid && b.registry_generation.value==b.raw_entry.value) {
                get(read,registry.value,offset,object);
                Field<uint64_t> again{};get(read,registry.value,offset+8,again);
                if(!wuwa_view_ub_trace::same(again,b.registry_generation)) object={};
            }
        }
        b.buffer=wuwa_view_ub_trace::buffer(object,read);
        bool registry_consistent=!tracked;
        if(tracked && object.valid && registry.valid && registry.value) {
            // A registry entry can recycle while its object's layout is read.
            // Validate both the pair and registry base after those reads.
            const auto offset=16*(b.raw_entry.value&0xffff);
            Field<uintptr_t> end_registry{},end_object{};Field<uint64_t> end_generation{};
            get(read,image_base,0x9610488,end_registry);
            get(read,registry.value,offset,end_object);
            get(read,registry.value,offset+8,end_generation);
            registry_consistent=wuwa_view_ub_trace::same(registry,end_registry) &&
                wuwa_view_ub_trace::same(object,end_object) &&
                wuwa_view_ub_trace::same(b.registry_generation,end_generation);
        }
        Field<uint64_t> again{};get(read,r.binding_data.value,8*i,again);
        b.entry_consistent=registry_consistent && wuwa_view_ub_trace::same(again,b.raw_entry);
        b.matches_view_own=object.valid && object.value && r.view.own_ub.valid && object.value==r.view.own_ub.value;
        // All valid UB lengths are evidence; 4608 is not silently rejected or
        // reinterpreted as the 5872-byte View structure. No payload is chased.
        b.complete=b.slot.valid && b.parameter_size.valid && b.entry_consistent && object.valid && object.value &&
            b.buffer.layout.valid && b.buffer.layout.value && b.buffer.size.valid && b.buffer.size.value>0 &&
            b.buffer.size.value<=1024*1024 && b.buffer.conversion_count.valid;
    }
    r.view_after=wuwa_view_ub_trace::identity(c.r9,frame_offset,read);
    Field<uintptr_t> end_layout{},end_data{};Field<uint8_t> end_mode{};Field<uint32_t> end_tracking{};
    get(read,r.bindings.value,0,end_layout);get(read,r.bindings.value,0x10,end_data);
    get(read,r.bindings.value,8,end_mode);get(read,image_base,0x9a8c5c8,end_tracking);
    r.anchors_consistent=wuwa_view_ub_trace::consistent(r.view,r.view_after) &&
        wuwa_view_ub_trace::same(r.binding_layout,end_layout) && wuwa_view_ub_trace::same(r.binding_data,end_data) &&
        wuwa_view_ub_trace::same(r.binding_mode,end_mode) && wuwa_view_ub_trace::same(r.tracking_enabled,end_tracking);
    r.complete=r.metadata_complete && r.anchors_consistent && r.code_index.valid && r.target_bits.valid &&
        (r.target_bits.value&15)==(c.rcx&15) && r.first_instance.valid && r.element_flags.valid &&
        r.primitive.valid && r.mesh.valid && r.shader_element_data.valid;
    for(uint32_t i=0;i<r.resource_count;++i) r.complete &= r.resources[i].complete;
    return r;
}
template<size_t Capacity=capacity> class Ring {
    struct Slot {Record record{};std::atomic<bool> ready{};};
    // Slot\'s member initializers initialize every field. Avoid MSVC expanding
    // thousands of nested aggregate initializers at compile time.
    static_assert(!std::is_trivially_default_constructible_v<Slot>);
    std::array<Slot,Capacity> slots;std::atomic<size_t> next{};size_t drained{};
    std::atomic<bool> overflow{};
public:
    bool append(const Record& r) noexcept {
        auto i=next.load(std::memory_order_relaxed);
        do {if(i>=Capacity){overflow=true;return false;}}
        while(!next.compare_exchange_weak(i,i+1,std::memory_order_relaxed));
        slots[i].record=r;slots[i].ready.store(true,std::memory_order_release);return true;
    }
    bool pop(Record& r) noexcept {
        if(drained>=Capacity || !slots[drained].ready.load(std::memory_order_acquire))return false;
        r=slots[drained++].record;return true;
    }
    bool truncated()const noexcept{return overflow.load();}
    void reset()noexcept{for(auto& s:slots)s.ready=false;next=0;drained=0;overflow=false;}
};
}
