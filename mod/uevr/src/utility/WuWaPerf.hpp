#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

// Frame-time recorder for A/B performance checks, opened by the WuWa test control `perf` op.
// on_frame() is called once per game frame from the native stereo path; it records the time
// since the previous call only while a recording is open (at most 30,000 frames).
namespace wuwa_perf {
struct Summary {
    size_t frames{};
    double mean_ms{}, p50_ms{}, p90_ms{}, p99_ms{}, max_ms{};
};

inline std::mutex mutex;
inline std::vector<float> samples;
inline std::chrono::steady_clock::time_point last{};
inline bool recording{};

inline void on_frame() {
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock _{mutex};
    if (recording && last.time_since_epoch().count() != 0 && samples.size() < 30000) {
        samples.push_back(std::chrono::duration<float, std::milli>(now - last).count());
    }
    last = now;
}

inline void start() {
    std::scoped_lock _{mutex};
    samples.clear();
    samples.reserve(30000);
    recording = true;
}

inline Summary summarize(std::vector<float> values) {
    Summary s{};
    s.frames = values.size();
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    double total{};
    for (const auto v : values) total += v;
    const auto at = [&](double q) { return values[std::min(values.size() - 1, static_cast<size_t>(q * (values.size() - 1) + 0.5))]; };
    s.mean_ms = total / values.size();
    s.p50_ms = at(0.50); s.p90_ms = at(0.90); s.p99_ms = at(0.99); s.max_ms = values.back();
    return s;
}

// Ends the recording (if open) and summarizes it.
inline Summary stop() {
    std::vector<float> copy;
    {
        std::scoped_lock _{mutex};
        recording = false;
        copy.swap(samples);
    }
    return summarize(std::move(copy));
}
}
