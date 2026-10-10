#include "WuWaVrMenuHost.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <string>
#include <vector>

#include "Framework.hpp"
#include "Mod.hpp"
#include "Mods.hpp"
#include "mods/VR.hpp"
#include "utility/WuWaLocalization.hpp"
#include "utility/WuWaStepPlan.hpp"
#include "utility/WuWaVrMenu.hpp"

namespace wuwa_menu_host {
namespace {
using namespace wuwa_menu;

bool classic{};
State state{};
MoveReader mover{};
wuwa_steps::Runner steps{};
std::vector<Page> pages{};
std::uint64_t built_revision{~0ull};
bool built_with_test{};

std::int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

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

// Test plans switch settings by config key, exactly like the plugin API
// (including the diorama, which is a request rather than a saved setting).
wuwa_steps::Settings settings_access() {
    wuwa_steps::Settings access;
    access.get = [](const std::string& key) -> std::optional<std::string> {
        if (key == "WuWaDiorama_Enabled") return VR::get()->is_diorama_enabled() ? "true" : "false";
        if (auto* value = find(key)) return value->get();
        return std::nullopt;
    };
    access.set = [](const std::string& key, const std::string& value) {
        if (key == "WuWaDiorama_Enabled") {
            if (VR::get()->physical_gamepad_passthrough()) return false;
            VR::get()->set_diorama_enabled(value == "true");
            return true;
        }
        auto* setting = find(key);
        if (!setting) return false;
        setting->set(value);
        return true;
    };
    return access;
}

// Saved with every answer and mark, so results say what was actually on.
nlohmann::json context() {
    auto* vr = VR::get().get();
    nlohmann::json settings = nlohmann::json::object();
    for (const char* key : {"VR_RenderingMethod", "VR_NativeStereoFix", "VR_ExtremeCompatibilityMode", "VR_2DScreenMode",
             "VR_MonoTheatreMode", "VR_HorizontalProjectionOverride", "UI_Size", "UI_Distance"}) {
        if (auto* value = find(key)) settings[key] = value->get();
    }
    for (const auto& [key, original] : steps.touched()) {
        if (auto* value = find(key)) settings[key] = value->get();
    }
    return {{"recording", vr->get_wuwa_controls().menu_recording_state()}, {"diorama", vr->is_diorama_enabled()},
        {"vr_controllers", vr->sightseeing_on()}, {"settings", settings}};
}

bool recording(const std::string& state) { return state == "starting" || state == "recording"; }

std::string recorder_line(const std::string& state) {
    if (state == "offline") return "Recorder: start the WuWa VR launcher to record.";
    if (state == "unavailable") return "Recorder: missing from this launcher package.";
    if (state == "waiting") return "Recorder: waiting for the launcher...";
    if (state == "starting") return "Recorder: starting.";
    if (state == "recording") return "Recorder: recording. Answers and marks note the time.";
    if (state == "finishing") return "Recorder: finishing the video.";
    if (state == "saved") return "Recorder: saved. Open recordings from the launcher.";
    if (state == "error") return "Recorder: failed. See WuWa Controls > Recording.";
    return "Recorder: ready.";
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

Item action(std::string label, std::string help, std::function<void()> run) {
    auto item = make(Kind::Action, std::move(label), std::move(help));
    item.run = std::move(run);
    return item;
}

Item paragraph(std::string text, bool emphasis = false) {
    auto item = make(Kind::Text, std::move(text));
    item.wrap = !emphasis;
    item.emphasis = emphasis;
    return item;
}

Page quick_page() {
    auto* vr = VR::get().get();
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
    return quick;
}

Page test_page() {
    Page page{"Test"};
    const auto* plan = steps.plan();
    if (!plan) return page;
    auto head = paragraph(plan->title, true);
    head.value_text = [] {
        return steps.finished() ? std::string{"·  finished"}
                                : "·  step " + std::to_string(steps.index() + 1) + " of " + std::to_string(steps.count());
    };
    page.items.push_back(std::move(head));
    if (!plan->scene.empty()) page.items.push_back(paragraph("Where: " + plan->scene));
    if (const auto* step = steps.step()) {
        page.items.push_back(paragraph(step->title, true));
        if (!step->action.empty()) page.items.push_back(paragraph("Do: " + step->action));
        if (!step->expect.empty()) page.items.push_back(paragraph("Expect: " + step->expect));
        for (const auto& answer : step->answers) {
            page.items.push_back(action(answer, "Saves this answer with the settings now in use, then shows the next step.",
                [answer] { steps.answer(answer, context(), unix_ms(), settings_access()); }));
        }
        page.items.push_back(action("Skip this step", "Saves \"skipped\" and shows the next step.",
            [] { steps.answer("skipped", context(), unix_ms(), settings_access()); }));
        auto back = action("Previous step", "Goes back one step. Your earlier answer stays saved.",
            [] { steps.back(unix_ms(), settings_access()); });
        back.enabled = [] { return steps.index() > 0; };
        page.items.push_back(std::move(back));
    } else {
        page.items.push_back(paragraph("Plan finished. Your answers are saved for Claude in wuwa-steps / results.jsonl."));
        page.items.push_back(action("Restart plan", "Starts again from step 1.", [] { steps.restart(unix_ms(), settings_access()); }));
    }
    page.items.push_back(action("Mark this moment", "Saves the time and current settings, so the moment is easy to find later.",
        [] { steps.mark(context(), unix_ms()); }));
    auto record = make(Kind::Toggle, "Record video", "Records the headset view through the launcher. Answers and marks are saved with the time.");
    record.enabled = [] {
        const auto state = VR::get()->get_wuwa_controls().menu_recording_state();
        return state != "offline" && state != "unavailable" && state != "waiting" && state != "finishing" && state != "busy";
    };
    record.get_bool = [] { return recording(VR::get()->get_wuwa_controls().menu_recording_state()); };
    record.set_bool = [](bool on) {
        if (on != recording(VR::get()->get_wuwa_controls().menu_recording_state())) VR::get()->get_wuwa_controls().menu_recording_toggle();
    };
    page.items.push_back(std::move(record));
    auto status = make(Kind::Text, "");
    status.value_text = [] { return recorder_line(VR::get()->get_wuwa_controls().menu_recording_state()); };
    page.items.push_back(std::move(status));
    auto problem = make(Kind::Text, "");
    problem.wrap = true;
    problem.value_text = [] { return steps.error().empty() ? std::string{} : "plan.json was not read: " + steps.error(); };
    page.items.push_back(std::move(problem));
    return page;
}

Page menu_page() {
    auto* vr = VR::get().get();
    Page page{"Menu"};
    page.items.push_back(setting_slider("Menu size", "How much of its layer this menu fills. Smaller also reads as further away.",
        "VR_WuWaVrMenuSize", 0.4f, 1.0f, 0.05f, "%.2f"));
    page.items.push_back(setting_slider("Menu distance", "How far away this menu floats. UEVR's classic window uses the same distance.",
        "UI_Framework_Distance", 0.8f, 4.0f, 0.1f, "%.1f m"));
    auto laser = make(Kind::Text, "");
    laser.value_text = [vr] { return vr->menu_pointer_status(); };
    page.items.push_back(std::move(laser));
    page.items.push_back(action("All settings (classic UEVR menu)",
        "Opens UEVR's full settings window. Every setting here edits the same values.", [] { classic = true; }));
    auto use = setting_toggle("Use this menu when opening UEVR",
        "Off: UEVR opens its classic window, as before. You can turn this menu back on under VR > WuWa Controls.", "VR_WuWaVrMenu");
    use.set_bool = [](bool on) {
        if (auto* v = typed<bool>("VR_WuWaVrMenu")) v->value() = on;
        if (!on) classic = true;
    };
    page.items.push_back(std::move(use));
    return page;
}

// Pages follow the plan: a Test page appears while one is loaded and is
// rebuilt as its steps change. The page you were on stays selected.
void refresh_pages() {
    const bool want_test = steps.loaded();
    if (!pages.empty() && want_test == built_with_test && (!want_test || steps.revision() == built_revision)) return;
    const auto current = pages.empty() ? std::string{"Quick"} : pages[std::clamp(state.page, 0, static_cast<int>(pages.size()) - 1)].title;
    pages.clear();
    pages.push_back(quick_page());
    if (want_test) pages.push_back(test_page());
    pages.push_back(menu_page());
    int index = 0;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        if (pages[i].title == current) index = i;
    }
    if (want_test && !built_with_test) index = 1; // a new plan opens on its Test page
    set_page(pages, state, index);
    built_with_test = want_test;
    built_revision = steps.revision();
}

Moves read_moves() {
    const auto& io = ImGui::GetIO();
    const auto down = [](ImGuiKey key) { return ImGui::IsKeyDown(key); };
    const auto pushed = [](ImGuiKey key) { return ImGui::GetKeyData(key)->AnalogValue >= stick_push; };
    const auto once = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    Held held;
    held.up = down(ImGuiKey_GamepadDpadUp) || down(ImGuiKey_UpArrow) || pushed(ImGuiKey_GamepadLStickUp);
    held.down = down(ImGuiKey_GamepadDpadDown) || down(ImGuiKey_DownArrow) || pushed(ImGuiKey_GamepadLStickDown);
    held.left = down(ImGuiKey_GamepadDpadLeft) || down(ImGuiKey_LeftArrow) || pushed(ImGuiKey_GamepadLStickLeft);
    held.right = down(ImGuiKey_GamepadDpadRight) || down(ImGuiKey_RightArrow) || pushed(ImGuiKey_GamepadLStickRight);
    const bool accept = once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Space);
    const bool back = once(ImGuiKey_GamepadFaceRight) || once(ImGuiKey_Escape);
    const bool prev_page = once(ImGuiKey_GamepadL1) || (once(ImGuiKey_Tab) && io.KeyShift);
    const bool next_page = once(ImGuiKey_GamepadR1) || (once(ImGuiKey_Tab) && !io.KeyShift);
    return mover.read(held, accept, back, prev_page, next_page, seconds());
}
}

void tick() {
    if (!wuwa_test::is_wuwa()) return;
    steps.poll(Framework::get_persistent_dir() / "wuwa-steps", unix_ms(), settings_access());
}

bool overlay_wanted() {
    return wuwa_test::is_wuwa() && steps.loaded();
}

bool draw(const ImVec2& target_size) {
    if (!wuwa_test::is_wuwa()) return false;
    tick();
    if (classic) return false;
    auto* enabled = typed<bool>("VR_WuWaVrMenu");
    if (enabled && !enabled->value()) return false;
    refresh_pages();

    // 1200 x 780 design, scaled to the chosen share of the UI target.
    const float fraction = std::clamp(VR::get()->vr_menu_size(), 0.4f, 1.0f);
    const float width = (std::min)(target_size.x, target_size.y * 1200.0f / 780.0f) * fraction;
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

void draw_step_overlay(ImDrawList* list, const ImVec2& size) {
    const auto* plan = steps.plan();
    if (!plan || !list) return;
    ImFont* font = wuwa_l10n::sheet_font() ? wuwa_l10n::sheet_font() : ImGui::GetFont();
    wuwa_menu::draw_step_card(list, size, font, *plan, steps.index(),
        recording(VR::get()->get_wuwa_controls().menu_recording_state()), steps.error());
}
}
