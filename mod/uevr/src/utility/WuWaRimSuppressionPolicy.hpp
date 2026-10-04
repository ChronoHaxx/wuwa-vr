#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

// Game-thread policy, independent of the SDK. IO resolves the named variable
// again for every read/write; never dereference an old, retained pointer.
namespace wuwa_rim {
struct Sample { uintptr_t identity{}; float value{}; int32_t integer{}; std::wstring text; };
enum class Code { Off, Waiting, TestActive, Suppressed, AlreadyZero, Restored,
    Unavailable, ApplyFailed, RestoreFailed, ExternalChange, PointerChanged };
struct Status {
    bool requested{}, eligible{}, owned{}, blocked{};
    Code code{Code::Off};
    std::optional<Sample> original;
};
inline const char* message(Code code) {
    switch (code) {
    case Code::Off: return "Rim workaround is off.";
    case Code::Waiting: return "Rim workaround waits for active native stereo with 2D screen off.";
    case Code::TestActive: return "Rim workaround waits for the current graphics comparison to end.";
    case Code::Suppressed: return "Character toon-depth rim is suppressed; the previous value is retained.";
    case Code::AlreadyZero: return "Character toon-depth rim was already zero; no write was needed.";
    case Code::Restored: return "The previous character rim value was restored and verified.";
    case Code::Unavailable: return "Rim variable is unavailable. Toggle off, then on to retry.";
    case Code::ApplyFailed: return "Rim suppression was not verified. Toggle off, then on to retry safely.";
    case Code::RestoreFailed: return "Rim restoration failed; the original is retained. Toggle off, then on to retry restoration first.";
    case Code::ExternalChange: return "Another setting changed the rim value; it was left untouched. Toggle off, then on to retry.";
    case Code::PointerChanged: return "The rim variable changed; no old pointer was used. Toggle off, then on to retry.";
    }
    return "Rim workaround is off.";
}

class Policy {
public:
    const Status& status() const { return s; }
    template<class IO> void tick(bool wanted, bool eligible, bool conflict, IO& io) {
        const bool rising = wanted && !s.requested;
        s.requested = wanted; s.eligible = eligible;
        if (rising) s.blocked = false;
        if (s.blocked) return; // Includes failed restoration: retain its original.
        if (s.owned) {
            if (!wanted || !eligible || restoring) { restore(io); return; }
            const auto current = io.read();
            if (!valid(current)) { restoring = true; block(Code::RestoreFailed); return; }
            if (current->identity != s.original->identity) { abandon(Code::PointerChanged); return; }
            if (!zero(*current)) { abandon(Code::ExternalChange); return; }
            return; // Observe ownership only; never enforce the value every frame.
        }
        if (!wanted) { s.code = Code::Off; return; }
        if (!eligible) { s.code = Code::Waiting; return; }
        if (conflict) { s.code = Code::TestActive; return; }
        const auto before = io.read();
        if (!valid(before)) { block(Code::Unavailable); return; }
        s.original = before; s.owned = true;
        if (zero(*before)) { s.code = Code::AlreadyZero; return; }
        io.write(*before, L"0");
        const auto after = io.read();
        if (!valid(after)) { restoring = true; block(Code::ApplyFailed); return; }
        if (after->identity != before->identity) { abandon(Code::PointerChanged); return; }
        if (zero(*after)) { s.code = Code::Suppressed; return; }
        if (same(*after, *before)) { abandon(Code::ApplyFailed); return; }
        abandon(Code::ExternalChange);
    }
private:
    static bool valid(const std::optional<Sample>& value) {
        return value && value->identity && std::isfinite(value->value) && !value->text.empty();
    }
    static bool zero(const Sample& v) { return v.value == 0.f && v.integer == 0; }
    static bool same(const Sample& a, const Sample& b) { return a.value == b.value && a.integer == b.integer; }
    void block(Code code) { s.blocked = true; s.code = code; }
    void abandon(Code code) { s.owned = false; restoring = false; s.original.reset(); block(code); }
    template<class IO> void restore(IO& io) {
        restoring = true;
        const auto current = io.read();
        if (!valid(current)) { block(Code::RestoreFailed); return; }
        if (current->identity != s.original->identity) { abandon(Code::PointerChanged); return; }
        // A delayed successful restore may already have put the original back.
        if (!same(*current, *s.original)) {
            if (!zero(*current)) { abandon(Code::ExternalChange); return; }
            io.write(*current, s.original->text);
            const auto after = io.read();
            if (!valid(after)) { block(Code::RestoreFailed); return; }
            if (after->identity != s.original->identity) { abandon(Code::PointerChanged); return; }
            if (!same(*after, *s.original)) {
                if (!zero(*after)) { abandon(Code::ExternalChange); return; }
                block(Code::RestoreFailed); return;
            }
        }
        s.owned = false; s.original.reset(); restoring = false; s.code = Code::Restored;
    }
    Status s;
    bool restoring{};
};
} // namespace wuwa_rim
