#pragma once
#include "WuWaCodeCheck.hpp"
#include "WuWaReflectionProjection.hpp"
#include "WuWaReflectionHistory.hpp"
#include <mutex>

namespace wuwa_reflection_capture {
namespace memory = wuwa_lgui_probe::detail;
using Json = nlohmann::json;
inline std::mutex control;
inline std::atomic<bool> enabled{}, faulted{};
inline std::atomic<uint64_t> calls{}, applied{}, skipped{};
inline bool attempted{};
inline safetyhook::MidHook* hook{};
inline std::array<safetyhook::MidHook*,3> history_hooks{};
inline uintptr_t factory_caller{};
inline std::atomic<uint64_t> history_calls{}, history_selected{}, history_skipped{}, refresh_calls{}, refreshed{}, refresh_skipped{};
inline HMODULE retained_backend{};
inline std::string error;

class EyeScope {
public:
    EyeScope(const void* view, uint32_t ordinal, bool requested) : previous{wuwa_reflection_history::current} {
        auto& eye=wuwa_reflection_history::current;
        eye={};
        uintptr_t state{};
        if (requested && enabled.load() && !faulted.load() && ordinal<2 &&
            memory::read_field(uintptr_t(view),8,state) && state) eye={state,0,ordinal,true};
    }
    ~EyeScope() { wuwa_reflection_history::current=previous; }
    EyeScope(const EyeScope&)=delete;
    EyeScope& operator=(const EyeScope&)=delete;
private:
    wuwa_reflection_history::Eye previous;
};

// The output is a freshly produced 64-byte matrix on this callback's stack.
// Validate the whole writable span and original bytes before a single copy.
inline bool apply(const wuwa_reflection_projection::Plan& p) noexcept {
    const auto at = p.output;
    if (!at || at > UINTPTR_MAX-sizeof(p.before)) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
        (region.Protect & 0xff) != PAGE_READWRITE || at < uintptr_t(region.BaseAddress) ||
        at-uintptr_t(region.BaseAddress) > region.RegionSize ||
        sizeof(p.before) > region.RegionSize-(at-uintptr_t(region.BaseAddress))) return false;
    __try {
        if (std::memcmp(reinterpret_cast<const void*>(at),p.before.data(),sizeof(p.before)) != 0) return false;
        std::memcpy(reinterpret_cast<void*>(at),p.after.data(),sizeof(p.after));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

inline void callback(safetyhook::Context& c) noexcept {
    if (!enabled.load(std::memory_order_relaxed) || faulted.load()) return;
    const auto last_error = GetLastError();
    try {
        ++calls;
        auto& eye=wuwa_reflection_history::current;
        eye.component=0;
        // Verified prologue: rbp = current rsp+0x100, output = rbp+0x450.
        // rbx is a view-array byte offset; NSF has one view so it must be zero.
        if (c.rsp > UINTPTR_MAX-0x590 || c.rbp != c.rsp+0x100 || c.rbx != 0) { ++skipped; }
        else if (const auto p = wuwa_reflection_projection::plan(c.r12,c.rdi,c.r14,c.rbp+0x450,
            [](uintptr_t at,auto& out){return memory::read(at,out);})) {
            if (apply(*p)) {
                ++applied;
                if (eye.active && eye.main_state==p->state) eye.component=c.r14;
            }
            else faulted = true;
        } else ++skipped;
    } catch (...) { faulted = true; }
    SetLastError(last_error);
}

inline void select_history(safetyhook::Context& c) noexcept {
    if (!enabled.load(std::memory_order_relaxed) || faulted.load()) return;
    const auto last_error=GetLastError();
    try {
        ++history_calls;
        if (c.rcx==c.r12 && wuwa_reflection_history::accepts_factory(wuwa_reflection_history::current,
            c.rcx,c.rbp,c.rsp,factory_caller,c.rdx,
            [](uintptr_t at,auto& value){return memory::read(at,value);})) {
            // Change only the GetViewState argument, not the loop/view index.
            // The game's component grows/owns/frees its own state array.
            c.rdx=wuwa_reflection_history::current.ordinal;
            ++history_selected;
        } else ++history_skipped;
    } catch (...) { faulted=true; }
    SetLastError(last_error);
}
inline void refresh(safetyhook::Context& c,uintptr_t main,uintptr_t capture,uintptr_t proxy,uintptr_t plane) noexcept {
    if (!enabled.load(std::memory_order_relaxed) || faulted.load()) return;
    const auto last_error=GetLastError();
    try {
        ++refresh_calls;
        // Verified CMP count,1: only the equality case is missing from the
        // engine's existing multi-view mirror refresh. Preserve every other flag.
        if ((c.rflags & (0x40|0x80|0x800))==0x40 &&
            wuwa_reflection_history::needs_single_view_refresh(main,capture,proxy,plane,
                [](uintptr_t at,auto& value){return memory::read(at,value);})) {
            c.rflags &= ~uintptr_t{0x40};
            ++refreshed;
        } else ++refresh_skipped;
    } catch (...) { faulted=true; }
    SetLastError(last_error);
}
inline void refresh_deferred(safetyhook::Context& c) noexcept { refresh(c,c.r15,c.rsi,c.r12,c.r13); }
inline void refresh_mobile(safetyhook::Context& c) noexcept { refresh(c,c.r14,c.rbx,c.r13,c.r12); }

inline void configure(bool requested) noexcept {
    if (!requested) { enabled = false; return; }
    if (faulted.load()) return;
    if (enabled.load()) return;
    const std::lock_guard lock{control};
    if (attempted) {
        if (hook && history_hooks[0] && history_hooks[1] && history_hooks[2]) enabled = true;
        return;
    }
    attempted = true;
    try {
        // Complete archived producer and both projection-helper stages. No
        // unverified address/layout fallback after a game update.
        constexpr std::array<wuwa_code_compatibility::Range,9> ranges{{
            {0x3d85d10,8282,0x6380221725f168ccULL},
            {0x3f63640,236,0x4f10d39ebbb0f37aULL},
            {0x3f63730,27,0x0e63f3701c79635fULL},
            {0x3f89ac0,6334,0xe7be3fff761f3891ULL},
            {0x4fe58c0,293,0xe0c1800f6ded95c5ULL},
            {0x3d87d70,1285,0x7d05f5929d6e1c6dULL},
            {0x3d88280,2853,0x684f2b9cdf6ac940ULL},
            {0x54e8f00,188,0x3b3e235d5c144105ULL},
            {0x54e8fc0,5681,0xe81e275647c604a6ULL}}};
        const auto base = wuwa_code_check::verify("Custom reflection capture projection",ranges);
        factory_caller=base+0x3d874e3;
        memory::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&callback),&retained_backend) != 0,"Cannot retain reflection capture backend");
        auto created = safetyhook::MidHook::create(reinterpret_cast<void*>(base+0x3d870dd),
            callback,safetyhook::MidHook::StartDisabled);
        memory::require(created.has_value(),"Cannot create reflection capture projection hook");
        hook = new safetyhook::MidHook(std::move(*created));
        const std::array<uintptr_t,3> sites{0x3f89dd0,0x3d8860d,0x3d87fcf};
        const std::array<safetyhook::MidHookFn,3> callbacks{select_history,refresh_deferred,refresh_mobile};
        for (size_t i=0;i<sites.size();++i) {
            auto next=safetyhook::MidHook::create(reinterpret_cast<void*>(base+sites[i]),
                callbacks[i],safetyhook::MidHook::StartDisabled);
            memory::require(next.has_value(),"Cannot create reflection history/pose hook");
            history_hooks[i]=new safetyhook::MidHook(std::move(*next));
        }
        memory::require(hook->enable().has_value(),"Cannot enable reflection capture projection hook");
        for (auto* next:history_hooks)
            memory::require(next->enable().has_value(),"Cannot enable reflection history/pose hook");
        enabled = true;
    } catch (const std::exception& e) { error = e.what(); faulted = true; }
    catch (...) { error = "Reflection capture initialization failed"; faulted = true; }
    if (faulted.load()) {
        enabled = false;
        if (hook) { try { (void)hook->disable(); } catch (...) {} }
        for (auto* next:history_hooks) if (next) { try { (void)next->disable(); } catch (...) {} }
        spdlog::error("[WuWaReflectionCapture] {}",error);
    }
}
inline Json status() {
    const std::lock_guard lock{control};
    return {{"enabled",enabled.load()},{"faulted",faulted.load()},{"calls",calls.load()},
        {"applied",applied.load()},{"skipped",skipped.load()},{"error",error},
        {"history_calls",history_calls.load()},{"history_selected",history_selected.load()},
        {"history_skipped",history_skipped.load()},{"refresh_calls",refresh_calls.load()},
        {"refreshed",refreshed.load()},{"refresh_skipped",refresh_skipped.load()},
        {"scope","NSF custom reflection: eye XY projection, component-owned separate histories and single-view mirror refresh"}};
}
inline void shutdown(bool process_exiting) noexcept {
    enabled = false;
    if (process_exiting) return;
    const std::lock_guard lock{control};
    // Keep the hook storage/module reference alive for any returning stub.
    faulted = true;
    if (hook) { try { (void)hook->disable(); } catch (...) {} }
    for (auto* next:history_hooks) if (next) { try { (void)next->disable(); } catch (...) {} }
}
} // namespace wuwa_reflection_capture
