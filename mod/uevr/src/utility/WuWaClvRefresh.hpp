#pragma once
// Far lighting fix (1 Oct 2026, game 3.7). WuWa's Cascade Lighting Volume (CLV) keeps
// camera-centred indirect-lighting cascades in each view state. When it refreshes (game
// start, CLV enable, loading), only the view state rendered first afterwards gets its far
// cascades filled; the other eye's state keeps empty far cascades, so distant objects lack
// indirect light in that eye (they match up close). A refresh during a frame that renders
// both eyes fills both, and they stay filled. This module turns r.CLV.RefreshEveryFrame on
// for one game tick after stereo rendering starts or resumes after a gap, after a camera
// teleport, and on request. Missing stereo pairs retain a bounded retry instead of losing
// the request. A CPU-observed pair is not proof of GPU completion or of an NPC material fix.
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <sdk/CVar.hpp>
#include <sdk/ConsoleManager.hpp>
#include <spdlog/spdlog.h>
#include "WuWaClvRefreshPolicy.hpp"

namespace wuwa_clv {
inline constexpr uint64_t gap_ms = RefreshPolicy::pair_max_age_ms;
inline constexpr float teleport_distance = 5000.f; // game units (cm) from every recent camera position

inline std::atomic<uint64_t> last_stereo_ms{}, pair_sequence{}, arm_sequence{1}, teleports{};
inline std::atomic<uint32_t> stable_frames{}, requested_frames{};
inline std::atomic<bool> armed{true};
inline RefreshPolicy refresh_policy{};          // driven only by the game thread
inline std::mutex policy_mutex{};               // diagnostics can read from another thread
inline sdk::IConsoleVariable* refresh_var{};     // game thread; diagnostics under policy_mutex
inline uint64_t access_failures{};              // under policy_mutex
// Recent game-camera positions. Eye calls interleave with reflection/capture cameras far away
// (mirrored below water), so a teleport is a position far from all of them, not from the last.
inline float recent[16][3]{};
inline uint32_t recent_count{}, recent_next{};

inline void arm() {
    ++arm_sequence;
    stable_frames = 0;
    armed = true;
}

// Game thread, once per native-stereo eye pair.
inline void on_stereo_frame() {
    const auto now = GetTickCount64();
    const auto last = last_stereo_ms.exchange(now);
    if (last == 0 || now - last > gap_ms) {
        arm();
    }
    ++pair_sequence;
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
        arm();
        ++teleports;
    }
    recent[recent_next][0] = x; recent[recent_next][1] = y; recent[recent_next][2] = z;
    recent_next = (recent_next + 1) % 16;
    if (recent_count < 16) ++recent_count;
}

// Any thread: queue a bounded refill for when native stereo is active again.
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

// GetInt() returns 0 when SDK vtable discovery fails, which cannot distinguish
// "restored to zero" from "unreadable". Reuse the SDK's discovered index but
// preserve its optional result. The inherited member pointer operates on the
// base object; there is no downcast, replacement vtable or hard-coded offset.
struct CheckedCVarAccess : sdk::IConsoleVariable {
    static std::optional<int32_t> read(sdk::IConsoleVariable* var) {
        if (var == nullptr) return std::nullopt;
        const auto probe = &CheckedCVarAccess::locate_vtable_indices;
        const auto info = (var->*probe)();
        if (!info || info->get_int_vtable_index == 0) return std::nullopt;
        using ReadFn = int32_t(__thiscall*)(sdk::IConsoleVariable*);
        return (*(ReadFn**)var)[info->get_int_vtable_index](var);
    }
};

// Game thread, every engine tick (before the frame's views are built).
inline void tick(bool automatic) {
    const std::scoped_lock lock{policy_mutex};
    if (const auto frames = requested_frames.exchange(0); frames != 0)
        refresh_policy.request_manual(frames);
    const auto decision = refresh_policy.tick({GetTickCount64(), pair_sequence.load(),
        last_stereo_ms.load(), arm_sequence.load(), stable_frames.load(), armed.load(), automatic});
    if (decision.action == Action::Start) {
        auto result = StartResult::Unavailable;
        bool write_attempted = false;
        try {
            if (auto* var = find_var()) {
                const auto before = CheckedCVarAccess::read(var);
                if (before && *before != 0) result = StartResult::AlreadyActive;
                else if (before) {
                    write_attempted = true;
                    var->Set(L"1");
                    // SDK writes can silently fail when the vtable isn't ready.
                    // If nonzero/uncertain, retain restoration duty. Only our 1
                    // is reset; a different external value must remain intact.
                    const auto after = CheckedCVarAccess::read(var);
                    if (!after || *after != 0) result = StartResult::Started;
                }
            }
        } catch (...) {
            ++access_failures;
            if (write_attempted) result = StartResult::Started;
        }
        refresh_policy.confirm_start(result);
        if (result == StartResult::Started)
            spdlog::info("[WuWaCLV] bounded refill attempt #{} ({})",
                refresh_policy.status().pulse_attempts,
                decision.source == Source::Manual ? "requested" : "stereo started, resumed or teleported");
    } else if (decision.action == Action::Stop) {
        bool restored = false;
        try {
            if (auto* var = find_var()) {
                const auto before = CheckedCVarAccess::read(var);
                if (before && *before == 1) {
                    var->Set(L"0");
                    const auto after = CheckedCVarAccess::read(var);
                    restored = after && *after != 1;
                } else if (before) restored = true;
            }
        } catch (...) { ++access_failures; }
        refresh_policy.confirm_stop(restored);
        // A failed reset retains ownership and is retried on every engine tick,
        // even with automatic refill off and no recent stereo submission.
    }
    if (const auto generation = refresh_policy.take_auto_consumed();
        generation && *generation == arm_sequence.load()) armed = false;
}

inline nlohmann::json status() {
    const std::scoped_lock lock{policy_mutex};
    const auto& s = refresh_policy.status();
    return {{"pulses", s.pulse_attempts}, {"failures", s.start_failures + s.stop_failures + access_failures},
        {"teleports", teleports.load()}, {"armed", armed.load()}, {"stable_frames", stable_frames.load()},
        {"found", refresh_var != nullptr}, {"pair_sequence", pair_sequence.load()},
        {"pair_observed_pulses", s.pair_observed_pulses}, {"missed_pair_pulses", s.missed_pair_pulses},
        {"exhausted_requests", s.exhausted_requests}, {"existing_value_skips", s.existing_value_skips},
        {"manual_pending", s.manual_pending || requested_frames.load() != 0},
        {"automatic_pending", s.automatic_pending}, {"owns_refresh", s.owns_refresh},
        {"observation", "CPU stereo pair submission only; GPU completion and visual outcome unverified"}};
}
} // namespace wuwa_clv
