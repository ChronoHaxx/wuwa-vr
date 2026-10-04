#pragma once
#include "WuWaCodeCompatibility.hpp"
#include "WuWaLodSnapshot.hpp"
#include <array>
#include <type_traits>
#include <atomic>
#include <cstdint>
#include <cstring>

// Observation policy only. The owner supplies the existing LOD lease, outer
// callback lock, guarded reader, executable check, ring sink and hook lifetime.
// No hook installation, game-memory writes, allocation, logging or file I/O.
namespace wuwa_view_ub_trace {
using wuwa_lod::Field;
inline constexpr uint32_t payload_size = 0x1740;
// The shared holder's second resource is InstancedView, not another View.
// Native metadata initializer 20205660 registers its 0x1200-byte layout.
inline constexpr uint32_t instanced_payload_size = 0x1200;
inline constexpr std::array<wuwa_code_compatibility::Range, 2> code_ranges{{
    {0x3f970c0, 546, 0x55662f443a9bf45fULL},
    {0x3faf5e0, 920, 0x956cb697ba839be3ULL}}};

enum class Stage : uint8_t { cache_decision, shared_update_returned_before_cache_store, own_update_returned };
inline constexpr std::array<uint32_t, 3> site_rvas{0x3f97251, 0x3f972bf, 0x3faf856};
enum class Decision : uint8_t { unknown, would_skip, would_update };
enum class Result : uint8_t { recorded, filtered, invalid, busy, watch_full, sink_full, exception, stopped };
struct Registers { uintptr_t rbp{}, rbx{}, rdi{}, r14{}; };

struct Identity {
    uintptr_t view{};
    Field<uintptr_t> family{}, state{}, payload{}, own_ub{};
    Field<uint32_t> frame{};
    Field<int32_t> pass{};
};
struct Holder {
    uintptr_t address{};
    Field<uintptr_t> cached_view{};
    std::array<Field<uintptr_t>, 2> ub{};
};
struct Buffer {
    // Object identity only; this is not a D3D12 GPU virtual address or CBV.
    Field<uintptr_t> object{}, layout{};
    Field<uint32_t> size{}, conversion_count{};
};
struct Payload {
    uintptr_t address{};
    Field<uint64_t> hash{};
    Field<std::array<uint32_t, 2>> previous_time_bits{};
    Field<std::array<uint32_t, 3>> time_bits{};
    Field<uint32_t> frame{}, state_frame_index{};
    // Raw final original-payload matrix values, extracted from the same
    // guarded structure copy as the hash/clocks. No unit/handedness inference.
    Field<std::array<float,16>> translated_world_to_clip{};        // +0x000
    Field<std::array<float,16>> translated_world_to_camera_view{}; // +0x140
    Field<std::array<float,16>> translated_world_to_view{};        // +0x0c0
    // Verified native member metadata, retained from the same bounded copy.
    // Wind-distance gates use WorldCameraOrigin independently of clip matrices.
    Field<std::array<float,3>> world_camera_origin{}, pre_view_translation{}; // +0x430/+0x460
    Field<std::array<float,3>> previous_world_camera_origin{}, previous_pre_view_translation{}; // +0x730/+0x750
    Field<std::array<float,3>> origin_location{}; // +0x1700, world-origin shift
    Field<uint32_t> force_draw_all_velocities_bits{}, kuro_is_planar_reflection_view_bits{}; // +0xa00/+0x1340
    bool clock_fields_consistent{};
};
struct Record {
    uint64_t sequence{}, tick_ms{}, loss_before{};
    uint32_t thread{}, site_rva{};
    Stage stage{};
    Decision decision{}; // Meaningful only at cache_decision.
    int eye{-1}; // -1 retains auxiliary writes to a watched main-view holder.
    Identity view{}, view_after{};
    Holder holder{}, holder_after{};
    std::array<Buffer, 3> buffers{}; // own, holder[0], holder[1]
    Payload payload{};
    Field<uint32_t> caller_rva{};
    bool anchors_consistent{}, identity_valid{}, complete{};
};

// Separate from the LOD binding ring: retain consecutive accepted events until
// capacity, never overwrite or throttle individual events. One control-thread
// consumer; reset only under the owner's exclusive callback/control locks.
template<size_t Capacity=4096> class Ring {
    static_assert(Capacity>0);
    struct Slot {Record record{};std::atomic<bool> ready{};};
public:
    bool append(const Record& record) noexcept {
        auto index=next.load(std::memory_order_relaxed);
        do { if(index>=Capacity) {truncated_flag=true;return false;} }
        while(!next.compare_exchange_weak(index,index+1,std::memory_order_relaxed));
        slots[index].record=record;
        slots[index].ready.store(true,std::memory_order_release);return true;
    }
    bool pop(Record& record) noexcept {
        if(drained>=Capacity || !slots[drained].ready.load(std::memory_order_acquire)) return false;
        record=slots[drained++].record;return true;
    }
    void reset() noexcept {
        for(auto& slot:slots) slot.ready=false;
        next=0;drained=0;truncated_flag=false;
    }
    size_t written() const noexcept {return next.load(std::memory_order_relaxed);}
    bool truncated() const noexcept {return truncated_flag.load(std::memory_order_relaxed);}
private:
    // Slot\'s member initializers initialize every field. Avoid MSVC expanding
    // thousands of nested aggregate initializers at compile time.
    static_assert(!std::is_trivially_default_constructible_v<Slot>);
    std::array<Slot,Capacity> slots;
    std::atomic<size_t> next{};
    size_t drained{};
    std::atomic<bool> truncated_flag{};
};

template<class T> bool same(const Field<T>& a, const Field<T>& b) noexcept {
    return a.valid && b.valid && a.value == b.value;
}
template<class Read> Identity identity(uintptr_t view, uint32_t frame_offset, Read& read) {
    Identity i{}; i.view = view;
    wuwa_lod::field(read, view, 0, i.family);
    wuwa_lod::field(read, view, 8, i.state);
    wuwa_lod::field(read, view, 0x10, i.own_ub);
    wuwa_lod::field(read, view, 0x1e90, i.payload);
    wuwa_lod::field(read, view, 0xc90, i.pass);
    if (frame_offset == 0x64 && i.family.valid)
        wuwa_lod::field(read, i.family.value, frame_offset, i.frame);
    return i;
}
inline bool consistent(const Identity& a, const Identity& b) noexcept {
    return a.view == b.view && same(a.family,b.family) && same(a.state,b.state) &&
        same(a.own_ub,b.own_ub) && same(a.payload,b.payload) && same(a.frame,b.frame) && same(a.pass,b.pass);
}
template<class Read> Holder holder(uintptr_t address, Read& read) {
    Holder h{}; h.address = address;
    wuwa_lod::field(read,address,0,h.ub[0]);
    wuwa_lod::field(read,address,8,h.ub[1]);
    wuwa_lod::field(read,address,0xf0,h.cached_view);
    return h;
}
inline bool consistent(const Holder& a, const Holder& b) noexcept {
    return a.address == b.address && same(a.ub[0],b.ub[0]) && same(a.ub[1],b.ub[1]) && same(a.cached_view,b.cached_view);
}
template<class Read> Buffer buffer(Field<uintptr_t> object, Read& read) {
    Buffer b{}; b.object = object;
    if (object.valid) wuwa_lod::field(read,object.value,0x20,b.layout);
    if (b.layout.valid) {
        wuwa_lod::field(read,b.layout.value,0,b.size);
        wuwa_lod::field(read,b.layout.value,0x78,b.conversion_count);
    }
    return b;
}
template<class Read> Payload payload(uintptr_t address, Read& read) {
    Payload p{}; p.address=address;
    // Hash the bounded original CPU structure. Pointer fields/padding can vary;
    // this is not a semantic comparison or an RHI-submitted-copy fingerprint.
    Field<std::array<uint8_t,payload_size>> bytes{};
    wuwa_lod::field(read,address,0,bytes);
    if (bytes.valid) {
        p.hash={wuwa_code_compatibility::fingerprint(bytes.value),true};
        const auto extract=[&](size_t offset,auto& field) {
            std::memcpy(&field.value,bytes.value.data()+offset,sizeof(field.value));field.valid=true;
        };
        extract(0x918,p.previous_time_bits);extract(0x948,p.time_bits);
        extract(0x960,p.frame);extract(0x968,p.state_frame_index);
        extract(0x000,p.translated_world_to_clip);
        extract(0x140,p.translated_world_to_camera_view);
        extract(0x0c0,p.translated_world_to_view);
        extract(0x430,p.world_camera_origin);extract(0x460,p.pre_view_translation);
        extract(0x730,p.previous_world_camera_origin);extract(0x750,p.previous_pre_view_translation);
        extract(0x1700,p.origin_location);
        extract(0xa00,p.force_draw_all_velocities_bits);extract(0x1340,p.kuro_is_planar_reflection_view_bits);
    }
    Field<std::array<uint32_t,2>> previous{};
    Field<std::array<uint32_t,3>> current{};
    Field<uint32_t> frame{},state_frame{};
    wuwa_lod::field(read,address,0x918,previous);
    wuwa_lod::field(read,address,0x948,current);
    wuwa_lod::field(read,address,0x960,frame);
    wuwa_lod::field(read,address,0x968,state_frame);
    p.clock_fields_consistent=same(p.previous_time_bits,previous) && same(p.time_bits,current) &&
        same(p.frame,frame) && same(p.state_frame_index,state_frame);
    return p;
}

// Exact verified contracts: both functions leave RBP=entry-RSP-0x5f.
// Shared sites: R14=holder, RBX=view. 235ec05f is BEFORE the cached pointer
// store and AFTER both RHI wrappers returned. Own site: RDI=view, all
// create/update branches have returned; R14 is not a valid cross-branch UB.
template<class Read, class Executable>
Record snapshot(Stage stage, Registers registers, uint32_t frame_offset,
    uintptr_t image_base, uint32_t image_size, Read read, Executable executable) {
    Record r{}; r.stage=stage;
    const auto index=static_cast<size_t>(stage);
    if (index>=site_rvas.size()) return r;
    r.site_rva=site_rvas[index];
    const bool shared=stage!=Stage::own_update_returned;
    const auto view=shared?registers.rbx:registers.rdi;
    r.view=identity(view,frame_offset,read);
    if (shared) r.holder=holder(registers.r14,read);
    r.identity_valid=view && r.view.family.valid && r.view.family.value &&
        r.view.state.valid && r.view.frame.valid && r.view.pass.valid;
    if (stage==Stage::cache_decision && view && r.holder.cached_view.valid)
        r.decision=r.holder.cached_view.value==view?Decision::would_skip:Decision::would_update;
    if (r.view.payload.valid) r.payload=payload(r.view.payload.value,read);
    r.buffers[0]=buffer(r.view.own_ub,read);
    if (shared) { r.buffers[1]=buffer(r.holder.ub[0],read); r.buffers[2]=buffer(r.holder.ub[1],read); }
    Field<uintptr_t> caller{};
    wuwa_lod::field(read,registers.rbp,0x5f,caller);
    if (caller.valid && image_base && image_size && image_base<=UINTPTR_MAX-image_size &&
        caller.value>=image_base && caller.value-image_base<image_size && executable(caller.value))
        r.caller_rva={static_cast<uint32_t>(caller.value-image_base),true};
    // Check anchors again after dependent reads. This detects pointer/frame
    // replacement during observation; it cannot establish an atomic GPU state.
    r.view_after=identity(view,frame_offset,read);
    r.anchors_consistent=consistent(r.view,r.view_after) && r.payload.clock_fields_consistent;
    if (shared) { r.holder_after=holder(registers.r14,read); r.anchors_consistent &= consistent(r.holder,r.holder_after); }
    r.complete=r.identity_valid && r.anchors_consistent && r.caller_rva.valid &&
        r.payload.hash.valid && r.payload.time_bits.valid && r.payload.previous_time_bits.valid &&
        r.payload.frame.valid && r.payload.state_frame_index.valid;
    for (size_t i=0;i<(shared?3u:1u);++i) r.complete &= r.buffers[i].object.valid &&
        r.buffers[i].object.value && r.buffers[i].layout.valid && r.buffers[i].layout.value &&
        r.buffers[i].size.valid && r.buffers[i].size.value==(i==2?instanced_payload_size:payload_size) &&
        r.buffers[i].conversion_count.valid;
    return r;
}

struct Counts {
    uint64_t recorded{}, filtered{}, invalid{}, busy{}, watch_full{}, sink_full{}, exceptions{}, unstable{};
};
class Observer {
public:
    static constexpr size_t holder_capacity=8;
    // Root must pass a nonblocking, non-reentrant ring sink returning whether
    // publication succeeded. No root control mutex or file drain in the sink.
    // Call under the existing LOD callback lock while its lease is active.
    template<class Context, class Read, class Executable, class Sink>
    Result capture(Stage stage, const Context& c, uint32_t frame_offset,
        const std::array<uintptr_t,2>& states, uintptr_t image_base, uint32_t image_size,
        uint64_t tick_ms, uint32_t thread, Read read, Executable executable, Sink sink) noexcept {
        if (stopped_flag.load(std::memory_order_relaxed)) return Result::stopped;
        if (lock.test_and_set(std::memory_order_acquire)) { ++busy_count; return Result::busy; }
        struct Unlock { std::atomic_flag& flag; ~Unlock(){flag.clear(std::memory_order_release);} } unlock{lock};
        try {
            if (stopped_flag.load(std::memory_order_relaxed)) return Result::stopped;
            if (static_cast<size_t>(stage)>=site_rvas.size() || frame_offset!=0x64 ||
                !states[0] || !states[1] || states[0]==states[1]) { ++invalid_count; return Result::invalid; }
            const bool shared=stage!=Stage::own_update_returned;
            const uintptr_t view=shared?c.rbx:c.rdi;
            Field<uintptr_t> state{}; wuwa_lod::field(read,view,8,state);
            const int eye=state.valid?(state.value==states[0]?0:state.value==states[1]?1:-1):-1;
            bool watched=false;
            if (shared && c.r14) for (auto address:holders) if (address==c.r14) {watched=true;break;}
            if (eye<0 && !watched) {
                if (!state.valid) {++invalid_count;return Result::invalid;}
                ++filtered_count; return Result::filtered;
            }
            if (shared && !watched) {
                if (!c.r14) {++invalid_count;return Result::invalid;}
                auto empty=holders.end();
                for (auto it=holders.begin();it!=holders.end();++it) if (!*it) {empty=it;break;}
                if (empty==holders.end()) {++watch_full_count;return Result::watch_full;}
                *empty=c.r14;
            }
            auto record=snapshot(stage,{c.rbp,c.rbx,c.rdi,c.r14},frame_offset,image_base,image_size,read,executable);
            record.eye=eye; record.sequence=++sequence; record.tick_ms=tick_ms; record.thread=thread;
            // Classification is reread too; reject a main-eye label if state
            // changed after the initial filtering read. Preserve the raw row.
            if (!same(state,record.view.state)) {record.eye=-1;record.anchors_consistent=false;record.complete=false;}
            if (!record.complete) ++invalid_count;
            if (!record.anchors_consistent) ++unstable_count;
            record.loss_before=loss();
            if (!sink(record)) {++sink_full_count;stopped_flag=true;return Result::sink_full;}
            ++recorded_count; return Result::recorded;
        } catch (...) { ++exception_count; return Result::exception; }
    }
    // Control path only, under the owner's EXCLUSIVE outer callback lock.
    // Nonblocking even in misuse; false means state has not been reset.
    bool reset() noexcept {
        if (lock.test_and_set(std::memory_order_acquire)) return false;
        holders={};sequence=0;stopped_flag=false;
        recorded_count=0;filtered_count=0;invalid_count=0;busy_count=0;watch_full_count=0;
        sink_full_count=0;exception_count=0;unstable_count=0;
        lock.clear(std::memory_order_release);return true;
    }
    Counts counts() const noexcept {
        return {recorded_count.load(),filtered_count.load(),invalid_count.load(),busy_count.load(),
            watch_full_count.load(),sink_full_count.load(),exception_count.load(),unstable_count.load()};
    }
    uint64_t loss() const noexcept {
        return invalid_count.load()+busy_count.load()+watch_full_count.load()+sink_full_count.load()+exception_count.load();
    }
    bool stopped() const noexcept {return stopped_flag.load(std::memory_order_relaxed);}
private:
    std::atomic_flag lock=ATOMIC_FLAG_INIT;
    std::atomic<bool> stopped_flag{};
    std::array<uintptr_t,holder_capacity> holders{};
    uint64_t sequence{};
    std::atomic<uint64_t> recorded_count{},filtered_count{},invalid_count{},busy_count{},watch_full_count{},sink_full_count{},exception_count{},unstable_count{};
};
} // namespace wuwa_view_ub_trace
