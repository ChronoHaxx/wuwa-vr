#pragma once
#include "WuWaWaterStereoPolicy.hpp"
#include "WuWaStereoParameters.hpp"
#include "WuWaLguiProbe.hpp"
#include <atomic>

namespace wuwa_water_stereo {
namespace memory = wuwa_lgui_probe::detail;
inline std::atomic<bool> enabled{}, faulted{};
inline std::atomic<uint64_t> entered{}, prepared{}, repeated{}, second_queued{}, preserved{}, aborted{};
inline std::atomic<uint32_t> fault_reason{};
inline std::atomic<uint64_t> stale_frames{};
inline std::array<std::atomic<uint64_t>, 6> refusals{};
inline uintptr_t base{}; // Set once before hooks are enabled.
struct Frame {
    bool active{}, second{}, first_wrote{}, first_expected{}, first_seen{}, second_seen{}, second_loaded{};
    std::array<uintptr_t,2> views{};
    safetyhook::Context saved{};
    // Original frame locals only. Never overwrite saved nonvolatile registers,
    // the security cookie, return address, or SafetyHook's own stack.
    std::array<uint8_t, 0x80> locals{}; // site RSP+0x50 .. +0xd0 (exclusive)
};
inline thread_local Frame frame{};
inline void fail(uint32_t reason) noexcept { fault_reason=reason; faulted=true; ++aborted; }
inline constexpr uintptr_t begin_site=0x230e1654, load_site=0x230e1896, queued_site=0x230e1961,
    finish_site=0x230e19c1, repeat_site=0x230e1689;
// A SafetyHook far jump may displace 14 bytes. The loop target must stay outside
// every patched span; putting begin at Views[0]'s load would overwrite it.
static_assert(repeat_site > begin_site+24 && repeat_site < queued_site);
inline std::array<uint8_t,16> resume_bytes{};
inline bool resume_captured{};
inline bool capture_resume_window() noexcept {
    resume_captured=memory::read(base+repeat_site,resume_bytes); return resume_captured;
}
inline bool resume_window_intact() noexcept {
    std::array<uint8_t,16> current{};
    return resume_captured && memory::read(base+repeat_site,current) && current==resume_bytes;
}
inline bool redirectable(safetyhook::Context& c) noexcept {
    return c.trampoline_rsp==reinterpret_cast<uintptr_t>(&c.rip);
}

inline bool write(uintptr_t address, const void* bytes, size_t size) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    if (!address || !size || address > UINTPTR_MAX-size ||
        VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
        (region.Protect & 0xff) != PAGE_READWRITE) return false;
    const auto start = reinterpret_cast<uintptr_t>(region.BaseAddress);
    if (address < start || address-start > region.RegionSize || size > region.RegionSize-(address-start)) return false;
    __try { std::memcpy(reinterpret_cast<void*>(address), bytes, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// After the shared texture is allocated once, before publishing it. The other
// eye receives its own original game-built parameters,
// uniform buffer, viewport and mesh command list. No pass ID is rewritten.
inline void begin(safetyhook::Context& c) noexcept {
    if (frame.active) {
        if (c.rsp < frame.saved.rsp) { fail(1); return; } // nested call: keep outer drain state
        frame={}; ++stale_frames; // a prior callback was missed; never reuse it for this call
    }
    if (!enabled.load(std::memory_order_relaxed) || faulted.load()) return;
    const auto last_error = GetLastError();
    try {
        ++entered;
        if (!redirectable(c)) { fail(8); }
        else {
            const auto pair = wuwa_stereo_parameters::renderer_pair(c.r14,
                [](uintptr_t at, auto& value) { return memory::read(at,value); });
            Layout x{}; x.serial_pair = wuwa_stereo_parameters::serial_pair(pair);
            uintptr_t vtable{}; wuwa_world_labels::Extent descriptor_extent{};
            std::array<uint32_t,2> gpu_masks{};
            bool valid = x.serial_pair && c.rsp <= UINTPTR_MAX-0xf8 && c.rbp == c.rsp+0xc9 &&
                c.r12==c.rdi+0x98 && memory::read_field(c.r14,0x8b0,x.feature_level) && memory::read(c.r15,vtable) &&
                vtable == base+0x2759ec60 && memory::read_field(c.r15,0x54,x.target_extent) &&
                memory::read(c.rbp-0x15,descriptor_extent) && descriptor_extent==x.target_extent;
            for (size_t i=0; valid && i<2; ++i) {
                valid = memory::read_field(pair.views[i].value,0xadec,x.draws[i]) &&
                    memory::read_field(pair.views[i].value,0xd964,x.flags[i]) &&
                    memory::read_field(pair.views[i].value,0x1e30,x.draw_rects[i]) &&
                    memory::read_field(pair.views[i].value,0xc9c,gpu_masks[i]);
            }
            valid=valid && gpu_masks[0]==gpu_masks[1];
            const auto reason = valid ? check(x) : Refusal::family;
            if (reason != Refusal::none) ++refusals[size_t(reason)];
            else if (memory::read(c.rsp+0x50,frame.locals)) {
                frame.saved=c; frame.views={pair.views[0].value,pair.views[1].value};
                frame.first_expected=x.draws[0]>0; frame.first_seen=false; frame.second_seen=false; frame.second_loaded=false;
                frame.first_wrote=false; frame.second=false; frame.active=true; ++prepared;
            } else ++aborted;
        }
    } catch (...) { fail(2); }
    SetLastError(last_error);
}

// Before uniform/pass construction, so even a constructor that caches load
// actions sees Load. Drains an already-started replay even when toggled off.
inline void load(safetyhook::Context& c) noexcept {
    if (!frame.active || !frame.second || c.rsp!=frame.saved.rsp || c.rbx!=frame.saved.rbx) return;
    const auto last_error=GetLastError();
    uintptr_t target{}, vtable{}; uint8_t action{};
    const bool valid=c.r13==frame.views[1] && c.r14==frame.saved.r14 &&
        memory::read(c.rdi,vtable) && vtable==base+0x274f5960 &&
        memory::read_field(c.rdi,0x20,target) && target==frame.saved.r15 &&
        memory::read_field(c.rdi,0x30,action) && action==2;
    const auto desired=load_action(frame.first_wrote);
    if (valid && (desired==2 || write(c.rdi+0x30,&desired,sizeof(desired)))) frame.second_loaded=true;
    else {
        fail(4);
        if (redirectable(c)) c.rip=base+finish_site;
    }
    SetLastError(last_error);
}

// Before the original function registers the new pass with the graph. Desktop
// constructor 0x230c41e0 copies {renderer,view,meshpass}; the execute method
// 0x230d2820 takes its viewport from that view+0x1e30 and draws that meshpass.
inline void queued(safetyhook::Context& c) noexcept {
    if (!frame.active || c.rsp != frame.saved.rsp || c.rbx!=frame.saved.rbx) return;
    const auto last_error=GetLastError();
    uintptr_t target{}, captured_view{}, outer_vtable{}, inner_vtable{}, parameter_vtable{}, renderer{}, mesh{};
    uint8_t load{};
    const auto expected=frame.views[frame.second ? 1:0];
    const bool valid=c.rsi && c.r13==expected && c.rbx==frame.saved.rbx && c.r14==frame.saved.r14 &&
        memory::read_field(c.rdi,0x20,target) && target==frame.saved.r15 &&
        memory::read(c.rsi,outer_vtable) && outer_vtable==base+0x276b85c0 &&
        memory::read_field(c.rsi,8,inner_vtable) && inner_vtable==base+0x276f9468 &&
        memory::read_field(c.rsi,0xf0,renderer) && renderer==frame.saved.r14 &&
        memory::read_field(c.rsi,0x100,mesh) && mesh==expected+0xaaf0 &&
        memory::read(c.rdi,parameter_vtable) && parameter_vtable==base+0x274f5960 &&
        memory::read_field(c.rdi,0x30,load) && load==(frame.second ? load_action(frame.first_wrote):2) &&
        memory::read_field(c.rsi,0xf8,captured_view) && captured_view==expected;
    if (!frame.second) {
        frame.first_seen=true; frame.first_wrote=valid;
        // Never replay with an uncertain first writer: a later Clear would erase it.
        if (!valid || !frame.first_expected) fail(3);
    }
    else if (valid && frame.second_loaded) {
        frame.second_seen=true;
        ++second_queued; if (frame.first_wrote) ++preserved;
    } else {
        fail(5);
        // Do not enqueue a second clear or an unknown pass. Arena allocations
        // remain owned by the graph; the function returns through its epilogue.
        if (redirectable(c)) c.rip=base+finish_site;
    }
    SetLastError(last_error);
}

inline void finish(safetyhook::Context& c) noexcept {
    if (!frame.active || c.rsp != frame.saved.rsp || c.rbx!=frame.saved.rbx) return;
    const auto last_error=GetLastError();
    if (!frame.second && frame.first_expected && !frame.first_seen) fail(6);
    if (!frame.second && enabled.load() && !faulted.load()) {
        if (!redirectable(c) || !resume_window_intact()) {
            fail(8); frame.active=false; SetLastError(last_error); return;
        }
        uintptr_t published{}, pooled{};
        if (!memory::read_field(frame.saved.rdi,0x88,published) ||
            !memory::read_field(frame.saved.r15,0x98,pooled) || published!=pooled) {
            fail(9); frame.active=false; SetLastError(last_error); return;
        }
        if (!write(c.rsp+0x50,frame.locals.data(),frame.locals.size())) {
            fail(7); frame.active=false; SetLastError(last_error); return;
        }
        const auto trampoline_rsp=c.trampoline_rsp;
        c=frame.saved;
        c.trampoline_rsp=trampoline_rsp; // Keep this hook's return machinery.
        c.r13=frame.views[1];
        c.rip=base+repeat_site; // After Views[0] load; re-run the original eligibility check.
        frame.second=true; ++repeated;
    } else {
        if (frame.second && !frame.second_seen && !faulted.load()) ++aborted;
        frame.active=false;
    }
    SetLastError(last_error);
}

inline nlohmann::json status() {
    nlohmann::json skipped=nlohmann::json::array();
    for (const auto& n:refusals) skipped.push_back(n.load());
    return {{"enabled",enabled.load()},{"faulted",faulted.load()},{"entered",entered.load()},
        {"prepared",prepared.load()},{"second_attempted",repeated.load()},
        {"second_queued",second_queued.load()},{"first_eye_preserved",preserved.load()},
        {"aborted",aborted.load()},{"fault_reason",fault_reason.load()},{"stale_frames",stale_frames.load()},{"refusals",skipped},
        {"refusal_order",{"none","family_or_read","feature_level","eligibility","draw_count","target_layout"}}};
}
} // namespace wuwa_water_stereo
