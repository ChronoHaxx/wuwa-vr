#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

// HUD in the desktop window "as in the headset". The desktop window shows a 16:9 crop of one eye with the
// game HUD pasted over the whole window, like a flat monitor; in the headset the HUD is a panel at its own
// distance and size (OverlayComponent's slate quad). The overlay publishes that panel in tracking space each
// frame; the desktop copy projects it into the shown eye with the eye's own projection and draws the HUD
// texture there. Conventions (OpenXR tracking space): +X right, +Y up, -Z forward.
namespace wuwa_desktop_hud {
struct Panel {
    glm::mat4 tracking{1.0f}; // panel centre and axes in tracking space (columns: right, up, back, position)
    float width{}, height{};  // metres
    uint64_t tick{};          // GetTickCount64() when published
};

inline std::mutex panel_mutex;
inline std::optional<Panel> panel_value;

inline void publish(const glm::mat4& tracking, float width, float height, uint64_t tick) {
    std::scoped_lock _{panel_mutex};
    panel_value = Panel{tracking, width, height, tick};
}
inline void clear() {
    std::scoped_lock _{panel_mutex};
    panel_value.reset();
}
inline std::optional<Panel> latest(uint64_t now, uint64_t max_age_ms = 250) {
    std::scoped_lock _{panel_mutex};
    if (!panel_value || now - panel_value->tick > max_age_ms) return std::nullopt;
    return panel_value;
}

// The eye image the desktop shows: the raw view tangents (left < 0 < right, top > 0 > bottom) and where those
// raw edges sit in the rendered texture (0..1, u left to right, v top to bottom; a projection override renders
// a wider texture than the raw view, see OpenXR.cpp view_bounds).
struct EyeImage {
    std::array<float, 4> tangents{};          // left, right, top, bottom
    std::array<float, 4> bounds{0, 1, 0, 1};  // u left, u right, v top, v bottom
};

// The panel's corners in eye-texture coordinates (0..1), clockwise from top left; nullopt when any corner is
// behind the eye or the projection is degenerate.
inline std::optional<std::array<glm::vec2, 4>> project(const Panel& panel, const glm::mat4& eye_tracking, const EyeImage& eye) {
    const float l = eye.tangents[0], r = eye.tangents[1], t = eye.tangents[2], b = eye.tangents[3];
    if (!(r > l) || !(t > b)) return std::nullopt;
    const glm::mat4 to_eye = glm::inverse(eye_tracking) * panel.tracking;
    const float hw = panel.width * 0.5f, hh = panel.height * 0.5f;
    const glm::vec2 local[4]{{-hw, hh}, {hw, hh}, {hw, -hh}, {-hw, -hh}};
    std::array<glm::vec2, 4> out{};
    for (int i = 0; i < 4; ++i) {
        const glm::vec4 p = to_eye * glm::vec4{local[i].x, local[i].y, 0.0f, 1.0f};
        if (!(p.z < -0.01f)) return std::nullopt;
        const float tx = p.x / -p.z, ty = p.y / -p.z;
        out[i] = {eye.bounds[0] + (tx - l) / (r - l) * (eye.bounds[1] - eye.bounds[0]),
                  eye.bounds[2] + (t - ty) / (t - b) * (eye.bounds[3] - eye.bounds[2])};
        if (!std::isfinite(out[i].x) || !std::isfinite(out[i].y)) return std::nullopt;
    }
    return out;
}

// A sprite placement for the projected panel in window pixels: centre, size and clockwise rotation (radians),
// given where the eye image lands in the window. to_window maps an eye-texture point (0..1) to window pixels.
struct Placement {
    glm::vec2 centre{}, size{};
    float rotation{};
};
template <class ToWindow>
inline std::optional<Placement> place(const std::array<glm::vec2, 4>& uv, ToWindow&& to_window) {
    glm::vec2 w[4];
    for (int i = 0; i < 4; ++i) w[i] = to_window(uv[i]);
    const glm::vec2 top = w[1] - w[0], bottom = w[2] - w[3], left = w[3] - w[0], right = w[2] - w[1];
    const float width = 0.5f * (glm::length(top) + glm::length(bottom));
    const float height = 0.5f * (glm::length(left) + glm::length(right));
    if (!(width > 1.0f) || !(height > 1.0f) || width > 1.0e5f || height > 1.0e5f) return std::nullopt;
    const glm::vec2 across = top + bottom;
    return Placement{0.25f * (w[0] + w[1] + w[2] + w[3]), {width, height}, std::atan2(across.y, across.x)};
}
} // namespace wuwa_desktop_hud
