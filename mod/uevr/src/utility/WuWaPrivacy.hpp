#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace wuwa_privacy {
// An ID text hidden at the source (wuwa_uid_hide.lua, 1.2.8; owner 12 Nov: hide the canvas, not a black box).
// Each tick the script reports "1" when the text was already at alpha 0 and "0" when it had to hide it again,
// lost it or stopped hiding. Its box is dropped only while steady reports keep coming and the game has not
// shown the text for a while; anything else (no script, a stale report, a re-show) brings the box back.
inline constexpr uint64_t source_report_ms = 1000, source_reshow_hold_ms = 2000;
inline bool source_hidden(uint64_t steady_ms, uint64_t reshown_ms, uint64_t now_ms) {
    if (steady_ms == 0 || now_ms < steady_ms || now_ms - steady_ms > source_report_ms) return false;
    return reshown_ms == 0 || (now_ms >= reshown_ms && now_ms - reshown_ms >= source_reshow_hold_ms);
}
struct SourceHide {
    std::atomic<uint64_t> steady_ms{0}, reshown_ms{0};
    void report(bool steady, uint64_t now_ms) {
        if (steady) { steady_ms.store(now_ms, std::memory_order_relaxed); return; }
        steady_ms.store(0, std::memory_order_relaxed);
        reshown_ms.store(now_ms, std::memory_order_relaxed);
    }
    bool hidden(uint64_t now_ms) const {
        return source_hidden(steady_ms.load(std::memory_order_relaxed), reshown_ms.load(std::memory_order_relaxed), now_ms);
    }
};
inline SourceHide uid_text, profile_id_text;

struct Rect {
    int32_t left{}, top{}, right{}, bottom{};
    bool valid() const { return right>left && bottom>top; }
};
// Round outward to cover edge pixels. Invalid input produces no GPU clear;
// callers must never pass an empty list as a whole-target clear by accident.
inline Rect pixels(float left,float top,float right,float bottom,int32_t width,int32_t height) {
    if (width<=0 || height<=0 || !std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(right) || !std::isfinite(bottom)) return {};
    const auto x=[&](float v) { return double(std::clamp(v,0.0f,1.0f))*width; };
    const auto y=[&](float v) { return double(std::clamp(v,0.0f,1.0f))*height; };
    Rect r{int32_t(std::floor(x(left))),int32_t(std::floor(y(top))),
        int32_t(std::ceil(x(right))),int32_t(std::ceil(y(bottom)))};
    return r.valid() ? r : Rect{};
}
}
