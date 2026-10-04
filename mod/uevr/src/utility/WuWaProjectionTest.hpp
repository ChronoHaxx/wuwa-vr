#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "OpenXRProjectionPolicy.hpp"

// Fixed, opt-in diagnostic only. No Windows/SDK calls or saved-setting writes.
// The request thread and OpenXR update boundary share only this mutex. Never
// acquire a runtime lock while holding it; publish copies made under that lock.
namespace wuwa_projection_test {
struct Base {
    int horizontal{}, vertical{};
    bool grow{}, screen{}, openxr{}, native{}, ready{}, native_fix{};
    uintptr_t runtime{};
    bool operator==(const Base& b) const noexcept {
        return horizontal == b.horizontal && vertical == b.vertical && grow == b.grow &&
            screen == b.screen && openxr == b.openxr && native == b.native &&
            ready == b.ready && native_fix == b.native_fix && runtime == b.runtime;
    }
    bool eligible() const noexcept {
        return horizontal == 0 && vertical == 0 && !grow && !screen &&
            openxr && native && ready && runtime != 0;
    }
};

struct Selection {
    Base base{};
    int horizontal{};
    uint64_t epoch{};
    std::string lease_id;
};
struct Applied {
    Selection selection{};
    openxr_projection::Inputs key{};
    uint64_t at_ms{}, sequence{};
    // Column-major, matching the copied GLM matrices. CPU calculation evidence,
    // not a submitted frame, GPU completion, or an NPC rendering verdict.
    std::array<std::array<float, 16>, 2> matrices{};
    std::array<std::array<float, 4>, 2> bounds{};
};
struct Status {
    bool active{};
    std::string id, reason{"inactive"};
    uint64_t remaining_ms{};
    std::optional<Base> base;
    int effective_horizontal{};
    std::optional<Applied> applied;
};

class Lease {
public:
    // An identical retry acknowledges the same lease without extending it.
    // Reusing a finished request ID is refused instead of restarting a test.
    void begin(uint64_t now, int seconds, const std::string& id,
               const Base& current, const Base& expected, bool conflict) {
        std::scoped_lock lock{mutex};
        observe_locked(now, current);
        if (!valid_id(id) || seconds < 1 || seconds > 30 ||
            now > (std::numeric_limits<uint64_t>::max)() - uint64_t(seconds) * 1000)
            throw std::runtime_error("Invalid projection test ID or duration (1..30 seconds)");
        if (conflict) throw std::runtime_error("Another graphics comparison is active");
        if (!(current == expected)) throw std::runtime_error("Projection baseline changed");
        if (!current.eligible()) throw std::runtime_error("Projection test requires ready OpenXR native stereo, Raw/Raw, growth off and 2D off");
        if (active) {
            if (id == owner && seconds == duration && current == initial) return;
            throw std::runtime_error("Another projection test is active");
        }
        if (std::find(consumed.begin(),consumed.end(),id) != consumed.end())
            throw std::runtime_error("Projection test ID was already consumed");
        // Retain identities across reset. Refuse a pathological number of tests
        // instead of evicting IDs and allowing an old retry to start a new lease.
        if (consumed.size() >= 1024) throw std::runtime_error("Projection test identity capacity reached; restart required");
        consumed.push_back(id);
        owner = id; initial = current; duration = seconds;
        started = now; until = now + uint64_t(seconds) * 1000;
        active = true; reason = "active";
    }

    void end(uint64_t now, const std::string& id) {
        std::scoped_lock lock{mutex};
        expire_locked(now);
        if (!valid_id(id) || id != owner) throw std::runtime_error("Projection lease owner differs");
        if (active) cancel_locked("ended");
    }

    Selection select(uint64_t now, const Base& current) {
        std::scoped_lock lock{mutex};
        observe_locked(now, current);
        return {current, active ? 1 : current.horizontal, epoch, active ? owner : std::string{}};
    }

    Status status(uint64_t now) {
        std::scoped_lock lock{mutex};
        expire_locked(now);
        return {active, owner, reason, active ? until - now : 0, observed,
            active ? 1 : (observed ? observed->horizontal : 0), applied};
    }

    // A request can expire/end while a pair is being calculated. Retain exactly
    // what was calculated, labelled with its selection; do not relabel it as
    // the new request. Reset invalidates late publications from the old runtime.
    void publish(Applied value) {
        std::scoped_lock lock{mutex};
        if (value.selection.epoch != epoch) return;
        value.sequence = ++sequence;
        applied = std::move(value);
    }

    void reset() {
        std::scoped_lock lock{mutex};
        active = false; until = 0; reason = "reset";
        observed.reset(); applied.reset(); ++epoch;
    }

private:
    static bool valid_id(const std::string& id) {
        if (id.empty() || id.size() > 64) return false;
        for (const unsigned char c : id)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        return true;
    }
    void cancel_locked(const char* why) { active = false; until = 0; reason = why; }
    void expire_locked(uint64_t now) {
        if (active && now < started) cancel_locked("clock_changed");
        else if (active && now >= until) cancel_locked("expired");
    }
    void observe_locked(uint64_t now, const Base& current) {
        expire_locked(now);
        if (active && !(current == initial)) cancel_locked("base_or_runtime_changed");
        observed = current;
    }
    std::mutex mutex;
    bool active{};
    int duration{};
    uint64_t started{}, until{}, epoch{}, sequence{};
    std::string owner, reason{"inactive"};
    std::vector<std::string> consumed;
    Base initial{};
    std::optional<Base> observed;
    std::optional<Applied> applied;
};

inline Lease lease;
} // namespace wuwa_projection_test
