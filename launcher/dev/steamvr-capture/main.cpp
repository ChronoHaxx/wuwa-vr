// Public compositor mirrors only. No input, pose submissions, settings changes or game-memory access.
#define NOMINMAX
#include "capture_image.hpp"
#include <tlhelp32.h>
#include <dxgi.h>
#include <openvr.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>

using capture::check;
using Microsoft::WRL::ComPtr;
using Json = nlohmann::json;
static int64_t utc() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
static bool steamvr_running() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W process{sizeof(process)};
    bool found = false;
    if (Process32FirstW(snapshot, &process)) do {
        if (_wcsicmp(process.szExeFile, L"vrserver.exe") == 0) found = true;
    } while (Process32NextW(snapshot, &process));
    CloseHandle(snapshot);
    return found;
}
struct Eye {
    vr::IVRCompositor* compositor{};
    ID3D11ShaderResourceView* mirror{};
    ComPtr<ID3D11Texture2D> source, staging;
    UINT width{}, height{};
    bool bgra{};
    ~Eye() { if (mirror) compositor->ReleaseMirrorTextureD3D11(mirror); }
    Eye() = default;
    Eye(const Eye&) = delete;
    Eye& operator=(const Eye&) = delete;
    void open(vr::IVRCompositor* owner, vr::EVREye eye, ID3D11Device* device) {
        compositor = owner;
        const auto code = owner->GetMirrorTextureD3D11(eye, device, reinterpret_cast<void**>(&mirror));
        if (code != vr::VRCompositorError_None || !mirror)
            throw std::runtime_error("Mirror unavailable: " + std::to_string(code));
        ComPtr<ID3D11Resource> resource;
        mirror->GetResource(&resource);
        check(resource.As(&source));
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        mirror->GetDesc(&view);
        bgra = view.Format == DXGI_FORMAT_B8G8R8A8_UNORM || view.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool rgba = view.Format == DXGI_FORMAT_R8G8B8A8_UNORM || view.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if ((!bgra && !rgba) || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1 ||
            view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
            throw std::runtime_error("Unsupported mirror layout/format");
        width = desc.Width; height = desc.Height;
        capture::image_bytes(width, height);
        capture::image_bytes(width * 2, height);
        desc.BindFlags = 0; desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc, nullptr, &staging));
    }
    capture::Image read(ID3D11DeviceContext* context) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        try {
            auto result = capture::pixels(mapped, width, height, bgra);
            context->Unmap(staging.Get(), 0);
            return result;
        } catch (...) { context->Unmap(staging.Get(), 0); throw; }
    }
};
static vr::Compositor_FrameTiming timing(vr::IVRCompositor* compositor, uint32_t pid) {
    if (compositor->GetCurrentSceneFocusProcess() != pid || compositor->GetLastFrameRenderer() != pid)
        throw std::runtime_error("Requested game is not rendering the SteamVR scene");
    vr::Compositor_FrameTiming result{sizeof(result)};
    if (!compositor->GetFrameTiming(&result)) throw std::runtime_error("Compositor frame timing unavailable");
    return result;
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    HMODULE library{};
    void (__cdecl* shutdown)(){};
    bool initialized = false, com = false;
    try {
        if (argc != 5) throw std::runtime_error("Expected openvr_api.dll, existing scene PID, new output directory, request token");
        size_t used{};
        const auto pid = std::stoul(argv[2], &used);
        if (!pid || argv[2][used]) throw std::runtime_error("Invalid game PID");
        const std::wstring token = argv[4];
        if (token.size() != 32 || token.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
            throw std::runtime_error("Invalid capture request token");
        std::string token_utf8;
        for (const auto character : token) token_utf8.push_back(static_cast<char>(character));
        const std::filesystem::path out = argv[3];
        if (std::filesystem::exists(out)) throw std::runtime_error("Output already exists; preserve prior capture");
        if (!steamvr_running()) throw std::runtime_error("SteamVR is not running; capture will not launch it");
        const auto started = utc();
        library = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("Cannot load the specified OpenVR library");
        const auto init = reinterpret_cast<uint32_t(__cdecl*)(vr::EVRInitError*, vr::EVRApplicationType, const char*)>(GetProcAddress(library, "VR_InitInternal2"));
        const auto get = reinterpret_cast<void*(__cdecl*)(const char*, vr::EVRInitError*)>(GetProcAddress(library, "VR_GetGenericInterface"));
        shutdown = reinterpret_cast<void(__cdecl*)()>(GetProcAddress(library, "VR_ShutdownInternal"));
        if (!init || !get || !shutdown) throw std::runtime_error("OpenVR exports missing");
        if (!steamvr_running()) throw std::runtime_error("SteamVR stopped before capture");
        vr::EVRInitError error{};
        // Background apps are rejected by OpenVR if vrserver is not already running.
        init(&error, vr::VRApplication_Background, nullptr);
        if (error) throw std::runtime_error("Background OpenVR init error " + std::to_string(error));
        initialized = true;
        auto* system = static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version, &error));
        if (error || !system) throw std::runtime_error("System interface unavailable");
        auto* compositor = static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version, &error));
        if (error || !compositor) throw std::runtime_error("Compositor interface unavailable");
        timing(compositor, pid);
        int adapter_index{};
        system->GetDXGIOutputInfo(&adapter_index);
        ComPtr<IDXGIFactory> factory;
        ComPtr<IDXGIAdapter> adapter;
        check(CreateDXGIFactory(IID_PPV_ARGS(&factory)));
        check(factory->EnumAdapters(adapter_index, &adapter));
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context));
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        com = true;
        {
            Eye left, right;
            left.open(compositor, vr::Eye_Left, device.Get());
            right.open(compositor, vr::Eye_Right, device.Get());
            const auto initial = timing(compositor, pid);
            auto before = initial, after = initial;
            capture::Image left_image, right_image;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            bool captured = false;
            int64_t copied{};
            while (std::chrono::steady_clock::now() < deadline) {
                Sleep(25);
                before = timing(compositor, pid);
                if (!capture::advanced(before.m_nFrameIndex, initial.m_nFrameIndex)) continue;
                // Queue adjacent copies before CPU readback/PNG encoding. Public
                // mirrors have no source-frame identifier or cross-eye lock.
                context->CopyResource(left.staging.Get(), left.source.Get());
                context->CopyResource(right.staging.Get(), right.source.Get());
                left_image = left.read(context.Get());
                right_image = right.read(context.Get());
                after = timing(compositor, pid);
                copied = utc();
                if (!capture::advanced(after.m_nFrameIndex, before.m_nFrameIndex, 0))
                    throw std::runtime_error("Compositor frame counter restarted during readback");
                if (capture::has_color(left_image) && capture::has_color(right_image)) { captured = true; break; }
            }
            if (!captured) throw std::runtime_error("No progressing nonblank stereo mirrors within six seconds");
            const auto pair = capture::side_by_side(left_image, right_image);
            if (!std::filesystem::create_directories(out)) throw std::runtime_error("Output directory appeared during capture");
            capture::png(out / "left.png", left_image);
            capture::png(out / "right.png", right_image);
            capture::png(out / "image.png", pair);
            Json receipt{{"version", 1}, {"pid", pid}, {"client_id", token_utf8},
                {"method", "OpenVR public per-eye mirror"}, {"captureKind", "steamvr-composited-mirrors"},
                {"layer", "all"}, {"eye", "both"}, {"width", pair.width}, {"height", pair.height},
                {"started_unix_ms", started}, {"copied_unix_ms", copied},
                {"frame_initial", initial.m_nFrameIndex}, {"frame_before", before.m_nFrameIndex}, {"frame_after", after.m_nFrameIndex},
                {"same_instant", false}, {"source_frame_binding", "unavailable_public_mirror_api"},
                {"alpha", "opaque; RGB unchanged"}, {"settings_changed", false}, {"input_sent", false}};
            std::ofstream file(out / "mirror.json", std::ios::binary);
            file << receipt.dump(2) << '\n';
            file.flush();
            if (!file) throw std::runtime_error("Cannot save mirror receipt");
        }
        shutdown(); initialized = false;
        FreeLibrary(library); library = nullptr;
        CoUninitialize(); com = false;
        std::cout << "Captured composited per-eye mirrors; exact source-frame pairing is unavailable.\n";
        return 0;
    } catch (const std::exception& error) {
        if (initialized && shutdown) shutdown();
        if (library) FreeLibrary(library);
        if (com) CoUninitialize();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
