#include "../mod/uevr/src/utility/WuWaWindowPresentation.hpp"

#include <cassert>
#include <iostream>
#include <limits>
#include <vector>

using namespace wuwa_window;

namespace {
// Distinct complete poses make accidental snapshot restoration visible.
using Pose = std::array<int, 4>;
using State = PresentationState<Pose>;
const Pose room{1, 2, 3, 4}, movie{5, 6, 7, 8}, user{9, 10, 11, 12};
const std::string valid = "1 1 0 1 2.4 1.35 2 0.1 0 0 0 0 0 1";
BridgeState active() { return *parse_bridge_state(valid); }

void entry_exit() {
    State state;
    assert(!state.bridge().active && !state.update_enabled(false));
    assert(state.update_enabled(true));
    assert(state.capture(room));
    assert(state.apply(active()));
    assert(!state.selected().valid && state.selected().recenter_pending);
    assert(state.capture(movie));
    assert(state.selected().pose == movie && state.regular().pose == room);
    assert(!state.capture(user)); // second eye / later frame never recenters
    state.apply(BridgeState{});
    assert(state.selected().valid && state.selected().pose == room);
    assert(!state.selected().recenter_pending);
    assert(!state.capture(user));
    state.apply(active()); // fresh cutscene gets a fresh transient anchor
    assert(state.capture(user) && state.selected().pose == user);
    state.clear_transient();
    assert(state.selected().pose == room);
}

void repeated_and_recenter() {
    State state;
    state.update_enabled(true);
    state.capture(room);
    auto next = active();
    state.apply(next);
    state.capture(movie);
    next.width = 4.0f; // repeated active update changes settings, not pose
    state.apply(next);
    assert(!state.capture(user));
    assert(state.bridge().width == 4.0f && state.selected().pose == movie);
    next.recenter = true;
    state.apply(next);
    assert(state.capture(user));
    assert(state.selected().pose == user && state.regular().pose == room);
    assert(!state.capture(movie)); // request consumed once, including first frame
    state.request_recenter(); // explicit player action survives cinematic exit
    assert(state.capture(movie));
    state.clear_transient();
    assert(state.selected().pose == movie && !state.selected().recenter_pending);
}

void portal_disabled_and_user_toggle() {
    State state;
    state.apply(active());
    assert(state.update_enabled(false));
    state.capture(movie);
    assert(!state.regular().valid);
    state.request_recenter();
    state.capture(user);
    state.clear_transient();
    assert(!state.update_enabled(false) && !state.selected().valid);
    assert(state.update_enabled(true));
    assert(state.capture(room) && state.selected().pose == room);
    state.apply(active());
    state.capture(movie);
    assert(state.update_enabled(false)); // player disables normal portal
    state.clear_transient();
    assert(!state.update_enabled(false) && !state.selected().valid);
    state.update_enabled(true);
    assert(state.capture(user) && state.selected().pose == user);
}

void interruption_and_reset() {
    State state;
    state.update_enabled(true);
    state.capture(room);
    state.apply(active());
    state.capture(movie);
    state.suspend(true); // plugin unload, before old callbacks / destructors
    assert(!state.bridge().active && state.selected().pose == room);
    assert(!state.apply(active())); // old callback cannot resurrect the mode
    state.clear_transient(); // Lua reset cleanup (after old callbacks)
    assert(!state.apply(active())); // clear must not release unload suspension
    state.suspend(false);
    assert(!state.bridge().active && state.selected().pose == room);
    assert(state.apply(active())); // a newly loaded producer can send a new event
    state.capture(movie);
    state.reset(); // graphics reset: require new tracking, retain normal enable
    assert(!state.bridge().active && !state.selected().valid);
    assert(state.update_enabled(true));
    assert(state.capture(user) && state.selected().pose == user);
    state.apply(active());
    // Entry interrupted before tracking arrives: no capture, no ordinary change.
    state.clear_transient();
    assert(state.selected().pose == user && !state.selected().recenter_pending);
    for (int i = 0; i < 100; ++i) state.update_enabled(true);
    assert(state.selected().pose == user); // no arbitrary lease / timeout
}

void payloads() {
    assert(parse_bridge_state(valid));
    assert(parse_bridge_state(" \t" + valid + "\r\n"));
    assert(parse_bridge_state("1 0 0 0 99 -2 .01 10 8 5 -1 2 3 -4"));
    const std::vector<std::string> bad{
        "", "1 0", "2" + valid.substr(1), "1 2" + valid.substr(3),
        "1 -1" + valid.substr(3), "1 1 2" + valid.substr(5),
        "1 1 0 -1" + valid.substr(7), "1 1.0" + valid.substr(3),
        "1+1 0 1" + valid.substr(7), valid + " trailing", valid + " 0",
        valid + std::string(1, '\0'), std::string(max_bridge_payload + 1, ' '),
    };
    State state;
    state.apply(active());
    for (const auto& text : bad) {
        const auto parsed = parse_bridge_state(text);
        assert(!parsed);
        if (parsed) state.apply(*parsed);
        assert(state.bridge().active); // malformed inactive event is not an exit
    }
    std::istringstream stream{valid};
    std::vector<std::string> fields;
    for (std::string field; stream >> field;) fields.push_back(field);
    for (size_t i = 4; i < fields.size(); ++i) {
        for (const auto* hostile : {"nan", "-nan", "inf", "-inf", "1e999", "2px"}) {
            auto modified = fields;
            modified[i] = hostile;
            std::string text;
            for (const auto& field : modified) text += field + " ";
            assert(!parse_bridge_state(text));
        }
    }
    assert(finite_clamp(99, .1f, 12, 2.4f) == 12);
    assert(finite_clamp(-1, .1f, 12, 2.4f) == .1f);
    assert(finite_clamp(std::numeric_limits<float>::quiet_NaN(), .1f, 12, 2.4f) == 2.4f);
    assert(finite_clamp(std::numeric_limits<float>::infinity(), 0, 1, 0) == 0);
}

void runtime_restart_with_disabled_portal() {
    State state;
    state.update_enabled(true);
    state.capture(room);
    state.apply(active());
    state.capture(movie);
    state.update_enabled(false); // Player's latest choice survives the restart.
    state.reset(); // New runtime/session; old tracking-space anchors are invalid.
    assert(!state.bridge().active && !state.selected().valid && !state.regular().valid);
    assert(!state.update_enabled(false) && !state.capture(user));
    assert(!state.selected().valid); // Tracking alone cannot revive the override.
    state.update_enabled(true); // A later explicit portal enable uses the new space.
    assert(state.capture(user) && state.selected().pose == user);
    state.apply(active());
    state.capture(movie);
    state.suspend(true); // A restart nested in unload cannot release suspension.
    state.reset();
    assert(!state.apply(active()));
    state.suspend(false);
    assert(!state.bridge().active && !state.selected().valid);
    assert(state.capture(user) && state.selected().pose == user);
}
}

int main() {
    entry_exit();
    repeated_and_recenter();
    portal_disabled_and_user_toggle();
    interruption_and_reset();
    runtime_restart_with_disabled_portal();
    payloads();
    std::cout << "PASS: cinematic anchor ownership, recenter, toggles, unload/reset, strict payloads\n";
}
