#pragma once

#include <cmath>
#include <cstdint>

namespace wuwa_depth_scale {

// This travels with the rendered PipelineState, not the current UI setting.
// required distinguishes our scoped native path from unchanged legacy paths.
struct Snapshot {
    bool required{};
    float world_units_per_metre{};

    bool valid() const noexcept {
        return std::isfinite(world_units_per_metre) && world_units_per_metre > 0.0f;
    }

    Snapshot for_submission(std::uint32_t rendered_frame, std::uint32_t stored_frame,
                            bool scoped) const noexcept {
        if (scoped && (rendered_frame != stored_frame || !required)) return {true, 0.0f};
        return *this;
    }
};

class Draw {
public:
    explicit Draw(bool required) noexcept : previous_{current_} {
        sample_.required = required;
        current_ = this;
    }
    ~Draw() { current_ = previous_; }
    Draw(const Draw&) = delete;
    Draw& operator=(const Draw&) = delete;
    Draw(Draw&&) = delete;
    Draw& operator=(Draw&&) = delete;

    // Called with the exact product used for an actual stereo view offset.
    // Any inconsistent eye/capture input invalidates this draw permanently.
    static void record(float units) noexcept {
        if (!current_ || !current_->sample_.required) return;
        auto& draw = *current_;
        if (!std::isfinite(units) || units <= 0.0f ||
            (draw.observed_ && draw.sample_.world_units_per_metre != units)) {
            draw.conflict_ = true;
        }
        draw.observed_ = true;
        draw.sample_.world_units_per_metre = draw.conflict_ ? 0.0f : units;
    }

    static Snapshot snapshot() noexcept {
        return current_ ? current_->sample_ : Snapshot{};
    }

private:
    // A nested viewport owns its sample: its engine WTM may differ even when
    // it inherits the same diorama choice. Restore the outer sample on exit.
    Draw* previous_{};
    Snapshot sample_{};
    bool observed_{};
    bool conflict_{};
    static inline thread_local Draw* current_{};
};

} // namespace wuwa_depth_scale
