// Actual production D3D11Hook/D3D12Hook and WindowFilter, own hidden HWNDs only.
// Framework.hpp is an explicit fixture providing ONLY the hook-monitor mutex.
// No backend, game injection, VR runtime, registry or profile access.
#include "Framework.hpp"
#include "D3D11Hook.hpp"
#include "D3D12Hook.hpp"
#include "WindowFilter.hpp"
#include <wrl/client.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

using Microsoft::WRL::ComPtr;
std::unique_ptr<Framework> g_framework = std::make_unique<Framework>();

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static void checked(HRESULT result, const char* what) {
    if (FAILED(result)) {
        std::ostringstream text; text << what << " failed 0x" << std::hex << static_cast<unsigned>(result);
        throw std::runtime_error(text.str());
    }
}
static void pump_for(std::chrono::milliseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(1);
    } while (std::chrono::steady_clock::now() < until);
}
struct HiddenWindow {
    HWND handle{};
    HiddenWindow() {
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"WuWaActualHookOwnProcessTest";
        require(RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "RegisterClass failed");
        handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Actual hook hidden test",
            WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        require(handle && !IsWindowVisible(handle), "Window must exist and remain hidden");
    }
    ~HiddenWindow() { if (handle) DestroyWindow(handle); }
};
struct Chain {
    ComPtr<IDXGISwapChain> base;
    ComPtr<IDXGISwapChain1> extended;
    ComPtr<IDXGISwapChain3> version3;
};
static Chain make11(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 64; desc.BufferDesc.Height = 64; desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1;
    desc.SampleDesc.Count = 1; desc.OutputWindow = hwnd; desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Chain result; ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    checked(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &result.base, &device, nullptr, &context), "D3D11CreateDeviceAndSwapChain");
    checked(result.base.As(&result.extended), "DX11 swapchain1");
    return result;
}
static Chain make12(HWND hwnd, IDXGIFactory4* factory, ID3D12CommandQueue* queue) {
    DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 64; desc.Height = 64;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; desc.SampleDesc.Count = 1; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH; desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    Chain result;
    checked(factory->CreateSwapChainForHwnd(queue, hwnd, &desc, nullptr, nullptr, &result.extended), "D3D12 CreateSwapChainForHwnd");
    checked(result.extended.As(&result.base), "DX12 swapchain base");
    checked(result.extended.As(&result.version3), "DX12 swapchain3");
    return result;
}
static void* slot_value(IUnknown* chain, unsigned index) { return (*reinterpret_cast<void***>(chain))[index]; }

int main() {
    try {
        spdlog::set_pattern("[%l] %v");
        std::cout << "scope\tactual_D3D11Hook_D3D12Hook_WindowFilter=true\tkananlib=actual_built_library"
            "\tFramework=mutex_only_stub\tfull_Framework_initialized=false\tgame_injected=false" << std::endl;
        HiddenWindow window11, window12;
        auto chain11 = make11(window11.handle);
        ComPtr<IDXGIFactory4> factory; checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<ID3D12Device> device12; checked(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device12)), "D3D12CreateDevice");
        ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC queue_desc{}; queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        checked(device12->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        auto chain12 = make12(window12.handle, factory.Get(), queue.Get());
        const auto saved11 = slot_value(chain11.base.Get(), 8), saved12 = slot_value(chain12.base.Get(), 8);
        const auto saved12_1 = slot_value(chain12.extended.Get(), 22);

        // Initialize the real shared filter before any window arrives. This is
        // the idle path that previously killed its title-query worker.
        WindowFilter::get();
        pump_for(std::chrono::milliseconds{450});

        unsigned callbacks12{}, callbacks11{}, matching12{}, matching11{}, mismatch11{}, post12{}, post11{}, hot_switches{};
        bool hot_switch_armed{};
        std::unique_ptr<D3D11Hook> hook11;
        std::unique_ptr<D3D12Hook> hook12;
        auto install12 = [&] {
            // Same ordering as Framework::hook_d3d12, including destruction of
            // the previously retired object BEFORE installing replacement hooks.
            hook12.reset();
            hook12 = std::make_unique<D3D12Hook>();
            hook12->on_present([&](D3D12Hook& current) {
                ++callbacks12;
                if (current.get_device() && current.get_swap_chain() == chain12.version3.Get()) ++matching12;
                require(current.get_command_queue() == queue.Get(), "Actual DX12 command queue discovery mismatched target");
            });
            hook12->on_post_present([&](D3D12Hook&) { ++post12; });
            require(hook12->hook(), "Actual D3D12Hook::hook failed");
        };
        auto install11 = [&] {
            hook11.reset();
            hook11 = std::make_unique<D3D11Hook>();
            hook11->on_present([&](D3D11Hook& current) {
                ++callbacks11;
                if (current.get_device()) ++matching11; else ++mismatch11;
                if (hot_switch_armed) {
                    require(!current.get_device(), "Hot mismatch switch received a real DX11 device");
                    hot_switch_armed = false;
                    require(current.unhook(), "Actual hot DX11 unhook failed");
                    // Executes inside the real Present callback holding the real
                    // recursive monitor mutex, as Framework::initialize does.
                    install12();
                    ++hot_switches;
                }
            });
            hook11->on_post_present([&](D3D11Hook&) { ++post11; });
            require(hook11->hook(), "Actual D3D11Hook::hook failed");
        };
        auto present_until = [&](Chain& chain, unsigned& counter, unsigned expected, const char* label) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            while (counter < expected && std::chrono::steady_clock::now() < deadline) {
                pump_for(std::chrono::milliseconds{10});
                checked(chain.base->Present(0, DXGI_PRESENT_TEST), label);
            }
            require(counter >= expected, std::string("No actual callback: ") + label);
        };
        {
            std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
            install12();
        }
        // The late first Present must pass the real filter and actual instance
        // VtableHook, not a manually invoked callback or a policy surrogate.
        pump_for(std::chrono::milliseconds{450});
        present_until(chain12, matching12, 1, "late DX12 first Present(TEST)");
        DXGI_PRESENT_PARAMETERS params{};
        const auto first_count = callbacks12;
        checked(chain12.extended->Present1(0, DXGI_PRESENT_TEST, &params), "Actual DX12 Present1(TEST)");
        require(callbacks12 == first_count + 1, "Actual Present1 callback absent");
        std::cout << "PASS\tlate_first_present\tDX12_callback=true\tPresent1_callback=true\tmatching_device_and_queue=true" << std::endl;

        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            {
                std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                require(hook12->unhook(), "Monitor-style DX12 unhook failed");
                install11();
                hot_switch_armed = true;
            }
            const auto expected_switch = hot_switches + 1;
            present_until(chain12, hot_switches, expected_switch, "DX11 wrong-device Present hot switch");
            require(!hot_switch_armed && !hook11->is_hooked() && hook12->is_hooked(), "Hot switch did not leave DX12 active");
            const auto expected12 = matching12 + 1;
            present_until(chain12, matching12, expected12, "DX12 after hot callback replacement");
            std::cout << "PASS\tactual_alternation_cycle=" << cycle + 1 << "\thot_callback_switches=" << hot_switches
                << "\tDX12_callbacks=" << callbacks12 << "\tDX11_wrong_device_callbacks=" << mismatch11 << std::endl;
        }

        {
            std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
            require(hook12->unhook(), "Final DX12 unhook failed");
            install11();
        }
        present_until(chain11, matching11, 1, "Actual matching DX11 Present(TEST)");
        require(hook11->get_swap_chain() == chain11.base.Get(), "Actual matching DX11 selected wrong swapchain");
        require(hook11->unhook(), "Final DX11 unhook failed");
        hook11.reset(); hook12.reset();
        require(slot_value(chain11.base.Get(), 8) == saved11 && slot_value(chain12.base.Get(), 8) == saved12 &&
            slot_value(chain12.extended.Get(), 22) == saved12_1, "Final actual hook restoration failed");
        require(!IsWindowVisible(window11.handle) && !IsWindowVisible(window12.handle), "Test window became visible");
        require(post11 > 0 && post12 > 0, "Actual post-Present callbacks absent");
        std::cout << "PASS\tactual_matching_DX11_and_final_restoration\tDX11=" << matching11 << "\tDX12=" << matching12
            << "\tpost11=" << post11 << "\tpost12=" << post12 << std::endl;
        std::cout << "LIMIT\tNo_full_Framework_monitor_or_render_init\tNo_Win11_or_remote_AMD_acceptance\tNo_game_or_headset" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL_OR_UNSUPPORTED\t" << error.what() << std::endl;
        return 1;
    }
}
