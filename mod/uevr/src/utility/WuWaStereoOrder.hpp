#pragma once
#include <atomic>
#include <cstdint>
#include <nlohmann/json.hpp>

// Local, expiring diagnostic. This does not change saved profile options or
// interchange eye images, projections, render targets or temporal histories.
namespace wuwa_stereo_order {
inline std::atomic<uint64_t> until{}, applied{}, refused{};
inline bool active(uint64_t now) { return now < until.load(); }
inline nlohmann::json status() {
    const auto now = GetTickCount64(), end = until.load();
    return {{"active", now < end}, {"remaining_ms", end > now ? end - now : 0},
        {"applied_pairs", applied.load()}, {"refused_pairs", refused.load()},
        {"scope", "submission order only; each eye keeps its own target and history"}};
}
} // namespace wuwa_stereo_order
