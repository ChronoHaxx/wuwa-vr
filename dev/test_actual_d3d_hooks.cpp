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
#include <atomic>
#include <condition_variable>
#include <future>

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
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
static PresentFn native_present{};
static std::atomic_uint original_presents{};
static HRESULT STDMETHODCALLTYPE counted_present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    ++original_presents;
    return native_present(chain, interval, flags);
}
using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ResizeTargetFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, const DXGI_MODE_DESC*);
static ResizeBuffersFn native_resize_buffers{};
static ResizeTargetFn native_resize_target{};
static unsigned original_resize_buffers{}, original_resize_target{};
static HRESULT last_resize_target_result{};
using Resize11Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
struct ResizeProbeState {
    IDXGISwapChain* chain{};
    Resize11Fn native{}, cached{};
    unsigned calls{}, native_calls{};
    bool recurse{};
};
static ResizeProbeState resize_probe[3];
static HRESULT STDMETHODCALLTYPE resize_probe_forward(IDXGISwapChain* chain, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    for (auto& state : resize_probe) if (state.chain == chain) {
        ++state.calls;
        // A labelled overlay-cycle model, not Valve's implementation. The cap
        // keeps an old-source regression safe instead of exhausting its stack.
        if (state.calls > 32) return E_UNEXPECTED;
        if (state.recurse) return state.cached(chain, count, w, h, format, flags);
        ++state.native_calls;
        return state.native(chain, count, w, h, format, flags);
    }
    return E_UNEXPECTED;
}
static HRESULT STDMETHODCALLTYPE wrong_resize_origin(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT) {
    return E_UNEXPECTED;
}
static HRESULT STDMETHODCALLTYPE counted_resize_buffers(IDXGISwapChain3* chain, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    ++original_resize_buffers;
    return native_resize_buffers(chain, count, w, h, format, flags);
}
static HRESULT STDMETHODCALLTYPE counted_resize_target(IDXGISwapChain3* chain, const DXGI_MODE_DESC* desc) {
    ++original_resize_target;
    return last_resize_target_result = native_resize_target(chain, desc);
}
// Catch the original source's access violation inside this inert fixture. No
// crash dialog/dump or recovery handler is installed in any other process.
static HRESULT guarded_resize_buffers(ResizeBuffersFn fn, IDXGISwapChain3* chain, UINT w, UINT h, DWORD* fault) {
    __try { return fn(chain, 2, w, h, DXGI_FORMAT_UNKNOWN, 0); }
    __except (*fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { return E_UNEXPECTED; }
}
static HRESULT guarded_resize_target(ResizeTargetFn fn, IDXGISwapChain3* chain, const DXGI_MODE_DESC* desc, DWORD* fault) {
    __try { return fn(chain, desc); }
    __except (*fault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { return E_UNEXPECTED; }
}
class TestD3D12Hook final : public D3D12Hook {
public:
    // Fault injection in fixture state only, never in the real COM allocation.
    void reject_queue_offset_for_test() { m_command_queue_offset = UINT32_MAX; }
};

// A real GPU submission and readback precede every non-TEST Present. Hidden
// HWND occlusion is allowed; verified pixels prove rendering, not visibility.
class RenderedFrames {
    ID3D12Device* device;
    ID3D12CommandQueue* queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12DescriptorHeap> rtvs;
    ComPtr<ID3D12Fence> fence;
    HANDLE ready{};
    UINT64 serial{};
public:
    unsigned verified_pixels{};
    RenderedFrames(ID3D12Device* d, ID3D12CommandQueue* q) : device{d}, queue{q} {
        checked(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Render allocator");
        checked(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Render list");
        checked(list->Close(), "Initial render Close");
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = 1;
        checked(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvs)), "Render RTV heap");
        checked(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Render fence");
        ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(ready != nullptr, "Render event");
    }
    ~RenderedFrames() { if (ready) CloseHandle(ready); }
    void draw_and_verify(IDXGISwapChain3* chain, unsigned frame) {
        ComPtr<ID3D12Resource> target;
        checked(chain->GetBuffer(chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&target)), "Render GetBuffer");
        const auto desc = target->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT rows{}; UINT64 row_bytes{}, total{};
        device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_bytes, &total);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = total; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> readback;
        checked(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Render readback");
        checked(allocator->Reset(), "Render allocator Reset");
        checked(list->Reset(allocator.Get(), nullptr), "Render list Reset");
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = target.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1, &barrier);
        const auto rtv = rtvs->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(target.Get(), nullptr, rtv);
        const float color[]{frame % 2 ? 1.0f : 0.0f, frame % 2 ? 0.0f : 1.0f, 0.0f, 1.0f};
        list->ClearRenderTargetView(rtv, color, 0, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = target.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = readback.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; to.PlacedFootprint = footprint;
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        list->ResourceBarrier(1, &barrier);
        checked(list->Close(), "Render Close");
        ID3D12CommandList* submitted[]{list.Get()}; queue->ExecuteCommandLists(1, submitted);
        checked(queue->Signal(fence.Get(), ++serial), "Render Signal");
        if (fence->GetCompletedValue() < serial) {
            checked(fence->SetEventOnCompletion(serial, ready), "Render fence event");
            require(WaitForSingleObject(ready, 2000) == WAIT_OBJECT_0, "GPU fence timed out");
        }
        unsigned char* bytes{}; D3D12_RANGE range{static_cast<SIZE_T>(footprint.Offset), static_cast<SIZE_T>(footprint.Offset + 4)};
        checked(readback->Map(0, &range, reinterpret_cast<void**>(&bytes)), "Render Map");
        const bool correct = bytes[footprint.Offset] == (frame % 2 ? 255 : 0) &&
            bytes[footprint.Offset + 1] == (frame % 2 ? 0 : 255) && bytes[footprint.Offset + 2] == 0 && bytes[footprint.Offset + 3] == 255;
        D3D12_RANGE no_write{}; readback->Unmap(0, &no_write);
        require(correct, "GPU readback does not match submitted clear");
        ++verified_pixels;
    }
};

int main(int argc, char** argv) {
    try {
        const bool repaired_dispatch = argc == 2 && std::string(argv[1]) == "--retained-fixed";
        const bool retained_dispatch = repaired_dispatch || (argc == 2 && std::string(argv[1]) == "--retained-dispatch");
        const bool rendered = argc == 2 && std::string(argv[1]) == "--rendered";
        const bool gate_baseline = argc == 2 && std::string(argv[1]) == "--entry-gate-baseline";
        const bool gate_fixed = argc == 2 && std::string(argv[1]) == "--entry-gate-fixed";
        const bool rendered_mode = rendered || gate_baseline || gate_fixed;
        const bool resize_baseline = argc == 2 && std::string(argv[1]) == "--resize-baseline";
        const bool resize_fixed = argc == 2 && std::string(argv[1]) == "--resize-fixed";
        const bool resize_probe_baseline = argc == 2 && std::string(argv[1]) == "--resize-probe-baseline";
        const bool resize_probe_fixed = argc == 2 && std::string(argv[1]) == "--resize-probe-fixed";
        require(argc == 1 || retained_dispatch || rendered_mode || resize_baseline || resize_fixed || resize_probe_baseline || resize_probe_fixed, "Unknown fixture mode");
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
        unsigned hot_switch_after{};
        std::unique_ptr<D3D11Hook> hook11;
        std::unique_ptr<D3D12Hook> hook12;
        std::function<void(D3D12Hook&)> on_new12;
        auto install12 = [&] {
            // Same ordering as Framework::hook_d3d12, including destruction of
            // the previously retired object BEFORE installing replacement hooks.
            hook12.reset();
            hook12 = std::make_unique<TestD3D12Hook>();
            hook12->on_present([&](D3D12Hook& current) {
                ++callbacks12;
                if (current.get_device() && current.get_swap_chain() == chain12.version3.Get()) ++matching12;
                require(current.get_command_queue() == queue.Get(), "Actual DX12 command queue discovery mismatched target");
            });
            hook12->on_post_present([&](D3D12Hook&) { ++post12; });
            require(hook12->hook(), "Actual D3D12Hook::hook failed");
            if (on_new12) on_new12(*hook12);
        };
        auto install11 = [&] {
            hook11.reset();
            hook11 = std::make_unique<D3D11Hook>();
            hook11->on_present([&](D3D11Hook& current) {
                ++callbacks11;
                if (current.get_device()) ++matching11; else ++mismatch11;
                if (hot_switch_armed && mismatch11 >= hot_switch_after) {
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
        if (resize_probe_baseline || resize_probe_fixed) {
            const auto original11 = slot_value(chain11.base.Get(), 13);
            const auto original12 = slot_value(chain12.base.Get(), 13);
            install11();
            if (resize_probe_baseline) {
                require(slot_value(chain12.base.Get(), 13) != original12,
                    "Baseline did not reproduce global DX11 probe changing real DX12 ResizeBuffers");
                require(callbacks11 == 0 && callbacks12 == 0, "Baseline unexpectedly needed a Present");
                hook11.reset();
                require(slot_value(chain12.base.Get(), 13) == original12, "Baseline cleanup did not restore resize");
                std::cout << "REPRODUCED\tDX11_probe_changes_real_DX12_resize_before_first_Present\tPresent_callbacks=0\tno_stack_overflow_triggered=true" << std::endl;
                return 0;
            }
            require(slot_value(chain11.base.Get(), 13) == original11 && slot_value(chain12.base.Get(), 13) == original12,
                "Renderer probe must not change either actual chain's ResizeBuffers slot");
            resize_probe[0] = {chain12.base.Get(), reinterpret_cast<Resize11Fn>(original12)};
            {
                VtableHook count12{chain12.base.Get()};
                require(count12.hook_method(13, reinterpret_cast<uintptr_t>(&resize_probe_forward)), "Count DX12 resize");
                checked(chain12.base->ResizeBuffers(2, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "DX12 resize during DX11 probe");
                DXGI_SWAP_CHAIN_DESC desc{}; checked(chain12.base->GetDesc(&desc), "DX12 resize result");
                require(resize_probe[0].calls == 1 && resize_probe[0].native_calls == 1 &&
                    desc.BufferDesc.Width == 80 && desc.BufferDesc.Height == 48 && callbacks11 == 0,
                    "DX12 bootstrap resize did not reach real native method once independently of Present");
            }
            std::cout << "PASS\tDX12_bootstrap_resize_untouched\tnative_calls=1\tbuffers=80x48\tPresent_callbacks=0" << std::endl;

            resize_probe[1] = {chain11.base.Get(), reinterpret_cast<Resize11Fn>(original11)};
            VtableHook count11{chain11.base.Get()};
            require(count11.hook_method(13, reinterpret_cast<uintptr_t>(&resize_probe_forward)), "Count DX11 resize");
            unsigned renderer_calls{};
            bool recurse_renderer{};
            HRESULT renderer_nested{};
            hook11->on_resize_buffers([&](D3D11Hook&, uint32_t, uint32_t) {
                ++renderer_calls;
                if (recurse_renderer) renderer_nested = resize_probe[1].cached(chain11.base.Get(), 1, 80, 48, DXGI_FORMAT_UNKNOWN, 0);
            });
            present_until(chain11, matching11, 1, "Prove real DX11 before binding resize");
            resize_probe[1].cached = reinterpret_cast<Resize11Fn>(slot_value(chain11.base.Get(), 13));
            require(resize_probe[1].cached != &resize_probe_forward, "Verified DX11 did not receive instance resize hook");
            // Mutate the old table after installation. A saved immutable original
            // must still reach the real forwarder, not this changed slot.
            require(count11.hook_method(13, reinterpret_cast<uintptr_t>(&wrong_resize_origin)), "Mutate old table for identity test");
            checked(chain11.base->ResizeBuffers(1, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "Verified DX11 resize");
            require(renderer_calls == 1 && resize_probe[1].calls == 1 && resize_probe[1].native_calls == 1,
                "Verified instance must call renderer and its immutable native original once");
            require(count11.hook_method(13, reinterpret_cast<uintptr_t>(&resize_probe_forward)), "Restore old table counter");
            std::cout << "PASS\tverified_DX11_instance_resize\trenderer_once=true\timmutable_original_once=true" << std::endl;

            recurse_renderer = true;
            checked(chain11.base->ResizeBuffers(1, 96, 64, DXGI_FORMAT_UNKNOWN, 0), "Renderer nested resize outer operation");
            require(renderer_nested == DXGI_ERROR_INVALID_CALL && renderer_calls == 2 && resize_probe[1].native_calls == 2,
                "Same-chain renderer reentry must fail before a second renderer/native call");
            recurse_renderer = false;
            auto cycle = [&](const char* label, bool expects_renderer) {
                const auto calls_before = resize_probe[1].calls, native_before = resize_probe[1].native_calls;
                const auto renderer_before = renderer_calls;
                resize_probe[1].recurse = true;
                const auto hr = resize_probe[1].cached(chain11.base.Get(), 1, 80, 48, DXGI_FORMAT_UNKNOWN, 0);
                resize_probe[1].recurse = false;
                require(hr == DXGI_ERROR_INVALID_CALL && resize_probe[1].calls == calls_before + 1 &&
                    resize_probe[1].native_calls == native_before && renderer_calls == renderer_before + (expects_renderer ? 1 : 0), label);
                std::cout << "PASS\t" << label << "\treentry=DXGI_ERROR_INVALID_CALL\touter_forward_once=true\tcode_bytes_untouched=true" << std::endl;
            };
            cycle("active_overlay_cycle_bounded", true);
            WindowFilter::get().filter_window(window11.handle);
            cycle("filtered_overlay_cycle_bounded", false);
            const auto before_filtered = resize_probe[1].native_calls;
            checked(resize_probe[1].cached(chain11.base.Get(), 1, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "Filtered normal resize");
            require(resize_probe[1].native_calls == before_filtered + 1, "Filtered resize must still execute normally");
            require(hook11->unhook(), "Retire verified DX11");
            require(slot_value(chain11.base.Get(), 13) == reinterpret_cast<void*>(&resize_probe_forward), "Retirement failed to restore instance table");
            cycle("retired_overlay_cycle_bounded", false);

            // A queued old-chain callback must not borrow the replacement's
            // original or reset the replacement renderer.
            HiddenWindow other_window;
            auto other_chain = make11(other_window.handle);
            resize_probe[2] = {other_chain.base.Get(), reinterpret_cast<Resize11Fn>(slot_value(other_chain.base.Get(), 13))};
            std::promise<void> queued; auto ready = queued.get_future();
            std::future<HRESULT> pending;
            {
                std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                pending = std::async(std::launch::async, [&] {
                    queued.set_value();
                    return resize_probe[1].cached(chain11.base.Get(), 1, 96, 64, DXGI_FORMAT_UNKNOWN, 0);
                });
                ready.get();
                install11();
            }
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{3};
            while (pending.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready && std::chrono::steady_clock::now() < end)
                pump_for(std::chrono::milliseconds{1});
            require(pending.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready, "Queued resize did not finish");
            checked(pending.get(), "Queued old-chain resize after replacement");
            VtableHook count_other{other_chain.base.Get()};
            require(count_other.hook_method(13, reinterpret_cast<uintptr_t>(&resize_probe_forward)), "Count other DX11 resize");
            unsigned replacement_resizes{};
            hook11->on_resize_buffers([&](D3D11Hook&, uint32_t, uint32_t) { ++replacement_resizes; });
            present_until(other_chain, matching11, matching11 + 1, "Select replacement DX11 chain");
            const auto old_native = resize_probe[1].native_calls;
            checked(resize_probe[1].cached(chain11.base.Get(), 1, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "Old resize while another chain selected");
            require(resize_probe[1].native_calls == old_native + 1 && resize_probe[2].native_calls == 0 && replacement_resizes == 0,
                "Old-chain resize borrowed new chain original or touched new renderer");
            hook11.reset();
            cycle("destroyed_hook_overlay_cycle_bounded", false);
            checked(resize_probe[1].cached(chain11.base.Get(), 1, 96, 64, DXGI_FORMAT_UNKNOWN, 0), "Destroyed hook normal resize");
            require(count_other.remove() && count11.remove(), "Fixture tables did not restore");
            require(slot_value(chain11.base.Get(), 13) == original11 && slot_value(chain12.base.Get(), 13) == original12 &&
                slot_value(chain11.base.Get(), 8) == saved11 && slot_value(chain12.base.Get(), 8) == saved12,
                "Resize isolation test left a hook installed");
            std::cout << "PASS\tqueued_replaced_other_chain_destroyed_resize\tnative_once=true\twrong_renderer_untouched=true\tall_slots_restored=true" << std::endl;
            std::cout << "LIMIT\tactual_hooks_and_DXGI\toverlay_cycle_is_controlled_model\tSteam_binary_not_loaded\tFramework_mutex_only_stub" << std::endl;
            return 0;
        }
        if (resize_baseline || resize_fixed) {
            const auto saved_buffers = slot_value(chain12.version3.Get(), 13);
            const auto saved_target = slot_value(chain12.version3.Get(), 14);
            native_resize_buffers = reinterpret_cast<ResizeBuffersFn>(saved_buffers);
            native_resize_target = reinterpret_cast<ResizeTargetFn>(saved_target);
            PointerHook count_buffers{&(*reinterpret_cast<void***>(chain12.version3.Get()))[13], reinterpret_cast<void*>(&counted_resize_buffers)};
            PointerHook count_target{&(*reinterpret_cast<void***>(chain12.version3.Get()))[14], reinterpret_cast<void*>(&counted_resize_target)};
            unsigned renderer_buffers{}, renderer_targets{};
            on_new12 = [&](D3D12Hook& current) {
                current.on_resize_buffers([&](D3D12Hook&, uint32_t, uint32_t) { ++renderer_buffers; });
                current.on_resize_target([&](D3D12Hook&, uint32_t, uint32_t) { ++renderer_targets; });
            };
            install12();
            present_until(chain12, matching12, 1, "Select chain before resize replacement");
            const auto cached_buffers = reinterpret_cast<ResizeBuffersFn>(slot_value(chain12.version3.Get(), 13));
            const auto cached_target = reinterpret_cast<ResizeTargetFn>(slot_value(chain12.version3.Get(), 14));
            require(cached_buffers != &counted_resize_buffers && cached_target != &counted_resize_target, "Actual instance resize hooks missing");
            {
                std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                install12(); // Framework's same-API monitor replacement order.
            }
            DWORD buffers_fault{}, target_fault{};
            DXGI_MODE_DESC mode{}; mode.Width = 96; mode.Height = 64; mode.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            const auto buffers_result = guarded_resize_buffers(cached_buffers, chain12.version3.Get(), 80, 48, &buffers_fault);
            const auto target_result = guarded_resize_target(cached_target, chain12.version3.Get(), &mode, &target_fault);
            if (resize_baseline) {
                require(buffers_fault == EXCEPTION_ACCESS_VIOLATION && target_fault == EXCEPTION_ACCESS_VIOLATION,
                    "Expected original ResizeBuffers and ResizeTarget null-instance access violations were not reproduced");
                require(original_resize_buffers == 0 && original_resize_target == 0 && renderer_buffers == 0 && renderer_targets == 0,
                    "Baseline unexpectedly dispatched resize");
                std::cout << "REPRODUCED\tcached_resize_before_replacement_first_Present\tResizeBuffers_exception=0x" << std::hex << buffers_fault
                    << "\tResizeTarget_exception=0x" << target_fault << std::dec << "\toriginals=0\tFramework_resize=0" << std::endl;
            } else {
                require(buffers_fault == 0 && target_fault == 0, "Cached resize raised an exception after replacement");
                checked(buffers_result, "Cached ResizeBuffers before first Present");
                require(target_result == last_resize_target_result, "Cached ResizeTarget must return exact original result");
                DXGI_SWAP_CHAIN_DESC desc{}; checked(chain12.base->GetDesc(&desc), "Read resized chain");
                require(desc.BufferDesc.Width == 80 && desc.BufferDesc.Height == 48 && original_resize_buffers == 1 && original_resize_target == 1,
                    "Cached resize must execute actual original exactly once and resize real buffers");
                require(renderer_buffers == 0 && renderer_targets == 0 && hook12->get_display_width() == 0,
                    "Unselected replacement must not receive stale resize state");
                std::cout << "PASS\tcached_resize_before_replacement_first_Present\toriginals_once=true\tactual_buffers=80x48\tnew_renderer_untouched=true" << std::endl;

                present_until(chain12, matching12, matching12 + 1, "Select replacement before queued resize");
                std::promise<void> queued; auto pending_ready = queued.get_future();
                std::future<HRESULT> pending;
                {
                    std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                    pending = std::async(std::launch::async, [&] {
                        queued.set_value();
                        return cached_buffers(chain12.version3.Get(), 2, 96, 64, DXGI_FORMAT_UNKNOWN, 0);
                    });
                    pending_ready.get(); // callback cannot pass the owned monitor mutex.
                    install12();
                }
                const auto pending_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
                while (pending.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready &&
                    std::chrono::steady_clock::now() < pending_deadline) pump_for(std::chrono::milliseconds{1});
                require(pending.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready, "Queued resize failed to resume after monitor unlock");
                checked(pending.get(), "Queued ResizeBuffers after replacement");
                require(original_resize_buffers == 2 && renderer_buffers == 0, "Queued resize dispatched more than once or touched replacement renderer");
                std::cout << "PASS\tqueued_resize_across_monitor_replacement\toriginal_once=true\tnew_renderer_untouched=true" << std::endl;

                present_until(chain12, matching12, matching12 + 1, "Select replacement before active resize");
                checked(cached_buffers(chain12.version3.Get(), 2, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "Active selected ResizeBuffers");
                const auto active_target_result = cached_target(chain12.version3.Get(), &mode);
                require(original_resize_buffers == 3 && original_resize_target == 2 && renderer_buffers == 1 && renderer_targets == 1 &&
                    active_target_result == last_resize_target_result, "Active selected resize must keep one original and one renderer callback");
                std::cout << "PASS\tactive_selected_resize\toriginals_once=true\trenderer_callbacks_once=true" << std::endl;

                HiddenWindow other_window; auto other_chain = make12(other_window.handle, factory.Get(), queue.Get());
                install12();
                present_until(other_chain, callbacks12, callbacks12 + 1, "Select a different replacement chain");
                checked(cached_buffers(chain12.version3.Get(), 2, 96, 64, DXGI_FORMAT_UNKNOWN, 0), "Old chain resize after selecting another");
                require(original_resize_buffers == 4 && renderer_buffers == 1 && hook12->get_display_width() == 0,
                    "Old chain resize contaminated the newly selected chain");
                hook12.reset();
                checked(cached_buffers(chain12.version3.Get(), 2, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "Cached ResizeBuffers after destruction");
                const auto destroyed_target_result = cached_target(chain12.version3.Get(), &mode);
                require(original_resize_buffers == 5 && original_resize_target == 3 && renderer_buffers == 1 && renderer_targets == 1 &&
                    destroyed_target_result == last_resize_target_result, "Destroyed hook must forward original resize once without renderer callbacks");
                std::cout << "PASS\tother_chain_and_destroyed_hook_resize\toriginals_once=true\trenderer_untouched=true" << std::endl;
            }
            hook12.reset();
            require(count_buffers.remove() && count_target.remove(), "Resize counter restoration failed");
            require(slot_value(chain12.version3.Get(), 13) == saved_buffers && slot_value(chain12.version3.Get(), 14) == saved_target &&
                slot_value(chain12.base.Get(), 8) == saved12 && slot_value(chain12.extended.Get(), 22) == saved12_1, "Resize test did not restore all original slots");
            require(!IsWindowVisible(window12.handle), "Resize test window became visible");
            std::cout << "LIMIT\tactual_production_hooks_and_DXGI\tFramework_monitor_mutex_only\tremote_crash_cause_NOT_established" << std::endl;
            return 0;
        }
        if (rendered_mode) {
            WindowFilter::get().is_filtered(window12.handle);
            pump_for(std::chrono::milliseconds{450});
            require(!WindowFilter::get().is_filtered(window12.handle), "Rendered target remains filtered");
            const auto saved_resize = slot_value(chain12.base.Get(), 13);
            auto** native_slot = &(*reinterpret_cast<void***>(chain12.base.Get()))[8];
            PointerHook count_original{native_slot, reinterpret_cast<void*>(&counted_present)};
            native_present = count_original.get_original<PresentFn>();
            void* primary12{};
            if (gate_baseline || gate_fixed) {
                install12();
                primary12 = slot_value(chain12.base.Get(), 8);
                require(hook12->unhook(), "Entry-gate initial probe cleanup failed");
                std::cout << "MODEL\tshared_slot_entry_gate=true\tprimary_DX12_entry_bypassed_to_original=true\tremote_internal_cause_NOT_claimed=true" << std::endl;
            }
            RenderedFrames renderer{device12.Get(), queue.Get()};
            std::atomic_bool installed{}, render_done{}, monitor_done{}, failed{}, retired11_destroyed{};
            std::atomic_uint monitor_checks{}, bypasses{}, resize12{}, resize11{};
            on_new12 = [&](D3D12Hook& current) {
                current.on_resize_buffers([&](D3D12Hook&, uint32_t w, uint32_t h) {
                    require(w == 80 && h == 48, "Renderer resize dimensions mismatch"); ++resize12;
                });
            };
            DWORD monitor_tid{}, render_tid{};
            std::mutex error_mutex; std::string error;
            auto record_failure = [&](const std::exception& e) {
                std::scoped_lock lock{error_mutex}; error = e.what(); failed = true;
            };
            void* working11{};
            auto real_present = [&] {
                const auto entry = slot_value(chain12.base.Get(), 8);
                if (hot_switches > 0 && !gate_baseline) require(entry == working11, "Verified handoff replaced the known-working Present entry");
                // Explicit fault model at the call site, with unmodified shared
                // vtable: the primary12 entry is not delivered. Normal Rendered
                // mode never takes this branch or suppresses any entry.
                if ((gate_baseline || gate_fixed) && entry == primary12) {
                    ++bypasses;
                    checked(counted_present(chain12.base.Get(), 0, 0), "Model original Present");
                } else {
                    checked(chain12.base->Present(0, 0), "Rendered non-TEST Present");
                }
            };
            std::jthread monitor([&] {
                monitor_tid = GetCurrentThreadId();
                try {
                    {
                        std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                        install11();
                        hook11->on_resize_buffers([&](D3D11Hook&, uint32_t, uint32_t) { ++resize11; });
                        working11 = slot_value(chain12.base.Get(), 8);
                        hot_switch_after = 61; hot_switch_armed = true;
                    }
                    installed = true;
                    while (!render_done && !failed) {
                        {
                            std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                            ++monitor_checks;
                            if (hook12 && hook12->is_hooked()) {
                                require(!hook11 || !hook11->is_hooked(), "Competing API hooks remain active");
                                if (!gate_baseline && matching12 > 0 && hook11) {
                                    // Disposal occurs between actual callbacks,
                                    // not while the old callback stack uses it.
                                    hook11.reset(); retired11_destroyed = true;
                                    require(slot_value(chain12.base.Get(), 8) == working11, "Retired DX11 destructor removed the newer slot owner");
                                }
                            }
                        }
                        Sleep(1);
                    }
                    std::scoped_lock lock{g_framework->get_hook_monitor_mutex()};
                    if (hook12) require(hook12->unhook(), "Rendered monitor DX12 retirement failed");
                    if (hook11) require(hook11->unhook(), "Rendered monitor DX11 retirement failed");
                } catch (const std::exception& e) { record_failure(e); }
                monitor_done = true;
            });
            std::jthread render_thread([&] {
                render_tid = GetCurrentThreadId();
                try {
                    while (!installed && !failed) Sleep(1);
                    if (failed) { render_done = true; return; }
                    for (unsigned frame = 0; frame < 90; ++frame) {
                        if (frame == 80) {
                            // Every local backbuffer reference was released and
                            // its GPU work fenced before this real resize call.
                            checked(chain12.base->ResizeBuffers(2, 80, 48, DXGI_FORMAT_UNKNOWN, 0), "Rendered ResizeBuffers");
                            DXGI_SWAP_CHAIN_DESC desc{}; checked(chain12.base->GetDesc(&desc), "Resized GetDesc");
                            require(desc.BufferDesc.Width == 80 && desc.BufferDesc.Height == 48, "Real swapchain size did not change");
                        }
                        renderer.draw_and_verify(chain12.version3.Get(), frame);
                        real_present();
                        Sleep(1);
                    }
                } catch (const std::exception& e) { record_failure(e); }
                render_done = true;
            });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
            while ((!render_done || !monitor_done) && std::chrono::steady_clock::now() < deadline) pump_for(std::chrono::milliseconds{1});
            if (!render_done || !monitor_done) {
                std::cerr << "FAIL\tthreaded_render_watchdog" << std::endl;
                std::quick_exit(3); // End only this isolated process; no detached worker survives.
            }
            monitor.join(); render_thread.join();
            require(!failed, error);
            require(monitor_tid != render_tid && render_tid != GetCurrentThreadId() && monitor_checks > 0, "Separate monitor/render threads not exercised");
            require(renderer.verified_pixels == 90 && original_presents == 90, "Rendered frames/original Present must each occur90 times");
            require(hot_switches == 1 && callbacks11 == 61 && mismatch11 == 61, "Real61-frame DX11 handoff missing");
            if (gate_baseline) {
                require(callbacks12 == 0 && bypasses == 29 && resize12 == 0, "Entry-gate baseline did not reproduce missing12 callbacks");
            } else {
                require(callbacks12 == 29 && matching12 == 29 && bypasses == 0, "Verified12 renderer did not receive every post-handoff frame");
                require(resize12 == 1 && resize11 == 0, "Real resize must notify only DX12 renderer exactly once");
                require(retired11_destroyed, "Retired DX11 destruction while DX12 owned dispatch was not exercised");
            }
            hook11.reset(); hook12.reset();
            require(count_original.remove(), "Rendered original counter cleanup failed");
            require(slot_value(chain12.base.Get(), 8) == saved12 && slot_value(chain12.base.Get(), 13) == saved_resize &&
                slot_value(chain12.extended.Get(), 22) == saved12_1, "Rendered hook slots not restored");
            require(!IsWindowVisible(window12.handle), "Rendered test window became visible");
            std::cout << (gate_baseline ? "REPRODUCED" : "PASS") << "\trendered_multithread\tentry_gate=" << (gate_baseline || gate_fixed)
                << "\tGPU_readbacks=" << renderer.verified_pixels << "\tnonTEST_original_Presents=" << original_presents
                << "\tDX11_callbacks=" << callbacks11 << "\tDX12_matching=" << matching12 << "\tentry_bypasses=" << bypasses
                << "\tresize12=" << resize12 << "\tresize11=" << resize11 << "\tmonitor_checks=" << monitor_checks
                << "\tretired11_destroyed=" << retired11_destroyed
                << "\tmonitor_tid=" << monitor_tid << "\trender_tid=" << render_tid << "\tworking_DX11_entry=" << working11
                << "\tcleanup=true" << std::endl;
            std::cout << "LIMIT\tFramework_mutex_stub_with_explicit61frame_bootstrap\tNo_full_Framework_or_remote_Win11_acceptance" << std::endl;
            return 0;
        }
        if (retained_dispatch) {
            // Pre-resolve the real title so no filter delay affects counts.
            WindowFilter::get().is_filtered(window12.handle);
            pump_for(std::chrono::milliseconds{450});
            require(!WindowFilter::get().is_filtered(window12.handle), "Target title remained filtered");
            using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
            // Instrument the original call without replacing any production
            // callback. The actual PointerHook forwards to the real DXGI entry.
            auto** native_slot = &(*reinterpret_cast<void***>(chain12.base.Get()))[8];
            PointerHook count_original{native_slot, reinterpret_cast<void*>(&counted_present)};
            native_present = count_original.get_original<PresentFn>();
            std::cout << "instrumentation\toriginal_Present_counter=PointerHook_forwarding_real_DXGI" << std::endl;
            for (const bool clone_vtable : {true, false}) {
                install11();
                const auto priming11 = callbacks11, priming_mismatch = mismatch11;
                const auto priming_originals = original_presents.load();
                for (unsigned i = 0; i < 61; ++i) checked(chain12.base->Present(0, DXGI_PRESENT_TEST), "Prime real DX11 observation of DX12 chain");
                require(callbacks11 - priming11 == 61 && mismatch11 - priming_mismatch == 61,
                    "Priming must deliver 61 actual DX11 wrong-device callbacks");
                require(original_presents - priming_originals == 61, "Priming did not present exactly once per call");
                const auto retained11 = reinterpret_cast<PresentFn>(slot_value(chain12.base.Get(), 8));
                require(reinterpret_cast<void*>(retained11) != saved12,
                    "Local driver did not route the target through the production DX11 probe");
                auto** global_slot = &(*reinterpret_cast<void***>(chain12.base.Get()))[8];
                std::unique_ptr<VtableHook> copied;
                if (clone_vtable) {
                    // Simulate another component copying a COM instance table
                    // while DX11 is active. No production hook code is changed.
                    copied = std::make_unique<VtableHook>(chain12.base.Get());
                }
                require(hook11->unhook(), "Retaining DX11 dispatch: unhook failed");
                install12();
                const auto before12 = callbacks12, before11 = callbacks11, before_matching12 = matching12;
                const auto before_originals = original_presents.load();
                auto dispatch = [&] {
                    return clone_vtable ? chain12.base->Present(0, DXGI_PRESENT_TEST) :
                        retained11(chain12.base.Get(), 0, DXGI_PRESENT_TEST);
                };
                auto** target_slot = &(*reinterpret_cast<void***>(chain12.base.Get()))[8];
                std::cout << "retained_dispatch_setup\tkind=" << (clone_vtable ? "private_vtable" : "captured_function")
                    << "\tglobal_slot=" << global_slot << "\tglobal_value=" << *global_slot
                    << "\ttarget_slot=" << target_slot << "\ttarget_value=" << *target_slot
                    << "\tretained_DX11=" << reinterpret_cast<void*>(retained11) << std::endl;
                for (unsigned i = 0; i < 61; ++i) checked(dispatch(), "Retained DX11 while DX12 active");
                require(original_presents - before_originals == 61, "Retained dispatch must call original Present exactly once per call");
                require(callbacks11 == before11, "Retired DX11 delivered an active DX11 callback");
                if (repaired_dispatch) {
                    require(callbacks12 - before12 == 61 && matching12 - before_matching12 == 61,
                        "Retained DX11 dispatch did not reach matching active DX12 callbacks");
                } else {
                    require(callbacks12 == before12,
                        "Baseline retained-dispatch failure absent: an active DX12 callback received the presents");
                }
                require(hook12->unhook(), "Retained dispatch DX12 retirement failed");
                install11();
                const auto next11 = callbacks11, mismatch_before = mismatch11, retired12 = callbacks12;
                const auto next_originals = original_presents.load();
                for (unsigned i = 0; i < 61; ++i) checked(dispatch(), "Retained DX11 while DX11 active");
                require(callbacks11 - next11 == 61 && mismatch11 - mismatch_before == 61 && callbacks12 == retired12,
                    "Retained dispatch did not reproduce 61 wrong-device DX11 callbacks");
                require(original_presents - next_originals == 61, "Active DX11 did not call original exactly once");
                // Restore the private instance table before removing the global
                // hook, so fixture cleanup itself cannot create stale dispatch.
                if (copied) { require(copied->remove(), "Fixture private vtable restore failed"); copied.reset(); }
                require(hook11->unhook(), "Retained dispatch final DX11 retirement failed");
                const auto fully_retired11 = callbacks11, fully_retired12 = callbacks12;
                const auto retired_originals = original_presents.load();
                checked(retained11(chain12.base.Get(), 0, DXGI_PRESENT_TEST), "Both hooks retired");
                require(callbacks11 == fully_retired11 && callbacks12 == fully_retired12 && original_presents - retired_originals == 1,
                    "Retired bridge must only call original once");
                std::cout << (repaired_dispatch ? "PASS" : "REPRODUCED") << "\tretained_dispatch\tkind=" << (clone_vtable ? "private_vtable" : "captured_function")
                    << "\tpriming_DX11_wrong_device_callbacks=61\twhile_DX12_active_calls=61\tDX12_callbacks=" << callbacks12 - before12
                    << "\twhile_DX11_active_callbacks=61\toriginals_exactly_once=true\tretired_bridge_inactive=true" << std::endl;
            }
            if (repaired_dispatch) {
                install11();
                checked(chain12.base->Present(0, DXGI_PRESENT_TEST), "Prime rejected bridge source");
                const auto rejected_retained = reinterpret_cast<PresentFn>(slot_value(chain12.base.Get(), 8));
                require(hook11->unhook(), "Rejected bridge DX11 retirement failed");
                install12();
                static_cast<TestD3D12Hook*>(hook12.get())->reject_queue_offset_for_test();
                const auto rejected12 = callbacks12, rejected11 = callbacks11;
                const auto rejected_originals = original_presents.load();
                for (unsigned i = 0; i < 61; ++i) checked(rejected_retained(chain12.base.Get(), 0, DXGI_PRESENT_TEST), "Invalid queue offset fallback");
                require(callbacks12 == rejected12 && callbacks11 == rejected11 && original_presents - rejected_originals == 61,
                    "Invalid queue offset must refuse bridge and forward original once");
                require(hook12->unhook(), "Invalid queue offset cleanup failed");
                std::cout << "PASS\tinvalid_queue_offset_refuses_bridge\trepeated_calls=61\toriginal_once=true" << std::endl;
            }
            // A positively identified DX11 chain must never enter the DX12
            // handoff, even if it retains the identical DX11 function address.
            WindowFilter::get().is_filtered(window11.handle);
            pump_for(std::chrono::milliseconds{450});
            install11();
            const auto before_real11 = matching11;
            checked(chain11.base->Present(0, DXGI_PRESENT_TEST), "Prime actual DX11 chain");
            require(matching11 == before_real11 + 1, "Actual DX11 device not identified");
            const auto real11_retained = reinterpret_cast<PresentFn>(slot_value(chain11.base.Get(), 8));
            require(hook11->unhook(), "Actual DX11 retirement failed");
            install12();
            const auto negative12 = callbacks12, negative11 = callbacks11;
            const auto negative_originals = original_presents.load();
            checked(real11_retained(chain11.base.Get(), 0, DXGI_PRESENT_TEST), "Retained real DX11 must not bridge");
            require(callbacks12 == negative12 && callbacks11 == negative11 && original_presents - negative_originals == 1,
                "Real DX11 was bridged or did not forward original exactly once");
            require(hook12->unhook(), "Real DX11 negative cleanup failed");
            std::cout << "PASS\tactual_DX11_never_bridged\toriginal_once=true" << std::endl;
            // DX11 has no Present1 hook. Test the appropriate separate case:
            // a captured real DX12 Present1 remains callable after replacing its
            // retired hook object with a new active DX12 object.
            install12();
            const auto retained12_1 = reinterpret_cast<Present1Fn>(slot_value(chain12.extended.Get(), 22));
            require(hook12->unhook(), "Captured Present1 retirement failed");
            install12();
            const auto before12_1 = matching12;
            DXGI_PRESENT_PARAMETERS params{};
            checked(retained12_1(chain12.extended.Get(), 0, DXGI_PRESENT_TEST, &params), "Retained DX12 Present1 after replacement");
            require(matching12 == before12_1 + 1, "Captured DX12 Present1 did not reach replacement hook");
            require(hook12->unhook(), "Captured Present1 cleanup failed");
            hook11.reset(); hook12.reset();
            require(count_original.remove(), "Original-call instrumentation removal failed");
            require(slot_value(chain12.base.Get(), 8) == saved12 && slot_value(chain12.extended.Get(), 22) == saved12_1,
                "Retained dispatch final restoration failed");
            require(!IsWindowVisible(window11.handle) && !IsWindowVisible(window12.handle), "Test window became visible");
            std::cout << "PASS\tcaptured_DX12_Present1_after_same_API_replacement\tfinal_restoration=true" << std::endl;
            std::cout << "LIMIT\tFixture_intentionally_retains_dispatch\tRemote_retention_NOT_established"
                "\tFull_Framework_not_linked\tNo_game_or_headset" << std::endl;
            return 0;
        }
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
