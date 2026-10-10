#pragma once

// VR-first settings menu (docs/VR-MENU.md). The model below is plain data with
// getters and setters, so the same pages can be drawn over UEVR's real settings
// in the game or over sample values in dev/imgui-preview. Navigation is the
// menu's own, not ImGui's, so a laser, a mouse, a gamepad and a keyboard all
// behave the same. Everything here runs on the UI thread.
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

struct ImFont;
struct ImVec2;

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
};

// Draws the menu into the current ImGui window, filling pos..pos+size. Pointer
// input comes from ImGui's mouse (the host feeds the laser into it).
void draw(std::vector<Page>& pages, State& state, const Moves& moves, const ImVec2& pos, const ImVec2& size, const Look& look);
}
