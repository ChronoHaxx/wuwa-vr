#pragma once
// Full VR ground detail: r.Landscape.KuroLODMode 1 while Full VR renders, the game's own
// value on the stereo and mono screens. Why: WuWaTerrainLodPolicy.hpp.
#include <mutex>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "WuWaRimSuppression.hpp"
#include "WuWaTerrainLodPolicy.hpp"

namespace wuwa_terrain_lod {
inline constexpr const char* variable_name = "r.Landscape.KuroLODMode";
inline constexpr const wchar_t* variable_wname = L"r.Landscape.KuroLODMode";
// Game thread owns policy/IO. Other threads can copy only cached status.
inline Policy policy;
inline std::mutex status_mutex;
inline Status cached;
inline uint64_t last_observed_ms{};
inline bool last_wanted{}, last_eligible{}, last_conflict{};
inline Code last_logged{Code::Off};
inline Status status() { std::scoped_lock lock{status_mutex}; return cached; }
inline bool busy() { const auto s = status(); return s.requested || s.owned; }

// Every game tick; transitions take priority, steady ownership is observed at most once a second.
inline void tick(bool wanted, bool eligible, bool conflict) {
    const auto now = GetTickCount64();
    const bool transition = wanted != last_wanted || eligible != last_eligible || conflict != last_conflict;
    last_wanted = wanted; last_eligible = eligible; last_conflict = conflict;
    if (!transition && last_observed_ms && now >= last_observed_ms && now - last_observed_ms < 1000) return;
    last_observed_ms = now;
    wuwa_rim::ConsoleIO io{variable_wname};
    policy.tick(wanted, eligible, conflict, now, io);
    const auto& s = policy.status();
    if (s.code != last_logged) {
        last_logged = s.code;
        if (s.code != Code::Off && s.code != Code::Waiting)
            spdlog::info("[WuWaTerrain] {} {}{}", variable_name, message(s.code),
                s.original ? fmt::format(" (game value {})", s.original->value) : std::string{});
    }
    std::scoped_lock lock{status_mutex}; cached = s;
}
inline nlohmann::json diagnostic_status() {
    const auto s = status();
    nlohmann::json result{{"requested",s.requested},{"eligible",s.eligible},{"owned",s.owned},
        {"blocked",s.blocked},{"status",message(s.code)},{"variable",variable_name},{"readopts",s.readopts}};
    if (s.original) { result["original_float"] = s.original->value; result["original_int"] = s.original->integer; }
    return result;
}
} // namespace wuwa_terrain_lod
