#pragma once
#include <windows.h>
#include <imgui.h>
#include <array>
#include <string>

namespace wuwa_sheet {
// A fallback font for the cards when the language fonts are unavailable.
void reserve_font(ImFontAtlas* atlas);

// The controls cards (1.2.0). Hold L3, or LT + RT, for half a second and the
// matching card rises under the view, lighting each button as it is used.
// Text is code-rendered and translated; the controller is drawn as shapes.
// The keyboard card (1.2.7): hold the VR key (` by default) for half a second.
enum class Card : int { l3, views, keyboard };
struct CardState {
    Card card{Card::l3};
    unsigned buttons{};      // XInput buttons held, lit on the card
    bool lt{}, rt{};         // triggers held
    bool vr_controllers{};   // name the VR controllers' triggers, grips and sticks
    int view{};              // 0 full VR, 1 stereo screen, 2 mono theatre
    bool blur{};
    bool hide_near{};        // Hide characters at your eyes (LT + RT, then Y)
    bool portal{}, diorama{}, mouse_mode{}, first_person{}, fixed_camera{}, ui_hidden{};
    bool story_nudge{};      // a story shot the camera nudge works in: Y / X move along the shot
    float story_nudge_cm{};  // the nudge in use there, cm (+ nearer); shown on the Y / X row when not 0
    bool pinned{};
    // Keyboard card: the chord keys held (wuwa_keyboard::State::chords), the wheel just used, freecam on.
    unsigned keys{};
    bool wheel_lit{}, freecam{};
};
// The VR key's name on the player's layout (` on US / UK) and the freecam keys' names by position (W A S D,
// Z Q S D on AZERTY), for the keyboard card, tips and notices. Set once a frame by the controls component.
void set_key_names(const std::string& vr_key, const std::array<std::string, 4>& move);
// 1600 x 900 design units, centred in size.
void draw_card(ImDrawList* draw, ImVec2 size, ImFont* font, const CardState& state);
// One line low in view: a highlighted lead ("New:", the view's name), then a
// line with {L3}-style button chips. Both are English catalog keys.
// note: an optional smaller line under the tip, already translated.
// keyboard: the line's {tokens} are keys ({VR} the VR key, {W} {A} {S} {D} the freecam keys by position).
void draw_tip(ImDrawList* draw, ImVec2 size, ImFont* font, const std::string& lead, const std::string& line, bool vr_controllers,
    const std::string& note = {}, bool keyboard = false);

// keyboard: name the keyboard shortcut (the VR key + H / + M) instead of the controller's.
void draw_hidden_ui_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool shortcut_available, bool menu_detected=true, bool keyboard=false);
void draw_mouse_mode_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool adjustment, bool hidden_menu, bool keyboard=false);
// The launcher ended a recording because the game ran under min_fps on average for seconds (1.2.7).
void draw_recording_stopped_warning(ImDrawList* draw, ImVec2 size, ImFont* font, int min_fps, int seconds);
}
