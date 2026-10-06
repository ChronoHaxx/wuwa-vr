// Own hidden windows only. No backend load, injection, Present, VR/profile work.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../mod/uevr/src/utility/WuWaSwapchainWindow.hpp"

using Microsoft::WRL::ComPtr;
using namespace wuwa_swapchain_window;
static unsigned checks{};
static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    ++checks;
}
static void checked(HRESULT hr, const char* message) { require(SUCCEEDED(hr), message); }
struct Window {
    HWND hwnd{};
    Window() {
        const wchar_t* name = L"WuWaWindowResolutionTest";
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = name;
        require(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "RegisterClass failed");
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, name, L"Hidden resolution test",
            WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        require(hwnd && !IsWindowVisible(hwnd), "Window must remain hidden");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); }
};
static ComPtr<IDXGISwapChain1> make12(IDXGIFactory2* factory, ID3D12CommandQueue* queue, HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 64; desc.Height = 64;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferCount = 2; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = hwnd ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = hwnd ? DXGI_ALPHA_MODE_UNSPECIFIED : DXGI_ALPHA_MODE_IGNORE;
    desc.Scaling = DXGI_SCALING_STRETCH;
    ComPtr<IDXGISwapChain1> chain;
    checked(hwnd ? factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &chain) :
        factory->CreateSwapChainForComposition(queue, &desc, nullptr, &chain), "CreateSwapChain12 failed");
    return chain;
}
static void describe(const char* name, const Result& r) {
    std::cout << name << " hwnd_hr=0x" << std::hex << static_cast<UINT>(r.hwnd_result)
        << " hwnd=" << r.hwnd << " desc_checked=" << r.desc_checked
        << " desc_hr=0x" << static_cast<UINT>(r.desc_result) << " desc_hwnd=" << r.desc_hwnd
        << " resolved=" << r.window << " fallback=" << r.used_desc << std::dec << '\n';
}
int main(int argc, char** argv) {
    try {
        const bool warp = argc == 2 && std::string(argv[1]) == "--warp";
        require(argc == 1 || warp, "Usage: swapchain-window.exe [--warp]");
        Window window12, window11;
        ComPtr<IDXGIFactory4> factory; checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory failed");
        ComPtr<IDXGIAdapter> adapter;
        if (warp) checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter failed");
        ComPtr<ID3D12Device> device;
        checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice failed");
        D3D12_COMMAND_QUEUE_DESC qdesc{}; qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue;
        checked(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue failed");
        auto hwnd_chain = make12(factory.Get(), queue.Get(), window12.hwnd);
        auto composition = make12(factory.Get(), queue.Get(), nullptr);
        auto modern = resolve(hwnd_chain.Get()); describe("modern_hwnd", modern);
        require(modern.window == window12.hwnd, "Modern chain must resolve its actual HWND");
        auto no_window = resolve(composition.Get()); describe("composition", no_window);
        require(!no_window.window && !no_window.used_desc, "Composition with no window must stay rejected");

        DXGI_SWAP_CHAIN_DESC legacy_desc{}; legacy_desc.BufferDesc.Width = 64; legacy_desc.BufferDesc.Height = 64;
        legacy_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        legacy_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; legacy_desc.BufferCount = 1;
        legacy_desc.SampleDesc.Count = 1; legacy_desc.OutputWindow = window11.hwnd;
        legacy_desc.Windowed = TRUE; legacy_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain> legacy; ComPtr<ID3D11Device> d11; ComPtr<ID3D11DeviceContext> c11;
        checked(D3D11CreateDeviceAndSwapChain(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &legacy_desc, &legacy, &d11, nullptr, &c11), "Create legacy chain failed");
        auto old = resolve(legacy.Get()); describe("legacy_hwnd", old);
        require(old.window == window11.hwnd, "Legacy chain must resolve its actual HWND");

        // Exercise the exact production decision policy, injecting only the
        // failed/newer-interface lookup. GetDesc remains the real DXGI call.
        auto real_desc = [&](DXGI_SWAP_CHAIN_DESC* d) { return hwnd_chain->GetDesc(d); };
        for (const HRESULT failure : {E_NOINTERFACE, DXGI_ERROR_INVALID_CALL}) {
            const auto r = resolve_with([&](HWND*) { return failure; }, real_desc);
            describe("forced_hwnd_failure", r);
            require(r.window == window12.hwnd && r.used_desc && r.desc_checked,
                "Failed GetHwnd must recover through the real GetDesc HWND");
        }
        const auto null_success = resolve_with([](HWND* h) { *h = nullptr; return S_OK; }, real_desc);
        require(null_success.window == window12.hwnd && null_success.used_desc, "S_OK/null must use GetDesc");
        const auto failed_dirty = resolve_with([&](HWND* h) { *h = window11.hwnd; return E_FAIL; }, real_desc);
        require(failed_dirty.window == window12.hwnd && failed_dirty.used_desc, "Failed HRESULT must not accept nonnull output");
        const auto both_failed = resolve_with([](HWND*) { return E_FAIL; }, [&](DXGI_SWAP_CHAIN_DESC* d) {
            d->OutputWindow = window12.hwnd; return E_FAIL;
        });
        require(!both_failed.window && !both_failed.used_desc, "Failed GetDesc must reject even nonnull output");
        const auto both_null = resolve_with([](HWND*) { return E_NOINTERFACE; }, [](DXGI_SWAP_CHAIN_DESC*) { return S_OK; });
        require(!both_null.window, "Successful null GetDesc cannot invent a window");
        bool desc_called{};
        const auto direct = resolve_with([&](HWND* h) { *h = window12.hwnd; return S_OK; }, [&](DXGI_SWAP_CHAIN_DESC*) {
            desc_called = true; return E_FAIL;
        });
        require(direct.window == window12.hwnd && !desc_called && !direct.used_desc,
            "Valid GetHwnd must preserve existing behavior without another lookup");
        require(!resolve(static_cast<IDXGISwapChain*>(nullptr)).window, "Null chain must stay rejected");

        DiagnosticBudget<int, 2> budget;
        require(budget.record(1) && !budget.record(1) && budget.record(2) && !budget.record(3),
            "Repeated and over-budget diagnostics must not flood the render thread");
        require(!IsWindowVisible(window11.hwnd) && !IsWindowVisible(window12.hwnd), "No visible windows permitted");
        std::cout << "PASS checks=" << checks << " driver=" << (warp ? "WARP" : "hardware")
            << " hidden_windows=true no_present=true no_backend=true\n"
            << "Scope: production resolver and forced-lookup fixtures, not reproduction of the reported Windows 11 game failure.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL_OR_UNSUPPORTED " << e.what() << '\n';
        return 1;
    }
}
