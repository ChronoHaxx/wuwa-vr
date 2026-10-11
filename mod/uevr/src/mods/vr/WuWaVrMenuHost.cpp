#include "WuWaVrMenuHost.hpp"

// Before windows.h (via Framework.hpp), whose min / max macros break std::min in it.
#include "utility/WuWaMonoNativeProjection.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "Framework.hpp"
#include "Mod.hpp"
#include "Mods.hpp"
#include "mods/VR.hpp"
#include "utility/WuWaDofSync.hpp"
#include "utility/WuWaLocalization.hpp"
#include "utility/WuWaStepPlan.hpp"
#include "utility/WuWaVrMenu.hpp"

namespace wuwa_menu_host {
namespace {
using namespace wuwa_menu;

bool classic{};
State state{};
MoveReader mover{};
StickReader stick{};
wuwa_steps::Runner steps{};
std::vector<Page> pages{};
std::string built_key{};
bool built_with_test{};
std::atomic<std::uint64_t> showing_until{};

// Recorder state, read once per frame; the clock starts when recording does.
std::string rec_state{"offline"};
double rec_since{-1.0};

// VR controller buttons read straight from the runtime, as presses.
struct PadEdges { bool accept{}, back{}, prev_page{}, next_page{}; } pad_was{};
// Move menu (test 3j): placing the menu around you with any input.
std::atomic<bool> moving_menu{false};
PadEdges move_was{};
double move_last{};
bool swallow_click{};

// Problem reports from the Record page.
int report_kind{};
std::string report_status{};

std::mutex hint_mutex{};
CinemaHint hint{};

std::int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string local_time() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char text[16]{};
    std::strftime(text, sizeof(text), "%H:%M:%S", &local);
    return text;
}

// Settings are found once by config key; UEVR's mods live for the whole process.
IModValue* find(std::string_view key) {
    for (auto& mod : g_framework->get_mods()->get_mods()) {
        if (auto* value = mod->get_value(key)) return value;
    }
    return nullptr;
}

IModValue* cached(const char* key) {
    static std::vector<std::pair<std::string, IModValue*>> cache;
    for (const auto& [name, value] : cache) {
        if (name == key) return value;
    }
    auto* value = find(key);
    if (value) cache.emplace_back(key, value);
    return value;
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

// The OpenXR runtime keeps its own options, outside the mods' settings that
// find() searches, so this setting is reached through the runtime.
ModSlider* resolution_scale() {
    auto* openxr = VR::get()->get_openxr_runtime();
    return openxr && openxr->loaded ? openxr->resolution_scale.get() : nullptr;
}

int camera_mode() {
    auto* mode = typed<int32_t>("WuWaControls_CameraMode");
    return mode ? mode->value() : 0;
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
        if (!setting) {
            spdlog::warn("[WuWaSteps] {} is not a setting; left alone", key);
            return false;
        }
        setting->set(value);
        spdlog::info("[WuWaSteps] {} = {} (reads back {})", key, value, setting->get());
        return true;
    };
    return access;
}

bool recording() { return rec_state == "starting" || rec_state == "recording"; }

std::string rec_clock() { return rec_since < 0.0 ? std::string{} : clock_text(seconds() - rec_since); }

void update_recording() {
    rec_state = VR::get()->get_wuwa_controls().menu_recording_state();
    if (rec_state == "recording") {
        if (rec_since < 0.0) rec_since = seconds();
    } else {
        rec_since = -1.0;
    }
}

// Saved with every answer, mark and report, so results say what was actually on.
nlohmann::json context() {
    auto* vr = VR::get().get();
    nlohmann::json settings = nlohmann::json::object();
    for (const char* key : {"VR_RenderingMethod", "VR_NativeStereoFix", "VR_ExtremeCompatibilityMode", "VR_2DScreenMode",
             "VR_MonoTheatreMode", "VR_HorizontalProjectionOverride", "VR_AutoCinema", "VR_DecoupledPitch",
             "WuWaControls_CameraMode", "UI_Size", "UI_Distance", "UI_WuWaHudShape", "WuWaControls_LensMatch",
             "WuWaControls_LensNearest", "WuWaStereo_DepthOfField", "WuWaPrivacy_Mode"}) {
        if (auto* value = cached(key)) settings[key] = value->get();
    }
    if (auto* scale = resolution_scale()) settings["OpenXR_ResolutionScale"] = scale->get();
    for (const auto& [key, original] : steps.touched()) {
        if (auto* value = find(key)) settings[key] = value->get();
    }
    nlohmann::json result{{"recording", rec_state}, {"diorama", vr->is_diorama_enabled()},
        {"vr_controllers", vr->sightseeing_on()}, {"screen", vr->is_using_2d_screen()}, {"mono", vr->is_using_mono_theatre()},
        {"settings", settings}};
    if (rec_since >= 0.0) result["video_seconds"] = static_cast<int>(seconds() - rec_since);
    return result;
}

std::string recorder_line() {
    if (rec_state == "offline") return "Recorder: open the WuWa VR app (launcher) to record.";
    if (rec_state == "unavailable") return "Recorder: missing from this launcher package.";
    if (rec_state == "waiting") return "Recorder: waiting for the launcher...";
    if (rec_state == "starting") return "Recorder: starting.";
    if (rec_state == "recording") return "Recorder: recording. Reports and marks note the time in the video.";
    if (rec_state == "finishing") return "Recorder: saving the video.";
    if (rec_state == "saved") return "Recorder: saved. Open recordings from the launcher.";
    if (rec_state == "error") return "Recorder: failed. See WuWa Controls > Recording in the classic menu.";
    return "Recorder: ready.";
}

// ---- Rows bound to settings ----

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

Item setting_int_slider(const char* label, const char* help, const char* key, int min, int max, int step, const char* format) {
    auto item = make(Kind::Slider, label, help);
    item.enabled = [key] { return typed<int32_t>(key) != nullptr; };
    item.get_float = [key, min] { auto* v = typed<int32_t>(key); return static_cast<float>(v ? v->value() : min); };
    item.set_float = [key](float value) { if (auto* v = typed<int32_t>(key)) v->value() = static_cast<int32_t>(std::lround(value)); };
    item.min = static_cast<float>(min); item.max = static_cast<float>(max); item.step = static_cast<float>(step); item.format = format;
    return item;
}

// Choices are the menu's own short labels for the setting's options, in order.
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

// A wrapped line whose text is live; it takes no space while empty.
Item live_text(std::function<std::string()> text) {
    auto item = make(Kind::Text, "");
    item.wrap = true;
    item.value_text = std::move(text);
    return item;
}

// ---- Where the menu floats (full VR and the screen views each keep their own) ----

bool screen_view() {
    auto* vr = VR::get().get();
    return vr->is_using_2d_screen() || vr->is_using_mono_theatre();
}
const char* menu_yaw_key() { return screen_view() ? "UI_WuWaMenuYawScreen" : "UI_WuWaMenuYaw"; }
const char* menu_pitch_key() { return screen_view() ? "UI_WuWaMenuPitchScreen" : "UI_WuWaMenuPitch"; }
// On a screen view "Low" is worked out from the picture each frame (the overlay
// writes it into the screen keys); moving the menu by hand keeps it where it is.
bool screen_low() {
    auto* low = typed<bool>("UI_WuWaMenuScreenLow");
    return screen_view() && low && low->value();
}
void leave_screen_low() {
    if (!screen_view()) return;
    if (auto* low = typed<bool>("UI_WuWaMenuScreenLow")) low->value() = false;
}

void start_moving_menu() {
    leave_screen_low();
    move_was = {true, true, true, true};
    move_last = 0.0;
    moving_menu.store(true, std::memory_order_relaxed);
}

Item menu_angle_slider(const char* label, const char* help, bool vertical) {
    auto item = make(Kind::Slider, label, help);
    const auto key = [vertical] { return vertical ? menu_pitch_key() : menu_yaw_key(); };
    item.enabled = [key] { return typed<float>(key()) != nullptr; };
    item.get_float = [key] { auto* v = typed<float>(key()); return v ? v->value() : 0.0f; };
    item.set_float = [key](float value) { leave_screen_low(); if (auto* v = typed<float>(key())) v->value() = value; };
    item.min = vertical ? -60.0f : -150.0f; item.max = vertical ? 60.0f : 150.0f; item.step = 2.0f; item.format = "%.0f deg";
    return item;
}

// Front, low, left or right of you; "Custom" after moving it anywhere else.
Item menu_place_choice() {
    struct Spot { float yaw, pitch; };
    static constexpr Spot spots[] = {{0.0f, 0.0f}, {0.0f, -28.0f}, {-40.0f, 0.0f}, {40.0f, 0.0f}};
    auto item = make(Kind::Choice, "Menu place",
        "Where this menu floats around you. Full VR and the screen views each keep their own place; on a screen view "
        "Low sits just under the picture, whatever its size and distance, and the menu always stays in front of it.");
    item.enabled = [] { return typed<float>(menu_yaw_key()) != nullptr; };
    item.get_int = [] {
        auto* yaw = typed<float>(menu_yaw_key());
        auto* pitch = typed<float>(menu_pitch_key());
        if (!yaw || !pitch) return 0;
        if (screen_low()) return 1;
        for (int i = 0; i < 4; ++i) {
            if (std::abs(yaw->value() - spots[i].yaw) < 1.0f && std::abs(pitch->value() - spots[i].pitch) < 1.0f) return i;
        }
        return 4;
    };
    item.set_int = [](int value) {
        auto* yaw = typed<float>(menu_yaw_key());
        auto* pitch = typed<float>(menu_pitch_key());
        if (!yaw || !pitch || value < 0 || value > 3) return;
        if (screen_view()) {
            if (auto* low = typed<bool>("UI_WuWaMenuScreenLow")) {
                low->value() = value == 1;
                if (value == 1) return;
            }
        }
        yaw->value() = spots[value].yaw;
        pitch->value() = spots[value].pitch;
    };
    item.choices = {"Front", "Low", "Left", "Right", "Custom"};
    return item;
}

// Close-up match lives in the camera script; the status is its live report.
Item lens_slider() {
    return setting_slider("Close-up match",
        "Dialogue shots use a zoom lens your headset's wide view can't show, so people look small and far. This moves you nearer, "
        "per shot, so they keep this share of their 2D size. 0% is the game camera's own spot; 100% matches the 2D shot; "
        "above 100% comes closer still. Game camera in full VR only.",
        "WuWaControls_LensMatch", 0.0f, 150.0f, 5.0f, "%.0f%%");
}

Item lens_nearest_slider() {
    return setting_slider("Closest distance",
        "How near Close-up match may bring you to the person in the shot. Lower comes closer. 100% Close-up match "
        "already gives the 2D size; this only stops it short when the shot is very tight.", "WuWaControls_LensNearest", 5.0f, 150.0f, 5.0f, "%.0f cm");
}

Item lens_subject_choice() {
    return setting_choice("Close-up aims at",
        "Characters in the shot: the person the game focuses on (or nearest the middle of the 2D frame), and Close-up match "
        "stops behind anyone in front of them, as in over-the-shoulder shots. Game focus only: the test 3f behaviour.",
        "WuWaControls_LensSubject", {"Characters in the shot", "Game focus only (3f)"});
}

Item lens_follow_choice() {
    return setting_choice("Within a shot",
        "Track the camera: the distance is measured at each cut, then follows only the game camera's own moves, so a focus "
        "change cannot move you. Fixed per shot: test 3g. Follow the focus: test 3h (moved with focus changes).",
        "WuWaControls_LensFollow", {"Track the camera", "Fixed per shot (3g)", "Follow the focus (3h)"});
}

Item lens_status() {
    return live_text([] {
        auto* status = cached("WuWaControls_LensStatus");
        const auto text = status ? status->get() : std::string{};
        return text.empty() ? std::string{} : "Now: " + text;
    });
}

// The OpenXR render resolution, in a few steps. Each change rebuilds the eye
// images (as a view switch does), so it is a choice rather than a held slider.
Item resolution_choice() {
    static const float scales[]{0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f};
    auto item = make(Kind::Choice, "Render resolution",
        "Pixels per eye compared with what your headset asks for. Lower is smoother in full VR. It changes after a moment.");
    item.enabled = [] { return resolution_scale() != nullptr; };
    item.get_int = [] {
        auto* v = resolution_scale();
        const float now = v ? v->value() : 1.0f;
        int best = 4;
        for (int i = 0; i < 7; ++i) {
            if (std::fabs(scales[i] - now) < std::fabs(scales[best] - now)) best = i;
        }
        return best;
    };
    item.set_int = [](int value) {
        if (auto* v = resolution_scale()) v->value() = scales[std::clamp(value, 0, 6)];
    };
    item.choices = {"60%", "70%", "80%", "90%", "100%", "110%", "120%"};
    return item;
}

Item view_choice() {
    auto item = make(Kind::Choice, "View",
        "Full VR: the game all around you. Stereo screen: a big 3D screen, for menus and scenes that look wrong in full VR. "
        "Mono theatre: a flat screen, the same picture in both eyes. Shortcut: hold LT + RT, then click R3 for the next view "
        "(hold R3: back to full VR).");
    item.get_int = [] {
        auto* vr = VR::get().get();
        return vr->is_using_mono_theatre() ? 2 : vr->is_using_2d_screen() ? 1 : 0;
    };
    item.set_int = [](int value) {
        auto* vr = VR::get().get();
        if (value == 2) {
            vr->set_mono_theatre_manually(true);
        } else {
            vr->set_mono_theatre_manually(false);
            vr->set_stereo_screen_manually(value == 1);
        }
    };
    item.choices = {"Full VR", "Stereo screen", "Mono theatre"};
    return item;
}

Item camera_choice() {
    return setting_choice("Camera",
        "Game camera: WuWa's own camera. Fixed third person: a steady camera behind you. Freecam: fly the camera anywhere, "
        "for cutscenes and photos. First person: through your character's eyes. Options for each are on the Camera page.",
        "WuWaControls_CameraMode", {"Game camera", "Fixed third person", "Freecam", "First person"});
}

Item record_toggle() {
    auto record = make(Kind::Toggle, "Record video",
        "Records the headset view through the WuWa VR app, which must be open. Player IDs are covered while it records "
        "(Hide player IDs, on the Record page).");
    record.enabled = [] {
        return rec_state != "offline" && rec_state != "unavailable" && rec_state != "waiting" && rec_state != "finishing" &&
            rec_state != "busy";
    };
    record.get_bool = [] { return recording(); };
    record.set_bool = [](bool on) {
        if (on != recording()) VR::get()->get_wuwa_controls().menu_recording_toggle();
    };
    record.value_text = [] {
        if (rec_state == "recording") return "REC " + rec_clock();
        if (rec_state == "starting") return std::string{"Starting"};
        if (rec_state == "finishing") return std::string{"Saving"};
        return std::string{};
    };
    return record;
}

void save_report(const std::string& problem) {
    try {
        const auto folder = Framework::get_persistent_dir() / "wuwa-reports";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        nlohmann::json line{{"event", "report"}, {"unix_ms", unix_ms()}, {"problem", problem}, {"context", context()}};
        if (const auto* step = steps.step()) {
            line["plan"] = steps.plan()->id;
            line["step"] = step->id;
        }
        std::ofstream out{folder / "reports.jsonl", std::ios::binary | std::ios::app};
        out << line.dump() << '\n';
        if (!out) throw std::runtime_error("could not write");
        report_status = "Saved at " + local_time() + (rec_since >= 0.0 ? ", " + rec_clock() + " into the video" : std::string{}) +
            ". Folder: UEVR profile > wuwa-reports.";
    } catch (const std::exception& e) {
        report_status = std::string{"Not saved: "} + e.what();
    }
}

// ---- Pages ----

Page quick_page() {
    auto* vr = VR::get().get();
    Page page{"Quick"};
    page.items.push_back(view_choice());
    page.items.push_back(camera_choice());
    auto controllers = make(Kind::Toggle, "VR controllers",
        "Use the VR controllers as an Xbox pad for walking and sightseeing. Holding the left Menu button for 1 second also turns them on or off.");
    controllers.get_bool = [vr] { return vr->sightseeing_on(); };
    controllers.set_bool = [vr](bool on) { vr->set_sightseeing_on(on, "VR menu"); };
    page.items.push_back(std::move(controllers));
    auto diorama = make(Kind::Toggle, "Diorama (miniature world)",
        "Shows the world as a 10x miniature around you, with the portal on or off. Needs Native Stereo. This launch only.");
    diorama.enabled = [vr] { return !vr->physical_gamepad_passthrough(); };
    diorama.get_bool = [vr] { return vr->is_diorama_enabled(); };
    diorama.set_bool = [vr](bool on) { vr->set_diorama_enabled(on); };
    page.items.push_back(std::move(diorama));
    page.items.push_back(setting_slider("HUD size", "How large the game's HUD and menus appear. More HUD options are on the HUD page.",
        "UI_Size", 0.5f, 5.0f, 0.05f, "%.2f"));
    page.items.push_back(setting_slider("HUD distance", "How far away the HUD sits. Its stereo depth follows this distance.",
        "UI_Distance", 0.5f, 6.0f, 0.1f, "%.1f m"));
    page.items.push_back(record_toggle());
    page.items.push_back(action("Recenter view", "Faces the view forward from where you are now.", [vr] { vr->recenter_view(); }));
    return page;
}

Page camera_page() {
    Page page{"Camera"};
    page.items.push_back(camera_choice());
    switch (camera_mode()) {
    case 1:
        page.items.push_back(paragraph("Fixed third person", true));
        page.items.push_back(setting_slider("Distance", "How far behind your character the camera stays, in game units (centimetres).",
            "WuWaControls_FixedDistance", 0.0f, 1000.0f, 10.0f, "%.0f"));
        page.items.push_back(setting_slider("Height", "How high above your character the camera sits, in game units.",
            "WuWaControls_FixedHeight", -100.0f, 400.0f, 5.0f, "%.0f"));
        break;
    case 2:
        page.items.push_back(paragraph("Freecam", true));
        page.items.push_back(setting_choice("Flying style",
            "Polar fly: simple flying. Hover drone: floats and drifts to a stop. Plane: always moves forward. Acro drone: full manual control.",
            "WuWaControls_FreeStyle", {"Polar fly", "Hover drone", "Plane", "Acro drone"}));
        page.items.push_back(setting_slider("Speed", "How fast the freecam moves.", "WuWaControls_FreeSpeed", 50.0f, 3000.0f, 50.0f, "%.0f"));
        page.items.push_back(setting_slider("Turn speed", "How fast the freecam turns.", "WuWaControls_FreeTurn", 10.0f, 300.0f, 10.0f, "%.0f"));
        page.items.push_back(setting_toggle("Collide with the world",
            "Stops the freecam at walls and the ground. Experimental.", "WuWaControls_FreeCollision"));
        page.items.push_back(action("Leave freecam", "Back to the game camera.", [] {
            if (auto* mode = typed<int32_t>("WuWaControls_CameraMode")) mode->value() = 0;
        }));
        break;
    case 3: {
        page.items.push_back(paragraph("First person", true));
        page.items.push_back(setting_choice("Motion",
            "Comfort keeps your height steady and the horizon level. Animated follows your character's head as it moves. "
            "Full animation also follows its turns (experimental). Custom uses the options in the classic menu.",
            "WuWaControls_FirstMotion", {"Custom", "Comfort", "Animated", "Full animation"}));
        page.items.push_back(setting_choice("Your body",
            "What you see of your own character. Hiding the head stops it from blocking the view.",
            "WuWaControls_MeshMode", {"Show all", "Hide body", "Hide head", "Hide body, keep shadow", "Hide head, full shadow"}));
        auto level = setting_toggle("Level horizon", "Keeps the horizon level in first person. Custom motion only.", "WuWaControls_LevelFirstPerson");
        level.enabled = [] {
            auto* motion = typed<int32_t>("WuWaControls_FirstMotion");
            return typed<bool>("WuWaControls_LevelFirstPerson") != nullptr && motion && motion->value() == 0;
        };
        page.items.push_back(std::move(level));
        break;
    }
    default:
        page.items.push_back(paragraph(
            "Game camera follows WuWa's own camera and its dialogue camera work. Close-up match brings you nearer "
            "in zoomed-in shots, so they frame people like the 2D view does."));
        page.items.push_back(lens_slider());
        page.items.push_back(lens_nearest_slider());
        page.items.push_back(lens_status());
        break;
    }
    page.items.push_back(paragraph("Everyone", true));
    page.items.push_back(setting_slider("World scale",
        "How big the world feels. Above 1 makes you smaller. Diorama uses its own scale and leaves this alone.",
        "VR_WorldScale", 0.5f, 2.0f, 0.025f, "%.3fx"));
    // Hold-RT camera adjustment is off in this menu (the trigger clicks), so the offsets are here.
    page.items.push_back(setting_slider("Forward / back",
        "Moves your viewpoint forward from the game camera. Forward brings dialogue close-ups nearer, closer to how the 2D shot frames them. Applies in normal play too.",
        "VR_CameraForwardOffset", -150.0f, 150.0f, 1.0f, "%+.0f cm"));
    page.items.push_back(setting_slider("Right / left", "Moves your viewpoint sideways from the game camera.",
        "VR_CameraRightOffset", -150.0f, 150.0f, 1.0f, "%+.0f cm"));
    page.items.push_back(setting_slider("Up / down", "Moves your viewpoint up or down from the game camera.",
        "VR_CameraUpOffset", -150.0f, 150.0f, 1.0f, "%+.0f cm"));
    page.items.push_back(action("Reset camera offset", "Sets all three offsets back to 0, the game camera's own position.", [] {
        for (const char* key : {"VR_CameraForwardOffset", "VR_CameraRightOffset", "VR_CameraUpOffset"}) {
            if (auto* v = typed<float>(key)) v->value() = 0.0f;
        }
    }));
    page.items.push_back(action("Recenter view", "Faces the view forward from where you are now.", [] { VR::get()->recenter_view(); }));
    return page;
}

Page cinema_page() {
    Page page{"Cinema"};
    page.items.push_back(view_choice());
    page.items.push_back(setting_choice("Screen lens",
        "Game lens: the stereo screen frames every shot exactly like the 2D view, in 3D. Wide: the fixed 90 degree view used "
        "before test 3f.", "VR_WuWaScreenLens", {"Game lens", "Wide 90 degrees"}));
    page.items.push_back(setting_slider("3D depth",
        "How strong the 3D is on the stereo screen. 1x matches the headset's eye distance to the lens; 0 is flat.",
        "VR_WuWaScreenDepth", 0.0f, 3.0f, 0.1f, "%.1fx"));
    page.items.push_back(paragraph(
        "Black bars in cutscenes? In WuWa's graphics settings, change Cinematic from Cutscene to Fullscreen. "
        "The bars go and cutscenes fill your view."));
    page.items.push_back(lens_slider());
    page.items.push_back(lens_nearest_slider());
    page.items.push_back(lens_subject_choice());
    page.items.push_back(lens_follow_choice());
    page.items.push_back(lens_status());
    page.items.push_back(paragraph("Depth of field and camera effects", true));
    page.items.push_back(setting_toggle("Depth of field",
        "The 2D view's background blur, in full VR and on the stereo screen. In full VR the blur is scaled for the headset's "
        "wider lens and the focus follows Close-up match.", "WuWaStereo_DepthOfField"));
    page.items.push_back(setting_choice("Both eyes get",
        "The game builds one eye from a plain default camera, so that eye misses the shot's camera settings. Blur only copies "
        "the depth of field across; All camera effects also copies grading, vignette, exposure and the rest.",
        "WuWaStereo_EyeEffects", {"Blur only", "All camera effects"}));
    page.items.push_back(setting_choice("Copy from",
        "Auto uses the eye that has the camera's focus. If both eyes stay sharp or look wrong, try First or Second.",
        "WuWaStereo_DepthOfFieldSource", {"First view", "Second view", "Auto"}));
    page.items.push_back(setting_slider("Blur strength, full VR", "Multiplies the blur in full VR. 0.5x suited best in test 3h.",
        "WuWaStereo_DepthOfFieldStrengthFullVR", 0.25f, 4.0f, 0.25f, "%.2fx"));
    page.items.push_back(setting_slider("Blur strength, screens", "Multiplies the blur on the stereo screen and mono theatre. 1x is the 2D view's look.",
        "WuWaStereo_DepthOfFieldStrength", 0.25f, 4.0f, 0.25f, "%.2fx"));
    page.items.push_back(paragraph("Shortcut: hold LT + RT, then click L3 to turn the blur on or off."));
    page.items.push_back(live_text([] {
        auto* on = typed<bool>("WuWaStereo_DepthOfField");
        auto* all = typed<int32_t>("WuWaStereo_EyeEffects");
        return (on && on->value()) || (all && all->value() == 1) ? std::string{"Both eyes: "} + wuwa_dof_sync::status.load() : std::string{};
    }));
    page.items.push_back(setting_toggle("Cinematic scene note",
        "When a letterboxed cinematic camera starts in full VR, a short note in view reminds you of the screen shortcut. "
        "A fallback for when automatic switching misses a scene.", "VR_CinemaHint"));
    page.items.push_back(setting_toggle("Automatic cinematic screen",
        "Switches to a screen during videos and story scenes it can detect, then back to full VR. Experimental.", "VR_AutoCinema"));
    auto story = setting_choice("Story scenes use", "What automatic switching shows during story scenes.",
        "VR_AutoStoryPresentation", {"Full VR", "Stereo screen", "Mono theatre"});
    story.enabled = [] {
        auto* automatic = typed<bool>("VR_AutoCinema");
        return automatic && automatic->value() && typed<int32_t>("VR_AutoStoryPresentation") != nullptr;
    };
    page.items.push_back(std::move(story));
    page.items.push_back(setting_toggle("Match cinematic framing",
        "Gives both eyes the same framing during letterboxed scenes. Leave on unless a scene looks cropped.", "VR_CinematicFramingFix"));
    return page;
}

Page hud_page() {
    Page page{"HUD"};
    page.items.push_back(setting_slider("HUD size", "How large the game's HUD and menus appear.", "UI_Size", 0.5f, 5.0f, 0.05f, "%.2f"));
    page.items.push_back(setting_slider("HUD distance", "How far away the HUD sits. Its stereo depth follows this distance.",
        "UI_Distance", 0.5f, 6.0f, 0.1f, "%.1f m"));
    page.items.push_back(setting_slider("HUD shape (full VR)",
        "In full VR the game lays out its HUD for a 4:3 screen, which then looks squashed; 1.33 undoes that. Raise it if round "
        "icons still look like flat ovals. The stereo screen and mono theatre always keep the game's own shape.",
        "UI_WuWaHudShape", 0.5f, 2.0f, 0.01f, "%.2f"));
    page.items.push_back(action("Reset HUD shape", "Back to 1.33, the full VR default.", [] {
        if (auto* v = typed<float>("UI_WuWaHudShape")) v->value() = 4.0f / 3.0f;
    }));
    page.items.push_back(setting_slider("Up / down", "Moves the HUD up or down.", "UI_Y_Offset", -1.5f, 1.5f, 0.05f, "%+.2f m"));
    page.items.push_back(setting_slider("Left / right", "Moves the HUD sideways.", "UI_X_Offset", -1.5f, 1.5f, 0.05f, "%+.2f m"));
    page.items.push_back(setting_toggle("HUD follows your head",
        "On: the HUD stays in front of your eyes. Off: it stays put in the world and you look at it.", "UI_FollowView"));
    page.items.push_back(action("Refresh HUD layout",
        "Asks the game to lay out its HUD again. Try it if the HUD looks stretched or cut off after switching views.", [] {
            if (auto* request = cached("WuWaControls_ResetHudAspect")) request->set("true");
        }));
    page.items.push_back(live_text([] {
        auto* status = cached("WuWaControls_HudAspectStatus");
        const auto text = status ? status->get() : std::string{};
        return text.empty() || text == "Ready" ? std::string{} : "HUD refresh: " + text;
    }));
    page.items.push_back(setting_toggle("Shortcut sheet",
        "The controller shortcut card below you while playing. A loaded test plan shows its step there instead.",
        "WuWaControls_ShowShortcutSheet"));
    return page;
}

Page comfort_page() {
    Page page{"Comfort"};
    page.items.push_back(setting_toggle("Decoupled pitch",
        "Ignores the game camera's up / down tilt, so only your head looks up and down. Steadier, and worth trying in "
        "gravity-flip areas such as Avinoleum.", "VR_DecoupledPitch"));
    auto hud = setting_toggle("HUD follows decoupled pitch", "Keeps the HUD where you expect it while decoupled pitch is on.",
        "VR_DecoupledPitchUIAdjust");
    hud.enabled = [] {
        auto* pitch = typed<bool>("VR_DecoupledPitch");
        return pitch && pitch->value() && typed<bool>("VR_DecoupledPitchUIAdjust") != nullptr;
    };
    page.items.push_back(std::move(hud));
    page.items.push_back(setting_toggle("Smooth camera turns",
        "Smooths the game camera's left / right turns, so quick camera swings feel gentler.", "VR_LerpCameraYaw"));
    page.items.push_back(setting_toggle("Smooth camera tilt", "Smooths the game camera's up / down tilt.", "VR_LerpCameraPitch"));
    page.items.push_back(setting_slider("Smoothing speed", "Higher follows the game camera more closely.",
        "VR_LerpCameraSpeed", 0.1f, 10.0f, 0.1f, "%.1f"));
    page.items.push_back(setting_toggle("Walk by default", "Walk instead of run; hold RB for normal speed.", "WuWaControls_PolarWalk"));
    page.items.push_back(setting_toggle("L3 + A resets position too",
        "Recentering with L3 + A also moves your seated position back to the middle.", "WuWaControls_RecenterPosition"));
    return page;
}

Page fixes_page() {
    Page page{"Fixes"};
    page.items.push_back(setting_toggle("Match far detail between eyes",
        "Far trees and props switch detail together in both eyes. Recommended.", "WuWaStereo_SyncEyeLod"));
    page.items.push_back(setting_toggle("Refresh far lighting",
        "Refreshes the far-lighting cache so distant lighting matches between eyes. A limited workaround.", "WuWaStereo_RefillFarLighting"));
    page.items.push_back(setting_toggle("Match cinematic framing",
        "Gives both eyes the same framing during letterboxed scenes.", "VR_CinematicFramingFix"));
    page.items.push_back(paragraph("Smoothness", true));
    page.items.push_back(paragraph(
        "Full VR draws two large eye images every frame, far more pixels than the stereo screen, so busy scenes such as "
        "dialogue run slower there. A lower resolution is smoother and a little softer."));
    page.items.push_back(resolution_choice());
    page.items.push_back(live_text([] {
        auto* vr = VR::get().get();
        char text[160];
        std::snprintf(text, sizeof(text), "Each eye: %u x %u pixels. Now about %.0f fps.", vr->get_hmd_width(),
            vr->get_hmd_height(), ImGui::GetIO().Framerate);
        return std::string{text};
    }));
    return page;
}

Page record_page() {
    Page page{"Record"};
    page.items.push_back(record_toggle());
    page.items.push_back(setting_choice("Hide player IDs",
        "Black boxes over your UID in the corner and the profile ID row in menus. While recording: only while the WuWa VR app "
        "records, so the boxes never cover HUD you need in play. Always: for other recorders such as OBS too. The boxes are "
        "approximate; check a short video before sharing. Other names and chat are not hidden.",
        "WuWaPrivacy_Mode", {"Off", "While recording", "Always"}));
    auto profile = setting_toggle("Also hide the profile ID row", "Covers the ID row on the ESC profile screen too.", "WuWaPrivacy_ProfileID");
    profile.enabled = [] {
        auto* hide = typed<int32_t>("WuWaPrivacy_Mode");
        return hide && hide->value() > 0 && typed<bool>("WuWaPrivacy_ProfileID") != nullptr;
    };
    page.items.push_back(std::move(profile));
    page.items.push_back(live_text([] { return recorder_line(); }));
    page.items.push_back(setting_choice("Frame rate", "Frames per second in the video.", "WuWaRecording_FPS", {"30 fps", "45 fps", "60 fps"}));
    page.items.push_back(setting_choice("Picture size", "Width per eye. Larger is sharper and makes bigger files.",
        "WuWaRecording_Width", {"720 px", "1024 px", "1280 px"}));
    page.items.push_back(setting_toggle("Camera and controller data",
        "Saves the camera and controller movement next to the video, to help find what went wrong.", "WuWaRecording_Telemetry"));
    page.items.push_back(paragraph("Report a problem", true));
    page.items.push_back(paragraph(
        "Pick what went wrong and save. The time, the video position and your settings go into the wuwa-reports "
        "folder of the UEVR profile. Share it together with the video."));
    auto kind = make(Kind::Choice, "What happened", "Left / right picks the kind of problem.");
    kind.get_int = [] { return report_kind; };
    kind.set_int = [](int value) { report_kind = value; };
    kind.choices = {"Just mark the moment", "Black bars", "Eyes don't match", "HUD or menu", "Camera position",
        "Stutter or freeze", "Controls", "Something else"};
    page.items.push_back(kind);
    const auto choices = kind.choices;
    page.items.push_back(action("Save report", "Saves the report now. Recording first makes it easy to find in the video.",
        [choices] { save_report(choices[std::clamp(report_kind, 0, static_cast<int>(choices.size()) - 1)]); }));
    page.items.push_back(live_text([] { return report_status; }));
    return page;
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

    // Free navigation: left / right moves between steps without answering.
    auto navigator = make(Kind::Choice, "Step", "Left / right moves between steps without answering. Your answers stay saved.");
    for (int i = 0; i < steps.count(); ++i) navigator.choices.push_back(std::to_string(i + 1) + " / " + std::to_string(steps.count()));
    if (steps.finished()) navigator.choices.push_back("Done");
    navigator.get_int = [] { return steps.index(); };
    navigator.set_int = [](int value) { steps.go(value, unix_ms(), settings_access()); };
    page.items.push_back(std::move(navigator));

    if (!plan->scene.empty()) page.items.push_back(paragraph("Where: " + plan->scene));
    if (const auto* step = steps.step()) {
        page.items.push_back(paragraph(step->title, true));
        if (!step->action.empty()) page.items.push_back(paragraph("Do: " + step->action));
        if (!step->expect.empty()) page.items.push_back(paragraph("Expect: " + step->expect));
        if (const auto earlier = steps.answer_for(steps.index()); !earlier.empty()) {
            page.items.push_back(paragraph("Your answer: " + earlier + ". Answer again to change it."));
        }
        for (const auto& answer : step->answers) {
            page.items.push_back(action(answer, "Saves this answer with the settings now in use, then shows the next step.",
                [answer] { steps.answer(answer, context(), unix_ms(), settings_access()); }));
        }
        page.items.push_back(action("Skip this step", "Saves \"skipped\" and shows the next step.",
            [] { steps.answer("skipped", context(), unix_ms(), settings_access()); }));
    } else {
        page.items.push_back(paragraph("Plan finished. Your answers are saved for Claude in wuwa-steps / results.jsonl."));
        page.items.push_back(action("Restart plan", "Starts again from step 1.", [] { steps.restart(unix_ms(), settings_access()); }));
    }
    page.items.push_back(action("Mark this moment", "Saves the time and current settings, so the moment is easy to find later.",
        [] { steps.mark(context(), unix_ms()); }));
    page.items.push_back(setting_toggle("Step card below you",
        "Shows the current step where the controls card usually is. Off shows the controls card, as a public release "
        "does; the plan stays loaded here.", "WuWaSteps_ShowCard"));
    page.items.push_back(record_toggle());
    page.items.push_back(live_text([] { return recorder_line(); }));
    page.items.push_back(live_text([] { return steps.error().empty() ? std::string{} : "plan.json was not read: " + steps.error(); }));
    return page;
}

Page menu_page() {
    auto* vr = VR::get().get();
    Page page{"Menu"};
    page.items.push_back(setting_slider("Menu size", "How much of its layer this menu fills. Smaller also reads as further away.",
        "VR_WuWaVrMenuSize", 0.4f, 1.0f, 0.05f, "%.2f"));
    page.items.push_back(setting_slider("Menu distance", "How far away this menu floats. UEVR's classic window uses the same distance.",
        "UI_Framework_Distance", 0.8f, 4.0f, 0.1f, "%.1f m"));
    page.items.push_back(action("Move menu",
        "Point with a controller and pull the trigger to put it down. Or use a stick, the D-pad, the arrow keys or WASD, and "
        "the right stick or Page Up / Down for distance; A, Enter or B puts it down. Gamepad shortcut: Y. Keyboard: M. "
        "Mouse: drag with the right button at any time.", [] { start_moving_menu(); }));
    page.items.push_back(menu_place_choice());
    page.items.push_back(menu_angle_slider("Menu left / right", "Degrees to the right of straight ahead (left is negative).", false));
    page.items.push_back(menu_angle_slider("Menu up / down", "Degrees above straight ahead (below is negative).", true));
    page.items.push_back(live_text([vr] { return vr->menu_pointer_status(); }));
    page.items.push_back(paragraph(
        "VR controllers: point and pull the trigger, or use either stick and A. B closes, the grips change page. "
        "Xbox: D-pad or stick, A, B, LB / RB. Keyboard: arrows, Enter, Esc, Tab. Mouse: click and scroll."));
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

// Pages follow the plan and the camera mode. After a rebuild the same page,
// and the same row when it still exists, stay selected.
std::string layout_key() {
    return (steps.loaded() ? "plan " + std::to_string(steps.revision()) : std::string{"no plan"}) + ", camera " +
        std::to_string(camera_mode());
}

void refresh_pages() {
    const auto key = layout_key();
    if (!pages.empty() && key == built_key) return;
    const bool want_test = steps.loaded();
    std::string page_title{"Quick"}, focus_label;
    if (!pages.empty()) {
        const auto& old = pages[std::clamp(state.page, 0, static_cast<int>(pages.size()) - 1)];
        page_title = old.title;
        if (state.focus >= 0 && state.focus < static_cast<int>(old.items.size())) focus_label = old.items[state.focus].label;
    }
    pages.clear();
    pages.push_back(quick_page());
    if (want_test) pages.push_back(test_page());
    pages.push_back(camera_page());
    pages.push_back(cinema_page());
    pages.push_back(hud_page());
    pages.push_back(comfort_page());
    pages.push_back(fixes_page());
    pages.push_back(record_page());
    pages.push_back(menu_page());
    int index = 0;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
        if (pages[i].title == page_title) index = i;
    }
    if (want_test && !built_with_test) index = 1; // a new plan opens on its Test page
    const auto scroll = state.scroll;
    set_page(pages, state, index);
    if (!focus_label.empty() && pages[index].title == page_title) {
        const auto& items = pages[index].items;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i].label == focus_label && focusable(items[i])) {
                state.focus = i;
                state.scroll = scroll;
                break;
            }
        }
    }
    built_with_test = want_test;
    built_key = key;
}

// Soft-edged across its width, so the thin beam quad has no hard sides.
// Premultiplied, like everything ImGui draws into the cleared UI image.
void draw_beam_swatch(ImDrawList* list) {
    if (!list) return;
    const float w = static_cast<float>(beam_swatch_width), h = static_cast<float>(beam_swatch_rows);
    const ImU32 edge = IM_COL32(255, 236, 190, 0), core = IM_COL32(255, 236, 190, 235);
    list->AddRectFilledMultiColor(ImVec2{0.0f, 0.0f}, ImVec2{w * 0.5f, h}, edge, core, core, edge);
    list->AddRectFilledMultiColor(ImVec2{w * 0.5f, 0.0f}, ImVec2{w, h}, core, edge, edge, core);
}

Moves read_moves() {
    const auto& io = ImGui::GetIO();
    const auto down = [](ImGuiKey key) { return ImGui::IsKeyDown(key); };
    const auto analog = [](ImGuiKey key) { return ImGui::GetKeyData(key)->AnalogValue; };
    const auto once = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    // Either stick steers; whichever is pushed further counts, one direction at a time.
    float x = analog(ImGuiKey_GamepadLStickRight) - analog(ImGuiKey_GamepadLStickLeft);
    float y = analog(ImGuiKey_GamepadLStickUp) - analog(ImGuiKey_GamepadLStickDown);
    const auto consider = [&](float cx, float cy) {
        if (cx * cx + cy * cy > x * x + y * y) { x = cx; y = cy; }
    };
    consider(analog(ImGuiKey_GamepadRStickRight) - analog(ImGuiKey_GamepadRStickLeft),
        analog(ImGuiKey_GamepadRStickUp) - analog(ImGuiKey_GamepadRStickDown));
    VR::MenuPad pad{};
    const bool direct = VR::get()->menu_controller_pad(pad);
    if (direct) {
        consider(pad.lx, pad.ly);
        consider(pad.rx, pad.ry);
    }
    const auto dir = stick.read(x, y);
    const auto press = [](bool now, bool& was) { const bool pressed = now && !was; was = now; return pressed; };
    const bool pad_accept = press(direct && pad.accept, pad_was.accept);
    const bool pad_back = press(direct && pad.back, pad_was.back);
    const bool pad_prev = press(direct && pad.prev_page, pad_was.prev_page);
    const bool pad_next = press(direct && pad.next_page, pad_was.next_page);
    Held held;
    held.up = down(ImGuiKey_GamepadDpadUp) || down(ImGuiKey_UpArrow) || dir == Dir::Up;
    held.down = down(ImGuiKey_GamepadDpadDown) || down(ImGuiKey_DownArrow) || dir == Dir::Down;
    held.left = down(ImGuiKey_GamepadDpadLeft) || down(ImGuiKey_LeftArrow) || dir == Dir::Left;
    held.right = down(ImGuiKey_GamepadDpadRight) || down(ImGuiKey_RightArrow) || dir == Dir::Right;
    const bool accept = once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) ||
        once(ImGuiKey_Space) || pad_accept;
    const bool back = once(ImGuiKey_GamepadFaceRight) || once(ImGuiKey_Escape) || pad_back;
    const bool prev_page = once(ImGuiKey_GamepadL1) || (once(ImGuiKey_Tab) && io.KeyShift) || pad_prev;
    const bool next_page = once(ImGuiKey_GamepadR1) || (once(ImGuiKey_Tab) && !io.KeyShift) || pad_next;
    if (once(ImGuiKey_GamepadFaceUp) || once(ImGuiKey_M)) start_moving_menu();
    // A gamepad or the keyboard ends the laser at once (otherwise it waits for a
    // controller that was put down or switched off to go quiet).
    const bool used = held.up || held.down || held.left || held.right || accept || back || prev_page || next_page;
    if (used && !pad_accept && !pad_back && !pad_prev && !pad_next && VR::get()->menu_pointer_active()) {
        VR::get()->menu_pointer_yield();
    }
    return mover.read(held, accept, back, prev_page, next_page, seconds());
}

// Move menu: sticks, D-pad, arrow keys or WASD turn the menu around you (a
// controller's laser aim does it in the overlay instead); the right stick, a VR
// thumbstick or Page Up / Down changes the distance. A, B, Y, Enter, Escape,
// Space, M, a controller's A / B or a click puts it down.
void move_menu() {
    const auto analog = [](ImGuiKey key) { return ImGui::GetKeyData(key)->AnalogValue; };
    const auto down = [](ImGuiKey key) { return ImGui::IsKeyDown(key); };
    const auto once = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, false); };
    float x = analog(ImGuiKey_GamepadLStickRight) - analog(ImGuiKey_GamepadLStickLeft);
    float y = analog(ImGuiKey_GamepadLStickUp) - analog(ImGuiKey_GamepadLStickDown);
    float z = analog(ImGuiKey_GamepadRStickUp) - analog(ImGuiKey_GamepadRStickDown);
    if (down(ImGuiKey_GamepadDpadRight) || down(ImGuiKey_RightArrow) || down(ImGuiKey_D)) x += 1.0f;
    if (down(ImGuiKey_GamepadDpadLeft) || down(ImGuiKey_LeftArrow) || down(ImGuiKey_A)) x -= 1.0f;
    if (down(ImGuiKey_GamepadDpadUp) || down(ImGuiKey_UpArrow) || down(ImGuiKey_W)) y += 1.0f;
    if (down(ImGuiKey_GamepadDpadDown) || down(ImGuiKey_DownArrow) || down(ImGuiKey_S)) y -= 1.0f;
    if (down(ImGuiKey_PageUp)) z += 1.0f;
    if (down(ImGuiKey_PageDown)) z -= 1.0f;
    VR::MenuPad pad{};
    const bool direct = VR::get()->menu_controller_pad(pad);
    if (direct) { x += pad.lx; y += pad.ly; z += pad.ry; }
    // The VR thumbsticks set the distance even while the game (VR controllers
    // mode) or UEVR's motion controls own them; the stronger push counts once.
    glm::vec2 left{}, right{};
    if (!direct && VR::get()->menu_sticks(left, right)) {
        const float stick = std::abs(right.y) >= std::abs(left.y) ? right.y : left.y;
        if (std::abs(stick) > std::abs(z)) z = stick;
    }
    const auto press = [](bool now, bool& was) { const bool pressed = now && !was; was = now; return pressed; };
    const bool pad_done = press(direct && pad.accept, move_was.accept) | press(direct && pad.back, move_was.back);
    const double now = seconds();
    const float dt = move_last > 0.0 ? static_cast<float>(std::clamp(now - move_last, 0.0, 0.1)) : 0.0f;
    move_last = now;
    const auto dead = [](float v) { return std::abs(v) < 0.2f ? 0.0f : std::clamp(v, -1.0f, 1.0f); };
    if (!VR::get()->menu_pointer_active()) {
        if (auto* yaw = typed<float>(menu_yaw_key())) yaw->value() = std::clamp(yaw->value() + dead(x) * 60.0f * dt, -150.0f, 150.0f);
        if (auto* pitch = typed<float>(menu_pitch_key())) pitch->value() = std::clamp(pitch->value() + dead(y) * 40.0f * dt, -60.0f, 60.0f);
    }
    if (auto* distance = typed<float>("UI_Framework_Distance")) distance->value() = std::clamp(distance->value() + dead(z) * 0.8f * dt, 0.8f, 4.0f);
    if (once(ImGuiKey_GamepadFaceDown) || once(ImGuiKey_GamepadFaceRight) || once(ImGuiKey_GamepadFaceUp) ||
        once(ImGuiKey_Enter) || once(ImGuiKey_KeypadEnter) || once(ImGuiKey_Escape) || once(ImGuiKey_Space) ||
        once(ImGuiKey_M) || pad_done) {
        moving_menu.store(false, std::memory_order_relaxed);
    }
}
}

void tick() {
    if (!wuwa_test::is_wuwa()) return;
    steps.poll(Framework::get_persistent_dir() / "wuwa-steps", unix_ms(), settings_access());
    update_recording();
}

// A finished plan, or the card switched off, gives the place back to the controls card.
bool overlay_wanted() {
    if (!wuwa_test::is_wuwa() || !steps.loaded() || steps.finished()) return false;
    auto* card = typed<bool>("WuWaSteps_ShowCard");
    return !card || card->value();
}

bool showing() {
    return GetTickCount64() < showing_until.load(std::memory_order_relaxed);
}

bool moving() {
    return moving_menu.load(std::memory_order_relaxed) && showing();
}

float height_share(float aspect) {
    const float fraction = std::clamp(VR::get()->vr_menu_size(), 0.4f, 1.0f);
    return (std::min)(aspect * 780.0f / 1200.0f, 1.0f) * fraction;
}

bool draw(const ImVec2& target_size) {
    if (!wuwa_test::is_wuwa()) return false;
    tick();
    if (classic) return false;
    auto* enabled = typed<bool>("VR_WuWaVrMenu");
    if (enabled && !enabled->value()) return false;
    showing_until.store(GetTickCount64() + 250, std::memory_order_relaxed);
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
    Look look{1.0f, wuwa_l10n::sheet_font()};
    if (rec_since >= 0.0) look.badge = "REC " + rec_clock();
    // Move menu: a click (the laser's trigger or the mouse) puts the menu down
    // and is kept from the rows under it until the button is released.
    auto& io = ImGui::GetIO();
    if (moving_menu.load(std::memory_order_relaxed) && io.MouseClicked[0]) {
        moving_menu.store(false, std::memory_order_relaxed);
        swallow_click = true;
    }
    if (swallow_click) {
        if (!io.MouseDown[0]) swallow_click = false;
        io.MouseDown[0] = false; io.MouseClicked[0] = false; io.MouseReleased[0] = false;
    }
    // The right mouse button drags the menu around you at any time.
    if (!moving_menu.load(std::memory_order_relaxed) && io.MouseDown[1] && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        leave_screen_low();
        if (auto* yaw = typed<float>(menu_yaw_key())) yaw->value() = std::clamp(yaw->value() + io.MouseDelta.x * 0.1f, -150.0f, 150.0f);
        if (auto* pitch = typed<float>(menu_pitch_key())) pitch->value() = std::clamp(pitch->value() - io.MouseDelta.y * 0.1f, -60.0f, 60.0f);
    }
    Moves moves{};
    if (moving_menu.load(std::memory_order_relaxed)) move_menu(); else moves = read_moves();
    wuwa_menu::draw(pages, state, moves, pos, size, look);
    if (moving_menu.load(std::memory_order_relaxed)) {
        const char* text = VR::get()->menu_pointer_active() ?
            "Moving the menu: point where you want it, thumbstick up / down for distance, then pull the trigger." :
            "Moving the menu: stick, D-pad or arrow keys. Right stick or Page Up / Down: distance. A or Enter: put it down.";
        auto* list = ImGui::GetForegroundDrawList();
        ImFont* font = wuwa_l10n::sheet_font() ? wuwa_l10n::sheet_font() : ImGui::GetFont();
        const float font_size = size.y * 0.04f;
        const ImVec2 text_size = font->CalcTextSizeA(font_size, size.x, 0.0f, text);
        const ImVec2 at{pos.x + (size.x - text_size.x) * 0.5f, pos.y - text_size.y - font_size * 0.6f};
        list->AddRectFilled(ImVec2{at.x - font_size * 0.5f, at.y - font_size * 0.3f},
            ImVec2{at.x + text_size.x + font_size * 0.5f, at.y + text_size.y + font_size * 0.3f}, IM_COL32(20, 18, 14, 230), font_size * 0.4f);
        list->AddText(font, font_size, at, IM_COL32(255, 236, 190, 255), text);
    }
    ImGui::End();
    draw_beam_swatch(ImGui::GetForegroundDrawList());
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
    pad_was = {};
    stick.held = Dir::None;
    moving_menu.store(false, std::memory_order_relaxed);
    swallow_click = false;
    showing_until.store(0, std::memory_order_relaxed);
}

void draw_step_overlay(ImDrawList* list, const ImVec2& size) {
    const auto* plan = steps.plan();
    if (!plan || !list) return;
    ImFont* font = wuwa_l10n::sheet_font() ? wuwa_l10n::sheet_font() : ImGui::GetFont();
    wuwa_menu::draw_step_card(list, size, font, *plan, steps.index(), rec_since >= 0.0 ? rec_clock() : std::string{}, steps.error());
}

bool hint_active() {
    if (!wuwa_test::is_wuwa()) return false;
    auto* vr = VR::get().get();
    const auto now = GetTickCount64();
    const auto seen = wuwa_cinematic_framing::constrained_ms.load(std::memory_order_relaxed);
    const bool cinematic = seen != 0 && now >= seen && now - seen < 500;
    const bool eligible = vr->is_cinema_hint_enabled() && vr->is_hmd_active() && !g_framework->is_drawing_ui() &&
        !vr->is_using_2d_screen() && !vr->is_using_mono_theatre();
    std::scoped_lock lock{hint_mutex};
    return hint.update(cinematic, eligible, now);
}

void draw_hint(ImDrawList* list, const ImVec2& size) {
    ImFont* font = wuwa_l10n::sheet_font() ? wuwa_l10n::sheet_font() : ImGui::GetFont();
    wuwa_menu::draw_note(list, size, font, "Cinematic scene",
        "Looks wrong? Hold LT + RT, then click R3 for the next view (stereo screen, then mono).",
        "Turn this note off in the menu, Cinema page.");
}
}
