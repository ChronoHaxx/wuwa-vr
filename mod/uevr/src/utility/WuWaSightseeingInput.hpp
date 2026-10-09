#pragma once

#include <array>
#include <cstdint>

// Pure value-state input mixing for the optional VR-controller walking mode.
// The caller owns synchronization, reads both VR hands, maps/deadzones their
// controls, and calls Mixer only for the chosen XInput slot.
namespace wuwa_sightseeing {

struct Pad {
    std::uint16_t buttons{};
    std::uint8_t lt{}, rt{};
    std::int16_t lx{}, ly{}, rx{}, ry{};
};

constexpr bool operator==(const Pad& a, const Pad& b) noexcept {
    return a.buttons == b.buttons && a.lt == b.lt && a.rt == b.rt &&
        a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry;
}
constexpr bool operator!=(const Pad& a, const Pad& b) noexcept { return !(a == b); }
constexpr bool neutral(const Pad& pad) noexcept { return pad == Pad{}; }

constexpr std::uint16_t start_bit = 0x0010, back_bit = 0x0020;
// The left Menu button maps to Start (or Back with the left grip). Its raw
// bits belong to MenuGesture and never reach the game directly.
constexpr std::uint16_t menu_bits = start_bit | back_bit;

// How VR input shares the selected slot with a real Xbox pad or treadmill.
// The values are the saved menu indices.
enum class Style { Both = 0, LastUsed = 1, VrOnly = 2 };
constexpr Style style_from(int value) noexcept {
    return value == 1 ? Style::LastUsed : value == 2 ? Style::VrOnly : Style::Both;
}
constexpr int slot_from(int value) noexcept { return value >= 0 && value <= 3 ? value : 0; }

enum class Source { None, Vr, Physical, Both };

// Diagnostics describe the same decision which produced Result. External
// blockers are reported by the caller's engine-thread sample.
enum class Status {
    Off, WaitingVr, WaitingPoll, Active, MenuReady,
    Passthrough, SlotFiltered, InputMuted, GameUnfocused, OpenXRRequired
};

struct Result {
    Pad pad{};
    Pad vr{};           // the VR share alone, after masking (UEVR menu navigation)
    bool connected{};
    bool vr_ready{};    // VR is contributing, though some inputs may be held back
    Source source{Source::None};
    bool held{};        // a VR input held across a transition waits for release
    Status status{Status::Off};
};

// One bit per input: 16 buttons, both triggers, both sticks.
constexpr std::uint32_t lt_bit = 1u << 16, rt_bit = 1u << 17, ls_bit = 1u << 18, rs_bit = 1u << 19;
// XInput's documented thresholds; VR sticks are already deadzoned by the reader.
constexpr std::int16_t left_deadzone = 7849, right_deadzone = 8689;
constexpr std::uint8_t trigger_threshold = 30;

constexpr std::int64_t magnitude(std::int16_t x, std::int16_t y) noexcept {
    return std::int64_t{x} * x + std::int64_t{y} * y;
}

// Any input away from rest. Masks use this, so a held input stays ignored
// until it has fully returned to zero.
constexpr std::uint32_t held(const Pad& p) noexcept {
    std::uint32_t set = p.buttons;
    if (p.lt) set |= lt_bit;
    if (p.rt) set |= rt_bit;
    if (p.lx || p.ly) set |= ls_bit;
    if (p.rx || p.ry) set |= rs_bit;
    return set;
}

// Inputs deliberately engaged, past XInput's thresholds. "Last used" ownership
// changes on a new engagement, so stick drift and steady holds never steal it.
constexpr std::uint32_t engaged(const Pad& p) noexcept {
    std::uint32_t set = p.buttons;
    if (p.lt > trigger_threshold) set |= lt_bit;
    if (p.rt > trigger_threshold) set |= rt_bit;
    if (magnitude(p.lx, p.ly) > std::int64_t{left_deadzone} * left_deadzone) set |= ls_bit;
    if (magnitude(p.rx, p.ry) > std::int64_t{right_deadzone} * right_deadzone) set |= rs_bit;
    return set;
}

constexpr Pad without(Pad p, std::uint32_t mask) noexcept {
    p.buttons = static_cast<std::uint16_t>(p.buttons & ~mask);
    if (mask & lt_bit) p.lt = 0;
    if (mask & rt_bit) p.rt = 0;
    if (mask & ls_bit) p.lx = p.ly = 0;
    if (mask & rs_bit) p.rx = p.ry = 0;
    return p;
}

// Buttons combine and triggers take the larger value. Each stick pair comes
// whole from the source deflected further; one source's X never pairs with the
// other's Y. Ties keep the physical pair.
constexpr Pad merge(const Pad& physical, const Pad& vr) noexcept {
    Pad m = physical;
    m.buttons = static_cast<std::uint16_t>(physical.buttons | vr.buttons);
    m.lt = physical.lt > vr.lt ? physical.lt : vr.lt;
    m.rt = physical.rt > vr.rt ? physical.rt : vr.rt;
    if (magnitude(vr.lx, vr.ly) > magnitude(physical.lx, physical.ly)) { m.lx = vr.lx; m.ly = vr.ly; }
    if (magnitude(vr.rx, vr.ry) > magnitude(physical.rx, physical.ry)) { m.rx = vr.rx; m.ry = vr.ry; }
    return m;
}

class Mixer {
public:
    static constexpr std::uint64_t max_age_ms = 250;

    Result apply(bool enabled, Style style, Pad physical, bool connected, Pad vr, bool valid,
        std::uint64_t sample_ms, std::uint64_t now_ms, bool ui_open) noexcept {
        if (!enabled) {
            reset();
            return {physical, {}, connected, false, connected ? Source::Physical : Source::None, false, Status::Off};
        }
        if (!connected) physical = {};

        const bool backwards = have_time_ && (now_ms < last_now_ms_ || sample_ms < last_sample_ms_);
        const bool fresh = sample_ms <= now_ms && now_ms - sample_ms <= max_age_ms;
        have_time_ = true;
        last_now_ms_ = now_ms;
        last_sample_ms_ = sample_ms;
        const bool ready = valid && fresh && !backwards;
        if (!ready) vr = {};

        // Inputs already held when VR starts contributing, or when UEVR opens
        // or closes, belong to that transition. Each is ignored until released;
        // everything else works at once.
        const bool starting = !active_ || style != style_;
        if (starting || ready != ready_ || ui_open != ui_open_) mask_ = held(vr);
        mask_ &= held(vr);
        const Pad live = without(vr, mask_);

        if (starting) {
            owner_ = Source::Vr;
            last_vr_ = live;
            last_physical_ = physical;
        }
        const auto vr_new = engaged(live) & ~engaged(last_vr_);
        const auto physical_new = engaged(physical) & ~engaged(last_physical_);
        if (physical_new && !vr_new) owner_ = Source::Physical;
        else if (vr_new && !physical_new) owner_ = Source::Vr;
        last_vr_ = live;
        last_physical_ = physical;
        active_ = true;
        style_ = style;
        ready_ = ready;
        ui_open_ = ui_open;

        Result r{{}, live, true, ready, Source::None, ready && mask_ != 0, ready ? Status::Active : Status::WaitingVr};
        switch (style) {
        case Style::VrOnly:
            r.pad = live;
            r.source = ready ? Source::Vr : Source::None;
            break;
        case Style::LastUsed:
            if (ready && owner_ == Source::Vr) {
                r.pad = live;
                r.source = Source::Vr;
            } else {
                r.pad = physical;
                r.source = connected ? Source::Physical : Source::None;
            }
            break;
        case Style::Both:
            r.pad = merge(physical, live);
            r.source = ready && connected ? Source::Both : ready ? Source::Vr :
                connected ? Source::Physical : Source::None;
            break;
        }
        return r;
    }

    void reset() noexcept { *this = {}; }

private:
    bool active_{}, ready_{}, ui_open_{}, have_time_{};
    Style style_{Style::Both};
    Source owner_{Source::Vr};
    std::uint32_t mask_{};
    Pad last_vr_{}, last_physical_{};
    std::uint64_t last_now_ms_{}, last_sample_ms_{};
};

// Hold the left Menu button to turn VR controllers on or off. A shorter press
// is still Start (or Back with the left grip), sent briefly on release.
class MenuGesture {
public:
    static constexpr std::uint64_t hold_ms = 1000;
    static constexpr std::uint64_t pulse_ms = 100;
    static constexpr int pulse_polls = 2;

    // Engine thread, once per VR sample. Returns true when the hold completes.
    bool update(bool valid, std::uint16_t raw_buttons, bool enabled, std::uint64_t now_ms) noexcept {
        const auto bits = static_cast<std::uint16_t>(valid ? raw_buttons & menu_bits : 0);
        if (!valid) {
            down_ = fired_ = false; // lost tracking cancels a press; no tap, no toggle
            return false;
        }
        if (bits) {
            if (!down_) {
                down_ = true;
                fired_ = false;
                since_ms_ = now_ms;
                bits_ = bits;
            }
            if (!fired_ && now_ms >= since_ms_ && now_ms - since_ms_ >= hold_ms) {
                fired_ = true;
                return true;
            }
            return false;
        }
        if (down_ && !fired_ && enabled) {
            pulse_ = bits_;
            pulse_until_ms_ = now_ms + pulse_ms;
            pulse_count_ = 0;
        }
        down_ = fired_ = false;
        return false;
    }

    // XInput hook, once per poll of the selected slot. Lasts pulse_ms and at
    // least pulse_polls polls, so a slow frame cannot swallow the tap.
    std::uint16_t take(std::uint64_t now_ms) noexcept {
        if (!pulse_) return 0;
        if (now_ms >= pulse_until_ms_ && pulse_count_ >= pulse_polls) {
            pulse_ = 0;
            return 0;
        }
        ++pulse_count_;
        return pulse_;
    }

    bool holding() const noexcept { return down_ && !fired_; }
    void cancel_pulse() noexcept { pulse_ = 0; }
    void reset() noexcept { *this = {}; }

private:
    bool down_{}, fired_{};
    std::uint16_t bits_{}, pulse_{};
    int pulse_count_{};
    std::uint64_t since_ms_{}, pulse_until_ms_{};
};

struct Snapshot {
    Status status{Status::Off};
    Source source{Source::None};
    bool held{};
};

// The caller holds its input mutex for every method.
class Readiness {
public:
    void reset() noexcept { *this = {}; }

    // Engine thread: Active when VR can contribute, otherwise the blocker.
    void sample(Status source, std::uint64_t now_ms) noexcept {
        source_ = source;
        sample_ms_ = now_ms;
        have_sample_ = true;
    }

    // XInput hook: what the selected slot delivered.
    void poll(const Result& result, std::uint64_t now_ms) noexcept {
        result_ = result;
        poll_ms_ = now_ms;
        have_poll_ = true;
    }

    bool polled(std::uint64_t now_ms) const noexcept { return have_poll_ && fresh(poll_ms_, now_ms); }

    Snapshot read(bool enabled, std::uint64_t now_ms, bool ui_open) const noexcept {
        if (!enabled) return {};
        if (!have_sample_ || !fresh(sample_ms_, now_ms)) return {Status::WaitingVr};
        // Blockers explain why VR is absent even if the game has stopped polling.
        if (source_ != Status::Active && source_ != Status::WaitingVr) return {source_};
        if (!polled(now_ms)) return {Status::WaitingPoll};
        if (ui_open) return {Status::MenuReady, result_.source};
        return {result_.status, result_.source, result_.held};
    }

private:
    static bool fresh(std::uint64_t sample, std::uint64_t now) noexcept {
        return sample <= now && now - sample <= Mixer::max_age_ms;
    }
    Status source_{Status::WaitingVr};
    Result result_{};
    std::uint64_t sample_ms_{}, poll_ms_{};
    bool have_sample_{}, have_poll_{};
};

class PacketCounter {
public:
    // The delivered state owns packet changes, not the raw backend's counters.
    // Deterministic zero initialization makes API 1.3/1.4 polling order irrelevant.
    // Keep this single counter shared across both hooks, under the caller's mutex.
    std::uint32_t stamp(std::uint32_t slot, bool connected, Pad pad, std::uint32_t raw_packet) noexcept {
        if (slot >= states_.size()) return raw_packet;
        auto& state = states_[slot];
        if (!state.seen) {
            state.seen = true;
        } else if (state.connected != connected || state.pad != pad) {
            ++state.packet; // Defined unsigned wrap; equality remains the XInput contract.
        }
        state.connected = connected;
        state.pad = pad;
        return state.packet;
    }

private:
    struct State {
        Pad pad{};
        std::uint32_t packet{};
        bool connected{}, seen{};
    };
    std::array<State, 4> states_{};
};

} // namespace wuwa_sightseeing
