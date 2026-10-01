#pragma once
// Diagnostic K5 (fix bench, timed, off by default): build WuWa's second eye view
// exactly like the first from the very start. The game's LocalPlayer::CalcSceneView
// (3.7 RVA 0x5266fd0) takes the stereo pass (2/3) and a per-eye index (0/1) and uses
// them before the view exists: camera view point, projection data, the view-state
// slot. During a window the second call (pass 3, index 1) is made with pass 2 and
// index 0. A thread-local flag keeps UEVR's eye offset and projection on the left eye,
// and the constructor hook restores the second view's own view state, so only the
// game's early per-eye logic changes. The hook is installed on first use, only after
// the function's entry bytes match this build.
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <windows.h>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>

namespace wuwa_second_eye {
inline constexpr uintptr_t calc_scene_view_rva = 0x5266fd0;
inline constexpr std::array<uint8_t, 16> entry_bytes{
    0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xac};

inline std::atomic<uint64_t> until{}, applied{}, state_restored{};
inline std::atomic<uintptr_t> second_state{};   // views[1]'s own view state, recorded on normal frames
inline std::atomic<int> install_result{};        // 0 not tried, 1 installed, -1 refused
inline thread_local bool forcing_left_eye = false;
inline safetyhook::InlineHook hook{};
inline std::once_flag install_once;

inline bool active() { return GetTickCount64() < until.load(); }
inline bool forcing() { return forcing_left_eye; }

using CalcSceneView = void* (*)(void*, void*, void*, void*, void*, void*, int32_t, int32_t);

inline void* calc_scene_view(void* player, void* family, void* location, void* rotation, void* viewport,
        void* drawer, int32_t pass, int32_t index) {
    if (active() && pass == 3 && index == 1) {
        forcing_left_eye = true;
        ++applied;
        auto* result = hook.call<void*>(player, family, location, rotation, viewport, drawer, int32_t{2}, int32_t{0});
        forcing_left_eye = false;
        return result;
    }
    return hook.call<void*>(player, family, location, rotation, viewport, drawer, pass, index);
}

__declspec(noinline) inline bool read_entry(uintptr_t address, uint8_t* out, size_t size) noexcept {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool install() {
    std::call_once(install_once, [] {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        std::array<uint8_t, 16> bytes{};
        const bool readable = read_entry(base + calc_scene_view_rva, bytes.data(), bytes.size());
        if (!readable || bytes != entry_bytes) {
            install_result = -1;
            spdlog::error("[WuWaSecondEye] CalcSceneView entry bytes did not match this build; K5 unavailable");
            return;
        }
        auto created = safetyhook::InlineHook::create(reinterpret_cast<void*>(base + calc_scene_view_rva),
            reinterpret_cast<void*>(&calc_scene_view));
        if (!created) {
            install_result = -1;
            spdlog::error("[WuWaSecondEye] could not hook CalcSceneView; K5 unavailable");
            return;
        }
        hook = std::move(*created);
        install_result = 1;
        spdlog::info("[WuWaSecondEye] CalcSceneView hooked at {:x}", base + calc_scene_view_rva);
    });
    return install_result.load() == 1;
}

// 0 ends the window. Installs the hook on first use; nothing is saved.
inline bool set_window(int seconds) {
    if (seconds <= 0) {
        until = 0;
        return true;
    }
    if (!install()) return false;
    until = GetTickCount64() + static_cast<uint64_t>(seconds) * 1000;
    return true;
}
} // namespace wuwa_second_eye
