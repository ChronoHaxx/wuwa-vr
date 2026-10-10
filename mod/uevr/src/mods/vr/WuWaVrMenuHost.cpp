#include "WuWaVrMenuHost.hpp"

// Before windows.h (via Framework.hpp), whose min / max macros break std::min in it.
#include "utility/WuWaMonoNativeProjection.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
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
             "WuWaControls_CameraMode", "UI_Size", "UI_Distance", "UI_WuWaHudShape"}) {
        if (auto* value = cached(key)) settings[key] = value->get();
    }
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

// Close-up match lives in the camera script; the status is its live report.
Item lens_slider() {
    return setting_slider("Close-up match",
        "Dialogue shots use a zoom lens your headset's wide view can't show, so people look small and far. This moves you nearer, "
        "per shot, so they keep this share of their 2D size. 0% is the game camera's own spot; 100% matches the 2D shot. "
        "Game camera in full VR only.",
        "WuWaControls_LensMatch", 0.0f, 100.0f, 5.0f, "%.0f%%");
}

Item lens_status() {
    return live_text([] {
        auto* status = cached("WuWaControls_LensStatus");
        const auto text = status ? status->get() : std::string{};
        return text.empty() ? std::string{} : "Now: " + text;
    });
}

Item view_choice() {
    auto item = make(Kind::Choice, "View",
        "Full VR: the game all around you. Stereo screen: a big 3D screen, for menus and scenes that look wrong in full VR. "
        "Mono theatre: a flat screen, the same picture in both eyes. Shortcut: hold LT + RT, then hold L3 (screen) or click R3 (mono).");
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
        "Records the headset view through the WuWa VR app, which must be open. Player IDs are hidden while the option below is on.");
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
    page.items.push_back(paragraph(
        "Black bars in cutscenes? In WuWa's graphics settings, change Cinematic from Cutscene to Fullscreen. "
        "The bars go and cutscenes fill your view."));
    page.items.push_back(lens_slider());
    page.items.push_back(lens_status());
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
    page.items.push_back(setting_slider("HUD shape",
        "Stretches the HUD taller (above 1) or flatter. If round icons look like flat ovals, raise this until they look round.",
        "UI_WuWaHudShape", 0.5f, 2.0f, 0.01f, "%.2f"));
    page.items.push_back(action("Reset HUD shape", "Back to the shape of the game's HUD image.", [] {
        if (auto* v = typed<float>("UI_WuWaHudShape")) v->value() = 1.0f;
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
    page.items.push_back(action("Restart VR runtime",
        "Reconnects to the VR runtime. Try it if stutter stays after switching view modes, or the SteamVR dashboard will not open. "
        "The view goes dark for a moment.", [] {
            if (auto* runtime = VR::get()->get_runtime()) runtime->wants_reinitialize = true;
        }));
    return page;
}

Page record_page() {
    Page page{"Record"};
    page.items.push_back(record_toggle());
    page.items.push_back(setting_toggle("Hide player IDs",
        "Covers your UID in the corner, and the profile ID row in menus, with black boxes, in the headset and in videos. "
        "Check a short video before sharing: other names and chat are not hidden.", "WuWaPrivacy_HideIDs"));
    auto profile = setting_toggle("Also hide the profile ID row", "Covers the ID row on the ESC profile screen too.", "WuWaPrivacy_ProfileID");
    profile.enabled = [] {
        auto* hide = typed<bool>("WuWaPrivacy_HideIDs");
        return hide && hide->value() && typed<bool>("WuWaPrivacy_ProfileID") != nullptr;
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
    return mover.read(held, accept, back, prev_page, next_page, seconds());
}
}

void tick() {
    if (!wuwa_test::is_wuwa()) return;
    steps.poll(Framework::get_persistent_dir() / "wuwa-steps", unix_ms(), settings_access());
    update_recording();
}

bool overlay_wanted() {
    return wuwa_test::is_wuwa() && steps.loaded();
}

bool showing() {
    return GetTickCount64() < showing_until.load(std::memory_order_relaxed);
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
    wuwa_menu::draw(pages, state, read_moves(), pos, size, look);
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
    pad_was = {};
    stick.held = Dir::None;
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
        "Looks wrong? Hold LT + RT, then hold L3 for a stereo screen (R3: mono).",
        "Turn this note off in the menu, Cinema page.");
}
}
