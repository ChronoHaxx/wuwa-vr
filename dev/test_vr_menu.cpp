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

    // Sticks: one direction at a time, never two from a diagonal push.
    StickReader stick;
    check(stick.read(0.0f, -0.9f) == Dir::Down, "a firm push down did not read as down");
    check(stick.read(0.45f, -0.8f) == Dir::Down, "a push down drifting sideways changed direction");
    check(stick.read(0.7f, -0.5f) == Dir::Down, "a held push down flipped sideways before it was clearly sideways");
    check(stick.read(0.9f, -0.2f) == Dir::Right, "a clearly sideways push did not take over");
    check(stick.read(0.2f, 0.1f) == Dir::None, "a stick back near centre still held a direction");
    StickReader fresh;
    check(fresh.read(0.55f, -0.7f) == Dir::None, "a fresh diagonal push engaged a direction");
    check(fresh.read(0.0f, 0.5f) == Dir::None, "a half push engaged");
    check(fresh.read(0.0f, 0.65f) == Dir::Up, "a firm push up did not engage");
    check(fresh.read(0.1f, 0.4f) == Dir::Up, "a push easing off but still past release let go");
    check(fresh.read(0.1f, 0.3f) == Dir::None, "a push below release still held");
    check(fresh.read(-0.8f, 0.0f) == Dir::Left, "a firm push left did not engage");
    check(fresh.read(std::nanf(""), 0.0f) == Dir::None, "a bad stick value held a direction");

    // Holding longer repeats faster and, on sliders, takes bigger steps.
    MoveReader accel;
    const auto hold = [&](double t) { return accel.read(right, false, false, false, false, t); };
    check(hold(0.0).right && hold(0.0).scale == 1, "a fresh push was not a single step");
    int moves_first = 0, moves_late = 0, biggest = 1;
    for (int frame = 1; frame <= 90; ++frame) { // 0 - 1.5 s at 60 fps
        const auto m = hold(frame / 60.0);
        if (m.right) { ++moves_first; biggest = (std::max)(biggest, m.scale); }
    }
    for (int frame = 181; frame <= 270; ++frame) { // 3 - 4.5 s
        const auto m = hold(frame / 60.0);
        if (m.right) { ++moves_late; biggest = (std::max)(biggest, m.scale); }
    }
    check(moves_late >= 2 * moves_first, "the repeat did not speed up while held");
    check(biggest == 10, "a long hold did not reach ten steps per move");
    check(hold_scale(0.5) == 1 && hold_scale(1.3) == 2 && hold_scale(2.5) == 5 && hold_scale(3.5) == 10,
        "hold scale thresholds changed");
    Values wide;
    auto wide_pages = pages_for(wide);
    auto& amount = wide_pages[0].items[4];
    wide.amount = 0.5f;
    nudge(amount, +1, 10); // range 0.5 - 2.0 in 0.25 steps: a tenth is under one step
    check(wide.amount == 0.75f, "a scaled move on a short slider took more than one step");
    Item offset = make(Kind::Slider, "Offset");
    float cm = 0.0f;
    offset.get_float = [&cm] { return cm; };
    offset.set_float = [&cm](float value) { cm = value; };
    offset.min = -150.0f; offset.max = 150.0f; offset.step = 1.0f;
    nudge(offset, +1, 10);
    check(cm == 10.0f, "a scaled move on a long slider did not take ten steps");
    nudge(offset, -1, 50);
    check(cm == -20.0f, "a scaled move was not capped at a tenth of the range");
    Values picks;
    auto choice_pages = pages_for(picks);
    nudge(choice_pages[0].items[3], +1, 10);
    check(picks.pick == 1, "a scaled move skipped choices");

    check(clock_text(0.0) == "0:00" && clock_text(75.9) == "1:15" && clock_text(3725.0) == "1:02:05" && clock_text(-4.0) == "0:00",
        "elapsed time was formatted wrongly");

    // VR setup: two questions over plain values.
    {
        int camera = 2, blur = 1, sets = 0;
        Setup setup;
        SetupQuestion first{"Camera", "How you see."};
        first.choices = {{"Game", "", true}, {"First", ""}, {"Fixed", ""}};
        first.get = [&camera] { return camera; };
        first.set = [&](int choice) { camera = choice; ++sets; };
        SetupQuestion second{"Blur", "Depth of field."};
        second.choices = {{"Off", "", true}, {"On", ""}};
        second.get = [&blur] { return blur; };
        second.set = [&](int choice) { blur = choice; ++sets; };
        setup.questions = {first, second};
        Moves accept; accept.accept = true;
        Moves down; down.down = true;
        Moves up; up.up = true;
        Moves back; back.back = true;

        setup_start(setup, true);
        check(setup.welcome() && setup.focus == 0 && setup.cards() == 2, "the setup did not open on its welcome");
        setup_apply_moves(setup, accept);
        check(setup.screen == 1 && setup.focus == 2, "a question did not focus the choice the settings already match");
        setup_apply_moves(setup, accept);
        check(camera == 2 && setup.screen == 2 && setup.focus == 1, "accepting the current choice changed it or did not move on");
        setup_apply_moves(setup, accept);
        check(setup.summary() && setup.cards() == 1 && blur == 1, "the summary did not follow the last question");
        setup_apply_moves(setup, accept);
        check(setup.result == SetupResult::Done && camera == 2 && blur == 1 && sets == 2,
            "pressing A through the setup changed a setting");

        setup_start(setup, true);
        setup_apply_moves(setup, accept);
        setup_apply_moves(setup, up);
        setup_apply_moves(setup, up);
        setup_apply_moves(setup, up);
        check(setup.focus == 0, "up went past the first card");
        setup_apply_moves(setup, accept);
        check(camera == 0 && setup.screen == 2, "picking a card did not apply it and move on");
        setup_apply_moves(setup, down);
        setup_apply_moves(setup, down);
        check(setup.focus == 1, "down went past the last card");
        setup_apply_moves(setup, back);
        check(setup.screen == 1 && setup.focus == 0 && setup.result == SetupResult::Open, "back did not return to the question before");
        setup_apply_moves(setup, back);
        setup_apply_moves(setup, back);
        check(setup.result == SetupResult::Skipped, "back on the welcome did not leave the setup");

        setup_start(setup, true);
        setup_apply_moves(setup, down);
        setup_apply_moves(setup, accept);
        check(setup.result == SetupResult::Skipped && camera == 0 && blur == 1, "Skip on the welcome changed a setting");

        setup_start(setup, false);
        check(setup.screen == 1 && setup.first == 1, "the Menu page's setup did not start at the first question");
        Moves sideways; sideways.left = sideways.right = sideways.next_page = sideways.reset = true;
        setup_apply_moves(setup, sideways);
        check(setup.screen == 1 && camera == 0 && setup.result == SetupResult::Open, "left / right or a page change acted in the setup");
        setup_apply_moves(setup, back);
        check(setup.result == SetupResult::Skipped, "back on the first question of a Menu page setup did not leave it");
        setup_apply_moves(setup, accept);
        check(setup.result == SetupResult::Skipped && camera == 0, "a finished setup still took moves");

        camera = 7; // a value no choice matches (the freecam)
        setup_start(setup, false);
        check(setup.focus == 0, "an unmatched setting did not focus the first choice");
        setup_pick(setup, 9);
        check(setup.screen == 1 && camera == 7, "a card that does not exist was picked");
    }

    // 1.2.7 lean menu: rows under More settings stay hidden until its row is pressed, then focus moves into them.
    {
        bool open = false;
        int runs = 0;
        Page page;
        page.title = "View";
        page.items.push_back(make(Kind::Action, "First"));
        page.items.back().run = [&runs] { ++runs; };
        Item more = make(Kind::Action, "More settings");
        more.visible = [&open] { return !open; };
        more.run = [&open] { open = true; };
        page.items.push_back(more);
        Item hidden = make(Kind::Toggle, "Rare");
        bool rare = false;
        hidden.get_bool = [&rare] { return rare; };
        hidden.set_bool = [&rare](bool on) { rare = on; };
        hidden.visible = [&open] { return open; };
        page.items.push_back(hidden);
        std::vector<Page> view{page};
        State s;
        set_page(view, s, 0);
        Moves down; down.down = true;
        Moves accept; accept.accept = true;
        apply_moves(view, s, down);
        check(s.focus == 1, "down did not reach the More settings row");
        apply_moves(view, s, down);
        check(s.focus == 1, "focus went to a hidden row");
        apply_moves(view, s, accept);
        check(open && s.focus == 2, "opening More settings did not move focus to the first row it showed");
        apply_moves(view, s, accept);
        check(rare, "a row shown by More settings did not take A");
        Moves up; up.up = true;
        apply_moves(view, s, up);
        check(s.focus == 0, "up stopped on the hidden More settings row");
    }

    // Home tiles: left / right step through them, up / down move a row of four.
    {
        std::vector<int> picks(6, 0);
        Page home;
        home.title = "Home";
        home.tiles = true;
        for (int i = 0; i < 6; ++i) {
            Item tile = make(Kind::Action, "Tile");
            tile.run = [&picks, i] { ++picks[i]; };
            home.items.push_back(tile);
        }
        std::vector<Page> pages{home};
        State s;
        set_page(pages, s, 0);
        Moves right; right.right = true;
        Moves left; left.left = true;
        Moves down; down.down = true;
        Moves up; up.up = true;
        Moves accept; accept.accept = true;
        apply_moves(pages, s, right);
        apply_moves(pages, s, right);
        check(s.focus == 2, "right did not step to the next tile");
        apply_moves(pages, s, down);
        check(s.focus == 5, "down from a top tile with nothing below it did not reach the last tile");
        apply_moves(pages, s, up);
        check(s.focus == 1, "up did not move a row of four");
        apply_moves(pages, s, left);
        apply_moves(pages, s, left);
        check(s.focus == 0, "left went past the first tile");
        apply_moves(pages, s, accept);
        check(picks[0] == 1 && picks[1] == 0, "A on a tile did not run that tile alone");
    }

    // Shortcut playback: held controls stay lit through the beats after them; a long press fills, then fires.
    {
        using namespace wuwa_pad3d;
        const Beats view = triggers_then(press(button::R3));
        const auto at_press = moment(view, beat_seconds(view[0]) + lead_seconds + 0.1);
        check(at_press.beat == 1 && (at_press.held & button::LT) && (at_press.held & button::RT) && (at_press.down & button::R3),
            "the triggers were not held while R3 was pressed");
        const auto resting = moment(view, loop_seconds(view) - 0.05);
        check(resting.beat == -1 && resting.held == 0 && resting.down == 0, "controls stayed lit in the rest between loops");
        const auto again = moment(view, loop_seconds(view) + 0.1);
        check(again.beat == 0 && (again.held & button::LT), "the playback did not loop");
        const Beats hold_menu{long_press(button::Menu, 1.0f)};
        const auto filling = moment(hold_menu, lead_seconds + 0.5);
        check(filling.progress > 0.45f && filling.progress < 0.55f && (filling.down & button::Menu), "a long press did not fill as it was held");
        const auto fired = moment(hold_menu, lead_seconds + 1.1);
        check(fired.down == 0 && fired.ripple >= 0.0f && (fired.ripple_mask & button::Menu), "a long press did not fire when it completed");
        const auto twice = moment(Beats{press(button::R3, 2)}, lead_seconds + tap_seconds + 0.1);
        check(twice.down & button::R3, "the second click of a double click was missing");

        Shortcut views;
        views.pad = view;
        check(views.beats(Device::vr).size() == 2 && views.beats(Device::keyboard).empty(), "a pad shortcut was not shown on VR controllers");
        Shortcut dpad;
        dpad.pad = l3_then(button::Down);
        check(dpad.beats(Device::vr).empty() && shown_on(dpad, Device::keyboard) == Device::xbox,
            "a D-pad shortcut showed on VR controllers or a key-less one not on the pad");
        Shortcut vr_menu;
        vr_menu.vr = hold_menu;
        vr_menu.vr_only = true;
        check(vr_menu.beats(Device::xbox).empty() && shown_on(vr_menu, Device::xbox) == Device::vr,
            "a VR-only shortcut was not shown on the VR controllers");
    }

    std::cout << "PASS: " << checks << " VR menu navigation checks (pure C++ values)\n";
    return 0;
}
