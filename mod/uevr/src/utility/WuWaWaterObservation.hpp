#pragma once
#include "WuWaCodeCheck.hpp"
#include "WuWaStereoParameters.hpp"
#include "WuWaWaterStereoLoop.hpp"
#include <mutex>

// The observer and optional per-eye tail replay share one installation: code
// verification must run before either one patches the setup function.
namespace wuwa_water_observation {
namespace memory = wuwa_lgui_probe::detail;
using Json = nlohmann::json;
inline constexpr std::array<wuwa_code_compatibility::Range, 4> functions{{
    {0x230e1550, 1181, 0x2a9d4259dc56bce5ULL}, // selects Views[0]
    {0x230d0c80, 1493, 0x72fcf9387627916cULL}, // iterates the view array
    {0x230d2820, 314, 0x1e52d2dfe93ac65fULL}, // deferred draw uses the captured view/mesh list
    {0x230c41e0, 247, 0x752dbca52907198eULL}, // copies renderer/view/mesh capture into the pass
}};
struct Eye {
    int32_t pass{}, draw_commands{};
    wuwa_planar_probe::Rect rect{};
    uint8_t instanced{}, multiview{}, eligibility_flags{};
    bool valid{};
};
struct Sample {
    uint64_t tick{};
    int32_t view_count{};
    bool count_valid{}, serial_pair{};
    std::array<Eye, 2> eyes{};
};
inline std::mutex control, samples_mutex;
inline std::atomic<bool> enabled{}, faulted{};
inline std::array<std::atomic<uint64_t>, 2> calls{}, sampled{}, next_sample{};
inline std::array<Sample, 2> samples{};
inline std::array<safetyhook::MidHook*, 6> hooks{};
inline HMODULE retained_backend{};
inline bool attempted{};
inline std::string error;

inline void observe(size_t route, uintptr_t renderer) noexcept {
    if (!enabled.load(std::memory_order_relaxed) || faulted.load()) return;
    const auto last_error = GetLastError();
    try {
        ++calls[route];
        const auto now = GetTickCount64();
        auto deadline = next_sample[route].load(std::memory_order_relaxed);
        if (now >= deadline && next_sample[route].compare_exchange_strong(deadline, now + 1000)) {
            const auto pair = wuwa_stereo_parameters::renderer_pair(renderer,
                [](uintptr_t at, auto& value) { return memory::read(at, value); });
            Sample sample{};
            sample.tick = now;
            sample.view_count = pair.view_count.value;
            sample.count_valid = pair.view_count.valid;
            sample.serial_pair = wuwa_stereo_parameters::serial_pair(pair);
            for (size_t i = 0; i < 2; ++i) {
                // renderer_pair only supplies addresses for a bounded two-view
                // array. Unknown counts are recorded without dereferencing it.
                if (!pair.views[i].valid || !pair.views[i].value) continue;
                auto& eye = sample.eyes[i];
                eye.pass = pair.passes[i].value;
                eye.rect = pair.view_rects[i].value;
                eye.instanced = pair.instanced[i].value;
                eye.multiview = pair.multiview[i].value;
                eye.valid = pair.passes[i].valid && pair.view_rects[i].valid &&
                    pair.instanced[i].valid && pair.multiview[i].valid &&
                    memory::read_field(pair.views[i].value, 0xadec, eye.draw_commands) &&
                    memory::read_field(pair.views[i].value, 0xd964, eye.eligibility_flags);
            }
            // The renderer never waits for the status writer.
            std::unique_lock lock{samples_mutex, std::try_to_lock};
            if (lock.owns_lock()) { samples[route] = sample; ++sampled[route]; }
        }
    } catch (...) { faulted = true; }
    SetLastError(last_error);
}
inline void setup(safetyhook::Context& c) noexcept { observe(0, c.rcx); }
inline void resolve(safetyhook::Context& c) noexcept { observe(1, c.rcx); }
inline void configure(bool value, bool correct=false) noexcept {
    if (!value && !correct) { enabled = false; wuwa_water_stereo::enabled=false; return; }
    if (faulted.load()) return;
    if (enabled.load()==value && wuwa_water_stereo::enabled.load()==correct) return;
    const std::lock_guard lock{control};
    if (attempted) { enabled=value; wuwa_water_stereo::enabled=correct; return; }
    attempted = true;
    try {
        const auto base = wuwa_code_check::verify("Kuro water pass observation", functions);
        wuwa_water_stereo::base=base;
        memory::require(wuwa_water_stereo::capture_resume_window(),"Cannot capture Kuro water resume code");
        uintptr_t execute{};
        memory::require(memory::read(base+0x276f9468+8,execute) && execute==base+0x230d2820,
            "Kuro water execute slot differs");
        memory::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&setup), &retained_backend) != 0, "Cannot retain water observer backend");
        const std::array<safetyhook::MidHookFn, 6> callbacks{setup, resolve,
            wuwa_water_stereo::begin,wuwa_water_stereo::load,wuwa_water_stereo::queued,wuwa_water_stereo::finish};
        const std::array<uintptr_t,6> sites{functions[0].rva,functions[1].rva,
            wuwa_water_stereo::begin_site,wuwa_water_stereo::load_site,wuwa_water_stereo::queued_site,wuwa_water_stereo::finish_site};
        for (size_t i = 0; i < hooks.size(); ++i) {
            auto hook = safetyhook::MidHook::create(reinterpret_cast<void*>(base + sites[i]),
                callbacks[i], safetyhook::MidHook::StartDisabled);
            memory::require(hook.has_value(), "Cannot create water pass observation hook");
            // Keep disabled stubs and the backend until process exit, just as
            // for the other renderer hooks, to avoid a late-return use-after-free.
            hooks[i] = new safetyhook::MidHook(std::move(*hook));
        }
        // Enable finish and binding guards before a new frame can arm.
        for (const size_t i:{5u,4u,3u,2u,1u,0u}) memory::require(hooks[i]->enable().has_value(), "Cannot enable water pass hook");
        memory::require(wuwa_water_stereo::resume_window_intact(),"Kuro water resume code was patched");
        enabled = value; wuwa_water_stereo::enabled=correct;
        spdlog::info("[WuWaWaterObservation] verified setup, resolve, constructor and deferred draw; stereo candidate {}",correct);
    } catch (const std::exception& e) { error = e.what(); faulted = true; }
    catch (...) { error = "Kuro water observation initialization failed"; faulted = true; }
    if (faulted.load()) {
        enabled = false;
        wuwa_water_stereo::enabled=false;
        for (auto* hook : hooks) if (hook) { try { (void)hook->disable(); } catch (...) {} }
        spdlog::error("[WuWaWaterObservation] {}", error);
    }
}
inline Json status() {
    const std::lock_guard control_lock{control};
    const std::lock_guard samples_lock{samples_mutex};
    Json result{{"enabled", enabled.load()}, {"faulted", faulted.load()}, {"error", error},
        {"observation_only", !wuwa_water_stereo::enabled.load()},{"stereo_pass",wuwa_water_stereo::status()}};
    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& sample = samples[i];
        Json eyes = Json::array();
        for (const auto& eye : sample.eyes) eyes.push_back({{"valid", eye.valid}, {"pass", eye.pass},
            {"rect", eye.rect}, {"draw_commands", eye.draw_commands}, {"instanced", eye.instanced},
            {"multiview", eye.multiview}, {"eligibility_flags", eye.eligibility_flags}});
        result[i ? "view_loop" : "first_view_setup"] = {{"calls", calls[i].load()},
            {"samples", sampled[i].load()}, {"sample_tick_ms", sample.tick},
            {"view_count_valid", sample.count_valid}, {"view_count", sample.view_count},
            {"serial_pair", sample.serial_pair}, {"eyes", eyes}};
    }
    return result;
}
inline void shutdown(bool process_exiting) noexcept {
    enabled = false;
    wuwa_water_stereo::enabled=false;
    // Keep pinned no-op hooks until process exit. A replay already inside the
    // second tail must still run its Load guard and finish callback.
    (void)process_exiting;
}
} // namespace wuwa_water_observation
