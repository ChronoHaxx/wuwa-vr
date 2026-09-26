#pragma once
#include <atomic>
#include <cstdint>

namespace wuwa_menu {
// Positive observations from the existing verified menu extraction routes.
// These expire; a stale menu must not leave an overlay floating in gameplay.
inline std::atomic<uint64_t> screen_overlay_at{}, transient_menu_at{};
inline bool recent(uint64_t now,uint64_t then) { return then!=0 && now>=then && now-then<=250; }
inline bool overlay(uint64_t now) { return recent(now,screen_overlay_at.load(std::memory_order_relaxed)); }
inline bool detected(uint64_t now,bool fresh_cursor) {
    return fresh_cursor || overlay(now) || recent(now,transient_menu_at.load(std::memory_order_relaxed));
}
inline bool profile_mask(int scope,uint64_t now,bool fresh_cursor) {
    return scope==2 || (scope==1 ? detected(now,fresh_cursor) : overlay(now));
}
}
