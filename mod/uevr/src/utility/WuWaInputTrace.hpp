#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <Windows.h>
#include <Xinput.h>
#include "WuWaInputPolicy.hpp"

namespace wuwa_test {
// Explicit recordings: timed or until stopped/row limit. No input is generated.
inline std::atomic_uint64_t input_trace_until{};
inline std::atomic_bool input_trace_session{};
inline std::atomic_uint64_t input_trace_epoch{};
inline std::atomic_uint64_t motion_input_muted_until{};
inline std::atomic_uint32_t input_trace_rows{};
inline std::atomic_uint32_t input_trace_dropped{}, input_trace_reader_overflow{}, input_focus_rows{};
inline std::atomic_uint64_t input_watch_until{};
inline std::atomic_uint64_t focus_losses{}, focus_losses_blocked{};
inline std::atomic_uint64_t focus_test_until{};
inline std::atomic_int focus_test_policy{}; // 1 Windows focus forwarding, 2 focused XR input

inline int active_focus_test() {
    return GetTickCount64() < focus_test_until.load() ? focus_test_policy.load() : 0;
}

inline bool is_wuwa() {
    static const bool result = [] {
        wchar_t path[MAX_PATH]{};
        const auto size = GetModuleFileNameW(nullptr, path, MAX_PATH);
        return size > 0 && size < MAX_PATH
            && _wcsicmp(std::filesystem::path{path}.filename().c_str(), L"Client-Win64-Shipping.exe") == 0;
    }();
    return result;
}

inline bool tracing_input() {
    return (input_trace_session.load(std::memory_order_relaxed)
        || GetTickCount64() < input_trace_until.load(std::memory_order_relaxed))
        && input_trace_rows.load(std::memory_order_relaxed) < 1000;
}

inline void stop_input_trace() {
    input_trace_session = false;
    input_trace_until = 0;
}

inline void start_input_trace(uint32_t seconds, bool until_stopped = false) {
    stop_input_trace();
    input_trace_rows = 0;
    input_trace_dropped = 0;
    input_trace_reader_overflow = 0;
    input_focus_rows = 0;
    ++input_trace_epoch;
    input_trace_until = seconds ? GetTickCount64() + seconds * 1000ull : 0;
    input_trace_session = until_stopped;
}

inline bool is_focus_message(UINT message) {
    return message == WM_ACTIVATE || message == WM_ACTIVATEAPP
        || message == WM_SETFOCUS || message == WM_KILLFOCUS;
}

inline bool motion_input_muted() {
    return GetTickCount64() < motion_input_muted_until.load(std::memory_order_relaxed);
}

inline bool observing_input() {
    return is_wuwa() && (tracing_input() || GetTickCount64() < input_watch_until.load(std::memory_order_relaxed));
}

inline bool same_gamepad(const XINPUT_GAMEPAD& a, const XINPUT_GAMEPAD& b) {
    // XINPUT_GAMEPAD may contain padding; don't memcmp it.
    return a.wButtons == b.wButtons && a.bLeftTrigger == b.bLeftTrigger && a.bRightTrigger == b.bRightTrigger
        && a.sThumbLX == b.sThumbLX && a.sThumbLY == b.sThumbLY
        && a.sThumbRX == b.sThumbRX && a.sThumbRY == b.sThumbRY;
}

struct InputSample {
    uint64_t at{};
    uint32_t api{}, index{}, raw_result{}, result{}, changed_by{};
    XINPUT_STATE raw{}, delivered{};
    uint32_t foreground_pid{};
    int xr_state{};
    bool game_foreground{}, menu{}, motion{}, muted{};
};
inline std::mutex input_samples_mutex;
inline std::array<InputSample, 8> input_samples{};

inline void publish_input(const InputSample& sample) {
    if (sample.index >= XUSER_MAX_COUNT) return;
    std::unique_lock lock{input_samples_mutex, std::try_to_lock};
    if (lock.owns_lock()) input_samples[(sample.api == 14 ? 0 : 4) + sample.index] = sample;
}

inline bool read_input_samples(std::array<InputSample, 8>& result) {
    std::unique_lock lock{input_samples_mutex, std::try_to_lock};
    if (!lock.owns_lock()) return false;
    result = input_samples;
    return true;
}
}
