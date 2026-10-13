#pragma once
// Live world behind menus (13 Oct 2026, game 3.7). With a full-screen menu open
// (quest log, Esc terminal, journey), Kuro's scene-colour cache keeps the scene
// colour from the moment the menu opened ("Scene texture preserved, using copy")
// and stops redrawing the world; later passes such as god rays still follow the
// head. In 2D that is a free GPU saving; in VR the world becomes a picture locked
// to the eyes. Simulator, quest menu: with the cache on, +-12 degrees of head yaw
// moved no geometry in either eye; with r.CacheSceneColor.Enable 0 and the menu
// reopened, both eyes shifted -50/+54 px with the head. The switch gates the next
// start of the cache, so it is set to 0 from the first engine ticks and set again
// if the game restores its default (1).
#include <cstdint>
#include <string>
#include <windows.h>
#include <sdk/CVar.hpp>
#include <sdk/ConsoleManager.hpp>
#include <spdlog/spdlog.h>
#include "WuWaClvRefresh.hpp"

namespace wuwa_scene_cache {
inline sdk::IConsoleVariable* enable_var{}; // game thread only
inline uint64_t next_check_ms{}, writes{}, failures{};
inline bool missing_logged{};

// Game thread, every engine tick; reads at most once a second.
inline void tick() {
    const auto now = GetTickCount64();
    if (now < next_check_ms) return;
    next_check_ms = now + 1000;
    try {
        if (enable_var == nullptr) {
            const auto manager = sdk::FConsoleManager::get();
            if (manager == nullptr) return;
            auto* object = manager->find(L"r.CacheSceneColor.Enable");
            if (object == nullptr || object->AsCommand() != nullptr) {
                if (!missing_logged) spdlog::info("[WuWaSceneCache] r.CacheSceneColor.Enable not found; menus keep the game's cache");
                missing_logged = true;
                return;
            }
            enable_var = static_cast<sdk::IConsoleVariable*>(object);
        }
        const auto before = wuwa_clv::CheckedCVarAccess::read(enable_var);
        if (!before || *before == 0) return;
        enable_var->Set(L"0");
        const auto after = wuwa_clv::CheckedCVarAccess::read(enable_var);
        if (after && *after == 0) {
            ++writes;
            spdlog::info("[WuWaSceneCache] r.CacheSceneColor.Enable {} -> 0: live world behind menus (#{})", *before, writes);
        } else if (++failures <= 3) {
            spdlog::warn("[WuWaSceneCache] r.CacheSceneColor.Enable stayed {}", after ? std::to_string(*after) : "unreadable");
        }
    } catch (...) { ++failures; }
}
} // namespace wuwa_scene_cache
