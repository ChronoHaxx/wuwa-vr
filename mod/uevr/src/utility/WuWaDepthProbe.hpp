#pragma once

// Close-up match from the rendered scene (test 3k). Story shots often set no
// focus, or one that does not name the subject (3j: almost every shot stayed
// wide), so the distance comes from what is actually drawn: the camera script
// asks with the 2D camera's horizontal FOV, the D3D12 present hook
// (mods/vr/d3d12/WuWaDepthSampler.hpp) samples the scene depth around where
// the 2D frame's centre lands in the eye image, and the script reads the
// distances back. Any thread.
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace wuwa_depth_probe {
// Request: the 2D camera's horizontal FOV in degrees (0: not wanted). A
// request lapses after 3 s, so samples stop if the script stops asking.
inline std::atomic<float> fov{0.0f};
inline std::atomic<uint64_t> asked_at{0};
// Result, in cm along the view: the nearest thing in the region (5th
// percentile), its 25th percentile (the subject) and median, and the share of
// the region with geometry (not sky). seq counts finished samples.
inline std::atomic<uint32_t> seq{0};
inline std::atomic<float> nearest{0.0f}, p25{0.0f}, p50{0.0f}, valid{0.0f};
enum State : int { Idle = 0, Sampling = 1, NoDepth = -1, Unsupported = -2, OutOfView = -3 };
inline std::atomic<int> state{Idle};

inline void request(const std::string& text) {
    const float value = std::strtof(text.c_str(), nullptr);
    fov.store(value > 1.0f && value < 120.0f ? value : 0.0f);
    asked_at.store(GetTickCount64());
}

inline bool wanted() {
    return fov.load() > 0.0f && GetTickCount64() - asked_at.load() < 3000;
}

// "seq nearest p25 p50 valid state" for the script.
inline std::string report() {
    char text[96];
    std::snprintf(text, sizeof(text), "%u %.0f %.0f %.0f %.2f %d", seq.load(), nearest.load(), p25.load(),
        p50.load(), valid.load(), state.load());
    return text;
}
}
