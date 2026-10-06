#pragma once

#include <array>
#include <cstdint>

// Pure value-state input mixing. The caller owns synchronization, obtains both
// valid VR hands, maps/deadzones their controls, and calls only the target slot.
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

// Off/unknown modes have no target. Merge modes explicitly name slot 0..3;
// there is no automatic "first connected controller" selection.
constexpr int target_slot(int mode) noexcept {
    return mode == 1 ? 0 : mode >= 2 && mode <= 5 ? mode - 2 : -1;
}

// Diagnostics describe the same decision which produced Result, not merely
// whether the runtime recently supplied a sample. External blockers are set by
// the caller before Mixer runs and never change the mixing policy.
enum class Status {
    Off, WaitingSample, WaitingPoll, WaitingRelease, MissingSlot, StaleSample,
    Armed, MenuReady, Passthrough, SlotFiltered, InputMuted, GameUnfocused,
    OpenXRRequired
};

struct Result {
    Pad pad{};
    bool connected{};
    bool vr_active{};
    Status status{Status::Off};
};

class Mixer {
public:
    static constexpr std::uint64_t max_age_ms = 250;
    static constexpr std::int16_t physical_look_deadzone = 8000;

    Result apply(int mode, Pad physical, bool connected, Pad vr, bool valid,
        std::uint64_t sample_ms, std::uint64_t now_ms, bool ui_open) noexcept {
        const bool merge = mode >= 2 && mode <= 5;
        if (target_slot(mode) < 0) {
            reset();
            return {physical, connected, false};
        }

        // UI transitions rearm, but a stable open UI may consume VR navigation.
        // Delivery to the game while its overlay is open is the caller's job.
        if (mode != mode_ || ui_open != ui_open_ || (merge && connected && !connected_)) armed_ = false;
        mode_ = mode;
        ui_open_ = ui_open;
        connected_ = connected;

        const bool backwards = have_time_ && (now_ms < last_now_ms_ || sample_ms < last_sample_ms_);
        const bool fresh = sample_ms <= now_ms && now_ms - sample_ms <= max_age_ms;
        // Resynchronize the time epoch on rejected samples; they still cannot
        // arm. A subsequent valid neutral sample is needed after clock reset.
        have_time_ = true;
        last_now_ms_ = now_ms;
        last_sample_ms_ = sample_ms;
        if (!valid || !fresh || backwards || (merge && !connected)) armed_ = false;
        const auto status = merge && !connected ? Status::MissingSlot :
            !valid ? Status::WaitingSample : !fresh || backwards ? Status::StaleSample : Status::WaitingRelease;
        const Result fallback = merge && connected ? Result{physical, true, false, status} :
            Result{{}, false, false, status};
        if (!valid || !fresh || backwards || (merge && !connected)) return fallback;

        // Only VR controls must be released. Real treadmill movement continues
        // unchanged while the player releases the VR controllers to arm them.
        if (!armed_) {
            if (!neutral(vr)) return fallback;
            armed_ = true;
        }
        if (!merge) return {vr, true, true, Status::Armed};

        Pad mixed = physical;
        mixed.buttons = static_cast<std::uint16_t>(physical.buttons | vr.buttons);
        mixed.lt = physical.lt > vr.lt ? physical.lt : vr.lt;
        mixed.rt = physical.rt > vr.rt ? physical.rt : vr.rt;
        // Preserve the physical right-stick pair if either axis is beyond the
        // deadzone. Never combine one source's X with the other source's Y.
        if (!physical_look(physical.rx) && !physical_look(physical.ry)) {
            mixed.rx = vr.rx;
            mixed.ry = vr.ry;
        }
        // lx/ly remain byte-for-byte physical, including a stationary treadmill.
        return {mixed, true, true, Status::Armed};
    }

    void reset() noexcept {
        mode_ = -1;
        armed_ = false;
        connected_ = false;
        ui_open_ = false;
        have_time_ = false;
        last_now_ms_ = last_sample_ms_ = 0;
    }

private:
    static constexpr bool physical_look(std::int16_t value) noexcept {
        return value > physical_look_deadzone || value < -physical_look_deadzone;
    }
    int mode_{-1};
    bool armed_{}, connected_{}, ui_open_{}, have_time_{};
    std::uint64_t last_now_ms_{}, last_sample_ms_{};
};

// The caller holds its input mutex for every method. Successful samples alone
// never claim that the game polled the selected slot or that neutral rearm ran.
class Readiness {
public:
    void reset() noexcept { *this = {}; }

    void sample(int mode, Status source, std::uint64_t now_ms) noexcept {
        if (mode != sample_mode_ || source != Status::Armed || source_ != Status::Armed)
            have_poll_ = false;
        sample_mode_ = mode;
        source_ = source;
        sample_ms_ = now_ms;
    }

    void poll(int mode, const Result& result, Status blocker, bool ui_open, std::uint64_t now_ms) noexcept {
        poll_mode_ = mode;
        delivered_ = blocker != Status::Armed ? blocker : result.status;
        ui_open_ = ui_open;
        poll_ms_ = now_ms;
        have_poll_ = true;
    }

    Status read(int mode, std::uint64_t now_ms, bool ui_open) const noexcept {
        if (target_slot(mode) < 0) return Status::Off;
        if (mode != sample_mode_) return Status::WaitingSample;
        if (!fresh(sample_ms_, now_ms)) return Status::StaleSample;
        if (source_ != Status::Armed) return source_;
        if (!have_poll_ || poll_mode_ != mode || !fresh(poll_ms_, now_ms) || ui_open != ui_open_)
            return Status::WaitingPoll;
        return delivered_ == Status::Armed && ui_open ? Status::MenuReady : delivered_;
    }

private:
    static bool fresh(std::uint64_t sample, std::uint64_t now) noexcept {
        return sample <= now && now - sample <= Mixer::max_age_ms;
    }
    int sample_mode_{-1}, poll_mode_{-1};
    Status source_{Status::WaitingSample}, delivered_{Status::WaitingPoll};
    std::uint64_t sample_ms_{}, poll_ms_{};
    bool have_poll_{}, ui_open_{};
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
