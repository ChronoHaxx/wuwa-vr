#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wuwa_privacy {
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
