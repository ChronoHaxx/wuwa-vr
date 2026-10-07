#pragma once

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <string>

#include <spdlog/spdlog.h>
#include <utility/Module.hpp>
#include <utility/PointerHook.hpp>

// One per-thread guard for every path that forwards Present/Present1 to the
// original (filtered window, retired probe, other swapchain, unhooked, and the
// normal call). An overlay such as Steam's can detour the native DXGI entry
// and call back into whichever hook it found installed. When the D3D12 probe
// times out and the D3D11 probe takes the same vtable slot, the first frames of
// the game window are "filtered" and were forwarded with no re-entry check: the
// call cycled until the stack overflowed (the 1.1.1 crash on Windows 10 + Steam,
// whose log shows three nested filtered entries in the same millisecond).
//
// A call that re-enters us while this thread is already forwarding is that
// cycle. Present once through the real DXGI function instead: if its entry is
// patched, restore the on-disk bytes first (stock UEVR's "Present fixed" remedy,
// which only covered the normal path). Calls that never re-enter (the normal
// case, and every Kuro-launcher start) behave exactly as before.
namespace wuwa_present_guard {
inline thread_local int depth = 0;
inline std::atomic<void*> dxgi_present{};
inline std::atomic<void*> dxgi_present1{};
inline std::atomic<unsigned> loop_logs{};
inline std::atomic<unsigned> restores{};
constexpr unsigned max_restores = 8;

struct Scope {
    Scope() { ++depth; }
    ~Scope() { --depth; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

inline HMODULE owner(const void* address) {
    HMODULE module{};
    if (address == nullptr || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(address), &module)) return nullptr;
    return module;
}

inline bool in_dxgi(const void* fn) {
    const auto dxgi = GetModuleHandleW(L"dxgi.dll");
    return dxgi != nullptr && owner(fn) == dxgi;
}

// "module+offset" for the log; never allocates a module list.
inline std::string where(const void* address) {
    const auto module = owner(address);
    if (module == nullptr) return fmt::format("{:x}", reinterpret_cast<uintptr_t>(address));
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* name = path;
    for (DWORD i = 0; i < length; ++i) if (path[i] == L'\\' || path[i] == L'/') name = path + i + 1;
    std::string narrow;
    for (const wchar_t* c = name; *c != 0; ++c) narrow.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    return fmt::format("{}+{:x}", narrow, reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module));
}

inline void log_loop(const void* fn, const void* target, bool present1) {
    unsigned char entry[14]{};
    SIZE_T read{};
    ReadProcessMemory(GetCurrentProcess(), fn, entry, sizeof(entry), &read);
    std::string bytes;
    for (SIZE_T i = 0; i < read; ++i) bytes += fmt::format("{:02x}", entry[i]);
    void* frames[16]{};
    const auto count = RtlCaptureStackBackTrace(1, 16, frames, nullptr);
    std::string chain;
    for (USHORT i = 0; i < count; ++i) chain += (i ? " < " : "") + where(frames[i]);
    spdlog::warn("[WuWaPresentGuard] Present{} re-entered while forwarding (depth {}); original={} entry={} target={} chain: {}",
        present1 ? "1" : "", depth, where(fn), bytes, where(target), chain);
}

// Restores a patched DXGI entry from dxgi.dll on disk. Only dxgi.dll, and only
// a bounded number of times, so a detour that re-patches every frame cannot
// make us rewrite code forever.
inline bool restore_entry(void* fn) {
    if (!in_dxgi(fn)) return false;
    const auto original = utility::get_original_bytes(Address{fn});
    if (!original) return false;  // entry already matches disk: nothing to restore
    if (restores.fetch_add(1) >= max_restores) return false;
    ProtectionOverride protection{fn, original->size(), PAGE_EXECUTE_READWRITE};
    std::memcpy(fn, original->data(), original->size());
    FlushInstructionCache(GetCurrentProcess(), fn, original->size());
    spdlog::warn("[WuWaPresentGuard] Restored {} patched bytes at {} (restore {} of {})",
        original->size(), where(fn), restores.load(), max_restores);
    return true;
}

// Hooks call this with the slot value they replace. Only a value inside
// dxgi.dll is the real function; anything else is another overlay's wrapper.
inline void remember(void* original, bool present1) {
    if (in_dxgi(original)) (present1 ? dxgi_present1 : dxgi_present).store(original);
}

// fn is the original this path would call; call(target) invokes a target with
// the caller's arguments. Returns the forwarded result.
template <typename Fn, typename Call>
HRESULT forward(Fn fn, bool present1, Call&& call) {
    if (depth == 0) {
        Scope scope;
        return call(fn);
    }
    const auto known = reinterpret_cast<Fn>((present1 ? dxgi_present1 : dxgi_present).load());
    const auto target = known != nullptr ? known : fn;
    if (loop_logs.fetch_add(1) < 3) log_loop(reinterpret_cast<void*>(fn), reinterpret_cast<void*>(target), present1);
    if (depth == 1) {
        // A different real function breaks a wrapper cycle by itself; the same
        // one only does once its patched entry is restored.
        const bool repaired = restore_entry(reinterpret_cast<void*>(target));
        if (target != fn || repaired) {
            Scope scope;
            return call(target);
        }
    }
    // Deeper re-entry, or nothing safe to call: end the cycle without presenting.
    return S_OK;
}
}
