#pragma once

#include <cstdint>
#include <limits>
#include <optional>

// Scheduling only: no SDK calls, camera changes or proof of GPU completion.
// The game-thread owner calls tick once per engine tick, then confirms each
// Start/Stop action synchronously. Pair sequence counts observed stereo pairs.
namespace wuwa_clv {

enum class Action { None, Start, Stop };
enum class Source { None, Automatic, Manual };
enum class StartResult { Started, Unavailable, AlreadyActive };

struct Snapshot {
    std::uint64_t now_ms{}, pair_sequence{}, last_pair_ms{}, arm_sequence{};
    std::uint32_t stable_frames{};
    bool automatic_armed{}, automatic_enabled{};
};

struct Decision {
    Action action{Action::None};
    Source source{Source::None};
};

struct PolicyStatus {
    std::uint64_t pulse_attempts{}, pair_observed_pulses{}, missed_pair_pulses{};
    std::uint64_t start_failures{}, stop_failures{}, existing_value_skips{}, exhausted_requests{};
    std::uint32_t manual_attempts{}, automatic_attempts{};
    bool manual_pending{}, automatic_pending{}, owns_refresh{};
};

class RefreshPolicy {
public:
    static constexpr std::uint64_t pair_max_age_ms = 1500;
    static constexpr std::uint64_t automatic_interval_ms = 20000;
    static constexpr std::uint64_t manual_retry_ms = 1000;
    static constexpr std::uint32_t required_stable_frames = 90;
    static constexpr std::uint32_t max_pulse_attempts = 3;

    // Repeated requests coalesce while pending, preserving the original retry
    // budget. A new request after completion/exhaustion begins a new episode.
    void request_manual(std::uint32_t frames = 1) noexcept {
        if (status_.manual_pending) return;
        status_.manual_pending = true;
        status_.manual_attempts = 0;
        manual_frames_ = frames < 1 ? 1 : frames > 10 ? 10 : frames;
        manual_due_ms_ = 0;
    }

    Decision tick(const Snapshot& snapshot) noexcept {
        snapshot_ = snapshot;
        if (!have_auto_generation_ || snapshot.arm_sequence != auto_generation_) {
            have_auto_generation_ = true;
            auto_generation_ = snapshot.arm_sequence;
            automatic_retired_ = false;
            status_.automatic_attempts = 0;
        }
        status_.automatic_pending = snapshot.automatic_enabled && snapshot.automatic_armed && !automatic_retired_;

        // Restoration always wins, including disabled/inactive rendering and a
        // clock which no longer advances. Failed restoration retries each tick.
        if (status_.owns_refresh) {
            if (snapshot.pair_sequence > pulse_pair_sequence_ &&
                snapshot.last_pair_ms >= pulse_start_ms_ && snapshot.last_pair_ms <= snapshot.now_ms)
                pair_observed_ = true;
            if (ticks_left_ > 0) --ticks_left_;
            if (ticks_left_ == 0) {
                awaiting_stop_ = true;
                return {Action::Stop, active_source_};
            }
            return {};
        }
        if (awaiting_start_) return {}; // The owner has not confirmed its write.

        // Old stable-frame counts never make stale/nonexistent pairs eligible.
        const bool recent = snapshot.pair_sequence != 0 && snapshot.last_pair_ms <= snapshot.now_ms &&
            snapshot.now_ms - snapshot.last_pair_ms < pair_max_age_ms;
        if (!recent) return {};

        Source source = Source::None;
        if (status_.manual_pending && snapshot.now_ms >= manual_due_ms_) source = Source::Manual;
        else if (status_.automatic_pending && snapshot.stable_frames >= required_stable_frames &&
            snapshot.now_ms >= automatic_due_ms_) source = Source::Automatic;
        if (source == Source::None) return {};

        active_source_ = source;
        awaiting_start_ = true;
        return {Action::Start, source};
    }

    // Started means the owner acquired a zero value and must restore it. A
    // failed/uncertain write must be made safe by the SDK owner before reporting
    // Unavailable. AlreadyActive leaves another owner's nonzero value untouched.
    void confirm_start(StartResult result) noexcept {
        if (!awaiting_start_) return;
        awaiting_start_ = false;
        const bool manual = active_source_ == Source::Manual;
        auto& due = manual ? manual_due_ms_ : automatic_due_ms_;
        due = later(snapshot_.now_ms, manual ? manual_retry_ms : automatic_interval_ms);
        if (result != StartResult::Started) {
            if (result == StartResult::AlreadyActive) ++status_.existing_value_skips;
            else ++status_.start_failures;
            active_source_ = Source::None;
            return;
        }

        // An acquired manual pulse can hitch too. A newer automatic arm must
        // wait after it, while explicit manual retries retain their 1s cadence.
        // Failed starts/external values did not acquire a pulse and do not move
        // this shared automatic deadline through the manual request path.
        const auto automatic_after_pulse = later(snapshot_.now_ms, automatic_interval_ms);
        if (automatic_due_ms_ < automatic_after_pulse) automatic_due_ms_ = automatic_after_pulse;
        status_.owns_refresh = true;
        ++status_.pulse_attempts;
        if (manual) ++status_.manual_attempts;
        else ++status_.automatic_attempts;
        pulse_attempt_number_ = manual ? status_.manual_attempts : status_.automatic_attempts;
        ticks_left_ = manual ? manual_frames_ : 1;
        pulse_start_ms_ = snapshot_.now_ms;
        pulse_pair_sequence_ = snapshot_.pair_sequence;
        pulse_arm_sequence_ = snapshot_.arm_sequence;
        pulse_auto_armed_ = snapshot_.automatic_armed;
        pair_observed_ = false;
    }

    // restored=true also covers relinquishing ownership because the SDK owner
    // observed a deliberate external value change. Never overwrite that change.
    void confirm_stop(bool restored) noexcept {
        if (!status_.owns_refresh || !awaiting_stop_) return;
        awaiting_stop_ = false;
        if (!restored) {
            ++status_.stop_failures;
            return;
        }
        status_.owns_refresh = false;
        if (pair_observed_) ++status_.pair_observed_pulses;
        else ++status_.missed_pair_pulses;

        const bool manual = active_source_ == Source::Manual;
        if (pair_observed_ || pulse_attempt_number_ >= max_pulse_attempts) {
            if (!pair_observed_) ++status_.exhausted_requests;
            if (manual) status_.manual_pending = false;
            // A successful manual pulse also satisfies the arm which existed
            // when it began. A newer teleport/loading arm remains independent.
            if (!manual || (pair_observed_ && pulse_auto_armed_)) {
                auto_consumed_ = pulse_arm_sequence_;
                if (pulse_arm_sequence_ == auto_generation_) {
                    automatic_retired_ = true;
                    status_.automatic_pending = false;
                }
            }
        }
        active_source_ = Source::None;
    }

    // Increment Snapshot::arm_sequence for each new start/gap/teleport arm.
    // Clear the owner's arm only if this token still matches that generation.
    // Exhaustion is not a successful refill; consult the separate counters.
    std::optional<std::uint64_t> take_auto_consumed() noexcept {
        const auto consumed = auto_consumed_;
        auto_consumed_.reset();
        return consumed;
    }

    const PolicyStatus& status() const noexcept { return status_; }

private:
    static std::uint64_t later(std::uint64_t now, std::uint64_t delay) noexcept {
        const auto max = (std::numeric_limits<std::uint64_t>::max)();
        return now > max - delay ? max : now + delay;
    }

    Snapshot snapshot_{};
    PolicyStatus status_{};
    Source active_source_{Source::None};
    std::uint64_t manual_due_ms_{}, automatic_due_ms_{};
    std::uint64_t pulse_start_ms_{}, pulse_pair_sequence_{}, pulse_arm_sequence_{}, auto_generation_{};
    std::uint32_t manual_frames_{1}, ticks_left_{}, pulse_attempt_number_{};
    std::optional<std::uint64_t> auto_consumed_{};
    bool awaiting_start_{}, awaiting_stop_{}, pair_observed_{}, pulse_auto_armed_{};
    bool have_auto_generation_{}, automatic_retired_{};
};

} // namespace wuwa_clv
