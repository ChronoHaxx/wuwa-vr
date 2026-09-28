#pragma once
#include "WuWaLguiProbe.hpp"
#include "WuWaStereoParameters.hpp"
#include "WuWaCodeCheck.hpp"
#include <mutex>
#include <vector>

namespace wuwa_translucency {
namespace memory=wuwa_lgui_probe::detail;
using Json=nlohmann::json;
struct Site { uintptr_t function, size, branch; uint64_t hash; };
inline constexpr std::array<Site,3> sites{{
    {0x23355020,436,0x23355067,0xc7402cc94a979f3fULL},
    {0x23360500,24697,0x23365862,0x51abfd207ddb1abbULL},
    {0x23368ff0,646,0x233690c4,0x40a97b06b9f17f62ULL}
}};
inline std::mutex control;
inline std::atomic<bool> enabled{}, faulted{};
inline std::atomic<uint64_t> calls{}, forced{}, skipped{}, already_full{};
inline bool attempted{};
inline HMODULE retained_backend{};
// Explicit lifetime: disabled assembly stubs and their owning module remain
// until process exit so a late return cannot enter freed executable storage.
inline std::array<safetyhook::MidHook*,3> hooks{};
inline std::string error;

inline uintptr_t verify() {
    std::array<wuwa_code_compatibility::Range, sites.size()> ranges{};
    for (size_t i = 0; i < sites.size(); ++i)
        ranges[i] = {uint32_t(sites[i].function), uint32_t(sites[i].size), sites[i].hash};
    return wuwa_code_check::verify("Translucent stereo materials", ranges);
}
inline void select_full_resolution(safetyhook::Context& context, uintptr_t renderer) noexcept {
    if (!enabled.load(std::memory_order_relaxed) || faulted.load()) return;
    const auto last_error=GetLastError();
    try {
        ++calls;
        // The original CMP already selects the full-resolution route. Leave
        // every register and flag exactly as the game supplied it in that case.
        if (!(context.rflags & 0x40)) { ++already_full; SetLastError(last_error); return; }
        const auto pair=wuwa_stereo_parameters::renderer_pair(renderer,
            [](uintptr_t at,auto& value) { return memory::read(at,value); });
        if (wuwa_stereo_parameters::serial_pair(pair)) {
            // Only the ZF consumed by the following verified JNE is changed.
            // This is equivalent to the game's FullRes=true branch locally;
            // its global Boolean CVar, other renderers and materials stay intact.
            context.rflags &= ~uintptr_t{0x40}; ++forced;
        } else ++skipped;
    } catch (...) { faulted.store(true); }
    SetLastError(last_error);
}
inline void first(safetyhook::Context& c) noexcept { select_full_resolution(c,c.rdi); }
inline void second(safetyhook::Context& c) noexcept { select_full_resolution(c,c.r15); }
inline void third(safetyhook::Context& c) noexcept { select_full_resolution(c,c.r15); }
inline void configure(bool value) noexcept {
    if (!value) { enabled.store(false); return; }
    if (enabled.load() || faulted.load()) return;
    const std::lock_guard lock{control};
    if (attempted) { if (hooks[0] && hooks[1] && hooks[2]) enabled=true; return; }
    attempted=true;
    try {
        const auto base=verify();
        memory::require(base!=0,"Translucency correction refused: game identity or branch code differs");
        memory::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&first),&retained_backend)!=0,"Cannot retain translucency hook backend");
        const std::array<safetyhook::MidHookFn,3> callbacks{first,second,third};
        for (size_t i=0;i<3;++i) {
            auto hook=safetyhook::MidHook::create(reinterpret_cast<void*>(base+sites[i].branch),callbacks[i],
                safetyhook::MidHook::StartDisabled);
            memory::require(hook.has_value(),"Cannot create the scoped translucency hook");
            hooks[i]=new safetyhook::MidHook(std::move(*hook));
        }
        for (auto* hook:hooks) memory::require(hook->enable().has_value(),"Cannot enable the scoped translucency hook");
        enabled=true;
        spdlog::info("[WuWaTranslucency] verified three full-resolution branches; serial-eye correction enabled");
    } catch (const std::exception& e) { error=e.what(); faulted=true; }
    catch (...) { error="Translucency hook initialization failed"; faulted=true; }
    if (faulted.load()) {
        enabled=false;
        for (auto* hook:hooks) if (hook) { try { (void)hook->disable(); } catch (...) {} }
        spdlog::error("[WuWaTranslucency] {}",error);
    }
}
inline Json status() {
    const std::lock_guard lock{control};
    return {{"enabled",enabled.load()},{"faulted",faulted.load()},{"calls",calls.load()},
        {"forced_full_resolution",forced.load()},{"already_full_resolution",already_full.load()},
        {"unsupported_views",skipped.load()},{"error",error}};
}
inline void shutdown(bool process_exiting) noexcept {
    enabled=false;
    if (process_exiting) return;
    const std::lock_guard lock{control};
    for (auto* hook:hooks) if (hook) { try { (void)hook->disable(); } catch (...) {} }
}
} // namespace wuwa_translucency
