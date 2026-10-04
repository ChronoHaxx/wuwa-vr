#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace wuwa_window {

inline constexpr size_t max_bridge_payload = 512;

struct BridgeState {
    bool active{};
    bool recenter{};
    bool lock_aspect{};
    float width{2.4f};
    float height{1.35f};
    float distance{2.0f};
    float feather{0.10f};
    float corner_radius{};
    float curvature{};
    std::array<float, 3> surround_color{};
    float opacity{};
};

// v1 is exactly four integers and ten finite floats. Keep finite out-of-range
// values compatible: the rendering settings still clamp them to slider bounds.
inline std::optional<BridgeState> parse_bridge_state(std::string_view payload) {
    if (payload.empty() || payload.size() > max_bridge_payload) return std::nullopt;
    std::istringstream stream{std::string{payload}};
    stream.imbue(std::locale::classic());
    const auto read_number = [&](auto& value) {
        std::string token;
        if (!(stream >> token)) return false;
        std::istringstream field{token};
        field.imbue(std::locale::classic());
        field >> value;
        return !field.fail() && field.peek() == std::char_traits<char>::eof();
    };
    int version{}, active{}, recenter{}, lock_aspect{};
    BridgeState next{};
    if (!read_number(version) || !read_number(active) || !read_number(recenter) ||
        !read_number(lock_aspect) || !read_number(next.width) || !read_number(next.height) ||
        !read_number(next.distance) || !read_number(next.feather) ||
        !read_number(next.corner_radius) || !read_number(next.curvature) ||
        !read_number(next.surround_color[0]) || !read_number(next.surround_color[1]) ||
        !read_number(next.surround_color[2]) || !read_number(next.opacity) ||
        version != 1 || (active != 0 && active != 1) ||
        (recenter != 0 && recenter != 1) || (lock_aspect != 0 && lock_aspect != 1)) {
        return std::nullopt;
    }
    stream >> std::ws;
    if (!stream.eof()) return std::nullopt;
    for (const auto value : {next.width, next.height, next.distance, next.feather,
             next.corner_radius, next.curvature, next.surround_color[0],
             next.surround_color[1], next.surround_color[2], next.opacity}) {
        if (!std::isfinite(value)) return std::nullopt;
    }
    next.active = active == 1;
    next.recenter = recenter == 1;
    next.lock_aspect = lock_aspect == 1;
    return next;
}

inline float finite_clamp(float value, float lower, float upper, float fallback) {
    return std::clamp(std::isfinite(value) ? value : fallback, lower, upper);
}

// No timer, detector or persisted user settings. The caller serializes access
// and supplies only validated tracking poses. Separate anchors avoid restoring
// an old snapshot over an explicit user recenter made during the override.
template <typename Pose>
class PresentationState {
public:
    struct Anchor {
        Pose pose{};
        bool valid{};
        bool recenter_pending{true};
        bool capture(const Pose& next) {
            const bool update = !valid || recenter_pending;
            recenter_pending = false; // consume even on the first valid frame
            if (update) { pose = next; valid = true; }
            return update;
        }
    };

    const BridgeState& bridge() const { return m_bridge; }
    const Anchor& selected() const { return m_bridge.active ? m_transient : m_regular; }
    const Anchor& regular() const { return m_regular; }

    bool apply(const BridgeState& next) {
        if (m_suspended) return false;
        if (!next.active || !m_bridge.active) m_transient = {};
        if (next.active && next.recenter) m_transient.recenter_pending = true;
        m_bridge = next;
        return true;
    }
    void clear_transient() { m_bridge = {}; m_transient = {}; }
    void suspend(bool suspended) {
        m_suspended = suspended;
        clear_transient();
    }
    void reset() { clear_transient(); m_regular = {}; }

    bool update_enabled(bool regular_enabled) {
        if (regular_enabled != m_regular_enabled) m_regular = {};
        m_regular_enabled = regular_enabled;
        return regular_enabled || m_bridge.active;
    }
    void request_recenter() {
        m_regular.recenter_pending = true;
        if (m_bridge.active) m_transient.recenter_pending = true;
    }
    bool capture(const Pose& pose) {
        // A user recenter is allowed to update the ordinary anchor even while
        // temporarily displaying a cutscene. Bridge recenter never requests it.
        const bool regular_updated = m_regular_enabled && m_regular.capture(pose);
        return m_bridge.active ? m_transient.capture(pose) : regular_updated;
    }

private:
    BridgeState m_bridge{};
    Anchor m_regular{};
    Anchor m_transient{};
    bool m_regular_enabled{};
    bool m_suspended{};
};

} // namespace wuwa_window
