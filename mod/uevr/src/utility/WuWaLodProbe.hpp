#pragma once
// Bounded read-only observation. No game camera, view, CVar or shader writes.
#include "WuWaCodeCheck.hpp"
#include "WuWaLodSnapshot.hpp"
#include "WuWaLodHookSpans.hpp"
#include "WuWaViewUbTrace.hpp"
#include "WuWaMeshBindingSnapshot.hpp"
#include "WuWaEyeDiff.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>

namespace wuwa_lod_probe {
using Json = nlohmann::json;
namespace memory = wuwa_lgui_probe::detail;
constexpr uintptr_t site_rva = 0x247c08e3;
constexpr uintptr_t uniform_site_rva = 0x24adbd0d;
constexpr size_t capacity = 8192;
inline SRWLOCK callbacks = SRWLOCK_INIT;
inline std::mutex control;
inline std::atomic<bool> armed{};
inline bool retired{};
inline bool installed{};
inline safetyhook::MidHook* hook{};
inline safetyhook::MidHook* uniform_hook{};
inline std::array<safetyhook::MidHook*, 3> view_hooks{};
inline bool view_hooks_installed{}, view_hooks_failed{};
inline safetyhook::MidHook* mesh_hook{};
inline bool mesh_hook_installed{}, mesh_hook_failed{};
// Shared originals are verified before either optional observer patches them.
// The control mutex serializes this cache; backend/hook storage is retained.
inline uintptr_t verified_view_ub_base{};
inline HMODULE retained_backend{};

template<class T> Json value(const wuwa_lod::Field<T>& f) { return f.valid ? Json(f.value) : Json(nullptr); }
inline Json view_json(const wuwa_lod::View& v) {
    return {{"view", v.address}, {"family", value(v.family)}, {"state", value(v.state)},
        {"frame", value(v.frame)}, {"pass", value(v.pass)},
        {"projection", value(v.projection)}, {"view_matrix", value(v.view_matrix)},
        {"mode_bytes_0fea", value(v.mode_bytes)}, {"fallback_origin", value(v.fallback_origin)}};
}
inline Json producer_json(const wuwa_lod::ProducerContext& p) {
    const auto relation = p.source_relation == wuwa_lod::MatrixSourceRelation::view_plus_320 ? "view_plus_0x320" :
        p.source_relation == wuwa_lod::MatrixSourceRelation::view_plus_7e0 ? "view_plus_0x7e0" : "external";
    return {{"parameters", p.parameters}, {"current_matrices", p.current_matrices},
        {"previous_matrices", p.previous_matrices}, {"caller_rva", value(p.caller_rva)},
        {"source_relation", relation}, {"snapshot_stage", "pre_tail_24adc060"}};
}
inline Json view_ub_json(const wuwa_view_ub_trace::Record& r) {
    using namespace wuwa_view_ub_trace;
    const auto identity_json = [](const Identity& v) -> Json {
        return {{"view",v.view},{"family",value(v.family)},{"state",value(v.state)},
            {"payload",value(v.payload)},{"own_ub",value(v.own_ub)},
            {"frame",value(v.frame)},{"pass",value(v.pass)}};
    };
    const auto holder_json = [](const Holder& h) -> Json {
        return {{"address",h.address},{"cached_view",value(h.cached_view)},
            {"buffers",Json::array({value(h.ub[0]),value(h.ub[1])})}};
    };
    Json buffers=Json::array();
    for (const auto& b : r.buffers) buffers.push_back({{"object",value(b.object)},
        {"layout",value(b.layout)},{"size",value(b.size)},
        {"conversion_count",value(b.conversion_count)}});
    const auto& p=r.payload;
    return {{"type","view_uniform_resource"},{"sequence",r.sequence},{"tick_ms",r.tick_ms},
        {"thread",r.thread},{"site_rva",r.site_rva},{"eye_slot",r.eye},
        {"stage",r.stage==Stage::cache_decision?"cache_decision":
            r.stage==Stage::own_update_returned?"own_update_returned":"shared_update_returned_before_cache_store"},
        {"decision",r.decision==Decision::would_skip?"would_skip":
            r.decision==Decision::would_update?"would_update":"unknown"},
        {"view",identity_json(r.view)},{"view_after",identity_json(r.view_after)},
        {"holder",holder_json(r.holder)},{"holder_after",holder_json(r.holder_after)},
        {"buffers",buffers},{"caller_rva",value(r.caller_rva)},
        {"payload",{{"address",p.address},{"hash",value(p.hash)},
            {"previous_time_bits",value(p.previous_time_bits)},{"time_bits",value(p.time_bits)},
            {"frame",value(p.frame)},{"state_frame_index",value(p.state_frame_index)},
            {"translated_world_to_clip",value(p.translated_world_to_clip)},
            {"translated_world_to_camera_view",value(p.translated_world_to_camera_view)},
            {"translated_world_to_view",value(p.translated_world_to_view)},
            {"world_camera_origin",value(p.world_camera_origin)},
            {"pre_view_translation",value(p.pre_view_translation)},
            {"previous_world_camera_origin",value(p.previous_world_camera_origin)},
            {"previous_pre_view_translation",value(p.previous_pre_view_translation)},
            {"origin_location",value(p.origin_location)},
            {"force_draw_all_velocities_bits",value(p.force_draw_all_velocities_bits)},
            {"kuro_is_planar_reflection_view_bits",value(p.kuro_is_planar_reflection_view_bits)},
            {"clock_fields_consistent",p.clock_fields_consistent}}},
        {"loss_before",r.loss_before},{"anchors_consistent",r.anchors_consistent},
        {"identity_valid",r.identity_valid},{"record_complete",r.complete}};
}
inline Json mesh_binding_json(const wuwa_mesh_binding::Record& r) {
    Json resources=Json::array();
    for(uint32_t i=0;i<r.resource_count;++i) {
        const auto& b=r.resources[i];
        resources.push_back({{"slot",value(b.slot)},{"parameter_size",value(b.parameter_size)},
            {"name_hash",value(b.name_hash)},{"collection_slot",b.collection_slot},
            {"raw_entry",value(b.raw_entry)},{"registry_generation",value(b.registry_generation)},
            {"object",value(b.buffer.object)},{"layout",value(b.buffer.layout)},
            {"size",value(b.buffer.size)},{"conversion_count",value(b.buffer.conversion_count)},
            {"matches_view_own",b.matches_view_own},{"entry_consistent",b.entry_consistent},{"complete",b.complete}});
    }
    return {{"type","mesh_binding"},{"stage","pre_vertex_factory_dispatch"},
        {"site_rva",wuwa_mesh_binding::site_rva},{"sequence",r.sequence},{"tick_ms",r.tick_ms},
        {"thread",r.thread},{"eye_slot",r.eye},{"loss_before",r.loss_before},
        {"view",r.view.view},{"state",value(r.view.state)},{"family",value(r.view.family)},
        {"frame",value(r.view.frame)},{"pass",value(r.view.pass)},{"view_own_ub",value(r.view.own_ub)},
        {"scene",r.scene},{"shader",r.shader},{"code_index",value(r.code_index)},
        {"target_bits",value(r.target_bits)},{"shader_resource_owner",nullptr},{"shader_hash",nullptr},
        {"vf_type",r.vf_type},{"vf_name_pointer",value(r.vf_name)},{"vf_dispatch",r.dispatch},
        {"vf_parameters",r.vf_parameters},{"vertex_factory",r.vertex_factory},
        {"primitive",value(r.primitive)},{"mesh",value(r.mesh)},{"element",r.element},
        {"first_instance",value(r.first_instance)},{"element_flags",value(r.element_flags)},
        {"shader_element_data",value(r.shader_element_data)},{"bindings",value(r.bindings)},
        {"binding_layout",value(r.binding_layout)},{"binding_data",value(r.binding_data)},
        {"binding_mode",value(r.binding_mode)},{"tracking_enabled",value(r.tracking_enabled)},
        {"layout_count",value(r.layout_count)},{"name_count",value(r.name_count)},
        {"collection_count",value(r.collection_count)},{"resources",resources},
        {"bounds_valid",r.bounds_valid},{"metadata_complete",r.metadata_complete},{"anchors_consistent",r.anchors_consistent},
        {"record_complete",r.complete},{"gpu_binding_proven",false}};
}
inline Json eye_diff_region_json(const auto& r) {
    Json deltas = Json::array();
    for (uint32_t i = 0; i < r.count; ++i)
        deltas.push_back(Json::array({r.deltas[i].offset, r.deltas[i].first, r.deltas[i].second}));
    return {{"begin", r.begin}, {"end", r.end}, {"valid", r.valid}, {"compared", r.compared},
        {"unreadable", r.unreadable}, {"differing", r.differing}, {"truncated", r.truncated()},
        {"deltas", deltas}};
}
inline Json eye_diff_json(const wuwa_eye_diff::Sample& s) {
    return {{"type", "eye_pair_diff"}, {"tick_ms", s.id.tick_ms}, {"thread", s.id.thread},
        {"sequence", s.id.sequence},
        {"phase", s.id.phase == 1 ? "before_submissions" : "after_submissions"},
        {"frames", s.id.frames},
        {"frames_read_at", s.id.phase == 1 ? "before_first_submission" : "after_submissions"},
        {"views", s.views}, {"states", s.states},
        {"view_region", eye_diff_region_json(s.view)}, {"state_region", eye_diff_region_json(s.state)},
        {"view_verified_end", wuwa_eye_diff::view_verified_end},
        {"first_is_eye_slot", 0}, {"gpu_binding_proven", false}};
}
struct Record {
    uint64_t tick{}, sequence{};
    uint32_t thread{}, phase{}, frame_offset{};
    int eye{-1};
    std::array<wuwa_lod::View, 2> pair{};
    wuwa_lod::Binding binding{};
    wuwa_lod::Uniforms uniforms{};
};
struct Slot { Record data{}; std::atomic<bool> ready{}; };
struct Probe {
    std::array<Slot, capacity> slots{};
    std::atomic<uint32_t> next{}, calls{}, dropped{}, reads_failed{}, lock_misses{};
    std::array<std::atomic<uint32_t>, 2> per_eye{};
    wuwa_lod::ProducerSampler uniform_sampler{};
    std::atomic<uint32_t> uniform_rows{};
    std::atomic<uint32_t> uniform_throttled{}, uniform_context_overflow{}, uniform_context_lock_misses{}, uniform_context_invalid{};
    uint32_t drained{};
    std::atomic<uint64_t> until{}, sequence{};
    // Protected by callbacks; replaced only on the game thread under exclusive lock.
    std::array<uintptr_t, 2> states{};
    uint32_t frame_offset{};
    // Initialized after code verification, before either callback is enabled.
    uintptr_t image_base{};
    uint32_t image_size{};
    std::ofstream file;
    std::filesystem::path path;
    wuwa_view_ub_trace::Observer view_observer;
    wuwa_view_ub_trace::Ring<4096> view_ring;
    std::atomic<bool> view_requested{};
    std::atomic<uint64_t> view_lock_misses{};
    std::ofstream view_file;
    std::filesystem::path view_path;
    uint32_t view_drained{};
    wuwa_eye_diff::Ring<> eye_diff_ring;
    wuwa_eye_diff::Sampler eye_diff_sampler;
    wuwa_eye_diff::Sample eye_diff_scratch; // pair() only, under pair_guard
    std::atomic<uint64_t> eye_diff_dropped{};
    uint32_t eye_diff_drained{};
    // pair() outcomes per phase: 0 before, 1 after first submission, 2 after both.
    struct PairCounts { std::atomic<uint32_t> calls{}, lock_misses{}, invalid{}, rows{}; };
    std::array<PairCounts, 3> pair_counts{};
    // Serializes the deferred before-row, eye-diff sampler and scratch between
    // pair() callers once phases 3 and 2 no longer hold the lock exclusively.
    std::atomic<bool> pair_guard{};
    std::atomic<uint32_t> pair_guard_misses{};
    Record before_row{}; // phase-1 row, emitted once its pair's assigned frame is known
    wuwa_mesh_binding::Ring<> mesh_ring;
    std::atomic<bool> mesh_requested{};
    std::atomic<uint64_t> mesh_calls{},mesh_filtered{},mesh_invalid{},mesh_dropped{},mesh_lock_misses{};
    std::array<std::atomic<uint32_t>,2> mesh_per_eye{};
    std::ofstream mesh_file;
    std::filesystem::path mesh_path;
    uint32_t mesh_drained{};
    std::string error;

    void append(const Record& r) noexcept {
        const auto i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= capacity) { ++dropped; return; }
        slots[i].data = r;
        slots[i].ready.store(true, std::memory_order_release);
    }
    void drain() {
        while (drained < capacity && slots[drained].ready.load(std::memory_order_acquire)) {
            const auto& r = slots[drained++].data;
            Json j{{"tick_ms", r.tick}, {"thread", r.thread}};
            if (r.phase == 4) {
                const auto& u = r.uniforms;
                j["type"] = "uniforms"; j["eye_slot"] = r.eye; j["view"] = view_json(u.view);
                j["game_time"] = value(u.game_time); j["real_time"] = value(u.real_time);
                j["delta_time"] = value(u.delta_time);
                j["previous_game_time"] = value(u.previous_game_time);
                j["previous_real_time"] = value(u.previous_real_time);
                j["frame_number"] = value(u.frame_number); j["state_frame_index"] = value(u.state_frame_index);
                j["matrix_block_0_600"] = value(u.matrix_block_0_600);
                j["producer_context"] = producer_json(u.producer);
            } else if (r.phase) {
                j["type"] = "pair"; j["phase"] = r.phase == 1 ? "before_submissions" :
                    r.phase == 3 ? "after_first_submission" : "after_submissions";
                j["sequence"] = r.sequence; j["frame_offset"] = r.frame_offset;
                j["views"] = Json::array({view_json(r.pair[0]), view_json(r.pair[1])});
            } else {
                const auto& b = r.binding;
                j["type"] = "binding"; j["eye_slot"] = r.eye; j["view"] = view_json(b.view);
                j["user_data"] = b.user; j["element"] = value(b.element);
                j["first_instance"] = value(b.first_instance); j["flags"] = value(b.flags);
                j["lod_branch"] = b.lod_branch;
                j["cuts"] = Json::array({value(b.cuts[0]), value(b.cuts[1])});
                j["origins"] = Json::array({value(b.origins[0]), value(b.origins[1])});
            }
            file << j.dump() << '\n';
            if (!file) throw std::runtime_error("LOD trace write failed");
        }
        wuwa_eye_diff::Sample eye_diff{};
        while (eye_diff_ring.pop(eye_diff)) {
            file << eye_diff_json(eye_diff).dump() << '\n';
            if (!file) throw std::runtime_error("Eye pair diff trace write failed");
            ++eye_diff_drained;
        }
        if (file.is_open()) {
            file.flush();
            if (!file) throw std::runtime_error("LOD trace flush failed");
        }
        wuwa_view_ub_trace::Record resource{};
        while (view_ring.pop(resource)) {
            view_file << view_ub_json(resource).dump() << '\n';
            if (!view_file) throw std::runtime_error("View uniform resource trace write failed");
            ++view_drained;
        }
        if (view_file.is_open()) {
            view_file.flush();
            if (!view_file) throw std::runtime_error("View uniform resource trace flush failed");
        }
        wuwa_mesh_binding::Record mesh{};
        while(mesh_ring.pop(mesh)) {
            mesh_file<<mesh_binding_json(mesh).dump()<<'\n';
            if(!mesh_file)throw std::runtime_error("Mesh binding trace write failed");
            ++mesh_drained;
        }
        if(mesh_file.is_open()) {
            mesh_file.flush();
            if(!mesh_file)throw std::runtime_error("Mesh binding trace flush failed");
        }
    }
};
// Retained after hook installation so late callbacks and assembly return stubs
// can never enter freed storage. Explicit shutdown disables observation/hooks.
inline Probe* owner{};

inline void mesh_binding(safetyhook::Context& c) noexcept {
    if(!armed.load(std::memory_order_relaxed))return;
    const auto last_error=GetLastError();
    if(TryAcquireSRWLockShared(&callbacks)) {
        auto* p=owner;
        try {
            const auto now=GetTickCount64();
            if(p && p->mesh_requested.load() && now<p->until.load()) {
                const auto call=p->mesh_calls.fetch_add(1)+1;
                uintptr_t state{},family{};uint32_t frame{};
                if(p->frame_offset==0x64 && memory::read_field(c.r9,8,state) && state &&
                    memory::read(c.r9,family) && memory::read_field(family,0x64,frame)) {
                    const int eye=state==p->states[0]?0:state==p->states[1]?1:-1;
                    if(eye>=0 && frame%30<3) {
                        if(p->mesh_per_eye[eye].fetch_add(1)<wuwa_mesh_binding::capacity/2) {
                            auto r=wuwa_mesh_binding::snapshot({c.rsp,c.rcx,c.rdx,c.r8,c.r9,c.r10,c.r13,c.r14,c.rdi,c.rsi},
                                p->frame_offset,p->image_base,[](uintptr_t at,auto& out){return memory::read(at,out);});
                            r.eye=eye;r.tick_ms=now;r.thread=GetCurrentThreadId();r.sequence=call;
                            if(!r.view.state.valid || r.view.state.value!=state ||
                                !r.view.family.valid || r.view.family.value!=family ||
                                !r.view.frame.valid || r.view.frame.value!=frame) {
                                r.eye=-1;r.complete=false;r.anchors_consistent=false;
                            }
                            r.loss_before=p->mesh_dropped.load()+p->mesh_lock_misses.load();
                            if(!r.complete)++p->mesh_invalid;
                            if(!p->mesh_ring.append(r))++p->mesh_dropped;
                        }else ++p->mesh_dropped;
                    }else ++p->mesh_filtered;
                }else ++p->mesh_invalid;
            }
        }catch(...){if(p)++p->mesh_invalid;}
        ReleaseSRWLockShared(&callbacks);
    }else if(owner && owner->mesh_requested.load())++owner->mesh_lock_misses;
    SetLastError(last_error);
}

template<wuwa_view_ub_trace::Stage Stage>
inline void view_resource(safetyhook::Context& c) noexcept {
    if (!armed.load(std::memory_order_relaxed)) return;
    const auto last_error=GetLastError();
    if (TryAcquireSRWLockShared(&callbacks)) {
        auto* p=owner;
        const auto now=GetTickCount64();
        if (p && p->view_requested.load(std::memory_order_relaxed) && now<p->until.load()) {
            p->view_observer.capture(Stage,c,p->frame_offset,p->states,p->image_base,p->image_size,
                now,GetCurrentThreadId(),[](uintptr_t at,auto& out){return memory::read(at,out);},
                [p](uintptr_t at){return memory::executable_site(at,1,reinterpret_cast<HMODULE>(p->image_base));},
                [p](const wuwa_view_ub_trace::Record& r){return p->view_ring.append(r);});
        }
        ReleaseSRWLockShared(&callbacks);
    } else if (owner && owner->view_requested.load()) ++owner->view_lock_misses;
    SetLastError(last_error);
}

inline void capture(safetyhook::Context& c) noexcept {
    if (!armed.load(std::memory_order_relaxed)) return;
    const auto last_error = GetLastError();
    if (TryAcquireSRWLockShared(&callbacks)) {
        auto* p = owner;
        try {
            const auto now = GetTickCount64();
            if (p && now < p->until.load()) {
                ++p->calls;
                uintptr_t view{}, state{}, family{}; uint32_t frame{};
                if (c.rbp >= 0x80 && p->frame_offset == 0x64 && memory::read(c.rbp - 0x70, view)
                    && memory::read_field(view, 8, state) && memory::read(view, family)
                    && memory::read_field(family, 0x64, frame) && state) {
                    const int eye = state == p->states[0] ? 0 : state == p->states[1] ? 1 : -1;
                    // Three consecutive family-frame numbers per 30; no claim
                    // of exhaustive draw coverage. Keep separate eye budgets.
                    if (eye >= 0 && frame % 30 < 3) {
                        if (p->per_eye[eye].fetch_add(1) < 3500) {
                            Record r{}; r.tick = now; r.thread = GetCurrentThreadId(); r.eye = eye;
                            r.binding = wuwa_lod::binding(c.rbp, c.r14, p->frame_offset,
                                [](uintptr_t at, auto& out) { return memory::read(at, out); });
                            p->append(r);
                        } else ++p->dropped;
                    }
                } else ++p->reads_failed;
                // Reaching the binding budget must not end the independent
                // time samples before they can show progression over seconds.
                if (p->next.load() >= capacity) armed = false;
            }
        } catch (...) { if (p) ++p->reads_failed; armed.store(false); }
        ReleaseSRWLockShared(&callbacks);
    } else if (owner) ++owner->lock_misses;
    SetLastError(last_error);
}

// Common uniform producer, after the time/frame stores and before its
// epilogue/tailcall. Independent of whether a reflection proxy/texture exists.
// R14=view, RBX=CPU constants, RDI=current matrices, RSI=previous matrices.
// The tailcalled 24adc060 still writes some matrix/rectangle fields afterward.
inline void uniform(safetyhook::Context& c) noexcept {
    if (!armed.load(std::memory_order_relaxed)) return;
    const auto last_error = GetLastError();
    if (TryAcquireSRWLockShared(&callbacks)) {
        auto* p = owner;
        try {
            const auto now = GetTickCount64();
            uintptr_t state{};
            if (p && now < p->until.load() && p->frame_offset == 0x64 &&
                memory::read_field(c.r14, 8, state) && state) {
                const int eye = state == p->states[0] ? 0 : state == p->states[1] ? 1 : -1;
                if (eye >= 0) {
                    const auto context = wuwa_lod::producer_context(c.r14, c.rbx, c.rdi, c.rsi, c.rbp,
                        p->image_base, p->image_size,
                        [](uintptr_t at, auto& out) { return memory::read(at, out); },
                        [p](uintptr_t at) { return memory::executable_site(at, 1, reinterpret_cast<HMODULE>(p->image_base)); });
                    const auto sample = p->uniform_sampler.take(size_t(eye), context, now);
                    if (sample == wuwa_lod::SampleResult::accepted) {
                        Record r{}; r.tick = now; r.thread = GetCurrentThreadId(); r.eye = eye; r.phase = 4;
                        r.uniforms = wuwa_lod::uniforms(c.r14, c.rbx, p->frame_offset,
                            [](uintptr_t at, auto& out) { return memory::read(at, out); }, false);
                        r.uniforms.producer = context;
                        p->append(r); ++p->uniform_rows;
                    } else if (sample == wuwa_lod::SampleResult::throttled) ++p->uniform_throttled;
                    else if (sample == wuwa_lod::SampleResult::overflow) ++p->uniform_context_overflow;
                    else if (sample == wuwa_lod::SampleResult::busy) ++p->uniform_context_lock_misses;
                    else ++p->uniform_context_invalid;
                }
            }
        } catch (...) { if (p) ++p->reads_failed; armed = false; }
        ReleaseSRWLockShared(&callbacks);
    } else if (owner) ++owner->lock_misses;
    SetLastError(last_error);
}

inline void install() {
    if (retired) throw std::runtime_error("LOD trace is shut down");
    if (installed) return;
    constexpr std::array<wuwa_code_compatibility::Range, 4> ranges{{
        {0x247c0240, 2858, 0x3a8267143c763b3cULL},
        {0x24aca750, 108, 0x76401d3e4ea2cf1aULL},
        {0x24ac5150, 55, 0xb6a4b49b98b88d6dULL},
        {0x24ada230, 7113, 0x14e741a3d369e4eaULL}}};
    try {
        const auto base = wuwa_code_check::verify("Read-only instanced LOD inputs", ranges);
        if (!owner) owner = new Probe;
        IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
        memory::require(memory::read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew >= sizeof(dos) && dos.e_lfanew <= 0x100000 &&
            memory::read_field(base, dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
            (nt.OptionalHeader.SizeOfImage == wuwa_code_compatibility::image_sizes[0] ||
             nt.OptionalHeader.SizeOfImage == wuwa_code_compatibility::image_sizes[1]),
            "Cannot read verified LOD producer module extent");
        owner->image_base = base; owner->image_size = nt.OptionalHeader.SizeOfImage;
        memory::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&capture), &retained_backend) != 0, "Cannot retain LOD observation backend");
        auto result = safetyhook::MidHook::create(reinterpret_cast<void*>(base + site_rva), capture,
            safetyhook::MidHook::StartDisabled);
        memory::require(result.has_value(), "Cannot create LOD observation hook");
        memory::require(wuwa_lod_hook_spans::valid(site_rva,result->original_bytes()),
            "LOD observation detour differs from the verified instruction span");
        hook = new safetyhook::MidHook(std::move(*result));
        auto time_result = safetyhook::MidHook::create(reinterpret_cast<void*>(base + uniform_site_rva), uniform,
            safetyhook::MidHook::StartDisabled);
        memory::require(time_result.has_value(), "Cannot create uniform time observation hook");
        memory::require(wuwa_lod_hook_spans::valid(uniform_site_rva,time_result->original_bytes()),
            "Uniform time observation detour differs from the verified instruction span");
        uniform_hook = new safetyhook::MidHook(std::move(*time_result));
        memory::require(hook->enable().has_value(), "Cannot enable LOD observation hook");
        memory::require(uniform_hook->enable().has_value(), "Cannot enable uniform time observation hook");
        installed = true;
    } catch (...) {
        retired = true;
        if (hook) { try { (void)hook->disable(); } catch (...) {} }
        if (uniform_hook) { try { (void)uniform_hook->disable(); } catch (...) {} }
        throw;
    }
}

// Optional hooks are installed only for an explicit resource trace. A failed
// installation never retries or disables the ordinary LOD/time observation.
inline uintptr_t verify_view_ub_originals() {
    if(verified_view_ub_base) {
        memory::require(owner && owner->image_base==verified_view_ub_base,"Verified view UB module identity changed");
        return verified_view_ub_base;
    }
    const auto base=wuwa_code_check::verify("Read-only final view uniform resources",wuwa_view_ub_trace::code_ranges);
    memory::require(owner && owner->image_base==base,"View uniform module differs from LOD module");
    verified_view_ub_base=base;
    return base;
}
inline void install_view_hooks() {
    if (view_hooks_installed) return;
    if (view_hooks_failed || retired) throw std::runtime_error("View uniform observation unavailable until restart");
    using namespace wuwa_view_ub_trace;
    try {
        const auto base=verify_view_ub_originals();
        constexpr std::array<void(*)(safetyhook::Context&),3> callbacks_at_sites{
            view_resource<Stage::cache_decision>,
            view_resource<Stage::shared_update_returned_before_cache_store>,
            view_resource<Stage::own_update_returned>};
        for (size_t i=0;i<view_hooks.size();++i) {
            auto result=safetyhook::MidHook::create(reinterpret_cast<void*>(base+site_rvas[i]),
                callbacks_at_sites[i],safetyhook::MidHook::StartDisabled);
            memory::require(result.has_value(),"Cannot create view uniform observation hook");
            memory::require(wuwa_lod_hook_spans::valid(site_rvas[i],result->original_bytes()),
                "View uniform observation detour differs from the verified instruction span");
            view_hooks[i]=new safetyhook::MidHook(std::move(*result));
        }
        for (auto* item:view_hooks) memory::require(item->enable().has_value(),"Cannot enable view uniform observation hook");
        view_hooks_installed=true;
    } catch (...) {
        view_hooks_failed=true;
        for (auto* item:view_hooks) if(item) {try {(void)item->disable();} catch (...) {}}
        throw;
    }
}
inline void install_mesh_hook() {
    if(mesh_hook_installed)return;
    if(mesh_hook_failed || retired)throw std::runtime_error("Mesh binding observation unavailable until restart");
    try {
        const auto base=wuwa_code_check::verify("Read-only mesh binding construction",wuwa_mesh_binding::code_ranges);
        memory::require(owner && owner->image_base==base,"Mesh binding module differs from LOD module");
        const auto ub_base=verify_view_ub_originals();
        memory::require(ub_base==base,"Mesh UB module differs from binding module");
        auto result=safetyhook::MidHook::create(reinterpret_cast<void*>(base+wuwa_mesh_binding::site_rva),
            mesh_binding,safetyhook::MidHook::StartDisabled);
        memory::require(result.has_value(),"Cannot create mesh binding observation hook");
        // Reject any larger detour before changing executable code. The next
        // native branch destination must remain reachable without our hook.
        memory::require(wuwa_mesh_binding::valid_hook_span(result->original_bytes()),
            "Mesh observation detour overlaps native control flow");
        mesh_hook=new safetyhook::MidHook(std::move(*result));
        memory::require(mesh_hook->enable().has_value(),"Cannot enable mesh binding observation hook");
        mesh_hook_installed=true;
    }catch(...){mesh_hook_failed=true;if(mesh_hook){try{(void)mesh_hook->disable();}catch(...){}}throw;}
}

// Called around the existing synchronous NSF submissions, in this order:
// phase 1 before the first submission (no sequence; issues the token and
// publishes the two eye states the render-thread callbacks match against),
// phase 3 after the first submission and phase 2 after both, each carrying
// phase 1's token. Render records carry the actual family frame. Matching CPU
// family frames is not proof of a shared GPU/compositor frame.
//
// Two lifecycle facts from the 28 Sep far capture shape this function:
// - Before the first submission the family frame is not yet the frame the
//   renderer assigns, so a phase-1 row is held back and emitted with the first
//   later snapshot of the same sequence whose frame is on the row interval, and
//   eye-diff sampling is keyed to the sequence, not the frame.
// - Only phase 1 writes state the callbacks read, so only phase 1 takes the
//   callback lock exclusively. Phases 3 and 2 take it shared and verify the
//   published states instead: after the submissions the render-thread
//   callbacks hold the shared lock almost continuously, and an exclusive try at
//   phase 2 produced a row for only 2 of 64 on-interval pairs.
inline uint64_t pair(const void* family, const void* first, const void* second,
                     uint32_t frame_offset, uint64_t sequence = 0, uint32_t phase = 0) noexcept {
    if (!armed.load(std::memory_order_relaxed)) return 0;
    const auto last_error = GetLastError();
    const bool publish = !sequence;
    const uint32_t kind = publish ? 1 : phase == 3 ? 3 : 2;
    const size_t slot = kind == 1 ? 0 : kind == 3 ? 1 : 2;
    uint64_t token{};
    if (publish ? TryAcquireSRWLockExclusive(&callbacks) : TryAcquireSRWLockShared(&callbacks)) {
        auto* p = owner;
        const auto now = GetTickCount64();
        if (p && now < p->until.load() && first && second && first != second && frame_offset == 0x64) {
            auto& counts = p->pair_counts[slot];
            ++counts.calls;
            Record r{}; r.tick = now; r.thread = GetCurrentThreadId(); r.frame_offset = frame_offset;
            auto read = [](uintptr_t at, auto& out) { return memory::read(at, out); };
            r.pair = {wuwa_lod::view(uintptr_t(first), frame_offset, read), wuwa_lod::view(uintptr_t(second), frame_offset, read)};
            const auto& a = r.pair[0]; const auto& b = r.pair[1];
            const bool valid = a.family.valid && b.family.valid && a.family.value == uintptr_t(family) && b.family.value == uintptr_t(family)
                && a.state.valid && b.state.valid && a.state.value && b.state.value && a.state.value != b.state.value
                && a.frame.valid && b.frame.valid;
            const bool published = publish || (p->frame_offset == frame_offset &&
                p->states[0] == a.state.value && p->states[1] == b.state.value);
            if (valid && published) {
                if (publish) { p->states = {a.state.value, b.state.value}; p->frame_offset = frame_offset; }
                token = sequence ? sequence : p->sequence.fetch_add(1) + 1;
                r.sequence = token; r.phase = kind;
                if (!p->pair_guard.exchange(true, std::memory_order_acquire)) {
                    if (publish) p->before_row = r;
                    else if (a.frame.value % 30 < 4 || b.frame.value % 30 < 4) {
                        if (p->before_row.sequence == token) {
                            p->append(p->before_row); ++p->pair_counts[0].rows;
                            p->before_row.sequence = 0;
                        }
                        p->append(r); ++counts.rows;
                    }
                    if (p->eye_diff_sampler.take(kind, token, p->eye_diff_ring.free_slots())) {
                        // Same lease as the pair record above: raw guarded reads
                        // of the two main views and their view states.
                        wuwa_eye_diff::sample(p->eye_diff_scratch,
                            {now, token, r.thread, kind, {a.frame.value, b.frame.value}},
                            a.address, b.address, a.state.value, b.state.value, read);
                        if (!p->eye_diff_ring.append(p->eye_diff_scratch)) ++p->eye_diff_dropped;
                    }
                    p->pair_guard.store(false, std::memory_order_release);
                } else ++p->pair_guard_misses;
            } else { ++p->reads_failed; ++counts.invalid; }
        }
        if (publish) ReleaseSRWLockExclusive(&callbacks); else ReleaseSRWLockShared(&callbacks);
    } else if (owner) { ++owner->lock_misses; ++owner->pair_counts[slot].lock_misses; }
    SetLastError(last_error);
    return token;
}

inline Json status_locked() {
    if (!owner) return {{"installed", false}, {"active", false}, {"read_only", true}, {"view_uniforms_supported",true},{"mesh_bindings_supported",true},
        {"mesh_binding_hook_revision",wuwa_mesh_binding::hook_revision}};
    auto* p = owner; const auto now = GetTickCount64(); const auto until = p->until.load();
    if (until <= now) armed = false;
    try { p->drain(); } catch (const std::exception& e) { p->error = e.what(); armed = false; p->until = 0; }
    const auto path = p->path.u8string();
    const auto view_path=p->view_path.u8string();
    const auto counts=p->view_observer.counts();
    const auto mesh_path=p->mesh_path.u8string();
    const auto pair_json=[](const Probe::PairCounts& c) -> Json {
        return {{"calls",c.calls.load()},{"lock_misses",c.lock_misses.load()},
            {"invalid",c.invalid.load()},{"rows",c.rows.load()}};
    };
    const auto& sampled=p->eye_diff_sampler.counts();
    return {{"installed", installed}, {"active", armed.load() && now < p->until.load()}, {"read_only", true},
        {"remaining_ms", until > now ? until - now : 0}, {"path", std::string(path.begin(), path.end())},
        {"written", p->drained}, {"calls", p->calls.load()}, {"dropped", p->dropped.load()},
        {"uniform_rows", p->uniform_rows.load()},
        {"uniform_throttled", p->uniform_throttled.load()}, {"uniform_context_overflow", p->uniform_context_overflow.load()},
        {"uniform_context_lock_misses", p->uniform_context_lock_misses.load()}, {"uniform_context_invalid", p->uniform_context_invalid.load()},
        {"read_failures", p->reads_failed.load()}, {"lock_misses", p->lock_misses.load()}, {"error", p->error},
        {"view_uniforms_supported",true},
        {"mesh_bindings_supported",true},
        {"mesh_binding_hook_revision",wuwa_mesh_binding::hook_revision},
        {"eye_pair_diff_supported",true},
        {"eye_pair_diff",{{"written",p->eye_diff_drained},{"capacity",wuwa_eye_diff::ring_capacity},
            {"dropped",p->eye_diff_dropped.load()},{"truncated",p->eye_diff_ring.truncated()},
            {"view_end",wuwa_eye_diff::view_end},{"state_end",wuwa_eye_diff::state_end},
            {"schedule","validated pair sequence"},{"interval_pairs",wuwa_eye_diff::interval_pairs},
            {"before_seen",sampled.before_seen.load()},{"before_taken",sampled.before_taken.load()},
            {"after_taken",sampled.after_taken.load()},{"ring_full",sampled.ring_full.load()},
            {"orphaned",sampled.orphaned.load()}}},
        {"pairs",{{"before_submissions",pair_json(p->pair_counts[0])},
            {"after_first_submission",pair_json(p->pair_counts[1])},
            {"after_submissions",pair_json(p->pair_counts[2])},
            {"guard_misses",p->pair_guard_misses.load()},
            {"locking","before: exclusive (publishes eye states); after: shared (verifies them)"}}},
        {"mesh_bindings",{{"requested",p->mesh_requested.load()},{"installed",mesh_hook_installed},
            {"installation_failed",mesh_hook_failed},{"path",std::string(mesh_path.begin(),mesh_path.end())},
            {"written",p->mesh_drained},{"capacity",wuwa_mesh_binding::capacity},
            {"calls",p->mesh_calls.load()},{"filtered",p->mesh_filtered.load()},
            {"invalid",p->mesh_invalid.load()},{"dropped",p->mesh_dropped.load()},
            {"lock_misses",p->mesh_lock_misses.load()},
            {"truncated",p->mesh_ring.truncated() || p->mesh_dropped.load()>0},
            {"gpu_binding_proven",false},{"shader_resource_owner_proven",false}}},
        {"view_uniforms",{{"requested",p->view_requested.load()},
            {"installed",view_hooks_installed},{"installation_failed",view_hooks_failed},
            {"path",std::string(view_path.begin(),view_path.end())},{"written",p->view_drained},
            {"truncated",p->view_ring.truncated()},{"stopped",p->view_observer.stopped()},
            {"recorded",counts.recorded},{"filtered",counts.filtered},{"invalid",counts.invalid},
            {"busy",counts.busy},{"watch_full",counts.watch_full},{"sink_full",counts.sink_full},
            {"exceptions",counts.exceptions},{"unstable",counts.unstable},
            {"outer_lock_misses",p->view_lock_misses.load()},
            {"exhaustive_buffer_write_history",false},
            {"gpu_binding_proven",false}}}};
}
inline Json status() { const std::lock_guard lock{control}; return status_locked(); }
inline Json request(const std::filesystem::path& directory, int seconds, bool view_uniforms=false, bool mesh_bindings=false) {
    if (seconds < 0 || seconds > 30) throw std::runtime_error("LOD trace duration must be 0..30 seconds");
    const std::lock_guard lock{control};
    if (!seconds) {
        armed = false;
        AcquireSRWLockExclusive(&callbacks); if (owner) owner->until = 0; ReleaseSRWLockExclusive(&callbacks);
        return status_locked();
    }
    if (owner && armed.load() && owner->until.load() > GetTickCount64())
        throw std::runtime_error("A LOD trace is already active");
    install();
    armed = false;
    if (view_uniforms) install_view_hooks();
    if (mesh_bindings) install_mesh_hook();
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto folder = directory / "diagnostics"; std::filesystem::create_directories(folder);
    const auto path = folder / ("lod-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(stamp) + ".jsonl");
    memory::require(!std::filesystem::exists(path), "LOD trace path already exists");
    std::ofstream file(path, std::ios::binary);
    file << Json{{"type", "header"}, {"version", 1}, {"pid", GetCurrentProcessId()}, {"seconds", seconds},
        {"unix_ms", stamp}, {"read_only", true}, {"site_rva", site_rva}, {"capacity", capacity},
        {"frame_sampling", "family_frame modulo 30 < 3; 3500 binding records per eye maximum"},
        {"uniform_site_rva", uniform_site_rva},
        {"uniform_sampling", "CPU pre-tail constants at most 10 Hz per (eye, verified caller RVA, current-matrix source relation); eight fixed contexts per eye; state-frame index unavailable; no GPU readback"},
        {"uniform_snapshot_stage", "pre_tail_24adc060"},
        {"eye_pair_diff", "raw dword differences between the two main-eye views and their view states; one before/after-submissions pair every 60 validated NSF pairs (probe sequence, not family frame: the frame read before the first submission is not the frame the renderer assigns); read-only guarded reads, 128 samples maximum, deltas ascending by offset and capped per region; addresses are historical identifiers, never dereference them; view-state extent unknown"},
        {"coverage", "instanced vertex-factory bindings only; cached draws, static mesh CPU LOD and impostors may not pass here"},
        {"identity", "eye slots from validated main-view states; family frames are CPU identity, not GPU frame proof"}}.dump() << '\n';
    memory::require(file.good(), "Cannot open LOD trace");
    std::ofstream view_file;
    std::filesystem::path view_path;
    if (view_uniforms) {
        view_path=folder/("view-ub-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(stamp)+".jsonl");
        memory::require(!std::filesystem::exists(view_path),"View uniform trace path already exists");
        view_file.open(view_path,std::ios::binary);
        view_file<<Json{{"type","header"},{"version",1},{"pid",GetCurrentProcessId()},
            {"seconds",seconds},{"unix_ms",stamp},{"read_only",true},{"capacity",4096},
            {"site_rvas",wuwa_view_ub_trace::site_rvas},
            {"coverage","Consecutive accepted events at three CPU/RHI-return sites until capacity; auxiliary events retained for watched holders. Other reset/upload helpers are not intercepted; secondary shared-buffer source may differ. record_complete describes local field reads only, not all buffer writes or GPU draw evidence."}}.dump()<<'\n';
        memory::require(view_file.good(),"Cannot open view uniform resource trace");
    }
    std::ofstream mesh_file;std::filesystem::path mesh_path;
    if(mesh_bindings) {
        mesh_path=folder/("mesh-bindings-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(stamp)+".jsonl");
        memory::require(!std::filesystem::exists(mesh_path),"Mesh binding trace path already exists");
        mesh_file.open(mesh_path,std::ios::binary);
        mesh_file<<Json{{"type","header"},{"version",1},{"pid",GetCurrentProcessId()},
            {"seconds",seconds},{"unix_ms",stamp},{"read_only",true},{"site_rva",wuwa_mesh_binding::site_rva},
            {"hook_revision",wuwa_mesh_binding::hook_revision},
            {"capacity",wuwa_mesh_binding::capacity},{"max_resources",wuwa_mesh_binding::max_resources},
            {"frame_sampling","main-state family frame modulo 30 < 3; 1024 records per eye maximum"},
            {"coverage","CPU generic mesh binding before VF dispatch; includes prebuilt material bindings, not final VF additions or GPU draw/PSO proof. Cached commands may bypass this callback. Unknown owner/hash and unmatched slots remain unknown; no shader bytecode or UB payload readback."}}.dump()<<'\n';
        memory::require(mesh_file.good(),"Cannot open mesh binding trace");
    }
    AcquireSRWLockExclusive(&callbacks);
    try {
        owner->drain(); owner->file = std::move(file); owner->path = path; owner->error.clear();
        owner->view_file=std::move(view_file); owner->view_path=view_path;
        memory::require(owner->view_observer.reset(),"Cannot reset view uniform observer");
        owner->view_ring.reset(); owner->view_drained=0; owner->view_lock_misses=0;
        owner->eye_diff_ring.reset(); owner->eye_diff_sampler.reset();
        owner->eye_diff_drained=0; owner->eye_diff_dropped=0;
        for (auto& c : owner->pair_counts) { c.calls=0; c.lock_misses=0; c.invalid=0; c.rows=0; }
        owner->pair_guard_misses=0; owner->before_row.sequence=0;
        owner->view_requested=view_uniforms;
        owner->mesh_file=std::move(mesh_file);owner->mesh_path=mesh_path;owner->mesh_requested=mesh_bindings;
        owner->mesh_ring.reset();owner->mesh_drained=0;owner->mesh_calls=0;owner->mesh_filtered=0;
        owner->mesh_invalid=0;owner->mesh_dropped=0;owner->mesh_lock_misses=0;
        for(auto& count:owner->mesh_per_eye)count=0;
        owner->states = {}; owner->frame_offset = 0; owner->next = 0; owner->drained = 0;
        owner->calls = 0; owner->dropped = 0; owner->reads_failed = 0; owner->lock_misses = 0;
        owner->sequence = 0; for (auto& n : owner->per_eye) n = 0;
        owner->uniform_rows = 0; owner->uniform_sampler.reset();
        owner->uniform_throttled = 0; owner->uniform_context_overflow = 0;
        owner->uniform_context_lock_misses = 0; owner->uniform_context_invalid = 0;
        for (auto& slot : owner->slots) slot.ready = false;
        owner->until = GetTickCount64() + seconds * 1000; armed = true;
    } catch (...) { ReleaseSRWLockExclusive(&callbacks); throw; }
    ReleaseSRWLockExclusive(&callbacks);
    return status_locked();
}
inline void shutdown(bool process_exiting) noexcept {
    armed = false;
    if (process_exiting) return;
    const std::lock_guard lock{control}; retired = true;
    // Hook suspension must happen outside the callback SRW lock.
    if (hook) { try { (void)hook->disable(); } catch (...) {} }
    if (uniform_hook) { try { (void)uniform_hook->disable(); } catch (...) {} }
    for (auto* item:view_hooks) if(item) {try {(void)item->disable();} catch (...) {}}
    if(mesh_hook){try{(void)mesh_hook->disable();}catch(...){}}
    AcquireSRWLockExclusive(&callbacks); if (owner) owner->until = 0; ReleaseSRWLockExclusive(&callbacks);
    try { if (owner) owner->drain(); } catch (...) {}
}
} // namespace wuwa_lod_probe
