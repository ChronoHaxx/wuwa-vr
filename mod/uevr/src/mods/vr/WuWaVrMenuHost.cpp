#include "WuWaVrMenuHost.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "Framework.hpp"
#include "Mod.hpp"
#include "Mods.hpp"
#include "mods/VR.hpp"
#include "utility/WuWaLocalization.hpp"
#include "utility/WuWaVrMenu.hpp"

namespace wuwa_menu_host {
namespace {
using namespace wuwa_menu;

bool classic{};
State state{};

// Settings are found once by config key; UEVR's mods live for the whole process.
IModValue* find(std::string_view key) {
    for (auto& mod : g_framework->get_mods()->get_mods()) {
        if (auto* value = mod->get_value(key)) return value;
    }
    return nullptr;
}

template <typename T>
ModValue<T>* typed(const char* key) {
    static std::vector<std::pair<std::string, ModValue<T>*>> cache;
    for (const auto& [name, value] : cache) {
        if (name == key) return value;
    }
    auto* value = dynamic_cast<ModValue<T>*>(find(key));
    if (value) cache.emplace_back(key, value);
    return value;
}

Item setting_toggle(const char* label, const char* help, const char* key) {
    auto item = make(Kind::Toggle, label, help);
    item.enabled = [key] { return typed<bool>(key) != nullptr; };
    item.get_bool = [key] { auto* v = typed<bool>(key); return v && v->value(); };
    item.set_bool = [key](bool on) { if (auto* v = typed<bool>(key)) v->value() = on; };
    return item;
}

Item setting_slider(const char* label, const char* help, const char* key, float min, float max, float step, const char* format) {
    auto item = make(Kind::Slider, label, help);
    item.enabled = [key] { return typed<float>(key) != nullptr; };
    item.get_float = [key, min] { auto* v = typed<float>(key); return v ? v->value() : min; };
    item.set_float = [key](float value) { if (auto* v = typed<float>(key)) v->value() = value; };
    item.min = min; item.max = max; item.step = step; item.format = format;
    return item;
}

Item setting_choice(const char* label, const char* help, const char* key, std::vector<std::string> choices) {
    auto item = make(Kind::Choice, label, help);
    item.enabled = [key] { return typed<int32_t>(key) != nullptr; };
    item.get_int = [key] { auto* v = typed<int32_t>(key); return v ? static_cast<int>(v->value()) : 0; };
    item.set_int = [key](int value) { if (auto* v = typed<int32_t>(key)) v->value() = value; };
    item.choices = std::move(choices);
    return item;
}

Item action(const char* label, const char* help, std::function<void()> run) {
    auto item = make(Kind::Action, label, help);
    item.run = std::move(run);
    return item;
}

std::vector<Page> build_pages() {
    auto* vr = VR::get().get();
    std::vector<Page> pages;

    Page quick{"Quick"};
    auto controllers = make(Kind::Toggle, "VR controllers",
        "Use the VR controllers as an Xbox pad for walking and sightseeing. Holding the left Menu button for 1 second also turns them on or off.");
    controllers.get_bool = [vr] { return vr->sightseeing_on(); };
    controllers.set_bool = [vr](bool on) { vr->set_sightseeing_on(on, "VR menu"); };
    quick.items.push_back(std::move(controllers));
    quick.items.push_back(setting_choice("Sharing with Xbox / treadmill",
        "Both together: buttons combine and each stick follows whichever is pushed further, so treadmill walking keeps working. Last used wins: whichever you used last controls the game.",
        "VR_WuWaVRControllerStyle", {"Both together", "Last used wins", "VR only (Xbox ignored)"}));
    auto screen = make(Kind::Toggle, "Stereo screen",
        "Shows the game on a flat stereo screen, for menus and cutscenes that look wrong in full VR. Same as fully holding LT + RT, then L3.");
    screen.get_bool = [vr] { return vr->is_using_2d_screen(); };
    screen.set_bool = [vr](bool on) { vr->set_stereo_screen_manually(on); };
    quick.items.push_back(std::move(screen));
    auto mono = make(Kind::Toggle, "Mono theatre",
        "One flat image shown identically to both eyes, with no stereo depth. Useful for problematic menus and cinematics.");
    mono.get_bool = [vr] { return vr->is_using_mono_theatre(); };
    mono.set_bool = [vr](bool on) { vr->set_mono_theatre_manually(on); };
    quick.items.push_back(std::move(mono));
    auto diorama = make(Kind::Toggle, "Diorama (miniature world)",
        "Shows the world as a 10x miniature around you, with the portal on or off. Needs Native Stereo. This launch only.");
    diorama.enabled = [vr] { return !vr->physical_gamepad_passthrough(); };
    diorama.get_bool = [vr] { return vr->is_diorama_enabled(); };
    diorama.set_bool = [vr](bool on) { vr->set_diorama_enabled(on); };
    quick.items.push_back(std::move(diorama));
    quick.items.push_back(setting_slider("HUD size", "How large the game's HUD and menus appear.", "UI_Size", 0.5f, 5.0f, 0.05f, "%.2f"));
    quick.items.push_back(setting_slider("HUD distance", "How far away the HUD sits. Its stereo depth follows this distance.", "UI_Distance", 0.5f, 6.0f, 0.1f, "%.1f m"));
    quick.items.push_back(action("Recenter view", "Faces the view forward from where you are now.", [vr] { vr->recenter_view(); }));
    quick.items.push_back(action("All settings (classic UEVR menu)",
        "Opens UEVR's full settings window. Every setting here edits the same values.", [] { classic = true; }));
    pages.push_back(std::move(quick));

    Page more{"More"};
    more.items.push_back(action("All settings (classic UEVR menu)",
        "Opens UEVR's full settings window. Every setting here edits the same values.", [] { classic = true; }));
    auto use = setting_toggle("Use this menu when opening UEVR",
        "Off: UEVR opens its classic window, as before. You can turn this menu back on under VR > WuWa Controls.", "VR_WuWaVrMenu");
    use.set_bool = [](bool on) {
        if (auto* v = typed<bool>("VR_WuWaVrMenu")) v->value() = on;
        if (!on) classic = true;
    };
    more.items.push_back(std::move(use));
    pages.push_back(std::move(more));
    return pages;
}

Moves read_moves() {
    const auto& io = ImGui::GetIO();
    const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };
    const auto once = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    Moves moves;
    moves.up = pressed(ImGuiKey_GamepadDpadUp) || pressed(ImGuiKey_GamepadLStickUp) || pressed(ImGuiKey_UpArrow);
    moves.down = pressed(ImGuiKey_GamepadDpadDown) || pressed(ImGuiKey_GamepadLStickDown) || pressed(ImGuiKey_DownArrow);
    moves.left = pressed(ImGuiKey_GamepadDpadLeft) || pressed(ImGuiKey_GamepadLStickLeft) || pressed(ImGuiKey_LeftArrow);
    moves.right = pressed(ImGuiKey_GamepadDpadRight) || pressed(ImGuiKey_GamepadLStickRight) || pressed(ImGuiKey_RightArrow);
    moves.accept = once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Space);
    moves.back = once(ImGuiKey_GamepadFaceRight) || once(ImGuiKey_Escape);
    moves.prev_page = once(ImGuiKey_GamepadL1) || (once(ImGuiKey_Tab) && io.KeyShift);
    moves.next_page = once(ImGuiKey_GamepadR1) || (once(ImGuiKey_Tab) && !io.KeyShift);
    return moves;
}
}

bool draw(const ImVec2& target_size) {
    if (!wuwa_test::is_wuwa() || classic) return false;
    auto* enabled = typed<bool>("VR_WuWaVrMenu");
    if (enabled && !enabled->value()) return false;
    static auto pages = build_pages();

    // 1200 x 780 design, as large as the UI target allows.
    const float width = (std::min)(target_size.x * 0.92f, target_size.y * 0.92f * 1200.0f / 780.0f);
    const ImVec2 size{width, width * 780.0f / 1200.0f};
    const ImVec2 pos{(target_size.x - size.x) * 0.5f, (target_size.y - size.y) * 0.5f};
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
    ImGui::SetNextWindowSize(target_size);
    ImGui::Begin("WuWa VR menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    wuwa_menu::draw(pages, state, read_moves(), pos, size, Look{1.0f, wuwa_l10n::sheet_font()});
    ImGui::End();
    if (state.close_requested) {
        state.close_requested = false;
        g_framework->set_draw_ui(false);
    }
    return true;
}

void draw_back_button() {
    if (!wuwa_test::is_wuwa() || !classic) return;
    if (ImGui::Button(wuwa_l10n::label("Back to the WuWa VR menu").c_str())) classic = false;
}

void on_closed() {
    classic = false;
    state.focus = -1;
    state.dragging = -1;
    state.close_requested = false;
}
}
