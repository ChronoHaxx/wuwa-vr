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
#include "utility/WuWaStepPlan.hpp"
#include "utility/WuWaVrMenu.hpp"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <utility>
#include <filesystem>
#include <functional>
#include <string>
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
};

// Sample values only: the menu model is drawn exactly as in the game, with the
// game's page and row layout (mods/vr/WuWaVrMenuHost.cpp).
struct Sample {
    bool controllers{true}, diorama{false}, recording{true}, hide_ids{true}, profile_row{true}, telemetry{true}, note{true},
        automatic{false}, framing{true}, follow{false}, sheet{true}, collide{false};
    int view{0}, camera{2}, style{0}, fps{2}, size{1}, kind{1}, step{1};
    float hud_size{1.1f}, hud_distance{2.0f}, hud_shape{1.33f}, hud_up{-0.15f}, hud_side{0.0f}, speed{600.0f}, turn{90.0f}, scale{0.875f};
};
Sample sample;

std::vector<wuwa_menu::Page> sample_pages() {
    using namespace wuwa_menu;
    const auto toggle = [](const char* label, const char* help, bool* value, const char* live = nullptr) {
        Item item = make(Kind::Toggle, label, help);
        item.get_bool = [value] { return *value; };
        item.set_bool = [value](bool v) { *value = v; };
        if (live) item.value_text = [live] { return std::string(live); };
        return item;
    };
    const auto choice = [](const char* label, const char* help, int* value, std::vector<std::string> choices) {
        Item item = make(Kind::Choice, label, help);
        item.get_int = [value] { return *value; };
        item.set_int = [value](int v) { *value = v; };
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
    const auto paragraph = [](const char* label, bool emphasis = false) {
        Item item = make(Kind::Text, label);
        item.wrap = !emphasis;
        item.emphasis = emphasis;
        return item;
    };
    const auto view = [&] {
        return choice("View", "Full VR: the game all around you. Stereo screen: a big 3D screen, for menus and scenes that look wrong in full VR. Mono theatre: a flat screen, the same picture in both eyes. Shortcut: hold LT + RT, then hold L3 (screen) or click R3 (mono).",
            &sample.view, {"Full VR", "Stereo screen", "Mono theatre"});
    };
    const auto camera = [&] {
        return choice("Camera", "Game camera: WuWa's own camera. Fixed third person: a steady camera behind you. Freecam: fly the camera anywhere, for cutscenes and photos. First person: through your character's eyes. Options for each are on the Camera page.",
            &sample.camera, {"Game camera", "Fixed third person", "Freecam", "First person"});
    };
    const auto record = [&] {
        return toggle("Record video", "Records the headset view through the WuWa VR app, which must be open. Player IDs are hidden while the option below is on.", &sample.recording, "REC 12:34");
    };
    std::vector<Page> pages;
    pages.push_back({"Quick", {
        view(), camera(),
        toggle("VR controllers", "Use the VR controllers as an Xbox pad for walking and sightseeing. Holding the left Menu button for 1 second also turns them on or off.", &sample.controllers),
        toggle("Diorama (miniature world)", "Shows the world as a 10x miniature around you, with the portal on or off. Needs Native Stereo. This launch only.", &sample.diorama),
        slider("HUD size", "How large the game's HUD and menus appear. More HUD options are on the HUD page.", &sample.hud_size, 0.5f, 5.0f, 0.05f, "%.2f"),
        slider("HUD distance", "How far away the HUD sits. Its stereo depth follows this distance.", &sample.hud_distance, 0.5f, 6.0f, 0.1f, "%.1f m"),
        record(),
        action("Recenter view", "Faces the view forward from where you are now."),
    }});
    std::vector<std::string> steps{"1 / 7", "2 / 7", "3 / 7", "4 / 7", "5 / 7", "6 / 7", "7 / 7"};
    pages.push_back({"Test", {
        paragraph("VR menu test 3  ·  step 2 of 7", true),
        choice("Step", "Left / right moves between steps without answering. Your answers stay saved.", &sample.step, steps),
        paragraph("Where: anywhere in the open world, standing still."),
        paragraph("Stick: one direction at a time", true),
        paragraph("Do: Open the menu. Push the left stick down slightly to the side, several times, then up the same way."),
        paragraph("Expect: Focus only moves up and down; values never change."),
        paragraph("Your answer: Pass. Answer again to change it."),
        action("Pass", "Saves this answer with the settings now in use, then shows the next step."),
        action("Fail", "Saves this answer with the settings now in use, then shows the next step."),
        action("Skip this step", "Saves \"skipped\" and shows the next step."),
        action("Mark this moment", "Saves the time and current settings, so the moment is easy to find later."),
        record(),
        paragraph("Recorder: recording. Reports and marks note the time in the video."),
    }});
    pages.push_back({"Camera", {
        camera(),
        paragraph("Freecam", true),
        choice("Flying style", "Polar fly: simple flying. Hover drone: floats and drifts to a stop. Plane: always moves forward. Acro drone: full manual control.", &sample.style, {"Polar fly", "Hover drone", "Plane", "Acro drone"}),
        slider("Speed", "How fast the freecam moves.", &sample.speed, 50.0f, 3000.0f, 50.0f, "%.0f"),
        slider("Turn speed", "How fast the freecam turns.", &sample.turn, 10.0f, 300.0f, 10.0f, "%.0f"),
        toggle("Collide with the world", "Stops the freecam at walls and the ground. Experimental.", &sample.collide),
        action("Leave freecam", "Back to the game camera."),
        paragraph("Everyone", true),
        slider("World scale", "How big the world feels. Above 1 makes you smaller. Diorama uses its own scale and leaves this alone.", &sample.scale, 0.5f, 2.0f, 0.025f, "%.3fx"),
        action("Reset camera offset", "Puts the camera back where the profile places it.", "Not moved"),
        action("Recenter view", "Faces the view forward from where you are now."),
    }});
    pages.push_back({"Cinema", {
        view(),
        paragraph("Black bars in cutscenes? In WuWa's graphics settings, change Cinematic from Cutscene to Fullscreen. The bars go and cutscenes fill your view."),
        toggle("Cinematic scene note", "When a letterboxed cinematic camera starts in full VR, a short note in view reminds you of the screen shortcut.", &sample.note),
        toggle("Automatic cinematic screen", "Switches to a screen during videos and story scenes it can detect, then back to full VR. Experimental.", &sample.automatic),
        toggle("Match cinematic framing", "Gives both eyes the same framing during letterboxed scenes.", &sample.framing),
    }});
    pages.push_back({"HUD", {
        slider("HUD size", "How large the game's HUD and menus appear.", &sample.hud_size, 0.5f, 5.0f, 0.05f, "%.2f"),
        slider("HUD distance", "How far away the HUD sits.", &sample.hud_distance, 0.5f, 6.0f, 0.1f, "%.1f m"),
        slider("HUD shape", "Stretches the HUD taller (above 1) or flatter. If round icons look like flat ovals, raise this until they look round.", &sample.hud_shape, 0.5f, 2.0f, 0.01f, "%.2f"),
        action("Reset HUD shape", "Back to the shape of the game's HUD image."),
        slider("Up / down", "Moves the HUD up or down.", &sample.hud_up, -1.5f, 1.5f, 0.05f, "%+.2f m"),
        slider("Left / right", "Moves the HUD sideways.", &sample.hud_side, -1.5f, 1.5f, 0.05f, "%+.2f m"),
        toggle("HUD follows your head", "On: the HUD stays in front of your eyes.", &sample.follow),
        action("Refresh HUD layout", "Asks the game to lay out its HUD again."),
        toggle("Shortcut sheet", "The controller shortcut card below you while playing.", &sample.sheet),
    }});
    pages.push_back({"Comfort", {}});
    pages.push_back({"Fixes", {}});
    pages.push_back({"Record", {
        record(),
        toggle("Hide player IDs", "Covers your UID in the corner, and the profile ID row in menus, with black boxes, in the headset and in videos. Check a short video before sharing: other names and chat are not hidden.", &sample.hide_ids),
        toggle("Also hide the profile ID row", "Covers the ID row on the ESC profile screen too.", &sample.profile_row),
        paragraph("Recorder: recording. Reports and marks note the time in the video."),
        choice("Frame rate", "Frames per second in the video.", &sample.fps, {"30 fps", "45 fps", "60 fps"}),
        choice("Picture size", "Width per eye.", &sample.size, {"720 px", "1024 px", "1280 px"}),
        toggle("Camera and controller data", "Saves the camera and controller movement next to the video.", &sample.telemetry),
        paragraph("Report a problem", true),
        paragraph("Pick what went wrong and save. The time, the video position and your settings go into the wuwa-reports folder of the UEVR profile. Share it together with the video."),
        choice("What happened", "Left / right picks the kind of problem.", &sample.kind, {"Just mark the moment", "Black bars", "Eyes don't match", "HUD or menu", "Camera position", "Stutter or freeze", "Controls", "Something else"}),
        action("Save report", "Saves the report now."),
        paragraph("Saved at 14:02:11, 12:30 into the video. Folder: UEVR profile > wuwa-reports."),
    }});
    pages.push_back({"Menu", {
        action("All settings (classic UEVR menu)", "Opens UEVR's full settings window."),
    }});
    return pages;
}

Scene menu_scene(std::string name, int page, int focus, wuwa_menu::Source source, ImVec2 mouse = ImVec2{-FLT_MAX, -FLT_MAX}) {
    return {std::move(name), [page, focus, source](ImVec2 size) {
        static auto pages = sample_pages();
        wuwa_menu::State state{};
        wuwa_menu::set_page(pages, state, page);
        state.focus = focus;
        state.source = source;
        // The game draws the menu on the UEVR quad; the preview fills a 1600 x 900 frame.
        const ImVec2 pos{100.0f, 40.0f}, menu{1400.0f, 820.0f};
        ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("WuWa VR menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
        wuwa_menu::Look look{1.0f, wuwa_l10n::sheet_font()};
        look.badge = "REC 12:34";
        wuwa_menu::draw(pages, state, wuwa_menu::Moves{}, pos, menu, look);
        ImGui::End();
    }, mouse};
}
}

int wmain(int argc, wchar_t** argv) {
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
    wuwa_l10n::apply_pending(module);
    io.Fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, 16.0f);
    wuwa_l10n::reserve_fonts(io.Fonts, module, 16.0f);
    wuwa_sheet::reserve_artwork(io.Fonts, module);
    io.Fonts->Build();
    if (!wuwa_sheet::fill_artwork(io.Fonts)) std::fprintf(stderr, "artwork: %s\n", wuwa_sheet::artwork_status());
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    std::printf("language %s (%s)\n", wuwa_l10n::language().c_str(), wuwa_l10n::status().c_str());

    std::vector<Scene> scenes;
    const char* pages[] = {"everyday", "cameras", "menus-hud", "uevr"};
    for (int page = 0; page < 4; ++page) {
        scenes.push_back({std::string("sheet-") + std::to_string(page + 1) + "-" + pages[page], [page](ImVec2 size) {
            wuwa_sheet::draw(ImGui::GetBackgroundDrawList(), size, page, ImGui::GetFont());
        }});
    }
    scenes.push_back({"sheet-3-menus-hud-mode-on", [](ImVec2 size) {
        wuwa_sheet::draw(ImGui::GetBackgroundDrawList(), size, 2, ImGui::GetFont(), 0, true, true);
    }});
    using wuwa_menu::Source;
    scenes.push_back(menu_scene("menu-1-quick-gamepad", 0, 1, Source::Buttons));
    // Laser / mouse resting on the camera row's value (design units scale by 1400 / 1200).
    scenes.push_back(menu_scene("menu-2-quick-pointer", 0, 1, Source::Pointer, ImVec2{100.0f + 960.0f * 1400.0f / 1200.0f, 40.0f + 216.0f * 1400.0f / 1200.0f}));
    scenes.push_back(menu_scene("menu-3-test-gamepad", 1, 1, Source::Buttons));
    scenes.push_back(menu_scene("menu-4-camera-gamepad", 2, 3, Source::Buttons));
    scenes.push_back(menu_scene("menu-5-hud-gamepad", 4, 2, Source::Buttons));
    scenes.push_back(menu_scene("menu-6-record-gamepad", 7, 10, Source::Buttons));
    scenes.push_back(menu_scene("menu-7-cinema-pointer", 3, 2, Source::Pointer, ImVec2{100.0f + 500.0f * 1400.0f / 1200.0f, 40.0f + 330.0f * 1400.0f / 1200.0f}));
    // The step card that replaces the shortcut sheet while a plan runs.
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

    for (const auto& scene : scenes) {
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
        const auto path = out / (scene.name + ".png");
        std::printf("%s %s\n", save_png(path, rgba, width, height) ? "wrote" : "FAILED", path.string().c_str());
    }
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
