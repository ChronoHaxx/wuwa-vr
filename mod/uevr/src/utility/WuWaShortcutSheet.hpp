#pragma once
#include <windows.h>
#include <imgui.h>
#include <string>

namespace wuwa_sheet {
// A fallback font for the cards when the language fonts are unavailable.
void reserve_font(ImFontAtlas* atlas);

// The controls cards (1.2.0). Hold L3, or LT + RT, for half a second and the
// matching card rises under the view, lighting each button as it is used.
// Text is code-rendered and translated; the controller is drawn as shapes.
enum class Card : int { l3, views };
struct CardState {
    Card card{Card::l3};
    unsigned buttons{};      // XInput buttons held, lit on the card
    bool lt{}, rt{};         // triggers held
    bool vr_controllers{};   // name the VR controllers' triggers, grips and sticks
    int view{};              // 0 full VR, 1 stereo screen, 2 mono theatre
    bool blur{};
    bool portal{}, diorama{}, mouse_mode{}, first_person{}, fixed_camera{}, ui_hidden{};
    bool pinned{};
};
// 1600 x 900 design units, centred in size.
void draw_card(ImDrawList* draw, ImVec2 size, ImFont* font, const CardState& state);
// One line low in view: a highlighted lead ("New:", the view's name), then a
// line with {L3}-style button chips. Both are English catalog keys.
void draw_tip(ImDrawList* draw, ImVec2 size, ImFont* font, const std::string& lead, const std::string& line, bool vr_controllers);

void draw_hidden_ui_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool shortcut_available, bool menu_detected=true);
void draw_mouse_mode_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool adjustment, bool hidden_menu);
}
