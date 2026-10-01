#pragma once
// Far lighting fix (1 Oct 2026, game 3.7). WuWa's Cascade Lighting Volume (CLV) keeps
// camera-centred indirect-lighting cascades in each view state. When it refreshes (game
// start, CLV enable, loading), only the view state rendered first afterwards gets its far
// cascades filled; the other eye's state keeps empty far cascades, so distant objects lack
// indirect light in that eye (they match up close). A refresh during a frame that renders
// both eyes fills both, and they stay filled. This module turns r.CLV.RefreshEveryFrame on
// for one game frame after stereo rendering starts or resumes after a gap, after a camera
// teleport, and on request. A refresh stalls rendering briefly, so it is never left on.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <sdk/CVar.hpp>
#include <sdk/ConsoleManager.hpp>
#include <spdlog/spdlog.h>

namespace wuwa_clv {
inline constexpr uint64_t gap_ms = 1500;          // no stereo frames this long: loading or paused
inline constexpr uint32_t settle_frames = 90;     // stereo frames after resuming before refilling
inline constexpr float teleport_distance = 5000.f; // game units (cm) from every recent camera position
inline constexpr uint64_t min_auto_interval_ms = 20000; // a refill stalls rendering ~0.2 s

inline std::atomic<uint64_t> last_stereo_ms{}, pulses{}, failures{}, teleports{};
inline std::atomic<uint32_t> stable_frames{}, requested_frames{};
inline std::atomic<bool> armed{true};
inline int pulse_frames_left{};                   // game thread only
inline sdk::IConsoleVariable* refresh_var{};      // game thread only
// Recent game-camera positions. Eye calls interleave with reflection/capture cameras far away
// (mirrored below water), so a teleport is a position far from all of them, not from the last.
inline float recent[16][3]{};
inline uint32_t recent_count{}, recent_next{};
inline uint64_t last_auto_ms{};

// Game thread, once per native-stereo eye pair.
inline void on_stereo_frame() {
    const auto now = GetTickCount64();
    const auto last = last_stereo_ms.exchange(now);
    if (last == 0 || now - last > gap_ms) {
        armed = true;
        stable_frames = 0;
    }
    if (armed.load()) ++stable_frames;
}

// Game thread, the game camera position at each stereo eye call (before any VR offset).
inline void on_camera(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    bool near_recent = recent_count == 0;
    for (uint32_t i = 0; i < recent_count && !near_recent; ++i) {
        const float dx = x - recent[i][0], dy = y - recent[i][1], dz = z - recent[i][2];
        near_recent = dx * dx + dy * dy + dz * dz <= teleport_distance * teleport_distance;
    }
    if (!near_recent) {
        armed = true;
        stable_frames = 0;
        ++teleports;
    }
    recent[recent_next][0] = x; recent[recent_next][1] = y; recent[recent_next][2] = z;
    recent_next = (recent_next + 1) % 16;
    if (recent_count < 16) ++recent_count;
}

// Any thread: refill on the next game tick for this many frames (1 is enough).
inline void request(uint32_t frames = 1) { requested_frames = frames < 1 ? 1 : (frames > 10 ? 10 : frames); }

inline sdk::IConsoleVariable* find_var() {
    if (refresh_var != nullptr) return refresh_var;
    const auto manager = sdk::FConsoleManager::get();
    if (manager == nullptr) return nullptr;
    auto* object = manager->find(L"r.CLV.RefreshEveryFrame");
    if (object == nullptr || object->AsCommand() != nullptr) return nullptr;
    refresh_var = static_cast<sdk::IConsoleVariable*>(object);
    return refresh_var;
}

// Game thread, every engine tick (before the frame's views are built).
inline void tick(bool automatic) {
    if (pulse_frames_left > 0) {
        if (--pulse_frames_left == 0) {
            if (auto* var = find_var()) var->Set(L"0");
        }
        return;
    }
    auto frames = requested_frames.exchange(0);
    const bool manual = frames != 0;
    if (!manual) {
        if (!automatic || !armed.load() || stable_frames.load() < settle_frames) return;
        const auto now = GetTickCount64();
        if (last_auto_ms != 0 && now - last_auto_ms < min_auto_interval_ms) return;  // stay armed
        last_auto_ms = now;
        frames = 1;
    }
    armed = false;
    auto* var = find_var();
    if (var == nullptr) { ++failures; return; }
    if (var->GetInt() != 0) return;   // already refreshing every frame (set by someone else)
    var->Set(L"1");
    pulse_frames_left = static_cast<int>(frames);
    ++pulses;
    spdlog::info("[WuWaCLV] far-lighting refill #{} for {} frame(s) ({})", pulses.load(), frames,
        manual ? "requested" : "stereo started, resumed or teleported");
}

inline nlohmann::json status() {
    return {{"pulses", pulses.load()}, {"failures", failures.load()}, {"teleports", teleports.load()},
        {"armed", armed.load()}, {"stable_frames", stable_frames.load()}, {"found", refresh_var != nullptr}};
}
} // namespace wuwa_clv
