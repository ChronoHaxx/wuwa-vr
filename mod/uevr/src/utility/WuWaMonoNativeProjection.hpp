#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace wuwa_mono_native {
// Ready means a guarded native projection reached a scene constructor. It is
// not a claim that the scene has been visually accepted on a headset.
enum class Status { not_requested, pending, ready, refused };
inline std::atomic<Status> current_status{Status::not_requested};
inline Status status() noexcept { return current_status.load(std::memory_order_relaxed); }
inline const char* status_text() noexcept {
    switch (status()) {
    case Status::ready: return "Native game camera active (headset check pending)";
    case Status::refused: return "Native game camera unavailable; compatibility fallback";
    case Status::pending: return "Waiting for native game camera";
    default: return "Off";
    }
}

using Rect = std::array<int32_t, 4>;
struct Fit { Rect full; Rect constrained; };
inline bool valid(const Rect& r) noexcept { return r[2] > r[0] && r[3] > r[1]; }

// Map the entire authored viewport uniformly into ONE eye's source extent.
// The constrained rectangle is relative to that viewport, including asymmetric
// letterboxing. Never stretch it independently or use the double-wide XR RT.
inline std::optional<Fit> fit(const Rect& full, const Rect& constrained,
    uint32_t target_width, uint32_t target_height) noexcept {
    if (!valid(full) || !valid(constrained) || !target_width || !target_height ||
        target_width > INT32_MAX || target_height > INT32_MAX ||
        constrained[0] < full[0] || constrained[1] < full[1] ||
        constrained[2] > full[2] || constrained[3] > full[3]) return std::nullopt;
    const double width = double(full[2]) - full[0];
    const double height = double(full[3]) - full[1];
    const double scale = std::min(target_width / width, target_height / height);
    const double left = (target_width - width * scale) / 2.0;
    const double top = (target_height - height * scale) / 2.0;
    auto x = [&](int32_t value) { return int32_t(std::clamp(std::llround(left +
        (double(value) - full[0]) * scale), 0LL, static_cast<long long>(target_width))); };
    auto y = [&](int32_t value) { return int32_t(std::clamp(std::llround(top +
        (double(value) - full[1]) * scale), 0LL, static_cast<long long>(target_height))); };
    Fit result{{0, 0, int32_t(target_width), int32_t(target_height)},
        {x(constrained[0]), y(constrained[1]), x(constrained[2]), y(constrained[3])}};
    if (!valid(result.constrained)) return std::nullopt;
    return result;
}

// Stereo retains each eye's own optical projection. Re-expressing clip X/Y
// into a smaller viewport preserves the pixel position of every surviving
// ray, including asymmetric principal points; it only crops the outer area.
struct ClipTransform { double x_scale, y_scale, x_offset, y_offset; };
inline std::optional<ClipTransform> crop_transform(const Rect& old_rect, const Rect& new_rect) noexcept {
    if (!valid(old_rect) || !valid(new_rect)) return std::nullopt;
    const double old_w = double(old_rect[2]) - old_rect[0], old_h = double(old_rect[3]) - old_rect[1];
    const double new_w = double(new_rect[2]) - new_rect[0], new_h = double(new_rect[3]) - new_rect[1];
    return ClipTransform{old_w / new_w, old_h / new_h,
        (2.0 * (double(old_rect[0]) - new_rect[0]) + old_w - new_w) / new_w,
        (2.0 * (double(new_rect[1]) - old_rect[1]) + new_h - old_h) / new_h};
}
inline std::optional<Rect> map_relative(const Rect& source_full, const Rect& source_crop,
    const Rect& target_full) noexcept {
    if (!valid(source_full) || !valid(source_crop) || !valid(target_full) ||
        source_crop[0] < source_full[0] || source_crop[1] < source_full[1] ||
        source_crop[2] > source_full[2] || source_crop[3] > source_full[3]) return std::nullopt;
    Rect result{};
    for (size_t i = 0; i < 4; ++i) {
        const auto axis = i % 2;
        const double ratio = (double(source_crop[i]) - source_full[axis]) /
            (double(source_full[axis + 2]) - source_full[axis]);
        result[i] = int32_t(std::llround(target_full[axis] + ratio *
            (double(target_full[axis + 2]) - target_full[axis])));
    }
    if (!valid(result)) return std::nullopt;
    return result;
}
} // namespace wuwa_mono_native

namespace wuwa_cinematic_framing {
enum class Status { off, waiting, ready, refused };
inline std::atomic<Status> current_status{Status::off};
inline std::atomic<uint64_t> authored_views{}, cropped_views{}, refused_pairs{}, differing_camera_decisions{};
// When the game last drew through an aspect-constrained (letterboxed) camera,
// as GetTickCount64 milliseconds: in-engine cinematics. 0 means never.
inline std::atomic<uint64_t> constrained_ms{};
inline const char* status_text() noexcept {
    switch (current_status.load(std::memory_order_relaxed)) {
    case Status::ready: return "Authored framing applied (headset check pending)";
    case Status::refused: return "Framing candidate refused; see diagnostic log";
    case Status::waiting: return "Waiting for an authored constrained camera";
    default: return "Off";
    }
}
} // namespace wuwa_cinematic_framing
