#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

// Start run / End run markers for exercise sessions. Each press appends one line to
// <profile>/wuwa-runs.jsonl ({"event","unix_ms"}, same clock as the motion log) and flashes a
// magenta square in the desktop view for a few frames, so a recording of the desktop view has an
// exact, findable sync frame. The headset image is not touched.
namespace wuwa_run {
inline std::atomic<int> flash_frames{};
inline std::atomic<bool> active{};
inline std::atomic<int64_t> started_ms{}, last_ms{};

inline int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline bool mark(const std::filesystem::path& profile, bool start) {
    const auto now = unix_ms();
    std::ofstream out(profile / "wuwa-runs.jsonl", std::ios::app | std::ios::binary);
    out << "{\"event\":\"" << (start ? "start" : "end") << "\",\"unix_ms\":" << now << "}\n";
    if (!out) return false;
    flash_frames = 6;
    active = start;
    last_ms = now;
    if (start) started_ms = now;
    return true;
}

// Called once per desktop-view draw; true while the sync square should be shown.
inline bool take_flash() {
    auto left = flash_frames.load();
    while (left > 0 && !flash_frames.compare_exchange_weak(left, left - 1)) {}
    return left > 0;
}
}
