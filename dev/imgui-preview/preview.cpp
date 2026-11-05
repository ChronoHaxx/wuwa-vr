// Offscreen preview of the mod's in-VR ImGui panels with the real fonts, catalogs and
// artwork embedded in a built UEVRBackend.dll. Needs no game, VR runtime or window.
//
//   preview.exe <UEVRBackend.dll> <output folder> [language] [width height]
//
// Writes one PNG per scene. Rendering uses the same ImGui configuration and DX11
// backend as the mod; only the scene setup below is preview-specific.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>
#include "imgui_impl_dx11.h"
#include "font_robotomedium.hpp"
#include "utility/WuWaLocalization.hpp"
#include "utility/WuWaShortcutSheet.hpp"
#include "utility/WuWaKeyboard.hpp"
#include "utility/WuWaStepPlan.hpp"
#include "utility/WuWaVrMenu.hpp"
#include "utility/WuWaBrand.hpp"
#include "utility/WuWaPad3D.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace {
bool save_png(const fs::path& path, const std::vector<unsigned char>& rgba, UINT width, UINT height) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height))) return false;
    // The PNG encoder takes BGRA.
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return false;
    auto bgra = rgba;
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data()))) return false;
    return SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

struct Scene {
    std::string name;
    std::function<void(ImVec2)> draw;
    ImVec2 mouse{-FLT_MAX, -FLT_MAX};
    int frames{1};      // more than one: name_000.png... at `fps`, for the animated pieces
    float start{1.6f};  // seconds on the scene clock at the first frame
};
// The scene clock (seconds), for animated drawing.
double scene_time = 1.6;
const float fps = 30.0f;

// As wuwa_ui::format (utility/WuWaLocalizedUI.hpp, which needs the backend): an English printf format, translated and filled.
template <typename... Args>
std::string format_text(const char* english, Args... args) {
    char text[256]{};
    std::snprintf(text, sizeof(text), wuwa_l10n::translate(english).c_str(), args...);
    return text;
}

// Sample values only: the menu model is drawn exactly as in the game, with the game's pages, rows, More
// settings and shortcuts (mods/vr/WuWaVrMenuHost.cpp, 1.2.7 lean menu), over these values.
struct Sample {
    bool controllers{true}, diorama{false}, recording{true}, ui{true}, portal{false}, dof{false}, automatic{true}, follow{false},
        cards{true}, sound{true}, pitch{false}, smooth_yaw{false}, smooth_pitch{false}, walk{false}, ambient{true}, bars{true},
        ground{true}, lod{true}, nudge{true}, hide_near{false};
    int view{0}, camera{0}, story{0}, lens_kind{0}, style{1}, privacy{1}, mic{1}, kind{0}, resolution{4}, language{0}, sharing{0}, slot{0},
        vr_key{1};
    float hud_size{1.1f}, hud_distance{2.0f}, depth{1.0f}, lens{75.0f}, scale{1.0f};
};
Sample sample;
std::vector<std::string> sample_more_open;

std::vector<wuwa_menu::Page> sample_pages() {
    using namespace wuwa_menu;
    namespace keys = wuwa_pad3d;
    namespace btn = wuwa_pad3d::button;
    const auto pad_and_key = [](keys::Beats pad, keys::Beats keyboard) {
        keys::Shortcut shortcut;
        shortcut.pad = std::move(pad);
        shortcut.keyboard = std::move(keyboard);
        return shortcut;
    };
    const auto l3_and_key = [&](std::uint32_t control, const char* key) { return pad_and_key(keys::l3_then(control), keys::vr_key_then(key)); };
    const auto with = [](Item item, keys::Shortcut shortcut, std::string label = {}) {
        item.shortcuts.push_back({std::move(label), std::move(shortcut)});
        return item;
    };
    const auto toggle = [](const char* label, const char* help, bool* value, bool fallback = false) {
        Item item = make(Kind::Toggle, label, help);
        item.get_bool = [value] { return *value; };
        item.set_bool = [value](bool v) { *value = v; };
        item.default_text = [fallback] { return std::string{fallback ? "On" : "Off"}; };
        item.is_default = [value, fallback] { return *value == fallback; };
        return item;
    };
    const auto choice = [](const char* label, const char* help, int* value, std::vector<std::string> choices) {
        Item item = make(Kind::Choice, label, help);
        item.get_int = [value] { return *value; };
        item.set_int = [value](int v) { *value = v; };
        item.default_text = [first = choices[0]] { return first; };
        item.is_default = [value] { return *value == 0; };
        item.choices = std::move(choices);
        return item;
    };
    const auto slider = [](const char* label, const char* help, float* value, float min, float max, float step, const char* format) {
        Item item = make(Kind::Slider, label, help);
        item.get_float = [value] { return *value; };
        item.set_float = [value](float v) { *value = v; };
        item.min = min; item.max = max; item.step = step; item.format = format;
        return item;
    };
    const auto action = [](const char* label, const char* help, const char* value = nullptr) {
        Item item = make(Kind::Action, label, help);
        if (value) item.value_text = [value] { return std::string(value); };
        item.run = [] {};
        return item;
    };
    const auto session = [](Item item) { item.session_only = true; return item; };
    const auto add_more = [](Page& page, std::vector<Item> rows) {
        const std::string title = page.title;
        const auto open = [title] { return std::find(sample_more_open.begin(), sample_more_open.end(), title) != sample_more_open.end(); };
        int count = 0;
        for (auto& row : rows) { if (row.kind != Kind::Text) ++count; row.visible = open; }
        Item more = make(Kind::Action, "More settings", "The rest of this page's settings, for edge cases. They stay open until the menu closes.");
        more.visible = [open] { return !open(); };
        more.run = [title] { sample_more_open.push_back(title); };
        more.value_text = [count] { return wuwa_l10n::visual(format_text("%d more", count)); };
        page.items.push_back(std::move(more));
        for (auto& row : rows) page.items.push_back(std::move(row));
    };

    auto view = with(with(choice("View", "Full VR: the game all around you. Stereo screen: a big 3D screen, for menus and scenes that look "
        "wrong in full VR. Mono theatre: a flat screen, the same picture in both eyes.", &sample.view, {"Full VR", "Stereo screen", "Mono theatre"}),
        pad_and_key(keys::triggers_then(keys::press(btn::R3)), keys::vr_key_then("1")), "Full VR / stereo screen"),
        pad_and_key(keys::triggers_then(keys::long_press(btn::R3, 0.8f)), keys::vr_key_then("2")), "Mono theatre");
    view.default_text = nullptr;
    auto camera = choice("Camera", "Game camera: WuWa's own camera. Fixed third person: a steady camera behind you. Freecam: fly the camera anywhere, "
        "for cutscenes and photos. First person: through your character's eyes. Options for each are on the Camera page.", &sample.camera,
        {"Game camera", "Fixed third person", "Freecam", "First person"});
    camera = with(with(with(camera, l3_and_key(btn::View, "F"), "First person"), l3_and_key(btn::RB, "T"), "Fixed third person"),
        pad_and_key({keys::press(btn::R3, 2)}, keys::vr_key_then("C")), "Freecam");
    auto game_ui = with(toggle("Show game UI", "The game's HUD and menus. Off hides them all, menus too, for clean pictures and videos. "
        "Shortcut: L3 + B, or hold the VR key (` by default) and press H.", &sample.ui, true), l3_and_key(btn::B, "H"));
    auto portal = with(toggle("Portal window", "Shows the game through a window in front of you instead of all around you. Shortcut: "
        "L3 + LT, F7, or the VR key + 3.", &sample.portal), l3_and_key(btn::LT, "3"));
    auto diorama = session(with(toggle("Diorama (miniature world)", "Shows the world as a miniature around you, with the portal on or off. "
        "Needs Native Stereo. This launch only.", &sample.diorama), l3_and_key(btn::RT, "4")));
    keys::Shortcut hold_menu;
    hold_menu.vr = {keys::long_press(btn::Menu, 1.0f)};
    hold_menu.vr_only = true;
    auto controllers = session(with(toggle("VR controllers", "Use the VR controllers as an Xbox pad for walking and sightseeing. Holding the "
        "left Menu button for 1 second also turns them on or off.", &sample.controllers), hold_menu));
    controllers.status = [] { return wuwa_l10n::text("VR controllers: on (Quest Pro, Touch Plus)"); };
    auto record = session(toggle("Record video", "Records the headset view through the WuWa VR app, which must be open. Player IDs are covered "
        "while it records (Hide player IDs, on the Record page).", &sample.recording));
    record.value_text = [] { return std::string{"REC 12:34"}; };
    record.status = [] { return wuwa_l10n::text("Recorder: recording. Reports and marks note the time in the video."); };
    auto recenter = with(action("Recenter view", "Faces the view forward from where you are now, as L3 + A does: the portal too, and your seated "
        "position when L3 + A resets position too is on (Comfort page)."), l3_and_key(btn::A, "R"));
    const auto tile = [](Item item, const char* label) { item.label = label; return item; };

    std::vector<Page> pages;
    {
        Page home{"Home"};
        home.tiles = true;
        home.items = {view, camera, tile(game_ui, "Game UI"), portal, tile(diorama, "Diorama"), controllers, record, tile(recenter, "Recenter")};
        pages.push_back(std::move(home));
    }
    {
        Page page{"View"};
        auto blur = with(toggle("Background blur", "The 2D view's background blur, in full VR and on the stereo screen. In full VR the blur is "
            "scaled for the headset's wider lens and the focus follows Close-up match.", &sample.dof, true),
            pad_and_key(keys::triggers_then(keys::press(btn::L3)), keys::vr_key_then("B")));
        page.items = {view, blur,
            toggle("Automatic cinematic screen", "Switches to a screen while a story video (mono theatre) or a story picture plays, then back to "
                "the view you had. Story scenes keep your view unless you choose otherwise below.", &sample.automatic),
            choice("Story scenes use", "What automatic switching shows during story scenes and conversations. Keep current view leaves full VR, "
                "the stereo screen or mono as you have it.", &sample.story, {"Keep current view", "Stereo screen", "Mono theatre"}),
            choice("Screen lens", "Game lens: the stereo screen frames every shot exactly like the 2D view, in 3D. Wide: the fixed 90 degree "
                "view used in older versions.", &sample.lens_kind, {"Game lens", "Wide 90 degrees"}),
            slider("3D depth", "How strong the 3D is on the stereo screen. 1x matches the headset's eye distance to the lens; 0 is flat.",
                &sample.depth, 0.0f, 3.0f, 0.1f, "%.1fx")};
        page.items[4].help_more = "Black bars in story scenes on the stereo or mono screen? In WuWa's Graphics settings, set Cutscene Mode to "
            "Full Screen. In full VR they are hidden for you (Graphics page).";
        add_more(page, {choice("Story pictures use", "What automatic switching shows while a story picture fills the screen.", &sample.story,
                            {"Keep current view", "Stereo screen", "Mono theatre"}),
            toggle("Cinematic scene note", "When a letterboxed cinematic camera starts in full VR, a short note in view reminds you of the screen "
                "shortcut. A fallback for when automatic switching misses a scene.", &sample.follow),
            choice("Both eyes get", "The game builds one eye from a plain default camera, so that eye misses the shot's camera settings. Blur only "
                "copies the depth of field across; All camera effects also copies grading, vignette, exposure and the rest.", &sample.slot,
                {"Blur only", "All camera effects"}),
            toggle("Match cinematic framing", "Gives both eyes the same framing during letterboxed scenes. Leave on unless a scene looks cropped.",
                &sample.lod, true)});
        pages.push_back(std::move(page));
    }
    {
        Page page{"Camera"};
        auto lens = slider("Close-up match", "Dialogue shots use a zoom lens your headset's wide view can't show, so people look small and far. "
            "This moves you nearer, per shot, so they keep this share of their 2D size. At 0 you stay at the game camera's own spot, 100 matches "
            "the 2D shot and above 100 comes closer still. Game camera in full VR only.", &sample.lens, 0.0f, 150.0f, 5.0f, "%.0f%%");
        lens.status = [] { return wuwa_l10n::visual(format_text("Now: %s", "+42 cm")); };
        auto nudge = with(with(toggle("Story camera nudge", "In story and dialogue shots, hold L3 and press Y to move nearer or X to move back; "
            "L3 + Y + X goes back to the game's spot. Keyboard: Page Up, Page Down and Home. It moves along the game camera's view and keeps "
            "following the shot's own camera moves. Game camera in full VR only.", &sample.nudge, true),
            l3_and_key(btn::Y, "E"), "Nearer"), l3_and_key(btn::X, "Q"), "Back");
        auto hide = with(toggle("Hide characters at your eyes", "In story close-ups, a character right at your eyes but outside the flat shot (VR "
            "shows more around it) is hidden whole while it is there. Anyone the flat shot shows stays. Shortcut: hold LT + RT, then press Y.",
            &sample.hide_near), pad_and_key(keys::triggers_then(keys::press(btn::Y)), keys::vr_key_then("N")));
        page.items = {camera, lens, nudge, hide, slider("World scale", "How big the world feels. Above 1 makes you smaller. Diorama uses its own "
            "scale and leaves this alone.", &sample.scale, 0.5f, 2.0f, 0.025f, "%.3fx"), recenter};
        add_more(page, {slider("Closest distance", "How near Close-up match may bring you to the person in the shot.", &sample.lens, 5.0f, 150.0f,
            5.0f, "%.0f cm")});
        pages.push_back(std::move(page));
    }
    {
        Page page{"Comfort"};
        page.items = {toggle("Decoupled pitch", "Ignores the game camera's up / down tilt, so only your head looks up and down. Steadier, and "
                "worth trying in gravity-flip areas such as Avinoleum.", &sample.pitch),
            toggle("Smooth camera turns", "Smooths the game camera's left / right turns, so quick camera swings feel gentler.", &sample.smooth_yaw),
            toggle("Smooth camera tilt", "Smooths the game camera's up / down tilt.", &sample.smooth_pitch),
            toggle("Walk by default", "Walk instead of run; hold RB for normal speed.", &sample.walk)};
        pages.push_back(std::move(page));
    }
    {
        Page page{"Controls"};
        auto key = choice("VR key", "The key you hold for the shortcuts. The key left of 1 is ` on US and UK keyboards (^ on German, \xC2\xB2 on "
            "French); it never reaches the game. Off turns the keyboard shortcuts off.", &sample.vr_key, {"Off", "Key left of 1", "Right Ctrl"});
        keys::Shortcut card;
        card.keyboard = {keys::key_long_press("VR", 0.5f)};
        key = with(key, card);
        page.items = {controllers, choice("Sharing with Xbox / treadmill", "Both together: buttons combine.", &sample.sharing,
            {"Both together", "Last used wins", "VR only (Xbox ignored)"}), key};
        pages.push_back(std::move(page));
    }
    {
        Page page{"Interface"};
        auto cards = toggle("Controls cards", "Hold L3, or LT + RT, for half a second: a card rises under your view and shows what each button "
            "does, lighting each one as you press it. Let go and it sinks away. On the keyboard, hold the VR key (` by default).", &sample.cards, true);
        keys::Shortcut l3_card;
        l3_card.pad = {keys::long_press(btn::L3, 0.5f)};
        l3_card.keyboard = {keys::key_long_press("VR", 0.5f)};
        keys::Shortcut views_card;
        views_card.pad = {keys::long_press(btn::LT | btn::RT, 0.5f)};
        cards = with(with(cards, l3_card, "Buttons card"), views_card, "Views card");
        page.items = {game_ui, slider("HUD size", "How large the game's HUD and menus appear.", &sample.hud_size, 0.5f, 5.0f, 0.05f, "%.2f"),
            slider("HUD distance", "How far away the HUD sits. Its stereo depth follows this distance.", &sample.hud_distance, 0.5f, 6.0f, 0.1f, "%.1f m"),
            cards, choice("Language", "For the WuWa VR menu, the controls cards and notices. The game's own language is set in the game.",
                &sample.language, {"English"}),
            action("VR setup", "The first-launch questions again: camera, background blur, camera tilt and story scenes. Each answer takes effect at once.")};
        pages.push_back(std::move(page));
    }
    {
        Page page{"Graphics"};
        auto resolution = choice("Render resolution", "Pixels per eye compared with what your headset asks for. Lower is smoother in full VR. "
            "It changes after a moment.", &sample.resolution, {"60%", "70%", "80%", "90%", "100%", "110%", "120%"});
        resolution.help_more = "Full VR draws two large eye images every frame, far more pixels than the stereo screen, so busy scenes such as "
            "dialogue run slower there. A lower resolution is smoother and a little softer.";
        resolution.status = [] { return wuwa_l10n::visual(format_text("Each eye: %u x %u pixels. Now about %.0f fps.", 2688u, 2688u, 72.0)); };
        page.items = {resolution, toggle("Ambient occlusion", "Soft contact shadows in corners and under objects.", &sample.ambient, true),
            toggle("Hide cutscene bars in full VR", "With the game's Cutscene Mode on letterbox, black bars cover the world above and below "
                "story scenes. In full VR they are hidden; the stereo and mono screens keep them. Recommended.", &sample.bars, true)};
        pages.push_back(std::move(page));
    }
    {
        Page page{"Record"};
        page.items = {record, choice("Hide player IDs", "Black boxes over your UID in the corner and the profile ID row in menus.", &sample.privacy,
                {"Off", "While recording", "Always"}),
            choice("Video style", "Side by side: both eyes, the whole view of each.", &sample.style, {"Both eyes, side by side",
                "Showcase: one eye, steady, 16:9", "Left eye, whole view", "Right eye, whole view"}),
            toggle("Record sound", "Everything your PC plays goes into the video: the game, and also voice chat and notifications. SteamVR "
                "recordings only.", &sample.sound, true)};
        pages.push_back(std::move(page));
    }
    return pages;
}

Scene menu_scene(std::string name, int page, int focus, wuwa_menu::Source source, wuwa_pad3d::Device device,
    std::vector<std::string> more_open = {}, ImVec2 mouse = ImVec2{-FLT_MAX, -FLT_MAX}, int frames = 1, float start = 1.6f) {
    return {std::move(name), [page, focus, source, device, more_open](ImVec2 size) {
        static auto pages = sample_pages();
        sample_more_open = more_open;
        wuwa_menu::State state{};
        wuwa_menu::set_page(pages, state, page);
        state.focus = focus;
        state.source = source;
        // The game draws the menu on the UEVR quad at 1200 x 780; the preview centres it in a 1600 x 900 frame.
        const ImVec2 pos{130.0f, 14.5f}, menu{1340.0f, 871.0f};
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("WuWa VR menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
        wuwa_menu::Look look{1.0f, wuwa_l10n::sheet_font()};
        look.badge = "REC 12:34";
        look.device = device;
        look.time = scene_time;
        wuwa_menu::draw(pages, state, wuwa_menu::Moves{}, pos, menu, look);
        ImGui::End();
    }, mouse, frames, start};
}
}

// The VR setup (WuWaVrMenuHost's setup_questions, over sample values).
wuwa_menu::Setup sample_setup() {
    using wuwa_menu::SetupQuestion;
    static int camera = 0, blur = 1, tilt = 0, story = 0;
    wuwa_menu::Setup setup;
    SetupQuestion q1{"Camera", "How you see the world. You can change it any time on the Home page."};
    q1.choices = {{"Game camera", "WuWa's own camera, all around you in 3D. Best for most play.", true},
        {"First person", "Through your character's eyes."},
        {"Fixed third person", "A steady camera behind your character that does not swing around."}};
    q1.get = [] { return camera; };
    SetupQuestion q2{"Background blur", "The 2D game blurs what lies behind the subject (depth of field). "
        "In VR your eyes do the focusing, so most players find it easier without."};
    q2.choices = {{"Off", "Everything stays sharp.", true}, {"On", "The 2D game's blur, in both eyes."}};
    q2.get = [] { return blur; };
    SetupQuestion q3{"Camera tilt", "When the game camera looks up or down, your view can tilt with it."};
    q3.choices = {{"Tilt with the game camera", "As in the 2D game. Some players feel it in their stomach."},
        {"Keep the horizon level", "Only your head looks up and down (decoupled pitch). Steadier. Story scenes and ultimates keep the game's tilt."}};
    q3.get = [] { return tilt; };
    SetupQuestion q4{"Story scenes", "Story scenes and conversations can stay in full VR or switch to a screen while they play."};
    q4.choices = {{"Keep your view", "Stay in full VR, or whichever view you are in.", true},
        {"Stereo screen", "A big 3D screen, framed like the 2D game."}, {"Mono theatre", "A flat screen, the same picture in both eyes."}};
    q4.get = [] { return story; };
    setup.questions = {q1, q2, q3, q4};
    setup.done_detail = "Back to the game.";
    return setup;
}

Scene setup_scene(std::string name, int screen, int focus, wuwa_menu::Source source, ImVec2 mouse = ImVec2{-FLT_MAX, -FLT_MAX}) {
    return {std::move(name), [screen, focus, source](ImVec2 size) {
        auto setup = sample_setup();
        wuwa_menu::setup_start(setup, true);
        wuwa_menu::setup_go(setup, screen);
        if (focus >= 0) setup.focus = focus;
        setup.source = source;
        const ImVec2 pos{130.0f, 14.5f}, menu{1340.0f, 871.0f};
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("WuWa VR menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
        wuwa_menu::Look look{1.0f, wuwa_l10n::sheet_font()};
        wuwa_menu::draw_setup(setup, wuwa_menu::Moves{}, pos, menu, look);
        ImGui::End();
    }, mouse};
}

// The 3D controllers as JSON for the website's live card (site/app.js draws them as the plugin does):
// preview.exe --export-pad3d <file>. Rounded to 4 significant digits.
int export_pad3d(const fs::path& file) {
    using wuwa_pad3d::Device;
    std::ostringstream json;
    const auto num = [&](float v) { char text[32]; std::snprintf(text, sizeof(text), "%.4g", v); json << text; };
    const auto vec = [&](wuwa_pad3d::V3 v) { json << '['; num(v.x); json << ','; num(v.y); json << ','; num(v.z); json << ']'; };
    json << "{\"source\":\"mod/uevr/src/utility/WuWaPad3D.hpp via dev/imgui-preview --export-pad3d\"";
    for (const auto& [name, device] : {std::pair{"xbox", Device::xbox}, std::pair{"playstation", Device::playstation}, std::pair{"vr", Device::vr}}) {
        const auto& m = wuwa_pad3d::model(device);
        json << ",\n\"" << name << "\":{\"pitch\":";
        num(m.pitch);
        json << ",\"points\":[";
        for (size_t i = 0; i < m.points.size(); ++i) { if (i) json << ','; vec(m.points[i]); }
        json << "],\"edges\":[";
        for (size_t i = 0; i < m.edges.size(); ++i) json << (i ? "," : "") << m.edges[i].first << ',' << m.edges[i].second;
        json << "],\"splats\":[";
        for (size_t i = 0; i < m.splats.size(); ++i) {
            if (i) json << ',';
            json << '['; num(m.splats[i].first.x); json << ','; num(m.splats[i].first.y); json << ','; num(m.splats[i].first.z);
            json << ','; num(m.splats[i].second); json << ']';
        }
        json << "],\"parts\":[";
        for (size_t i = 0; i < m.parts.size(); ++i) {
            const auto& p = m.parts[i];
            char tint[16]{};
            if (p.tint) std::snprintf(tint, sizeof(tint), "#%02x%02x%02x", p.tint & 0xFF, (p.tint >> 8) & 0xFF, (p.tint >> 16) & 0xFF);
            json << (i ? ",\n" : "\n") << "{\"control\":" << p.control << ",\"shape\":" << static_cast<int>(p.shape) << ",\"label\":\"" << p.label
                 << "\",\"tint\":\"" << tint << "\",\"c\":";
            vec(p.c); json << ",\"u\":"; vec(p.u); json << ",\"v\":"; vec(p.v); json << ",\"n\":"; vec(p.n);
            json << ",\"ru\":"; num(p.ru); json << ",\"rv\":"; num(p.rv); json << ",\"depth\":"; num(p.depth); json << '}';
        }
        json << "]}";
    }
    json << "}\n";
    std::ofstream(file, std::ios::binary) << json.str();
    std::printf("wrote %s\n", file.string().c_str());
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && std::wstring_view{argv[1]} == L"--export-pad3d") return export_pad3d(argv[2]);
    if (argc < 3) {
        std::fprintf(stderr, "usage: preview.exe <UEVRBackend.dll> <output folder> [language] [width height]\n");
        return 2;
    }
    const fs::path dll = argv[1], out = argv[2];
    const std::string language = argc > 3 ? fs::path(argv[3]).string() : "en";
    const UINT width = argc > 5 ? _wtoi(argv[4]) : 1600, height = argc > 5 ? _wtoi(argv[5]) : 900;
    fs::create_directories(out);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    const HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!module) { std::fprintf(stderr, "cannot load resources from %ls\n", dll.c_str()); return 1; }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    for (const auto type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (SUCCEEDED(D3D11CreateDevice(nullptr, type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context))) break;
    }
    if (!device) { std::fprintf(stderr, "no D3D11 device\n"); return 1; }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    device->CreateTexture2D(&desc, nullptr, &target);
    device->CreateRenderTargetView(target.Get(), nullptr, &rtv);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    device->CreateTexture2D(&desc, nullptr, &staging);

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::StyleColorsDark();

    // Same font order as Framework::update_fonts: default, catalog fonts, sheet artwork.
    const auto overrides = fs::temp_directory_path() / "wuwa-preview-languages";
    fs::create_directories(overrides);
    wuwa_l10n::request(language, overrides);
    // apply_pending waits for a request to settle for 400 ms (the in-game picker debounce).
    Sleep(450);
    wuwa_l10n::apply_pending(module);
    io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, 16.0f);
    wuwa_l10n::reserve_fonts(io.Fonts, module, 16.0f);
    wuwa_sheet::reserve_font(io.Fonts);
    // The crest: from the DLL when it has it, else straight from the repo's asset files.
    static std::array<std::string, 3> crest_files;
    if (wuwa_brand::rcdata(module, 4840).empty()) {
        const auto assets = fs::path(__FILE__).parent_path() / ".." / ".." / "mod" / "uevr" / "src" / "assets";
        const char* names[] = {"wuwa-crest-72.rgba", "wuwa-crest-144.rgba", "wuwa-crest-288.rgba"};
        for (int i = 0; i < 3; ++i) {
            std::ifstream file(assets / names[i], std::ios::binary);
            std::ostringstream bytes;
            bytes << file.rdbuf();
            crest_files[i] = bytes.str();
        }
        wuwa_brand::reserve_data(io.Fonts, {crest_files[0], crest_files[1], crest_files[2]});
    } else {
        wuwa_brand::reserve(io.Fonts, module);
    }
    io.Fonts->Build();
    wuwa_brand::fill(io.Fonts);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    std::printf("language %s (%s)\n", wuwa_l10n::language().c_str(), wuwa_l10n::status().c_str());

    std::vector<Scene> scenes;
    // The controls cards and the tip line (utility/WuWaShortcutSheet).
    using wuwa_sheet::Card;
    using wuwa_sheet::CardState;
    const auto card = [&](std::string name, CardState state) {
        scenes.push_back({std::move(name), [state](ImVec2 size) {
            wuwa_sheet::draw_card(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), state);
        }});
    };
    card("card-1-hold-l3", CardState{});
    {
        CardState state;
        state.buttons = 0x2000;  // B held
        state.ui_hidden = true; state.portal = true; state.pinned = true;
        card("card-2-hold-l3-pressing-b", state);
    }
    {
        CardState state;
        state.vr_controllers = true; state.rt = true; state.diorama = true;
        card("card-3-hold-l3-vr-controllers", state);
    }
    {
        CardState state;
        state.card = Card::views; state.view = 1; state.blur = true; state.lt = state.rt = true;
        card("card-4-hold-lt-rt", state);
    }
    {
        CardState state;
        state.card = Card::views; state.view = 0; state.blur = false; state.lt = state.rt = true; state.buttons = 0x0080;
        card("card-5-hold-lt-rt-pressing-r3", state);
    }
    // The 3D controller on the cards (1.2.7): PlayStation and VR controller names, the story nudge note with caps.
    {
        CardState state;
        state.playstation = true; state.buttons = 0x2000; state.ui_hidden = true; state.story_nudge = true; state.story_nudge_cm = 12.0f;
        card("card-8-hold-l3-playstation", state);
    }
    {
        CardState state;
        state.vr_controllers = true; state.first_person = true; state.buttons = 0x0020;
        card("card-9-hold-l3-vr-view", state);
    }
    {
        CardState state;
        state.card = Card::views; state.vr_controllers = true; state.view = 2; state.hide_near = true; state.buttons = 0x8000;
        card("card-10-hold-lt-rt-vr", state);
    }
    {
        CardState state;
        state.card = Card::views; state.playstation = true; state.view = 0; state.buttons = 0x0040;
        card("card-11-hold-lt-rt-playstation", state);
    }
    // The keyboard card (1.2.7): hold the VR key; H held here, then the fixed camera with the wheel in use.
    wuwa_sheet::set_key_names("`", {"W", "A", "S", "D"});
    {
        CardState state;
        state.card = Card::keyboard; state.keys = wuwa_keyboard::State::chord_bit('H');
        state.ui_hidden = true; state.portal = true; state.pinned = true; state.blur = true; state.first_person = true;
        card("card-6-keyboard-pressing-h", state);
    }
    {
        CardState state;
        state.card = Card::keyboard; state.keys = wuwa_keyboard::State::up_bit; state.fixed_camera = true; state.wheel_lit = true;
        state.view = 2; state.hide_near = true;
        card("card-7-keyboard-fixed-camera", state);
    }
    scenes.push_back({"tip-3-keyboard", [](ImVec2 size) {
        wuwa_sheet::draw_tip(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), "New:",
            "hold {VR} to see the keyboard shortcuts \xC2\xB7 {Insert} opens the WuWa VR menu", false, {}, true);
    }});
    scenes.push_back({"warning-keyboard-hidden-ui", [](ImVec2 size) {
        wuwa_sheet::draw_hidden_ui_warning(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), true, true, true);
    }});
    scenes.push_back({"warning-recording-stopped", [](ImVec2 size) {
        wuwa_sheet::draw_recording_stopped_warning(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), 5, 20);
    }});
    scenes.push_back({"warning-keyboard-mouse-mode", [](ImVec2 size) {
        wuwa_sheet::draw_mouse_mode_warning(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), true, false, true);
    }});
    scenes.push_back({"tip-1-first-launches", [](ImVec2 size) {
        wuwa_sheet::draw_tip(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), "New:",
            "hold {L3} or {LT}+{RT} to see what each button does \xC2\xB7 {L3}+{R3} opens the WuWa VR menu", false);
    }});
    scenes.push_back({"tip-2-view-switch", [](ImVec2 size) {
        wuwa_sheet::draw_tip(ImGui::GetBackgroundDrawList(), size, ImGui::GetFont(), "Stereo screen",
            "{LT}+{RT}, hold {R3} for full VR", false);
    }});
    using wuwa_menu::Source;
    using wuwa_pad3d::Device;
    // Menu scenes, 1600 x 900 (design units scale by 1340 / 1200 from (130, 14.5)).
    const auto menu_point = [](float x, float y) { return ImVec2{130.0f + x * 1340.0f / 1200.0f, 14.5f + y * 1340.0f / 1200.0f}; };
    scenes.push_back(menu_scene("menu-01-home-xbox", 0, 0, Source::Buttons, Device::xbox));
    scenes.push_back(menu_scene("menu-02-home-pointer-vr", 0, 5, Source::Pointer, Device::vr, {}, menu_point(234.0f + 1.5f * 227.0f, 92.0f + 1.5f * 220.0f)));
    scenes.push_back(menu_scene("menu-03-view-playstation", 1, 1, Source::Buttons, Device::playstation));
    scenes.push_back(menu_scene("menu-04-view-more-open", 1, 7, Source::Buttons, Device::xbox, {"View"}));
    scenes.push_back(menu_scene("menu-05-camera-vr", 2, 2, Source::Buttons, Device::vr));
    scenes.push_back(menu_scene("menu-06-interface-keyboard", 5, 3, Source::Buttons, Device::keyboard));
    scenes.push_back(menu_scene("menu-07-graphics", 6, 0, Source::Buttons, Device::xbox));
    scenes.push_back(menu_scene("menu-08-record-pointer", 7, 0, Source::Pointer, Device::keyboard, {}, menu_point(600.0f, 126.0f)));
    scenes.push_back(menu_scene("menu-anim-home", 0, 0, Source::Buttons, Device::xbox, {}, ImVec2{-FLT_MAX, -FLT_MAX}, 210, 0.0f));
    scenes.push_back(setup_scene("setup-1-welcome", 0, -1, Source::Buttons));
    scenes.push_back(setup_scene("setup-2-camera", 1, -1, Source::Buttons));
    scenes.push_back(setup_scene("setup-3-blur", 2, 0, Source::Buttons));
    // Laser / mouse on the second card of the tilt question.
    scenes.push_back(setup_scene("setup-4-tilt-pointer", 3, -1, Source::Pointer, ImVec2{100.0f + 600.0f * 1400.0f / 1200.0f, 40.0f + 400.0f * 1400.0f / 1200.0f}));
    scenes.push_back(setup_scene("setup-5-story", 4, -1, Source::Buttons));
    scenes.push_back(setup_scene("setup-6-summary", 5, -1, Source::Buttons));
    // The step card that replaces the controls card while a plan runs.
    static const auto plan = wuwa_steps::parse(nlohmann::json::parse(R"({"version": 1, "id": "bars-1", "title": "Dialogue black bars",
        "steps": [
            {"id": "baseline", "title": "Baseline", "do": "Start a dialogue scene with black bars. Close your left eye, then your right eye.",
             "expect": "Bars in both eyes, or none.", "answers": ["No bars", "Left eye only", "Right eye only", "Both eyes"]},
            {"id": "letterbox-off", "title": "Letterbox off", "do": "Start the same dialogue again. Close your left eye, then your right eye.",
             "expect": "No black bars in either eye.", "answers": ["No bars", "Left eye only", "Right eye only", "Both eyes"]}]})"));
    scenes.push_back({"step-1-card-recording", [](ImVec2 size) {
        wuwa_menu::draw_step_card(ImGui::GetBackgroundDrawList(), size, wuwa_l10n::sheet_font(), plan, 1, "12:34", "");
    }});
    scenes.push_back({"step-2-card-finished", [](ImVec2 size) {
        wuwa_menu::draw_step_card(ImGui::GetBackgroundDrawList(), size, wuwa_l10n::sheet_font(), plan, 2, "", "");
    }});
    scenes.push_back({"note-cinematic", [](ImVec2 size) {
        wuwa_menu::draw_note(ImGui::GetBackgroundDrawList(), size, wuwa_l10n::sheet_font(), "Cinematic scene",
            "Looks wrong? Hold LT + RT, then hold L3 for a stereo screen (R3: mono).", "Turn this note off in the menu, Cinema page.");
    }});

    // The live 3D controller (utility/WuWaPad3D): every device playing a shortcut, still and animated.
    {
        using namespace wuwa_pad3d;
        struct Cell { Device device; Beats beats; const char* title; };
        const auto grid = [&](std::string name, std::vector<Cell> cells, int frames, float start) {
            scenes.push_back({std::move(name), [cells](ImVec2 size) {
                auto* list = ImGui::GetBackgroundDrawList();
                list->AddRectFilled(ImVec2{0, 0}, size, IM_COL32(12, 11, 9, 255));
                ImFont* font = wuwa_l10n::sheet_font();
                const float cw = size.x / 2.0f, ch = size.y / 2.0f;
                for (size_t i = 0; i < cells.size(); ++i) {
                    const float ox = (i % 2) * cw, oy = (i / 2) * ch;
                    list->AddRect(ImVec2{ox + 8, oy + 8}, ImVec2{ox + cw - 8, oy + ch - 8}, IM_COL32(214, 175, 91, 60), 14.0f, 0, 1.5f);
                    list->AddText(font, 22.0f, ImVec2{ox + 28, oy + 22}, IM_COL32(214, 175, 91, 200), cells[i].title);
                    const auto now = draw(list, font, ImVec2{ox + 40, oy + 52}, ImVec2{ox + cw - 40, oy + ch - 86}, cells[i].device,
                        cells[i].beats, scene_time);
                    draw_captions(list, font, ImVec2{ox + cw * 0.5f, oy + ch - 74}, 30.0f, cw - 70, cells[i].device, cells[i].beats, now.beat);
                }
                const float crest_h = 44.0f, crest_w = 44.0f * wuwa_brand::crest_aspect();
                wuwa_brand::draw_crest(list, ImVec2{size.x * 0.5f - crest_w * 0.5f, size.y * 0.5f - crest_h * 0.5f}, crest_h);
            }, ImVec2{-FLT_MAX, -FLT_MAX}, frames, start});
        };
        Beat hold_triggers;
        hold_triggers.hold = button::LT | button::RT;
        Beat click_r3;
        click_r3.press = button::R3;
        Beat hold_vr_key;
        hold_vr_key.key_hold = "VR";
        Beat press_2;
        press_2.key_press = "2";
        const std::vector<Cell> view{{Device::xbox, {hold_triggers, click_r3}, "Xbox"},
            {Device::playstation, {hold_triggers, click_r3}, "PlayStation"},
            {Device::vr, {hold_triggers, click_r3}, "VR controllers"}, {Device::keyboard, {hold_vr_key, press_2}, "Keyboard"}};
        grid("pad3d-1-view", view, 1, 1.6f);
        grid("pad3d-2-view-anim", view, 100, 0.0f);
        Beat hold_l3;
        hold_l3.hold = button::L3;
        Beat press_a;
        press_a.press = button::A;
        Beat twice_r3;
        twice_r3.press = button::R3;
        twice_r3.taps = 2;
        Beat long_menu;
        long_menu.press = button::Menu;
        long_menu.long_press = 1.0f;
        Beat insert;
        insert.key_press = "Insert";
        const std::vector<Cell> mixed{{Device::xbox, {hold_l3, press_a}, "Recenter"}, {Device::playstation, {twice_r3}, "Camera"},
            {Device::vr, {long_menu}, "VR controllers on / off"}, {Device::keyboard, {insert}, "Menu"}};
        grid("pad3d-3-mixed", mixed, 1, 1.5f);
        grid("pad3d-4-mixed-anim", mixed, 90, 0.0f);
    }

    // WUWA_PREVIEW_ONLY=prefix renders only the scenes whose names start with it.
    char only[128]{};
    GetEnvironmentVariableA("WUWA_PREVIEW_ONLY", only, sizeof(only));
    for (const auto& scene : scenes) {
      if (only[0] && scene.name.rfind(only, 0) != 0) continue;
      for (int frame_index = 0; frame_index < scene.frames; ++frame_index) {
        scene_time = scene.start + frame_index / fps;
        // Two frames so ImGui settles any first-frame layout.
        for (int frame = 0; frame < 2; ++frame) {
            io.MousePos = scene.mouse;
            ImGui_ImplDX11_NewFrame();
            ImGui::NewFrame();
            scene.draw(io.DisplaySize);
            ImGui::Render();
        }
        const float clear[4] = {0.12f, 0.12f, 0.13f, 1.0f};
        context->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->ClearRenderTargetView(rtv.Get(), clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        std::vector<unsigned char> rgba(size_t(width) * height * 4);
        for (UINT y = 0; y < height; ++y) {
            std::memcpy(rgba.data() + size_t(y) * width * 4, static_cast<unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch, width * 4);
            for (UINT x = 0; x < width; ++x) rgba[(size_t(y) * width + x) * 4 + 3] = 255; // opaque preview
        }
        context->Unmap(staging.Get(), 0);
        char suffix[16]{};
        if (scene.frames > 1) std::snprintf(suffix, sizeof(suffix), "_%03d", frame_index);
        const auto path = out / (scene.name + suffix + ".png");
        const bool saved = save_png(path, rgba, width, height);
        if (!saved || scene.frames == 1 || frame_index == 0) std::printf("%s %s\n", saved ? "wrote" : "FAILED", path.string().c_str());
      }
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
