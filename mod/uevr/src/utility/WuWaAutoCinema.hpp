#pragma once
#include <cstdint>
#include <string>

// Pure lease policy. Callers serialize access and supply monotonic milliseconds.
// This class never owns or edits persisted display preferences.
namespace wuwa_auto_cinema {
constexpr uint8_t movie = 1, dialogue = 2, sources = movie | dialogue;
enum class Presentation : uint8_t { none, stereo_screen, mono_theatre };
struct View { bool screen, mono; };
inline View resolve(bool saved_screen, bool saved_mono, Presentation automatic) {
    if (automatic != Presentation::none) return {true, automatic == Presentation::mono_theatre};
    return {saved_screen || saved_mono, saved_mono};
}
inline void manual_screen(bool& saved_screen, bool& saved_mono, bool enabled) {
    saved_screen = enabled;
    if (!enabled) saved_mono = false; // An explicit screen OFF must leave all screen modes.
}
class Lease {
public:
    uint64_t start() { ++generation_; clear_lease(); return generation_; }
    void stop(uint64_t generation) { if (generation == generation_) { ++generation_; clear_lease(); } }
    void reset() { ++generation_; clear_lease(); world_.clear(); seen_ = 0; suppressed_ = false; }
    void invalidate() { clear_lease(); }
    uint64_t generation() const { return generation_; }
    uint8_t seen_sources() const { return seen_; }
    bool active(uint64_t now) const { return until_ != 0 && now >= last_ && now < until_ && !suppressed_; }
    Presentation presentation(uint64_t now) const { return active(now) ? presentation_ : Presentation::none; }
    void manual() { suppressed_ = true; clear_lease(); }
    // known includes validated false/playing/paused/preparing observations.
    // Unknown observations neither enter nor prolong the lease. A paused or
    // preparing movie may hold an established episode, but cannot start one.
    bool sample(uint64_t generation, const std::string& world, uint8_t playing,
                uint8_t holding, uint8_t known, Presentation presentation, uint64_t now) {
        if (!generation || generation != generation_ || world.empty() || world.size() > 32 ||
            (playing | holding | known) > sources || (playing & ~known) || (holding & ~known) ||
            presentation > Presentation::mono_theatre) return false;
        // Losing access to World is not proof that the episode ended. Release
        // presentation immediately while retaining a manual suppression fence.
        if (world == "0") { clear_lease(); last_ = now; return true; }
        if (world != world_) { clear_lease(); seen_ = 0; if (!world_.empty()) suppressed_ = false; world_ = world; }
        if (now < last_ || (until_ && now >= until_)) clear_lease();
        last_ = now;
        if (playing) {
            seen_ |= playing; clear_pending_ = false;
            if (suppressed_) return true;
            if (presentation == Presentation::none) { clear_lease(); return true; }
            presentation_ = presentation;
            if (until_) { until_ = now + 900; return true; }
            if (!enter_pending_) { enter_pending_ = true; enter_at_ = now; }
            else if (now - enter_at_ >= 100) { until_ = now + 900; enter_pending_ = false; }
            return true;
        }
        enter_pending_ = false;
        if ((holding & seen_) && until_ && !suppressed_) {
            clear_pending_ = false; until_ = now + 900; return true;
        }
        // Only a known stop of every source that owned this episode can end
        // it. Missing objects/reflection failures are not a fabricated stop.
        const auto required = seen_ ? seen_ : known;
        const bool stopped = required && (known & required) == required && !(holding & required);
        if (stopped) {
            if (!clear_pending_) { clear_pending_ = true; clear_at_ = now; }
            if (now - clear_at_ >= 500) { clear_lease(); seen_ = 0; suppressed_ = false; }
            else if (until_ && !suppressed_) until_ = now + 900;
        } else { clear_pending_ = false; }
        return true;
    }
    const char* state(uint64_t now) const {
        if (suppressed_) return "Manual override; waiting for this scene to end";
        if (active(now)) return clear_pending_ ? "Scene ending; restoring previous view" :
            (presentation_ == Presentation::mono_theatre ? "Automatic mono theatre active" : "Automatic stereo screen active");
        if (enter_pending_) return "Confirming cinematic signal";
        if (until_) return "Automatic presentation lease expired; previous view restored";
        return "Waiting for a verified cinematic signal";
    }
private:
    void clear_lease() { until_ = 0; enter_pending_ = clear_pending_ = false; }
    uint64_t generation_{}, last_{}, until_{}, enter_at_{}, clear_at_{};
    uint8_t seen_{};
    Presentation presentation_{Presentation::none};
    bool suppressed_{}, enter_pending_{}, clear_pending_{};
    std::string world_;
};
}
