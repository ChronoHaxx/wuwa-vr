#pragma once
#include "WuWaLguiProbe.hpp"
#include "WuWaStereoParameters.hpp"
#include "WuWaCodeCheck.hpp"
#include "WuWaReflectionLayout.hpp"
#include <mutex>
#include <vector>

// Read-only layout evidence for the custom reflection texture; no UV/texture repair yet.
// Adds no register writes; mode 2 (hide) is unchanged and remains a workaround.
namespace wuwa_kuro_reflection {
namespace memory=wuwa_lgui_probe::detail;
using Json=nlohmann::json;
using Rect=wuwa_planar_probe::Rect;
inline std::mutex control, layout_lock;
inline std::atomic<int> mode{}; // 0 off, 1 observe, 2 substitute the game's null-texture fallback
inline std::atomic<bool> faulted{};
inline std::atomic<uint64_t> calls{}, skipped{}, suppressed{}, selection_mismatch{};
inline std::array<std::atomic<uint64_t>,2> eye_calls{};
inline std::array<std::atomic<uintptr_t>,2> eye_textures{};
inline std::array<std::atomic<uint64_t>,2> single_calls{};
inline bool attempted{};
inline HMODULE retained_backend{};
inline safetyhook::MidHook* hook{};
inline std::string error;

// What 0x23617eab..0x23617f04 selected, and the atlas layout questions that
// decide the reflection fix. Offsets marked "inferred" are validated only by
// the consistency checks below; nothing here is written back.
struct Layout {
    uint64_t tick_ms{};
    uintptr_t view_state{};
    uintptr_t element{}, resource{}, texture{};
    int32_t priority{}, candidates{};
    std::array<uint32_t,2> size{};       // inferred FRHITexture +0x54/+0x58 (see 0x23618031..0x236180c2)
    uint8_t stereo{};                    // inferred planar proxy +0x1c
    std::array<Rect,2> proxy_rects{};    // inferred planar proxy +0x100/+0x110
    std::array<Rect,2> rects_2f8{}, rects_1e30{};
    Rect current_rect{};
    Rect render_rect{};
    int32_t family_views{}, stereo_pass{};
    size_t family_index{}, spatial_half{};
    wuwa_reflection_layout::Uniforms uniforms{};
    bool size_valid{}, proxy_valid{}, atlas_tiled{}, atlas_padded{}, order_matches{}, render_rect_valid{}, render_uniforms_match{};
};
inline std::array<Layout,2> layouts{};
inline std::array<Layout,2> single_layouts{};
using Sampler=wuwa_reflection_layout::StateSamples<8>;
inline Sampler state_samples; // guarded by layout_lock
inline std::array<Layout,8> state_layouts{}; // slots are observations, not headset eye labels
inline std::atomic<uint64_t> state_lock_misses{}, state_read_failures{};

template<class T> inline bool get(uintptr_t at,T& value) { return memory::read(at,value); }

// Same selection as the game: enabled (+0x168), non-null resource (+0x160),
// strictly greatest signed priority (+0x16c) starting from INT_MIN.
inline bool selected(uintptr_t scene,Layout& l) {
    uintptr_t data{}; int32_t count{};
    if (!get(scene+0x3e60,data) || !get(scene+0x3e68,count) || count<0 || count>256 || (count && !data)) return false;
    int32_t best=(std::numeric_limits<int32_t>::min)();
    l.candidates=count;
    for (int32_t i=0;i<count;++i) {
        uintptr_t element{}, resource{}; uint8_t enabled{}; int32_t priority{};
        if (!get(data+uintptr_t(i)*8,element) || !element || !get(element+0x168,enabled) ||
            !get(element+0x160,resource) || !get(element+0x16c,priority)) return false;
        if (!enabled || !resource || priority<=best) continue;
        best=priority; l.element=element; l.resource=resource; l.priority=priority;
    }
    return !l.element || get(l.resource+0x10,l.texture);
}

inline std::optional<Sampler::Ticket> sample_state(const safetyhook::Context& c) {
    uintptr_t state{};
    if(!c.rcx) return {}; // Keep the last non-null texture sample after menu exit.
    if(!memory::read_field(c.r12,8,state) || !state) {++state_read_failures;return {};}
    const std::unique_lock lock{layout_lock,std::try_to_lock};
    if(!lock.owns_lock()) {++state_lock_misses;return {};}
    auto ticket=state_samples.take(state,GetTickCount64());
    if(ticket && state_layouts[ticket->slot].view_state!=state) state_layouts[ticket->slot]={};
    return ticket;
}

inline void record(const safetyhook::Context& c,const wuwa_planar_probe::Snapshot& pair,
    const wuwa_reflection_layout::View& view,const Sampler::Ticket& ticket) {
    // Keep the last non-null texture observation when leaving a menu, with an
    // explicit age. At most one sample per view state each second.
    if (!c.rcx) return;
    const auto now=GetTickCount64();
    Layout l{}; l.tick_ms=now;l.view_state=ticket.state;
    if (!selected(c.r15,l) || l.texture!=c.rcx) { ++selection_mismatch; return; }
    l.family_views=pair.view_count.value; l.stereo_pass=pair.stereo_pass.value;
    l.family_index=view.family_index; l.spatial_half=view.spatial_half; l.current_rect=view.rect;
    l.uniforms=wuwa_reflection_layout::uniforms(c.rsi,view.rect,
        [](uintptr_t at,auto& value){return memory::read(at,value);});
    for (size_t i=0;i<size_t(pair.view_count.value);++i) {
        l.rects_2f8[i]=pair.view_rects[i].value;
        (void)get(pair.views[i].value+0x1e30,l.rects_1e30[i]);
    }
    if (c.rcx) l.size_valid=get(c.rcx+0x54,l.size) && l.size[0] && l.size[1] && l.size[0]<=32768 && l.size[1]<=32768;
    if (l.element) {
        l.proxy_valid=get(l.element+0x1c,l.stereo) && get(l.element+0x100,l.proxy_rects[0]) &&
            get(l.element+0x110,l.proxy_rects[1]) && l.stereo<=1;
        if (l.proxy_valid && l.size_valid) {
            l.atlas_tiled=wuwa_stereo_parameters::tiled(l.proxy_rects[0],l.proxy_rects[1],l.size[0],l.size[1]);
            l.atlas_padded=wuwa_reflection_layout::padded_sbs(l.proxy_rects,l.size);
            l.order_matches=!view.single &&
                (l.proxy_rects[0][0]<l.proxy_rects[1][0])==(l.rects_2f8[0][0]<l.rects_2f8[1][0]);
        }
    }
    // +0x2f8 is the pre-screen-percentage rectangle in observed NSF frames.
    // Compare the actual render rectangle independently; do not rewrite either.
    l.render_rect_valid=memory::read_field(c.r12,0x1e30,l.render_rect);
    if(l.render_rect_valid) l.render_uniforms_match=wuwa_reflection_layout::uniforms(c.rsi,l.render_rect,
        [](uintptr_t at,auto& value){return memory::read(at,value);}).consistent;
    if (layout_lock.try_lock()) {
        if(state_samples.owns(ticket)) state_layouts[ticket.slot]=l;
        if (view.single) single_layouts[view.spatial_half]=l; else layouts[view.family_index]=l;
        layout_lock.unlock();
    } else ++state_lock_misses;
}

inline uintptr_t verify() {
    // Check both the uniform builder and the game's null-texture fallback.
    constexpr std::array<wuwa_code_compatibility::Range, 2> ranges{{
        {0x23614a50, 14883, 0xe93cd4f449fd3e84ULL},
        {0x2360e1b0, 71, 0xacbfba708c9a4ff6ULL}}};
    return wuwa_code_check::verify("Kuro reflection texture", ranges) + 0x23617f07;
}
inline void callback(safetyhook::Context& c) noexcept {
    const auto requested=mode.load(std::memory_order_relaxed);
    if (!requested || faulted.load()) return;
    const auto last_error=GetLastError();
    try {
        ++calls;
        // At the verified call site r12 remains the original FViewInfo, r15 the
        // FScene and rcx the selected raw texture. Observation changes no registers.
        // Throttle by state: separate eye targets can both have rectangle x=0.
        // Hiding retains the previous full validation on every call.
        const auto ticket=sample_state(c);
        if(requested==1 && !ticket) {SetLastError(last_error);return;}
        const auto pair=wuwa_planar_probe::snapshot(c.r12,0,0,0,0,
            [](uintptr_t at,auto& value){return memory::read(at,value);});
        if (const auto view=wuwa_reflection_layout::observation_view(pair)) {
            if (view->single) ++single_calls[view->spatial_half];
            else {++eye_calls[view->family_index]; eye_textures[view->family_index].store(c.rcx);}
            if(ticket) record(c,pair,*view,*ticket);
            // Observation supports NSF; the removal workaround still requires
            // the original verified two-view contract and is never broadened.
            if (requested==2 && !view->single && c.rcx) {c.rcx=0; ++suppressed;}
        } else ++skipped;
    } catch (...) {faulted=true;}
    SetLastError(last_error);
}
inline void configure(bool observe,bool hide) noexcept {
    const int requested=hide ? 2:observe ? 1:0;
    if (!requested) {mode=0; return;}
    if (faulted.load()) return;
    if (mode.load()) {mode=requested; return;}
    const std::lock_guard guard{control};
    if (attempted) {if (hook) mode=requested; return;}
    attempted=true;
    try {
        const auto site=verify();
        memory::require(site!=0,"Kuro reflection comparison refused: game or texture-binding code differs");
        memory::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&callback),&retained_backend)!=0,"Cannot retain reflection comparison backend");
        auto created=safetyhook::MidHook::create(reinterpret_cast<void*>(site),callback,safetyhook::MidHook::StartDisabled);
        memory::require(created.has_value(),"Cannot create the Kuro reflection comparison hook");
        hook=new safetyhook::MidHook(std::move(*created));
        memory::require(hook->enable().has_value(),"Cannot enable the Kuro reflection comparison hook");
        mode=requested;
        spdlog::info("[WuWaKuroReflection] verified custom texture binding; comparison ready");
    } catch(const std::exception& e) {error=e.what();faulted=true;}
    catch(...) {error="Kuro reflection hook initialization failed";faulted=true;}
    if (faulted.load()) {
        mode=0; if(hook) {try{(void)hook->disable();}catch(...){}}
        spdlog::error("[WuWaKuroReflection] {}",error);
    }
}
inline Json layout_json(const Layout& l) {
    return {{"sample_tick_ms",l.tick_ms},{"age_ms",l.tick_ms ? GetTickCount64()-l.tick_ms:0},{"view_state",l.view_state},{"element",l.element},{"resource",l.resource},{"texture",l.texture},{"priority",l.priority},
        {"candidates",l.candidates},{"size_valid",l.size_valid},{"size",l.size},
        {"proxy_valid",l.proxy_valid},{"proxy_stereo",l.stereo},{"proxy_rects",l.proxy_rects},
        {"rects_2f8",l.rects_2f8},{"rects_1e30",l.rects_1e30},
        {"family_view_count",l.family_views},{"family_index",l.family_index},
        {"stereo_pass",l.stereo_pass},{"spatial_half",l.spatial_half},{"current_rect",l.current_rect},
        {"render_rect",l.render_rect},{"render_rect_valid",l.render_rect_valid},{"render_uniforms_match",l.render_uniforms_match},
        {"uniforms",{{"read_valid",l.uniforms.view_min.valid && l.uniforms.view_size_inv.valid && l.uniforms.buffer_size_inv.valid},
            {"consistent",l.uniforms.consistent},{"view_min",l.uniforms.view_min.value},
            {"view_size_inv",l.uniforms.view_size_inv.value},{"buffer_size_inv",l.uniforms.buffer_size_inv.value}}},
        {"atlas_tiled",l.atlas_tiled},{"atlas_padded_sbs",l.atlas_padded},
        {"order_matches",l.order_matches},{"order_known",l.family_views==2}};
}
inline Json status() {
    std::array<Layout,2> seen{},single{};
    std::array<Layout,8> states{};uint64_t evictions{};
    { const std::lock_guard lock{layout_lock}; seen=layouts; single=single_layouts;states=state_layouts;evictions=state_samples.evictions; }
    auto by_state=Json::array();
    for(const auto& state:states) if(state.tick_ms) by_state.push_back(layout_json(state));
    const std::lock_guard guard{control};
    return {{"mode",mode.load()},{"faulted",faulted.load()},{"calls",calls.load()},
        {"eye_calls",{eye_calls[0].load(),eye_calls[1].load()}},
        {"original_textures",{eye_textures[0].load(),eye_textures[1].load()}},
        {"layout",{layout_json(seen[0]),layout_json(seen[1])}},
        {"single_family_samples",{single_calls[0].load(),single_calls[1].load()}},
        {"single_family_layout",{layout_json(single[0]),layout_json(single[1])}},
        {"single_family_indexing","legacy spatial summary; separate NSF targets can share x=0; use view_state_layout"},
        {"view_state_layout",by_state},{"state_slot_evictions",evictions},{"state_lock_misses",state_lock_misses.load()},
        {"state_read_failures",state_read_failures.load()},
        {"state_slot_identity","view-state addresses only, not OpenXR eye labels or matched GPU frames; maximum 8 recent states"},
        {"selection_mismatch",selection_mismatch.load()},
        {"suppressed",suppressed.load()},{"unsupported_views",skipped.load()},{"error",error}};
}
inline void shutdown(bool process_exiting) noexcept {
    mode=0;
    if (process_exiting) return;
    const std::lock_guard guard{control};
    if (hook) {try{(void)hook->disable();}catch(...){}}
    // Keep disabled stubs/module until exit: a callback may still be returning.
}
} // namespace wuwa_kuro_reflection
