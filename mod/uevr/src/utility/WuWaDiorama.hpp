#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>

namespace wuwa_diorama {

// A presentation override, never a saved setting. The normal slider/presets
// remain authoritative when the override is off, including intentional edits.
class Scale {
public:
    // How small the world gets (VR_WuWaDioramaScale). 10x is the action-figure
    // view; lower keeps a close game camera from crossing the eyes.
    static constexpr float default_miniature = 10.0f, min_miniature = 2.0f, max_miniature = 10.0f;

    bool requested() const noexcept { return requested_.load(); }
    void request(bool value) noexcept { requested_.store(value); }
    void set_miniature(float value) noexcept {
        miniature_.store(std::isfinite(value) ? std::clamp(value, min_miniature, max_miniature) : default_miniature);
    }
    float miniature() const noexcept { return miniature_.load(); }

    // One choice, miniature and normal scale for the complete viewport draw,
    // including nested draws. A UI click between the two eyes takes effect next draw.
    class Draw {
    public:
        Draw(Scale& owner, float normal) noexcept : owner_{owner}, previous_{current_} {
            if (previous_ && &previous_->owner_ == &owner) {
                value_ = previous_->value_;
            } else {
                const bool active = owner.requested();
                owner.active_.store(active);
                value_ = active ? owner.miniature() : normal;
            }
            current_ = this;
        }
        ~Draw() { current_ = previous_; }
        Draw(const Draw&) = delete;
        Draw& operator=(const Draw&) = delete;
        Draw(Draw&&) = delete;
        Draw& operator=(Draw&&) = delete;

    private:
        friend class Scale;
        Scale& owner_;
        const Draw* previous_;
        float value_{};
        static inline thread_local const Draw* current_{};
    };

    float effective(float normal) const noexcept {
        if (Draw::current_ && &Draw::current_->owner_ == this)
            return Draw::current_->value_;
        return active_.load() ? miniature() : normal;
    }

private:
    std::atomic<bool> requested_{false};
    std::atomic<bool> active_{false};
    std::atomic<float> miniature_{default_miniature};
};

} // namespace wuwa_diorama
