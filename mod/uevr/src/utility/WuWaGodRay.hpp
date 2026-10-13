#pragma once
// Kuro's screen-space god ray (storm sky at the Mengzhou Fogveil Pagoda, 13 Oct 2026).
// KuroGlobalGI::ApplyGodRay hands the scene a 256x256 GodRayRT and ExtraFOV; each
// scene render then runs a pass that draws the god-ray source into that target from
// Views[0]'s TranslatedViewMatrix (+0x520 = ViewMatrices +0x200; ViewMatrices sit at
// +0x320, ViewMatrix at +0x3e0) with a square projection of Views[0].FOV (+0xca4) plus
// ExtraFOV. The region's post material (parent M_GodRayPost) then blurs it on screen.
// In VR the streaks stay put on screen when only the head turns (simulator, game
// camera fixed: the world moves +-49 px for 12 degrees of head yaw, the fan 11-12 px,
// also with a symmetric FOV and IPD 0), so one rotation in that chain is the game
// camera's rather than the eye's.
//
// This hook wraps the pass. Every couple of seconds it logs how far the pass camera
// (+0x520) is from the eye's ViewMatrix rotation (+0x3e0) and from the game camera
// recorded before the head pose is applied. The camera setting picks what the pass
// renders from, for the duration of the call only:
//   0  the game's own matrix (unchanged)
//   1  the eye's ViewMatrix rotation (head-tracked)
//   2  the game camera without the head pose (diagnostic)
// The function is found by its code bytes; if they do not match, nothing is hooked.
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>

#include <nlohmann/json.hpp>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>
#include <utility/Scan.hpp>

#include "WuWaLguiProbe.hpp"

namespace wuwa_god_ray {
namespace checked = wuwa_lgui_probe::detail;

inline constexpr const char* pass_pattern =
    "40 57 41 54 41 56 48 81 EC 20 01 00 00 4C 8B B1 08 01 00 00 48 8B FA 4C 8B E1 "
    "41 83 BE 6C CA 00 00 00 0F 8E ? ? ? ? 49 8B 06 48 8B 48 20 48 8B 01 FF 90 00 06 00 00 "
    "48 85 C0 0F 84 ? ? ? ? 48 8B 10 48 8B C8 FF 92 E8 01 00 00";
inline constexpr uint32_t views_offset = 0x108, view_count_offset = 0x110;
inline constexpr uint32_t pass_offset = 0xc90, fov_offset = 0xca4;
inline constexpr uint32_t view_matrix_offset = 0x3e0, translated_view_offset = 0x520;

using Matrix = std::array<float, 16>; // UE FMatrix, row-major, row vectors

inline std::atomic<int> camera_mode{}, install_result{}; // install: 0 not tried, 1 hooked, -1 refused
inline std::atomic<uint64_t> calls{}, replaced{}, faults{}, next_log_ms{};
inline std::atomic<float> pre_pitch{}, pre_yaw{}, pre_roll{};
inline std::atomic<bool> has_pre{};
inline std::atomic<float> last_eye_angle{-1.0f}, last_game_angle{-1.0f}, last_fov{};
inline safetyhook::InlineHook hook{};
inline std::once_flag install_once;

// Game camera rotation before UEVR adds the head pose (first eye of each frame).
inline void record_pre_rotation(float pitch, float yaw, float roll) noexcept {
    pre_pitch.store(pitch, std::memory_order_relaxed);
    pre_yaw.store(yaw, std::memory_order_relaxed);
    pre_roll.store(roll, std::memory_order_relaxed);
    has_pre.store(true, std::memory_order_release);
}

// FInverseRotationMatrix(rotator) * view planes, i.e. UE's ViewRotationMatrix.
inline Matrix view_rotation_matrix(float pitch_deg, float yaw_deg, float roll_deg) noexcept {
    constexpr float to_rad = 3.14159265358979f / 180.0f;
    const float sp = std::sin(pitch_deg * to_rad), cp = std::cos(pitch_deg * to_rad);
    const float sy = std::sin(yaw_deg * to_rad), cy = std::cos(yaw_deg * to_rad);
    const float sr = std::sin(roll_deg * to_rad), cr = std::cos(roll_deg * to_rad);
    // FRotationMatrix rows; the inverse of a rotation is its transpose.
    const float r[3][3] = {
        {cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp}};
    const float inv[3][3] = {{r[0][0], r[1][0], r[2][0]}, {r[0][1], r[1][1], r[2][1]}, {r[0][2], r[1][2], r[2][2]}};
    // View planes: x' = z, y' = x, z' = y (rows (0,0,1), (1,0,0), (0,1,0)).
    Matrix m{};
    for (int i = 0; i < 3; ++i) {
        m[i * 4 + 0] = inv[i][1];
        m[i * 4 + 1] = inv[i][2];
        m[i * 4 + 2] = inv[i][0];
    }
    m[15] = 1.0f;
    return m;
}

// Angle in degrees between the 3x3 rotations of two matrices.
inline float rotation_angle(const Matrix& a, const Matrix& b) noexcept {
    float trace = 0.0f;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) trace += a[k * 4 + i] * b[k * 4 + i];
    const float c = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
    return std::acos(c) * 180.0f / 3.14159265358979f;
}

inline bool plausible_rotation(const Matrix& m) noexcept {
    for (int i = 0; i < 3; ++i) {
        float len = 0.0f;
        for (int k = 0; k < 3; ++k) len += m[i * 4 + k] * m[i * 4 + k];
        if (!std::isfinite(len) || std::abs(len - 1.0f) > 1e-2f) return false;
    }
    return true;
}

inline void pass_hook(void* renderer, void* graph) {
    ++calls;
    const auto now = GetTickCount64();
    const int mode = camera_mode.load(std::memory_order_relaxed);
    uintptr_t views{};
    int32_t count{};
    const auto base = reinterpret_cast<uintptr_t>(renderer);
    if (!checked::read(base + views_offset, views) || !checked::read(base + view_count_offset, count) ||
        !views || count < 1) {
        hook.call<void>(renderer, graph);
        return;
    }
    Matrix pass_matrix{}, eye_matrix{};
    int32_t pass{};
    float fov{};
    const bool readable = checked::read(views + translated_view_offset, pass_matrix) &&
        checked::read(views + view_matrix_offset, eye_matrix) &&
        checked::read(views + pass_offset, pass) && checked::read(views + fov_offset, fov) &&
        plausible_rotation(pass_matrix) && plausible_rotation(eye_matrix);
    if (!readable) {
        ++faults;
        hook.call<void>(renderer, graph);
        return;
    }
    const bool pre_known = has_pre.load(std::memory_order_acquire);
    const auto game_matrix = view_rotation_matrix(pre_pitch.load(), pre_yaw.load(), pre_roll.load());
    const float eye_angle = rotation_angle(pass_matrix, eye_matrix);
    const float game_angle = pre_known ? rotation_angle(pass_matrix, game_matrix) : -1.0f;
    last_eye_angle.store(eye_angle, std::memory_order_relaxed);
    last_game_angle.store(game_angle, std::memory_order_relaxed);
    last_fov.store(fov, std::memory_order_relaxed);
    if (now >= next_log_ms.load(std::memory_order_relaxed)) {
        next_log_ms.store(now + 2000, std::memory_order_relaxed);
        spdlog::info("[WuWaGodRay] pass={} fov={:.2f} views={} mode={} pass_vs_eye={:.2f} deg "
            "pass_vs_game={:.2f} deg eye_vs_game={:.2f} deg calls={} replaced={}",
            pass, fov, count, mode, eye_angle, game_angle,
            pre_known ? rotation_angle(eye_matrix, game_matrix) : -1.0f, calls.load(), replaced.load());
    }
    if (mode != 1 && !(mode == 2 && pre_known)) {
        hook.call<void>(renderer, graph);
        return;
    }
    // Swap only the 3x3 rotation for this call; the row of zeros and the 1 stay.
    auto replacement = pass_matrix;
    const auto& source = mode == 1 ? eye_matrix : game_matrix;
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) replacement[i * 4 + k] = source[i * 4 + k];
    const auto target = reinterpret_cast<void*>(views + translated_view_offset);
    if (!checked::guarded_copy(target, replacement.data(), sizeof(replacement))) {
        ++faults;
        hook.call<void>(renderer, graph);
        return;
    }
    ++replaced;
    hook.call<void>(renderer, graph);
    checked::guarded_copy(target, pass_matrix.data(), sizeof(pass_matrix));
}

inline bool install() {
    std::call_once(install_once, [] {
        const auto module = GetModuleHandleW(nullptr);
        const auto found = utility::scan(module, pass_pattern);
        if (!found) {
            install_result = -1;
            spdlog::error("[WuWaGodRay] god-ray pass not found in this build; camera setting unavailable");
            return;
        }
        auto created = safetyhook::InlineHook::create(reinterpret_cast<void*>(*found), reinterpret_cast<void*>(&pass_hook));
        if (!created) {
            install_result = -1;
            spdlog::error("[WuWaGodRay] could not hook the god-ray pass at {:x}", *found);
            return;
        }
        hook = std::move(*created);
        install_result = 1;
        spdlog::info("[WuWaGodRay] god-ray pass hooked at {:x}", *found);
    });
    return install_result.load() == 1;
}

// Game thread, every frame. The hook is installed once on first call and stays.
inline void configure(bool stereo, int mode) {
    if (!stereo) {
        camera_mode.store(0, std::memory_order_relaxed);
        return;
    }
    if (!install()) return;
    if (camera_mode.exchange(mode, std::memory_order_relaxed) != mode)
        spdlog::info("[WuWaGodRay] camera mode {}", mode);
}

inline nlohmann::json status() {
    return {{"installed", install_result.load()}, {"mode", camera_mode.load()}, {"calls", calls.load()},
        {"replaced", replaced.load()}, {"faults", faults.load()}, {"fov", last_fov.load()},
        {"pass_vs_eye_deg", last_eye_angle.load()}, {"pass_vs_game_deg", last_game_angle.load()}};
}
} // namespace wuwa_god_ray
