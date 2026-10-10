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
    std::function<float()> get_float;
    std::function<void(float)> set_float;
    float min{0.0f}, max{1.0f}, step{0.1f};
    const char* format{"%.2f"};
    std::function<void()> run;
    std::function<std::string()> value_text; // live text for Action and Text rows
    bool wrap{};     // Text: a paragraph, as tall as it needs
    bool emphasis{}; // Text: a heading in gold
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
};

// Discrete moves from a gamepad or keyboard, gathered by the host each frame as
// new presses (or key repeats), never as held state.
struct Moves {
    bool up{}, down{}, left{}, right{}, accept{}, back{}, prev_page{}, next_page{};
    bool any() const { return up || down || left || right || accept || back || prev_page || next_page; }
};

// Turns a held direction into moves: one when pressed, then, after a pause,
// a steady repeat. Sticks count as held only when pushed well past centre.
struct Repeater {
    double pause{0.45}, every{0.18};
    double since{-1.0}, last{0.0};
    bool step(bool held, double now) {
        if (!held) { since = -1.0; return false; }
        if (since < 0.0) { since = last = now; return true; }
        if (now - since >= pause && now - last >= every) { last = now; return true; }
        return false;
    }
};

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

inline bool focusable(const Item& item) {
    return item.kind != Kind::Text && (!item.enabled || item.enabled());
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

// Left / right on a row: choices and sliders move one step, toggles set off / on.
inline void nudge(Item& item, int direction) {
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
        if (item.get_float && item.set_float) item.set_float(snap(item, item.get_float() + direction * item.step));
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
    if (moves.up) state.focus = step_focus(page, state.focus, -1);
    if (moves.down) state.focus = step_focus(page, state.focus, +1);
    auto& item = page.items[state.focus];
    if (moves.left) nudge(item, -1);
    if (moves.right) nudge(item, +1);
    if (moves.accept) activate(item);
}

struct Look {
    float scale{1.0f};
    ImFont* font{}; // a large baked font (the sheet font); null uses the current font
    bool right_to_left{};
    std::string badge; // e.g. "REC 1:23", shown in red in the header; empty for none
};

// Draws the menu into the current ImGui window, filling pos..pos+size. Pointer
// input comes from ImGui's mouse (the host feeds the laser into it).
void draw(std::vector<Page>& pages, State& state, const Moves& moves, const ImVec2& pos, const ImVec2& size, const Look& look);

// The current step of a guided test plan, drawn where the shortcut sheet goes
// while the menu is closed. index == steps.size() means the plan is finished.
// recording is the recorder clock ("1:23") while recording, else empty.
void draw_step_card(ImDrawList* list, const ImVec2& size, ImFont* font, const wuwa_steps::Plan& plan, int index,
    const std::string& recording, const std::string& error);

// A short note in view: a title and up to two lines, on a 1600 x 900 design.
void draw_note(ImDrawList* list, const ImVec2& size, ImFont* font, const std::string& title, const std::string& line1,
    const std::string& line2);
}
