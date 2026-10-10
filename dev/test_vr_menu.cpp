// Navigation checks for the VR menu model (mod/uevr/src/utility/WuWaVrMenu.hpp).
// Pure values: no ImGui, devices or Windows APIs.
//   g++ -std=c++20 -Wall -Wextra dev/test_vr_menu.cpp -o test_vr_menu && test_vr_menu
#include "../mod/uevr/src/utility/WuWaVrMenu.hpp"

#include <cstdlib>
#include <iostream>

using namespace wuwa_menu;

namespace {
int checks{};
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}

struct Values {
    bool flag{false}, locked{false};
    int pick{0};
    float amount{1.0f};
    int runs{0};
};

std::vector<Page> pages_for(Values& v) {
    Item heading = make(Kind::Text, "Heading");
    Item toggle = make(Kind::Toggle, "Flag");
    toggle.get_bool = [&v] { return v.flag; };
    toggle.set_bool = [&v](bool on) { v.flag = on; };
    Item disabled = make(Kind::Toggle, "Disabled");
    disabled.enabled = [&v] { return v.locked; };
    disabled.get_bool = [] { return false; };
    disabled.set_bool = [](bool) {};
    Item choice = make(Kind::Choice, "Pick");
    choice.get_int = [&v] { return v.pick; };
    choice.set_int = [&v](int value) { v.pick = value; };
    choice.choices = {"A", "B", "C"};
    Item slider = make(Kind::Slider, "Amount");
    slider.get_float = [&v] { return v.amount; };
    slider.set_float = [&v](float value) { v.amount = value; };
    slider.min = 0.5f; slider.max = 2.0f; slider.step = 0.25f;
    Item action = make(Kind::Action, "Run");
    action.run = [&v] { ++v.runs; };
    return {
        {"First", {heading, toggle, disabled, choice, slider, action}},
        {"Second", {make(Kind::Text, "Only text")}},
        {"Third", {action}},
    };
}

Moves move(bool Moves::* which) { Moves m; m.*which = true; return m; }
}

int main() {
    Values v;
    auto pages = pages_for(v);
    State state;

    // Focus starts hidden; the first up / down only shows it, on the first usable row.
    apply_moves(pages, state, move(&Moves::down));
    check(state.page == 0 && state.focus == 1, "first press did not land on the first usable row");
    check(state.source == Source::Buttons, "a button press did not take over focus display");

    // Text and disabled rows are skipped; rows stop at the ends.
    apply_moves(pages, state, move(&Moves::down));
    check(state.focus == 3, "down did not skip the disabled row");
    apply_moves(pages, state, move(&Moves::up));
    check(state.focus == 1, "up did not skip the disabled row");
    apply_moves(pages, state, move(&Moves::up));
    check(state.focus == 1, "up moved past the first usable row onto a heading");
    v.locked = true;
    apply_moves(pages, state, move(&Moves::down));
    check(state.focus == 2, "an enabled row was skipped");
    v.locked = false;

    // Toggle: left / right set off / on, accept flips.
    state.focus = 1;
    apply_moves(pages, state, move(&Moves::right));
    check(v.flag, "right did not turn the toggle on");
    apply_moves(pages, state, move(&Moves::right));
    check(v.flag, "right twice turned the toggle off");
    apply_moves(pages, state, move(&Moves::accept));
    check(!v.flag, "accept did not flip the toggle");

    // Choice: left / right clamp at the ends, accept wraps.
    state.focus = 3;
    apply_moves(pages, state, move(&Moves::left));
    check(v.pick == 0, "left went below the first choice");
    apply_moves(pages, state, move(&Moves::right));
    apply_moves(pages, state, move(&Moves::right));
    apply_moves(pages, state, move(&Moves::right));
    check(v.pick == 2, "right went past the last choice");
    apply_moves(pages, state, move(&Moves::accept));
    check(v.pick == 0, "accept on the last choice did not wrap to the first");

    // Slider: one step per press, snapped and clamped.
    state.focus = 4;
    apply_moves(pages, state, move(&Moves::right));
    check(v.amount == 1.25f, "slider step was not 0.25");
    v.amount = 1.9f;
    apply_moves(pages, state, move(&Moves::right));
    check(v.amount == 2.0f, "slider passed its maximum");
    v.amount = 0.6f;
    apply_moves(pages, state, move(&Moves::left));
    check(v.amount == 0.5f, "slider passed its minimum");
    check(snap(pages[0].items[4], 1.12f) == 1.0f && snap(pages[0].items[4], 1.13f) == 1.25f, "snap did not round to the nearest step");
    apply_moves(pages, state, move(&Moves::accept));
    check(v.amount == 0.5f, "accept changed a slider");

    // Action: accept runs it once; left / right do nothing.
    state.focus = 5;
    apply_moves(pages, state, move(&Moves::left));
    apply_moves(pages, state, move(&Moves::accept));
    check(v.runs == 1, "accept did not run the action exactly once");

    // Pages wrap and reset focus; a page without usable rows has no focus.
    apply_moves(pages, state, move(&Moves::next_page));
    check(state.page == 1 && state.focus == -1, "next page did not clear focus on a text-only page");
    apply_moves(pages, state, move(&Moves::down));
    check(state.focus == -1, "a text-only page gained focus");
    apply_moves(pages, state, move(&Moves::next_page));
    check(state.page == 2 && state.focus == 0, "next page did not focus the first row");
    apply_moves(pages, state, move(&Moves::next_page));
    check(state.page == 0 && state.focus == 1, "pages did not wrap forwards");
    apply_moves(pages, state, move(&Moves::prev_page));
    check(state.page == 2, "pages did not wrap backwards");

    // A page change outranks other moves in the same frame.
    Moves both;
    both.next_page = true;
    both.accept = true;
    const int runs = v.runs;
    apply_moves(pages, state, both);
    check(state.page == 0 && v.runs == runs, "accept ran during a page change");

    // Back asks the host to close; no moves change nothing.
    apply_moves(pages, state, move(&Moves::back));
    check(state.close_requested, "back did not request close");
    const State before = state;
    state.source = Source::Pointer;
    apply_moves(pages, state, Moves{});
    check(state.focus == before.focus && state.page == before.page && state.source == Source::Pointer,
        "an empty frame changed the menu");

    // A stale page index from an earlier, longer page list is repaired.
    state.page = 7;
    apply_moves(pages, state, Moves{});
    check(state.page == 0 && state.focus == 1, "an out-of-range page was not reset");

    // Held directions: one move on press, a pause, then a steady repeat.
    MoveReader reader;
    Held right;
    right.right = true;
    const auto held = [&](const Held& h, double t) { return reader.read(h, false, false, false, false, t); };
    check(held(right, 0.0).right, "a fresh push did not move at once");
    check(!held(right, 0.2).right && !held(right, 0.44).right, "a held push repeated before the pause");
    check(held(right, 0.46).right, "a held push did not repeat after the pause");
    check(!held(right, 0.55).right, "the repeat came faster than every 0.18 s");
    check(held(right, 0.65).right, "the repeat stopped while held");
    check(!held(Held{}, 0.70).right, "a released direction still moved");
    check(held(right, 0.71).right, "a new push after release did not move at once");
    Held sideways;
    sideways.left = sideways.right = true;
    const auto opposite = held(sideways, 1.0);
    check(!opposite.left && !opposite.right, "opposite directions held together moved");
    const auto buttons = reader.read(Held{}, true, false, false, true, 1.1);
    check(buttons.accept && buttons.next_page && !buttons.up && !buttons.right, "single presses were not passed through");

    std::cout << "PASS: " << checks << " VR menu navigation checks (pure C++ values)\n";
    return 0;
}
