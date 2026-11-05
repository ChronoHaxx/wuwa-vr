#pragma once

// VR-first settings menu (docs/VR-MENU.md). The model below is plain data with
// getters and setters, so the same pages can be drawn over UEVR's real settings
// in the game or over sample values in dev/imgui-preview. Navigation is the
// menu's own, not ImGui's, so a laser, a mouse, a gamepad and a keyboard all
// behave the same. Everything here runs on the UI thread.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "WuWaShortcutBeats.hpp"

struct ImDrawList;
struct ImFont;
struct ImVec2;
namespace wuwa_steps { struct Plan; }

namespace wuwa_menu {
enum class Kind { Toggle, Choice, Slider, Action, Text };

struct Item {
    Kind kind{Kind::Text};
    std::string label; // English; translated when drawn
    std::string help;  // shown in the help strip while the item has focus
    std::function<bool()> enabled; // empty means always enabled
    std::function<bool()> get_bool;
    std::function<void(bool)> set_bool;
    std::function<int()> get_int;
    std::function<void(int)> set_int;
    std::vector<std::string> choices; // English; translated when drawn
    // Set when some choices are names (devices) that must not go through translation: the shown text of choice i.
    std::function<std::string(int)> choice_text;
    std::function<float()> get_float;
    std::function<void(float)> set_float;
    float min{0.0f}, max{1.0f}, step{0.1f};
    const char* format{"%.2f"};
    std::function<void()> run;
    std::function<std::string()> value_text; // live text for Action and Text rows
    bool wrap{};     // Text: a paragraph, as tall as it needs
    bool emphasis{}; // Text: a heading in gold
    // Settings: the default as shown (English or a number), whether the value is the
    // default, and a reset (X / Delete). Rows without them show no default line.
    std::function<std::string()> default_text;
    std::function<bool()> is_default;
    std::function<void()> reset;
    bool session_only{}; // this launch only: never saved
    // 1.2.7 lean menu (owner, 5 Nov 2026). Hidden rows take no space and no focus (rows under More settings).
    std::function<bool()> visible;
    // A second help paragraph (English, translated on its own): notes that used to be rows keep their translations.
    std::string help_more;
    // A live line in the help strip while the row has focus (status that used to take a row of its own), as shown.
    std::function<std::string()> status;
    // The row's shortcuts, played one after another on a live 3D controller in the help strip, on the device
    // used last; label names each when there are several (English; translated when drawn).
    struct Keys { std::string label; wuwa_pad3d::Shortcut shortcut; };
    std::vector<Keys> shortcuts;
};

inline Item make(Kind kind, std::string label, std::string help = {}) {
    Item item;
    item.kind = kind;
    item.label = std::move(label);
    item.help = std::move(help);
    return item;
}

struct Page {
    std::string title; // English
    std::vector<Item> items;
    bool tiles{};      // Home: large tiles in a grid of tile_columns; left / right / up / down move between them
};
constexpr int tile_columns = 4;

// Discrete moves from a gamepad or keyboard, gathered by the host each frame as
// new presses (or key repeats), never as held state.
struct Moves {
    bool up{}, down{}, left{}, right{}, accept{}, back{}, prev_page{}, next_page{}, reset{};
    int scale{1}; // steps per left / right move on a slider; grows while held
    bool any() const { return up || down || left || right || accept || back || prev_page || next_page || reset; }
};

// Turns a held direction into moves: one when pressed, then, after a pause,
// a repeat that speeds up the longer it is held (twice as often after 1.5 s,
// three times after 3 s). Sticks count as held only when pushed well past centre.
struct Repeater {
    double pause{0.45}, every{0.18};
    double since{-1.0}, last{0.0};
    double interval(double held) const { return held >= 3.0 ? every / 3.0 : held >= 1.5 ? every / 2.0 : every; }
    bool step(bool held, double now) {
        if (!held) { since = -1.0; return false; }
        if (since < 0.0) { since = last = now; return true; }
        if (now - since >= pause && now - last >= interval(now - since) - 1e-9) { last = now; return true; }
        return false;
    }
    double held_for(double now) const { return since < 0.0 ? 0.0 : now - since; }
};

// Slider steps per move after holding left / right this long, so extreme values
// are seconds away instead of a minute. nudge() caps it at a tenth of the range.
inline int hold_scale(double held) {
    return held >= 3.0 ? 10 : held >= 2.0 ? 5 : held >= 1.2 ? 2 : 1;
}

struct Held {
    bool up{}, down{}, left{}, right{};
};

// A stick gives at most one direction at a time. A push engages only when it
// is well past centre and mostly along one axis, so a slightly diagonal push
// never moves two ways. A held direction lasts until the stick falls back
// toward centre, so wobble during a push neither flickers nor repeats it.
enum class Dir { None, Up, Down, Left, Right };

struct StickReader {
    float engage{0.6f};    // fraction of full deflection along the axis
    float release{0.35f};  // a held direction ends below this
    float dominance{1.6f}; // the axis must lead the other by this ratio (about 32 degrees)
    Dir held{Dir::None};

    static float along(Dir dir, float x, float y) {
        switch (dir) {
        case Dir::Up: return y;
        case Dir::Down: return -y;
        case Dir::Left: return -x;
        case Dir::Right: return x;
        default: return 0.0f;
        }
    }

    Dir read(float x, float y) {
        if (!std::isfinite(x) || !std::isfinite(y)) x = y = 0.0f;
        const float ax = std::fabs(x), ay = std::fabs(y);
        Dir candidate = Dir::None;
        if (ay >= engage && ay >= dominance * ax) candidate = y > 0.0f ? Dir::Up : Dir::Down;
        else if (ax >= engage && ax >= dominance * ay) candidate = x > 0.0f ? Dir::Right : Dir::Left;
        if (held != Dir::None && along(held, x, y) >= release && (candidate == Dir::None || candidate == held)) return held;
        held = candidate;
        return held;
    }
};

// When to show the "cinematic scene" note: once per cinematic, after it has
// lasted a moment, for a few seconds. A cinematic ends after `gap` without one.
// Times are milliseconds from any steady clock.
struct CinemaHint {
    std::uint64_t settle{1200}, show{8000}, gap{10000};
    std::uint64_t since{}, last{}, shown{};
    bool done{};

    bool update(bool cinematic, bool eligible, std::uint64_t now) {
        if (cinematic) {
            if (since == 0 || now - last > gap) { since = now; shown = 0; done = false; }
            last = now;
        }
        if (done || since == 0) return false;
        if (shown != 0 && (!eligible || now - shown >= show)) { done = true; return false; }
        if (shown == 0) {
            if (!cinematic || !eligible || now - since < settle) return false;
            shown = now;
        }
        return true;
    }
};

// Elapsed time as m:ss, or h:mm:ss past an hour.
inline std::string clock_text(double seconds) {
    const long long total = seconds > 0.0 ? static_cast<long long>(seconds) : 0;
    const long long h = total / 3600, m = (total / 60) % 60, s = total % 60;
    char text[32];
    if (h > 0) std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", h, m, s);
    else std::snprintf(text, sizeof(text), "%lld:%02lld", m, s);
    return text;
}

class MoveReader {
public:
    // Directions repeat; accept, back and page changes are single presses.
    Moves read(const Held& held, bool accept, bool back, bool prev_page, bool next_page, double now) {
        Moves moves;
        moves.up = m_up.step(held.up && !held.down, now);
        moves.down = m_down.step(held.down && !held.up, now);
        moves.left = m_left.step(held.left && !held.right, now);
        moves.right = m_right.step(held.right && !held.left, now);
        moves.scale = hold_scale(moves.left ? m_left.held_for(now) : moves.right ? m_right.held_for(now) : 0.0);
        moves.accept = accept;
        moves.back = back;
        moves.prev_page = prev_page;
        moves.next_page = next_page;
        return moves;
    }

private:
    Repeater m_up, m_down, m_left, m_right;
};

// Whatever was used last decides how focus is shown and hinted.
enum class Source { Pointer, Buttons };

struct State {
    int page{0};
    int focus{-1};
    float scroll{0.0f};
    Source source{Source::Buttons};
    bool close_requested{};
    int dragging{-1}; // slider row held by the pointer
};

inline bool shown(const Item& item) {
    return !item.visible || item.visible();
}

inline bool focusable(const Item& item) {
    return item.kind != Kind::Text && shown(item) && (!item.enabled || item.enabled());
}

inline int first_focusable(const Page& page) {
    for (int i = 0; i < static_cast<int>(page.items.size()); ++i) {
        if (focusable(page.items[i])) return i;
    }
    return -1;
}

// Next focusable row in a direction, or the current row at either end.
inline int step_focus(const Page& page, int from, int direction) {
    const int count = static_cast<int>(page.items.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction) {
        if (focusable(page.items[i])) return i;
    }
    return from;
}

inline float snap(const Item& item, float value) {
    if (item.step > 0.0f) value = item.min + std::round((value - item.min) / item.step) * item.step;
    return std::clamp(value, item.min, item.max);
}

// Left / right on a row: choices move one entry, toggles set off / on, sliders
// move `scale` steps (at most a tenth of their range at once).
inline void nudge(Item& item, int direction, int scale = 1) {
    switch (item.kind) {
    case Kind::Toggle:
        if (item.set_bool) item.set_bool(direction > 0);
        break;
    case Kind::Choice:
        if (item.get_int && item.set_int && !item.choices.empty()) {
            const int last = static_cast<int>(item.choices.size()) - 1;
            item.set_int(std::clamp(item.get_int() + direction, 0, last));
        }
        break;
    case Kind::Slider:
        if (item.get_float && item.set_float) {
            int steps = (std::max)(scale, 1);
            if (item.step > 0.0f) {
                const int tenth = static_cast<int>((item.max - item.min) / (10.0f * item.step) + 0.5f);
                steps = (std::min)(steps, (std::max)(tenth, 1));
            }
            item.set_float(snap(item, item.get_float() + direction * steps * item.step));
        }
        break;
    default:
        break;
    }
}

// Accept (A, Enter, trigger, click) on a row.
inline void activate(Item& item) {
    switch (item.kind) {
    case Kind::Toggle:
        if (item.get_bool && item.set_bool) item.set_bool(!item.get_bool());
        break;
    case Kind::Choice:
        if (item.get_int && item.set_int && !item.choices.empty()) {
            item.set_int((item.get_int() + 1) % static_cast<int>(item.choices.size()));
        }
        break;
    case Kind::Action:
        if (item.run) item.run();
        break;
    default:
        break;
    }
}

inline void set_page(const std::vector<Page>& pages, State& state, int page) {
    if (pages.empty()) return;
    state.page = std::clamp(page, 0, static_cast<int>(pages.size()) - 1);
    state.focus = first_focusable(pages[state.page]);
    state.scroll = 0.0f;
    state.dragging = -1;
}

// After a row hid itself (More settings opens the rows below it): focus moves on to the next row, or back.
inline void settle_focus(const Page& page, State& state) {
    const int count = static_cast<int>(page.items.size());
    if (state.focus < 0 || state.focus >= count || focusable(page.items[state.focus])) return;
    for (int i = state.focus + 1; i < count; ++i) {
        if (focusable(page.items[i])) { state.focus = i; return; }
    }
    for (int i = state.focus - 1; i >= 0; --i) {
        if (focusable(page.items[i])) { state.focus = i; return; }
    }
    state.focus = -1;
}

// A tile page: left / right step through the tiles in order, up / down move a row of the grid.
inline void tile_move(const Page& page, State& state, const Moves& moves) {
    const int count = static_cast<int>(page.items.size());
    if (moves.left) state.focus = step_focus(page, state.focus, -1);
    if (moves.right) state.focus = step_focus(page, state.focus, +1);
    const int row = moves.up ? -tile_columns : moves.down ? tile_columns : 0;
    if (row != 0) {
        const int target = state.focus + row;
        if (target >= 0 && target < count && focusable(page.items[target])) state.focus = target;
        else if (row > 0 && target >= count) {
            // Down from a top-row tile above an empty slot: the last tile.
            for (int i = count - 1; i > state.focus; --i) {
                if (focusable(page.items[i])) { state.focus = i; break; }
            }
        }
    }
}

// Applies one frame of gamepad / keyboard moves. Pages wrap; rows do not.
inline void apply_moves(std::vector<Page>& pages, State& state, const Moves& moves) {
    if (pages.empty()) return;
    if (state.page < 0 || state.page >= static_cast<int>(pages.size())) set_page(pages, state, 0);
    if (!moves.any()) return;
    state.source = Source::Buttons;
    const int count = static_cast<int>(pages.size());
    if (moves.prev_page) { set_page(pages, state, (state.page + count - 1) % count); return; }
    if (moves.next_page) { set_page(pages, state, (state.page + 1) % count); return; }
    if (moves.back) { state.close_requested = true; return; }
    auto& page = pages[state.page];
    if (state.focus < 0 || state.focus >= static_cast<int>(page.items.size()) || !focusable(page.items[state.focus])) {
        state.focus = first_focusable(page);
        if (state.focus < 0) return;
        if (moves.up || moves.down) return; // the first press only shows where focus is
    }
    if (page.tiles) {
        tile_move(page, state, moves);
    } else {
        if (moves.up) state.focus = step_focus(page, state.focus, -1);
        if (moves.down) state.focus = step_focus(page, state.focus, +1);
    }
    auto& item = page.items[state.focus];
    if (!page.tiles && moves.left) nudge(item, -1, moves.scale);
    if (!page.tiles && moves.right) nudge(item, +1, moves.scale);
    if (moves.accept) activate(item);
    if (moves.reset && item.reset && (!item.enabled || item.enabled())) item.reset();
    settle_focus(page, state);
}

// VR setup (1.2.7; owner, 5 Nov 2026: a first-launch VR setup rather than new defaults). A few questions,
// one screen each, answered with big cards. Picking a card applies it at once and moves on; the last
// screen sums up. Focus starts on the choice the settings already match, so pressing A through every
// screen changes nothing.
struct SetupChoice {
    std::string label;  // English
    std::string detail; // English, a line or two
    bool recommended{};
};

struct SetupQuestion {
    std::string title; // English; also its name on the summary
    std::string text;  // English
    std::vector<SetupChoice> choices;
    std::function<int()> get;     // the choice the settings match now, or -1
    std::function<void(int)> set; // applies a choice
};

enum class SetupResult { Open, Done, Skipped };

struct Setup {
    std::vector<SetupQuestion> questions;
    int screen{0}; // 0 the welcome, 1..n the questions, n + 1 the summary
    int first{0};  // where this run started: Back there leaves the setup
    int focus{0};  // the card in focus on this screen
    Source source{Source::Buttons};
    SetupResult result{SetupResult::Open};
    std::string done_detail; // English, under Done: where it goes back to

    int count() const { return static_cast<int>(questions.size()); }
    bool welcome() const { return screen == 0; }
    bool summary() const { return screen == count() + 1; }
    // Cards on this screen: Set up now and Skip on the welcome, the choices, Done on the summary.
    int cards() const { return welcome() ? 2 : summary() ? 1 : static_cast<int>(questions[screen - 1].choices.size()); }
};

inline void setup_go(Setup& setup, int screen) {
    setup.screen = std::clamp(screen, 0, setup.count() + 1);
    setup.focus = 0;
    if (!setup.welcome() && !setup.summary()) {
        const auto& question = setup.questions[setup.screen - 1];
        const int now = question.get ? question.get() : -1;
        if (now >= 0 && now < static_cast<int>(question.choices.size())) setup.focus = now;
    }
}

// From the welcome (first launch) or straight to the first question (the Menu page).
inline void setup_start(Setup& setup, bool welcome) {
    setup.result = SetupResult::Open;
    setup.source = Source::Buttons;
    setup.first = welcome ? 0 : 1;
    setup_go(setup, setup.first);
}

// A card picked (accept or a click): applies it and moves on.
inline void setup_pick(Setup& setup, int card) {
    if (setup.result != SetupResult::Open || card < 0 || card >= setup.cards()) return;
    if (setup.welcome()) {
        if (card == 0) setup_go(setup, 1);
        else setup.result = SetupResult::Skipped;
        return;
    }
    if (setup.summary()) {
        setup.result = SetupResult::Done;
        return;
    }
    auto& question = setup.questions[setup.screen - 1];
    if (question.set) question.set(card);
    setup_go(setup, setup.screen + 1);
}

// Back: the screen before; where this run started, it leaves the setup (as Skip does).
inline void setup_back(Setup& setup) {
    if (setup.result != SetupResult::Open) return;
    if (setup.screen <= setup.first) setup.result = SetupResult::Skipped;
    else setup_go(setup, setup.screen - 1);
}

// One frame of gamepad / keyboard moves: up / down focus a card, accept picks it, back goes back. Left /
// right and page changes do nothing here, so a stray push never changes a setting.
inline void setup_apply_moves(Setup& setup, const Moves& moves) {
    if (!moves.any() || setup.result != SetupResult::Open) return;
    setup.source = Source::Buttons;
    if (moves.up) setup.focus = (std::max)(setup.focus - 1, 0);
    if (moves.down) setup.focus = (std::min)(setup.focus + 1, setup.cards() - 1);
    if (moves.accept) setup_pick(setup, setup.focus);
    else if (moves.back) setup_back(setup);
}

struct Look {
    float scale{1.0f};
    ImFont* font{}; // a large baked font (the sheet font); null uses the current font
    bool right_to_left{};
    std::string badge; // e.g. "REC 1:23", shown in red in the header; empty for none
    wuwa_pad3d::Device device{wuwa_pad3d::Device::xbox}; // used last: shortcuts and hints are shown for it
    double time{};     // seconds, for the shortcut playback
};

// Draws the menu into the current ImGui window, filling pos..pos+size. Pointer
// input comes from ImGui's mouse (the host feeds the laser into it).
void draw(std::vector<Page>& pages, State& state, const Moves& moves, const ImVec2& pos, const ImVec2& size, const Look& look);

// The VR setup in the menu's place and size, applying this frame's moves and the pointer.
void draw_setup(Setup& setup, const Moves& moves, const ImVec2& pos, const ImVec2& size, const Look& look);

// The current step of a guided test plan, drawn where the controls card goes
// while the menu is closed. index == steps.size() means the plan is finished.
// recording is the recorder clock ("1:23") while recording, else empty.
void draw_step_card(ImDrawList* list, const ImVec2& size, ImFont* font, const wuwa_steps::Plan& plan, int index,
    const std::string& recording, const std::string& error);

// A short note in view: a title and up to two lines, on a 1600 x 900 design. Low in view by
// default; compact is a smaller one at the top, clear of dialogue lines and choices.
void draw_note(ImDrawList* list, const ImVec2& size, ImFont* font, const std::string& title, const std::string& line1,
    const std::string& line2, bool compact = false);
}
