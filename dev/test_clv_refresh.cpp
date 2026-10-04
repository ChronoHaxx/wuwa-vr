#include "../mod/uevr/src/utility/WuWaClvRefreshPolicy.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace wuwa_clv;

namespace {
int checks{}, groups{};
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}
Snapshot fresh(std::uint64_t now, std::uint64_t pair = 1, bool armed = false,
    std::uint64_t generation = 1, std::uint32_t stable = 90, bool enabled = true) {
    return {now, pair, now, generation, stable, armed, enabled};
}
void expect(RefreshPolicy& policy, Snapshot snapshot, Action action, Source source, const char* message) {
    const auto decision = policy.tick(snapshot);
    check(decision.action == action && decision.source == source, message);
}
void start(RefreshPolicy& policy, Snapshot snapshot, Source source) {
    expect(policy, snapshot, Action::Start, source, "eligible request did not start");
    policy.confirm_start(StartResult::Started);
    check(policy.status().owns_refresh, "accepted start did not retain restoration duty");
}
void stop(RefreshPolicy& policy, Snapshot snapshot, Source source) {
    expect(policy, snapshot, Action::Stop, source, "owned pulse did not stop on its next tick");
    policy.confirm_stop(true);
    check(!policy.status().owns_refresh, "confirmed stop retained ownership");
}
}

int main() {
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        auto s = fresh(1000, 0);
        expect(policy, s, Action::None, Source::None, "manual request started without a pair");
        s = fresh(3000); s.last_pair_ms = 1500;
        expect(policy, s, Action::None, Source::None, "1500ms-old pair was treated as recent");
        s.last_pair_ms = 3001;
        expect(policy, s, Action::None, Source::None, "future pair timestamp was accepted");
        check(policy.status().manual_pending && policy.status().pulse_attempts == 0,
            "inactive manual request was consumed");
        s.last_pair_ms = 1501;
        start(policy, s, Source::Manual);
        stop(policy, fresh(3010, 2), Source::Manual);
        check(!policy.status().manual_pending && policy.status().pair_observed_pulses == 1,
            "observed manual pulse was not completed");
        check(!policy.take_auto_consumed(), "manual pulse consumed an absent automatic arm");
    }
    {
        ++groups;
        RefreshPolicy policy;
        expect(policy, fresh(1000, 1, true, 1, 89), Action::None, Source::None,
            "automatic pulse ignored the 90-pair settling requirement");
        auto stale = fresh(3000, 90, true); stale.last_pair_ms = 1500;
        expect(policy, stale, Action::None, Source::None, "stale settled count started a pulse");
        start(policy, fresh(3010, 91, true), Source::Automatic);
        stop(policy, fresh(3020, 92, true), Source::Automatic);
        const auto consumed = policy.take_auto_consumed();
        check(consumed && *consumed == 1, "completed automatic generation was not returned");
        check(!policy.take_auto_consumed(), "automatic token was returned twice");
        expect(policy, fresh(50000, 93, true), Action::None, Source::None,
            "same completed automatic generation repeated");
        expect(policy, fresh(23009, 94, true, 2), Action::None, Source::None,
            "new automatic arm bypassed the 20-second interval");
        start(policy, fresh(23010, 95, true, 2), Source::Automatic);
        stop(policy, fresh(23020, 96, true, 2), Source::Automatic);
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        expect(policy, fresh(1000), Action::Start, Source::Manual, "manual lookup was not attempted");
        policy.confirm_start(StartResult::Unavailable);
        check(policy.status().manual_pending && policy.status().start_failures == 1 &&
            policy.status().pulse_attempts == 0, "failed lookup consumed work or counted a pulse");
        expect(policy, fresh(1999), Action::None, Source::None, "manual failed lookup busy-looped");
        expect(policy, fresh(2000), Action::Start, Source::Manual, "manual failed lookup did not retry");
        policy.confirm_start(StartResult::Unavailable);
        start(policy, fresh(3000), Source::Manual);
        stop(policy, fresh(3010, 2), Source::Manual);
        check(policy.status().pulse_attempts == 1 && policy.status().start_failures == 2,
            "lookup/write failures consumed the delivered-pulse budget");
    }
    {
        ++groups;
        RefreshPolicy policy;
        expect(policy, fresh(1000, 1, true), Action::Start, Source::Automatic, "auto lookup was not attempted");
        policy.confirm_start(StartResult::Unavailable);
        expect(policy, fresh(20999, 2, true), Action::None, Source::None, "auto lookup retry bypassed cooldown");
        auto stale = fresh(21000, 2, true); stale.last_pair_ms = 19000;
        expect(policy, stale, Action::None, Source::None, "auto retry started on stale settled frames");
        start(policy, fresh(21010, 3, true), Source::Automatic);
        stop(policy, fresh(21020, 4, true), Source::Automatic);
        check(policy.status().start_failures == 1 && policy.status().pair_observed_pulses == 1,
            "automatic retry lost its pending arm");
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        for (std::uint64_t attempt = 0; attempt < 3; ++attempt) {
            const auto time = 1000 + attempt * 1000;
            start(policy, fresh(time), Source::Manual);
            stop(policy, fresh(time + 10), Source::Manual); // Sequence never advances.
            if (attempt < 2) {
                check(policy.status().manual_pending, "missed pair discarded a retryable manual request");
                policy.request_manual(10); // Coalescing must not reset retries or extend the hold.
                expect(policy, fresh(time + 999), Action::None, Source::None, "manual retry ran before 1 second");
            }
        }
        check(!policy.status().manual_pending && policy.status().pulse_attempts == 3 &&
            policy.status().missed_pair_pulses == 3 && policy.status().exhausted_requests == 1 &&
            policy.status().pair_observed_pulses == 0, "manual missed-pair retry limit was not enforced");
        expect(policy, fresh(100000, 2), Action::None, Source::None, "exhausted manual request restarted itself");
        policy.request_manual();
        start(policy, fresh(100010, 2), Source::Manual);
        stop(policy, fresh(100020, 3), Source::Manual);
        check(policy.status().manual_attempts == 1, "new explicit manual request did not get a fresh budget");
    }
    {
        ++groups;
        RefreshPolicy policy;
        for (std::uint64_t attempt = 0; attempt < 3; ++attempt) {
            const auto time = 1000 + attempt * 20000;
            start(policy, fresh(time, 1, true, 7), Source::Automatic);
            stop(policy, fresh(time + 10, 1, true, 7), Source::Automatic);
            if (attempt < 2) {
                check(!policy.take_auto_consumed(), "retryable automatic request was consumed");
                expect(policy, fresh(time + 19999, 1, true, 7), Action::None, Source::None,
                    "automatic missed-pair retry bypassed 20-second cooldown");
            }
        }
        const auto token = policy.take_auto_consumed();
        check(token && *token == 7 && policy.status().exhausted_requests == 1 &&
            policy.status().pair_observed_pulses == 0, "automatic exhaustion claimed success or lost generation");
        expect(policy, fresh(100000, 2, true, 7), Action::None, Source::None,
            "exhausted automatic generation retried indefinitely");
        start(policy, fresh(100010, 2, true, 8), Source::Automatic);
        check(policy.status().automatic_attempts == 1, "new auto generation kept old exhausted budget");
        stop(policy, fresh(100020, 3, true, 8), Source::Automatic);
    }
    {
        ++groups;
        RefreshPolicy policy;
        start(policy, fresh(1000, 1, true), Source::Automatic);
        auto inactive = fresh(1010, 1, true, 1, 90, false); inactive.last_pair_ms = 0;
        stop(policy, inactive, Source::Automatic);
        expect(policy, fresh(50000, 2, true, 1, 90, false), Action::None, Source::None,
            "disabled automatic retry remained eligible");
        expect(policy, fresh(50010, 2, false), Action::None, Source::None,
            "cancelled automatic arm retried");
        policy.request_manual();
        start(policy, fresh(50020, 2, false, 1, 0, false), Source::Manual);
        stop(policy, fresh(50030, 3, false, 1, 0, false), Source::Manual);
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        start(policy, fresh(1000), Source::Manual);
        auto inactive = fresh(1001, 1, false, 1, 0, false); inactive.last_pair_ms = 0;
        expect(policy, inactive, Action::Stop, Source::Manual, "inactive rendering prevented release");
        policy.confirm_stop(false);
        for (int retry = 0; retry < 4; ++retry) {
            expect(policy, inactive, Action::Stop, Source::Manual,
                "restore failure waited for freshness, elapsed time or cooldown");
            policy.confirm_stop(false);
        }
        check(policy.status().owns_refresh && policy.status().stop_failures == 5,
            "failed restoration relinquished ownership");
        expect(policy, inactive, Action::Stop, Source::Manual, "restoration retry disappeared");
        policy.confirm_stop(true);
        check(!policy.status().owns_refresh && policy.status().manual_pending,
            "successful eventual release dropped the unobserved request");
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        expect(policy, fresh(1000, 1, true), Action::Start, Source::Manual, "existing-value check not requested");
        policy.confirm_start(StartResult::AlreadyActive);
        expect(policy, fresh(1010, 2, true, 1, 0), Action::None, Source::None,
            "external nonzero value was scheduled for release");
        check(!policy.status().owns_refresh && policy.status().manual_pending &&
            policy.status().pulse_attempts == 0 && policy.status().existing_value_skips == 1 &&
            !policy.take_auto_consumed(), "existing external refresh was claimed as owned/successful");
        start(policy, fresh(2000, 3, true), Source::Manual);
        expect(policy, fresh(2010, 4, true), Action::Stop, Source::Manual, "owned release action missing");
        // The SDK wrapper may relinquish after an external edit instead of
        // writing zero. Policy must accept that release and issue no more Stop.
        policy.confirm_stop(true);
        expect(policy, fresh(2020, 5, true), Action::None, Source::None,
            "acknowledged ownership release caused another write action");
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        start(policy, fresh(1000, 1, true, 5), Source::Manual);
        stop(policy, fresh(1010, 2, true, 5), Source::Manual);
        const auto token = policy.take_auto_consumed();
        check(token && *token == 5, "successful manual pulse did not satisfy its existing auto arm");
        expect(policy, fresh(1020, 3, true, 5), Action::None, Source::None,
            "successful manual pulse caused an immediate duplicate automatic hitch");

        RefreshPolicy changed;
        changed.request_manual();
        start(changed, fresh(1000, 1, true, 5), Source::Manual);
        stop(changed, fresh(1010, 2, true, 6, 0), Source::Manual);
        const auto older = changed.take_auto_consumed();
        check(older && *older == 5 && changed.status().automatic_pending,
            "manual completion erased a newer teleport/loading generation");
        expect(changed, fresh(1020, 3, true, 6, 89), Action::None, Source::None,
            "newer generation skipped settling");
        expect(changed, fresh(1030, 4, true, 6), Action::None, Source::None,
            "newer settled generation hitched immediately after a manual pulse");
        expect(changed, fresh(20999, 5, true, 6), Action::None, Source::None,
            "automatic refill ignored the manual pulse's 20-second spacing");
        start(changed, fresh(21000, 6, true, 6), Source::Automatic);
        stop(changed, fresh(21010, 7, true, 6), Source::Automatic);
        const auto newer = changed.take_auto_consumed();
        check(newer && *newer == 6, "newer automatic generation could not complete");
    }
    {
        ++groups;
        RefreshPolicy delivered;
        delivered.request_manual();
        start(delivered, fresh(1000), Source::Manual);
        stop(delivered, fresh(1010), Source::Manual); // No pair: still an acquired, potentially costly pulse.
        expect(delivered, fresh(1999, 2, true, 2), Action::None, Source::None,
            "unobserved manual pulse failed to delay a new automatic arm");
        start(delivered, fresh(2000, 2, true, 2), Source::Manual);
        stop(delivered, fresh(2010, 2, true, 2), Source::Manual);
        expect(delivered, fresh(2999, 3, true, 3), Action::None, Source::None,
            "manual retry failed to extend automatic spacing");
        start(delivered, fresh(3000, 3, true, 3), Source::Manual);
        stop(delivered, fresh(3010, 3, true, 3), Source::Manual); // Exhausted manual request.
        expect(delivered, fresh(22999, 4, true, 3), Action::None, Source::None,
            "automatic started within 20 seconds of the latest manual retry");
        start(delivered, fresh(23000, 5, true, 3), Source::Automatic);
        stop(delivered, fresh(23010, 6, true, 3), Source::Automatic);

        for (const auto result : {StartResult::Unavailable, StartResult::AlreadyActive}) {
            RefreshPolicy unacquired;
            unacquired.request_manual();
            expect(unacquired, fresh(1000), Action::Start, Source::Manual,
                "manual failure fixture did not request a start");
            unacquired.confirm_start(result);
            start(unacquired, fresh(1010, 2, true, 2), Source::Automatic);
            stop(unacquired, fresh(1020, 3, true, 2), Source::Automatic);
            check(unacquired.status().pulse_attempts == 1,
                "unacquired manual value was counted as a pulse or postponed automatic work");
        }
    }
    {
        ++groups;
        RefreshPolicy policy;
        start(policy, fresh(1000, 1, true, 1), Source::Automatic);
        stop(policy, fresh(1010, 2, true, 2, 0), Source::Automatic);
        const auto older = policy.take_auto_consumed();
        check(older && *older == 1 && policy.status().automatic_pending,
            "old automatic completion consumed a new generation");
        expect(policy, fresh(20999, 3, true, 2), Action::None, Source::None,
            "new generation bypassed global automatic cooldown");
        start(policy, fresh(21000, 4, true, 2), Source::Automatic);
        check(policy.status().automatic_attempts == 1, "generation change did not reset attempt budget");
        stop(policy, fresh(21010, 5, true, 2), Source::Automatic);
    }
    {
        ++groups;
        for (const auto frames : {0u, 1u, 3u, 10u, 99u}) {
            RefreshPolicy policy;
            policy.request_manual(frames);
            const auto held = frames < 1 ? 1 : frames > 10 ? 10 : frames;
            start(policy, fresh(1000), Source::Manual);
            for (unsigned tick = 1; tick < held; ++tick)
                expect(policy, fresh(1000 + tick * 10, 1), Action::None, Source::None,
                    "bounded multi-tick manual request stopped early");
            stop(policy, fresh(1000 + held * 10, 1), Source::Manual);
            check(policy.status().missed_pair_pulses == 1 && !policy.status().owns_refresh,
                "multi-tick request waited forever for a pair");
        }
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual(3);
        start(policy, fresh(1000, 10), Source::Manual);
        expect(policy, fresh(1010, 11), Action::None, Source::None, "three-tick pulse stopped early");
        expect(policy, fresh(1020, 1), Action::None, Source::None, "counter reset stopped pulse early");
        stop(policy, fresh(1030, 1), Source::Manual);
        check(policy.status().pair_observed_pulses == 1, "observed pair was forgotten before pulse ended");

        RefreshPolicy reset;
        reset.request_manual();
        start(reset, fresh(1000, 10), Source::Manual);
        stop(reset, fresh(1010, 1), Source::Manual);
        check(reset.status().pair_observed_pulses == 0 && reset.status().manual_pending,
            "backwards pair counter was counted as observed progress");

        RefreshPolicy timestamp;
        timestamp.request_manual();
        start(timestamp, fresh(1000, 10), Source::Manual);
        auto old = fresh(1010, 11); old.last_pair_ms = 999;
        stop(timestamp, old, Source::Manual);
        check(timestamp.status().pair_observed_pulses == 0, "pre-pulse pair timestamp claimed completion");
    }
    {
        ++groups;
        RefreshPolicy policy;
        policy.request_manual();
        const auto near_max = (std::numeric_limits<std::uint64_t>::max)() - 10;
        expect(policy, fresh(near_max), Action::Start, Source::Manual, "large timestamp request did not start");
        policy.confirm_start(StartResult::Unavailable);
        expect(policy, fresh(near_max + 1), Action::None, Source::None, "retry deadline overflowed into the past");
        policy.confirm_start(StartResult::Started); // No outstanding Start: ignored.
        policy.confirm_stop(true); // No owned pulse: ignored.
        check(!policy.status().owns_refresh && policy.status().pulse_attempts == 0,
            "out-of-order confirmations invented an owned pulse");
    }
    std::cout << "PASS " << checks << " checks in " << groups << " CLV scheduling groups\n";
}
