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
};
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

    for (const auto& scene : scenes) {
        // Two frames so ImGui settles any first-frame layout.
        for (int frame = 0; frame < 2; ++frame) {
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
