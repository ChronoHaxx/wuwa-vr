// Isolated process-local DXGI routing probe. Does not load UEVR/backend.dll,
// inject another process, show windows, or change any VR/profile settings.
// Compile with the actual kananlib PointerHook.cpp (see companion runner).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <utility/PointerHook.hpp>
#include "../mod/uevr/src/utility/WuWaD3DProbePolicy.hpp"

using Microsoft::WRL::ComPtr;
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static PresentFn original_present{};
static Present1Fn original_present1{};
static unsigned callbacks{};
static unsigned matching11{}, matching12{};

static std::string hex(std::uintptr_t value) {
    std::ostringstream out; out << "0x" << std::hex << value; return out.str();
}
static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
static void checked(HRESULT hr, const char* operation) {
    if (FAILED(hr)) throw std::runtime_error(std::string(operation) + " failed " + hex(static_cast<UINT>(hr)));
}
static void count_device(IDXGISwapChain* chain) {
    ++callbacks;
    ComPtr<ID3D11Device> d11;
    ComPtr<ID3D12Device> d12;
    if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&d11)))) ++matching11;
    if (SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&d12)))) ++matching12;
}
static HRESULT STDMETHODCALLTYPE hooked_present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    count_device(chain);
    return original_present(chain, interval, flags);
}
static HRESULT STDMETHODCALLTYPE hooked_present1(IDXGISwapChain1* chain, UINT interval, UINT flags,
                                                 const DXGI_PRESENT_PARAMETERS* params) {
    count_device(chain);
    return original_present1(chain, interval, flags, params);
}
static void** slot(IUnknown* object, size_t index) { return &(*reinterpret_cast<void***>(object))[index]; }

// Reproduce a callback already dispatched before the monitor removes its slot.
// This exercises the real PointerHook and DXGI dispatch, with only the monitor
// mutex/active flag modeled here (the full production hooks require Framework).
struct QueuedHookState {
    std::recursive_mutex monitor;
    HANDLE entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE finished{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    PointerHook* retained{};
    bool active{true};
    unsigned original_calls{}, framework_calls{};
    HRESULT result{E_UNEXPECTED};
    ~QueuedHookState() { if (entered) CloseHandle(entered); if (finished) CloseHandle(finished); }
};
static QueuedHookState* queued_state{};
static HRESULT STDMETHODCALLTYPE queued_present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    auto& state = *queued_state;
    SetEvent(state.entered);
    std::scoped_lock lock(state.monitor);
    const auto original = state.retained->get_original<PresentFn>();
    if (state.active) ++state.framework_calls;
    ++state.original_calls;
    return original(chain, interval, flags);
}
static HRESULT STDMETHODCALLTYPE queued_present1(IDXGISwapChain1* chain, UINT interval, UINT flags,
                                                const DXGI_PRESENT_PARAMETERS* params) {
    auto& state = *queued_state;
    SetEvent(state.entered);
    std::scoped_lock lock(state.monitor);
    const auto original = state.retained->get_original<Present1Fn>();
    if (state.active) ++state.framework_calls;
    ++state.original_calls;
    return original(chain, interval, flags, params);
}

struct HiddenWindow {
    HWND handle{};
    HiddenWindow() {
        static const wchar_t* name = L"WuWaIsolatedDxgiProbe";
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = name;
        if (!RegisterClassW(&wc)) require(GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "RegisterClass failed");
        handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, name, L"Isolated DXGI probe",
                                 WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        require(handle != nullptr, "CreateWindow failed");
        require(!IsWindowVisible(handle), "Probe window must stay hidden");
    }
    ~HiddenWindow() { if (handle) DestroyWindow(handle); }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
};
struct Chain {
    std::string name;
    ComPtr<IDXGISwapChain> base;
    ComPtr<IDXGISwapChain1> extended;
    bool d12{}, target{true};
};

static Chain make11(HWND window, bool warp, bool null_probe) {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 64; desc.BufferDesc.Height = 64;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1;
    desc.SampleDesc.Count = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Chain result; result.name = null_probe ? "D3D11_NULL_probe" : "D3D11_HWND"; result.target = !null_probe;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    checked(D3D11CreateDeviceAndSwapChain(nullptr, null_probe ? D3D_DRIVER_TYPE_NULL :
        warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &desc, &result.base, &device, nullptr, &context), "D3D11CreateDeviceAndSwapChain");
    checked(result.base.As(&result.extended), "D3D11 IDXGISwapChain1");
    return result;
}
static Chain make12(IDXGIFactory2* factory, ID3D12CommandQueue* queue, HWND window) {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 64; desc.Height = 64; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2; desc.SampleDesc.Count = 1;
    desc.SwapEffect = window ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = window ? DXGI_ALPHA_MODE_UNSPECIFIED : DXGI_ALPHA_MODE_IGNORE;
    Chain result; result.name = window ? "D3D12_HWND" : "D3D12_composition"; result.d12 = true;
    checked(window ? factory->CreateSwapChainForHwnd(queue, window, &desc, nullptr, nullptr, &result.extended) :
        factory->CreateSwapChainForComposition(queue, &desc, nullptr, &result.extended), "CreateSwapChain D3D12");
    checked(result.extended.As(&result.base), "D3D12 IDXGISwapChain");
    return result;
}
static HRESULT invoke(Chain& chain, bool present1, UINT flags) {
    if (present1) { DXGI_PRESENT_PARAMETERS params{}; return chain.extended->Present1(0, flags, &params); }
    return chain.base->Present(0, flags);
}
static void queued_retirement(Chain& composition, bool present1) {
    QueuedHookState state;
    require(state.entered && state.finished, "Could not create queued-callback synchronization events");
    queued_state = &state;
    auto hook_slot = present1 ? slot(composition.extended.Get(), 22) : slot(composition.base.Get(), 8);
    auto saved = *hook_slot;
    std::unique_lock monitor(state.monitor);
    PointerHook retained(hook_slot, present1 ? reinterpret_cast<void*>(&queued_present1) : reinterpret_cast<void*>(&queued_present));
    state.retained = &retained;
    // Composition avoids a second-thread Present depending on the hidden HWND
    // owner's message pump. No scene, commands, or visible frames are produced.
    std::thread worker([&] {
        state.result = invoke(composition, present1, DXGI_PRESENT_TEST);
        SetEvent(state.finished);
    });
    const bool entered = WaitForSingleObject(state.entered, 3000) == WAIT_OBJECT_0;
    const bool removed = retained.remove();
    state.active = false;
    monitor.unlock();
    if (WaitForSingleObject(state.finished, 5000) != WAIT_OBJECT_0) {
        // Do not unwind a live worker into destroyed stack/COM state. This is
        // only the isolated probe process; its owner also has a 30s timeout.
        std::cerr << "FAIL\tQueued callback did not finish; ending isolated probe." << std::endl;
        ExitProcess(3);
    }
    worker.join();
    queued_state = nullptr;
    require(entered, "Callback did not enter before hook retirement");
    require(removed && *hook_slot == saved, "Queued callback slot not restored");
    require(state.original_calls == 1, "Retired callback must call retained original exactly once");
    require(state.framework_calls == 0, "Retired callback must not call Framework or pin renderer state");
    checked(state.result, "Queued retired callback Present(TEST)");
    std::cout << "queued_retirement\tmethod=" << (present1 ? "Present1" : "Present")
        << "\toriginal_calls=" << state.original_calls << "\tframework_calls=" << state.framework_calls
        << "\thr=" << hex(static_cast<UINT>(state.result)) << std::endl;
}
static void describe(Chain& chain) {
    ComPtr<ID3D11Device> d11; ComPtr<ID3D12Device> d12;
    const auto hr11 = chain.base->GetDevice(IID_PPV_ARGS(&d11));
    const auto hr12 = chain.base->GetDevice(IID_PPV_ARGS(&d12));
    require(chain.d12 ? SUCCEEDED(hr12) && FAILED(hr11) : SUCCEEDED(hr11) && FAILED(hr12), "Unexpected GetDevice identity");
    auto present = slot(chain.base.Get(), 8); auto present1 = slot(chain.extended.Get(), 22);
    std::cout << "chain\t" << chain.name << "\tpresent_slot=" << present << "\tpresent_fn=" << *present
        << "\tpresent1_slot=" << present1 << "\tpresent1_fn=" << *present1
        << "\tget_device11=" << hex(static_cast<UINT>(hr11)) << "\tget_device12=" << hex(static_cast<UINT>(hr12)) << std::endl;
    if (chain.target) {
        // One real nonblocking Present per chain. Hidden windows may be occluded;
        // this is a successful status, not proof of a visible rendered picture.
        const auto hr = invoke(chain, false, DXGI_PRESENT_DO_NOT_WAIT);
        std::cout << "baseline_present\t" << chain.name << "\thr=" << hex(static_cast<UINT>(hr)) << std::endl;
        checked(hr, "Baseline nonblocking Present");
    }
}
static void matrix(std::vector<Chain>& chains, bool present1) {
    for (auto& probe : chains) {
        // D3D11's production probe hooks Present, not Present1.
        if (present1 && !probe.d12) continue;
        void** hook_slot = present1 ? slot(probe.extended.Get(), 22) : slot(probe.base.Get(), 8);
        void* saved = *hook_slot;
        {
            PointerHook hook(hook_slot, present1 ? reinterpret_cast<void*>(&hooked_present1) : reinterpret_cast<void*>(&hooked_present));
            if (present1) original_present1 = hook.get_original<Present1Fn>();
            else original_present = hook.get_original<PresentFn>();
            for (auto& target : chains) {
                if (!target.target) continue;
                callbacks = matching11 = matching12 = 0;
                void** target_slot = present1 ? slot(target.extended.Get(), 22) : slot(target.base.Get(), 8);
                // TEST checks real interface dispatch while avoiding repeated
                // presents filling an unattached composition chain's buffers.
                const auto hr = invoke(target, present1, DXGI_PRESENT_TEST);
                const bool shared = target_slot == hook_slot;
                std::cout << "route\tprobe=" << probe.name << "\ttarget=" << target.name
                    << "\tmethod=" << (present1 ? "Present1" : "Present") << "\tshared_slot=" << shared
                    << "\tcallbacks=" << callbacks << "\td11=" << matching11 << "\td12=" << matching12
                    << "\thr=" << hex(static_cast<UINT>(hr)) << std::endl;
                require(!shared || callbacks > 0, "Shared vtable slot did not reach PointerHook callback");
                require(callbacks == 0 || (target.d12 ? matching12 == callbacks : matching11 == callbacks), "Callback device identity mismatch");
                // Present1 may be unsupported on a legacy DISCARD chain; keep
                // its HRESULT as evidence instead of mistaking it for hook loss.
                if (!present1) checked(hr, "Matrix Present(TEST)");
            }
            require(hook.remove(), "PointerHook remove failed");
            require(*hook_slot == saved, "PointerHook did not restore the original slot");
        }
        require(*hook_slot == saved, "Hook destructor changed restored slot");
    }
}
static bool no_present_fallback(std::vector<Chain>& chains) {
    using namespace wuwa_d3d_probe;
    auto& composition = chains[1]; auto& game11 = chains[0]; auto& probe11 = chains[3];
    auto first_slot = slot(composition.base.Get(), 8); auto first_original = *first_slot;
    callbacks = matching11 = matching12 = 0;
    {
        PointerHook first(first_slot, reinterpret_cast<void*>(&hooked_present));
        original_present = first.get_original<PresentFn>();
        // Deliberately no Present on this successful dummy hook. We invoke the
        // production policy at its timeout boundary, not a synthetic stopwatch.
        require(callbacks == 0 && next_probe(Api::D3D12, false, false) == Api::D3D11,
                "No-Present bootstrap did not choose D3D11");
        require(first.remove() && *first_slot == first_original, "Old probe not removed before fallback");
    }
    auto second_slot = slot(probe11.base.Get(), 8); auto second_original = *second_slot;
    {
        PointerHook second(second_slot, reinterpret_cast<void*>(&hooked_present));
        original_present = second.get_original<PresentFn>();
        const auto hr = game11.base->Present(0, DXGI_PRESENT_TEST);
        checked(hr, "D3D11 fallback Present");
        std::cout << "fallback\tNULL_probe_to_HWND\tcallbacks=" << callbacks << "\td11=" << matching11
            << "\tshared_slot=" << (second_slot == slot(game11.base.Get(), 8)) << std::endl;
        // A different driver implementation can expose distinct vtables. Such
        // a result is an explicit limitation, not a fabricated universal pass.
        const bool routed = callbacks > 0 && matching11 == callbacks;
        require(next_probe(Api::D3D11, false, routed) == (routed ? Api::D3D11 : Api::D3D12), "Observed/unobserved renderer policy mismatch");
        require(next_probe(Api::D3D11, true, true) == Api::D3D11, "Initialized renderer switched");
        require(second.remove() && *second_slot == second_original, "Fallback probe not restored");
        return routed;
    }
}
int main(int argc, char** argv) {
    try {
        const bool warp = argc == 2 && std::string(argv[1]) == "--warp";
        require(argc == 1 || warp, "Usage: renderer-probe-dxgi.exe [--warp]");
        std::cout << "scope\tisolated_pointer_hooks\tdriver=" << (warp ? "WARP" : "hardware")
            << "\thidden_windows=true\tframework_loaded=false\tgame_injected=false" << std::endl;
        HiddenWindow window11, window12, null_window;
        ComPtr<IDXGIFactory4> factory; checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<IDXGIAdapter> adapter;
        if (warp) checked(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        ComPtr<ID3D12Device> device; checked(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC queue_desc{}; queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ComPtr<ID3D12CommandQueue> queue; checked(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        std::vector<Chain> chains;
        chains.push_back(make11(window11.handle, warp, false));
        chains.push_back(make12(factory.Get(), queue.Get(), nullptr));
        chains.push_back(make12(factory.Get(), queue.Get(), window12.handle));
        chains.push_back(make11(null_window.handle, false, true));
        for (auto& chain : chains) describe(chain);
        matrix(chains, false); matrix(chains, true);
        const bool fallback_routed = no_present_fallback(chains);
        queued_retirement(chains[1], false);
        queued_retirement(chains[1], true);
        require(!IsWindowVisible(window11.handle) && !IsWindowVisible(window12.handle) && !IsWindowVisible(null_window.handle), "Window became visible");
        std::cout << "PASS\tPointerHook dispatch/restoration and no-Present policy.\tNULL_to_HWND_fallback_observed=" << fallback_routed
            << "\tDifferent vtable routes are recorded, not assumed equal."
            "\tNot full UEVR WindowFilter/monitor/init, game, Win11 or headset acceptance." << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL_OR_UNSUPPORTED\t" << e.what() << std::endl;
        return 1;
    }
}
