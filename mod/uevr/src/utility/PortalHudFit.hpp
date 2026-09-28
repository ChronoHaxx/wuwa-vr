#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace portal_hud {
struct Fit { float width{}, height{}, x{}, y{}; };
enum class Sizing { SafeFit, PreserveAspect, FullFrame };

// Fit the entire source texture inside the flat aperture, preserving aspect.
// The inset keeps all four corners inside its rounded, feathered boundary.
inline std::optional<Fit> fit(float width, float height, float corner, float feather,
        float aspect, float coverage, float horizontal, float vertical, Sizing sizing = Sizing::SafeFit) {
    for (const auto v : {width, height, corner, feather, aspect, coverage, horizontal, vertical}) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    if (width <= 0 || height <= 0 || aspect <= 0) return std::nullopt;
    const float radius = std::clamp(corner, 0.0f, (std::min)(width, height) / 2);
    const float inset = sizing == Sizing::SafeFit
        ? (std::max)(feather, 0.0f) + radius * (1.0f - std::sqrt(0.5f)) : 0.0f;
    const float available_w = width - 2 * inset;
    const float available_h = height - 2 * inset;
    if (available_w <= 0.01f || available_h <= 0.01f) return std::nullopt;
    Fit result{};
    const float fill = std::clamp(coverage, 0.4f, 1.0f);
    result.height = (sizing == Sizing::FullFrame ? available_h : (std::min)(available_h, available_w / aspect)) * fill;
    result.width = sizing == Sizing::FullFrame ? available_w * fill : result.height * aspect;
    result.x = std::clamp(horizontal, -1.0f, 1.0f) * (available_w - result.width) / 2;
    result.y = std::clamp(vertical, -1.0f, 1.0f) * (available_h - result.height) / 2;
    return result;
}
} // namespace portal_hud
