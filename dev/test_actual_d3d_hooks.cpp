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
        require(argc == 1 || retained_dispatch || rendered_mode, "Unknown fixture mode");
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
