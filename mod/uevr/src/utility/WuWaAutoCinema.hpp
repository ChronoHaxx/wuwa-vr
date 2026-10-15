#pragma once
#ifndef N_
#define N_(english) english // shown English for the catalog (WuWaLocalization.hpp)
#endif
#include <cstdint>
#include <string>

// Pure lease policy. Callers serialize access and supply monotonic milliseconds.
// This class never owns or edits persisted display preferences.
namespace wuwa_auto_cinema {
// lease-v2 samples add the plot system's own state (wuwa_auto_cinema.lua). The story
// layout uses it in place of the plot-skip widget whenever it is known; automatic
// cinema keeps the movie and widget bits only. lease-v3 adds a story picture on
// screen (the plot view's SprPhoto), for automatic cinema only.
constexpr uint8_t movie = 1, dialogue = 2, plot = 4, picture = 8, sources = movie | dialogue | picture;
struct Bits { uint8_t active, hold, known; };
inline Bits cinema_bits(Bits b) { return {uint8_t(b.active & sources), uint8_t(b.hold & sources), uint8_t(b.known & sources)}; }
inline Bits story_bits(Bits b) {
    constexpr uint8_t scene = movie | dialogue;
    if (!(b.known & plot)) return {uint8_t(b.active & scene), uint8_t(b.hold & scene), uint8_t(b.known & scene)};
    const auto story = [](uint8_t v) { return uint8_t((v & movie) | ((v & plot) ? dialogue : 0)); };
    return {story(b.active), story(b.hold), uint8_t((b.known & movie) | dialogue)};
}
// lease-v4 (test 17g): the camera is at the character screen's showroom. Only the
// character-screen layout reads it, through a lease of its own (as the dialogue bit).
constexpr uint8_t showroom = 16;
inline Bits showroom_bits(Bits b) {
    const auto at = [](uint8_t v) { return uint8_t((v & showroom) ? dialogue : 0); };
    return {at(b.active), at(b.hold), at(b.known)};
}
enum class Presentation : uint8_t { none, stereo_screen, mono_theatre };
// What automatic cinema asks for: a story video wins, then a story picture (also for a
// moment after one, so the crossfade between pictures does not flip the view), then
// the scene's own choice.
inline Presentation choose(Bits b, uint8_t seen, bool picture_recent, Presentation story, Presentation pictures) {
    if ((b.active & movie) || (b.hold & seen & movie)) return Presentation::mono_theatre;
    if ((b.active & picture) || picture_recent) return pictures;
    return story;
}
struct View { bool screen, mono; };
inline View resolve(bool saved_screen, bool saved_mono, Presentation automatic) {
    if (automatic != Presentation::none) return {true, automatic == Presentation::mono_theatre};
    return {saved_screen || saved_mono, saved_mono};
}
inline void manual_screen(bool& saved_screen, bool& saved_mono, bool enabled) {
    saved_screen = enabled;
    if (!enabled) saved_mono = false; // An explicit screen OFF must leave all screen modes.
}
// 1.2.3 turns automatic cinema on once for a profile saved before it (no marker): such a
// profile holds the old defaults, off with story scenes on the stereo screen. Story scenes
// then keep the player's view (choice 0). A profile that already had it on keeps its choices.
constexpr const char* defaults_marker = "VR_AutoCinemaDefaults";
struct Saved { bool automatic; int32_t story; };
inline Saved upgrade(bool marked, Saved saved) {
    if (marked || saved.automatic) return saved;
    return {true, 0};
}
class Lease {
public:
    uint64_t start() { ++generation_; clear_lease(); return generation_; }
    void stop(uint64_t generation) { if (generation == generation_) { ++generation_; clear_lease(); } }
    void reset() { ++generation_; clear(); }
    // As reset, but the running producer keeps its generation: the story-scene
    // detector shares it and must survive a switch of automatic cinema.
    void clear() { clear_lease(); world_.clear(); seen_ = 0; suppressed_ = false; }
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
            ((playing | holding | known) & ~sources) || (playing & ~known) || (holding & ~known) ||
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
        if (suppressed_) return N_("Manual override; waiting for this scene to end");
        if (active(now)) return clear_pending_ ? N_("Scene ending; restoring previous view") :
            (presentation_ == Presentation::mono_theatre ? N_("Automatic mono theatre active") : N_("Automatic stereo screen active"));
        if (enter_pending_) return N_("Confirming cinematic signal");
        if (until_) return N_("Automatic presentation lease expired; previous view restored");
        return N_("Waiting for a verified cinematic signal");
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
