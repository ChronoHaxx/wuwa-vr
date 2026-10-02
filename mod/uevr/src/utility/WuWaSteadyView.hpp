#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <shared_mutex>
#include <cmath>

// Steady desktop view. The desktop window (what OBS records) shows a 16:9 crop of one eye, so
// every small head movement shakes it. This keeps a smoothed copy of the head rotation each frame
// was rendered with and moves/rotates the crop by the difference, so the window looks as if the
// smoothed head had rendered it. Stick turns move the game camera, not the head, and pass through.
// Only the desktop copy changes; the headset image is untouched.
// Conventions (OpenXR and OpenVR tracking space): +X right, +Y up, -Z forward.
namespace wuwa_steady_view {
struct Crop {
    float dx{}, dy{};   // crop centre offset in eye-texture pixels (+x right, +y down)
    float roll{};       // radians the smoothed view's "up" is turned clockwise in the eye image
};

// Test control: a timed bypass for A/B measurements, and what the last steadied frame did.
inline std::atomic<int64_t> bypass_until_ms{};
inline std::atomic<uint64_t> frames_steadied{};
inline std::atomic<float> last_dx{}, last_dy{}, last_roll{};
inline int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void bypass(int seconds) { bypass_until_ms = seconds > 0 ? now_ms() + int64_t{seconds} * 1000 : 0; }
inline bool bypassed() { return now_ms() < bypass_until_ms.load(); }

inline float angle_between(const glm::quat& a, const glm::quat& b) {
    const float d = (std::min)(1.0f, std::abs(glm::dot(glm::normalize(a), glm::normalize(b))));
    return 2.0f * std::acos(d);
}

class Smoother {
public:
    // actual: head rotation the frame was rendered with. tau: smoothing time constant (s).
    // max_angle: largest correction the crop margin can hold (rad). fx, fy: eye-texture pixels
    // per unit tangent. A gap, a recenter or a teleport-sized jump restarts from the actual pose.
    Crop update(glm::quat actual, double now, float tau, float max_angle, float fx, float fy) {
        actual = glm::normalize(actual);
        const double dt = now - m_last;
        if (!m_valid || dt > 0.5 || dt < 0.0 || angle_between(m_smooth, actual) > 0.6f) {
            m_smooth = actual;
            m_last = now;
            m_valid = true;
            return {};
        }
        m_last = now;
        if (dt > 0.0) {
            const float follow = 1.0f - static_cast<float>(std::exp(-dt / (std::max)(0.01f, tau)));
            m_smooth = glm::normalize(glm::slerp(m_smooth, actual, follow));
        }
        const float lag = angle_between(m_smooth, actual);
        if (lag > max_angle && lag > 0.0f) {
            m_smooth = glm::normalize(glm::slerp(actual, m_smooth, max_angle / lag));
        }
        return crop(actual, m_smooth, fx, fy);
    }

    // Where the smoothed view's centre and up direction land in the actual eye image.
    static Crop crop(const glm::quat& actual, const glm::quat& smooth, float fx, float fy) {
        const glm::quat d = glm::inverse(actual) * smooth;   // smoothed-camera -> actual-camera
        const glm::vec3 forward = d * glm::vec3{0.0f, 0.0f, -1.0f};
        const glm::vec3 up = d * glm::vec3{0.0f, 1.0f, 0.0f};
        if (forward.z > -0.1f) return {};
        return {fx * forward.x / -forward.z, -fy * forward.y / -forward.z, std::atan2(up.x, up.y)};
    }

    void reset() { m_valid = false; }

private:
    glm::quat m_smooth{1.0f, 0.0f, 0.0f, 0.0f};
    double m_last{};
    bool m_valid{};
};
}
