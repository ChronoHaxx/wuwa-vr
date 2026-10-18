#pragma once
// Full VR ground detail (18 Oct 2026, game 3.7). Kuro's landscape chooses its LOD from
// distance and screen size together (r.Landscape.KuroLODMode 2). The square VR eye shows
// the ground smaller than the 16:9 view does, so the terrain near the player drops below
// LOD 0, and only LOD 0 cuts the holes where stone steps and ledges sit
// (r.LandscapeAutoMaterialMaskLOD 1). Dirt then covers the steps and the feet sink in,
// because collision follows the real steps. Mode 1 keeps LOD 0 near the player, as the
// flat view has it. Simulator, Kuro copy, rock stairs at (-69304, -69227): mode 1 brought
// the steps back; 39.0 vs 39.2 fps; both eyes kept the same LOD factor and FOV.
// Game-thread policy, independent of the SDK (dev/test_terrain_lod.cpp). IO resolves the
// variable again for every read and write.
#include "WuWaRimSuppressionPolicy.hpp"

namespace wuwa_terrain_lod {
using Sample = wuwa_rim::Sample;
inline constexpr float target_value = 1.f;
inline constexpr const wchar_t* target_text = L"1";
// The game may set its own value again (for example a graphics-quality change); that value
// becomes the one to restore. More than this many inside a minute is a fight, so stop.
inline constexpr uint32_t readopt_limit = 5;
inline constexpr uint64_t readopt_window_ms = 60000;

enum class Code { Off, Waiting, TestActive, Applied, AlreadySet, Restored, LeftChanged,
    Unavailable, ApplyFailed, RestoreFailed, PointerChanged, GameKeepsChanging };
struct Status {
    bool requested{}, eligible{}, owned{}, blocked{};
    Code code{Code::Off};
    std::optional<Sample> original;
    uint32_t readopts{};
};
// Log and diagnostic text, English only.
inline const char* message(Code code) {
    switch (code) {
    case Code::Off: return "Full ground detail is off; the game's terrain setting is untouched.";
    case Code::Waiting: return "Waiting for Full VR (stereo and mono screens keep the game's setting).";
    case Code::TestActive: return "Waiting for the current graphics comparison to end.";
    case Code::Applied: return "Terrain LOD by distance in Full VR; the game's value is kept for the screens.";
    case Code::AlreadySet: return "Terrain LOD was already by distance; nothing was written.";
    case Code::Restored: return "The game's terrain setting was restored and verified.";
    case Code::LeftChanged: return "Something else changed the terrain setting; it was left as it is.";
    case Code::Unavailable: return "The terrain variable is unavailable. Toggle off, then on to retry.";
    case Code::ApplyFailed: return "The terrain setting did not change. Toggle off, then on to retry.";
    case Code::RestoreFailed: return "The game's terrain setting was not restored; its value is kept. Toggle off, then on to retry.";
    case Code::PointerChanged: return "The terrain variable changed; no old pointer was used. Toggle off, then on to retry.";
    case Code::GameKeepsChanging: return "The game keeps changing the terrain setting, so it is left to the game. Toggle off, then on to retry.";
    }
    return "Full ground detail is off; the game's terrain setting is untouched.";
}

class Policy {
public:
    const Status& status() const { return s; }
    template<class IO> void tick(bool wanted, bool eligible, bool conflict, uint64_t now_ms, IO& io) {
        const bool rising = wanted && !s.requested;
        s.requested = wanted; s.eligible = eligible;
        if (rising) { s.blocked = false; window_count = 0; }
        if (s.blocked) return; // A failed restore keeps owned and original for the retry.
        if (s.owned) {
            if (!wanted || !eligible) { restore(io); return; }
            const auto current = io.read();
            if (!valid(current)) { block(Code::Unavailable); return; }
            if (current->identity != s.original->identity) { abandon(Code::PointerChanged); return; }
            if (target(*current)) return; // Observe only; never enforce every frame.
            if (window_count == 0 || now_ms - window_start_ms > readopt_window_ms) { window_start_ms = now_ms; window_count = 0; }
            if (++window_count > readopt_limit) { abandon(Code::GameKeepsChanging); return; }
            ++s.readopts;
            s.owned = false; s.original.reset();
            apply(current, io);
            return;
        }
        if (!wanted) { s.code = Code::Off; return; }
        if (!eligible) { s.code = Code::Waiting; return; }
        if (conflict) { s.code = Code::TestActive; return; }
        apply(io.read(), io);
    }
private:
    static bool valid(const std::optional<Sample>& value) {
        return value && value->identity && std::isfinite(value->value) && !value->text.empty();
    }
    static bool target(const Sample& v) { return v.value == target_value && v.integer == static_cast<int32_t>(target_value); }
    static bool same(const Sample& a, const Sample& b) { return a.value == b.value && a.integer == b.integer; }
    void block(Code code) { s.blocked = true; s.code = code; }
    void abandon(Code code) { s.owned = false; s.original.reset(); block(code); }
    void release(Code code) { s.owned = false; s.original.reset(); s.code = code; }
    template<class IO> void apply(const std::optional<Sample>& before, IO& io) {
        if (!valid(before)) { block(Code::Unavailable); return; }
        s.original = before; s.owned = true;
        if (target(*before)) { s.code = Code::AlreadySet; return; }
        io.write(*before, target_text);
        const auto after = io.read();
        if (!valid(after)) { block(Code::ApplyFailed); return; } // Unknown: keep the original to restore.
        if (after->identity != before->identity) { abandon(Code::PointerChanged); return; }
        if (target(*after)) { s.code = Code::Applied; return; }
        abandon(Code::ApplyFailed);
    }
    template<class IO> void restore(IO& io) {
        const auto current = io.read();
        if (!valid(current)) { block(Code::RestoreFailed); return; }
        if (current->identity != s.original->identity) { abandon(Code::PointerChanged); return; }
        if (!target(*current)) { release(Code::LeftChanged); return; }
        if (!same(*current, *s.original)) {
            io.write(*current, s.original->text);
            const auto after = io.read();
            if (!valid(after) || after->identity != s.original->identity || !same(*after, *s.original)) {
                block(Code::RestoreFailed); return;
            }
        }
        release(Code::Restored);
    }
    Status s;
    uint64_t window_start_ms{};
    uint32_t window_count{};
};
} // namespace wuwa_terrain_lod
