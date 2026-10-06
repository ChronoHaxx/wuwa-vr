// Actual production Present hooks with only Framework's mutex stubbed.
// Adapted from test_present_recovery_dxgi.cpp for WuWaPresentGuard: a loop is
// broken by restoring the patched DXGI entry and presenting once; an
// unrecoverable loop is ended with S_OK (no frame) instead of an error the
// game could treat as fatal.
// Inline patches affect system DXGI loaded in THIS isolated fixture process.
// No Steam/game/backend DLL is loaded and no other process is touched.
#include "Framework.hpp"
#include "D3D11Hook.hpp"
#include "D3D12Hook.hpp"
#include "WindowFilter.hpp"
#ifdef WUWA_GUARD_AVAILABLE
#include "utility/WuWaPresentGuard.hpp"
#endif
#include <wrl/client.h>
#include <spdlog/spdlog.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

using Microsoft::WRL::ComPtr;
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
std::unique_ptr<Framework> g_framework = std::make_unique<Framework>();
static Present captured_present{}, native_present{};
static Present cross_class_reentry{};
static Present1 captured_present1{}, native_present1{};
static unsigned overlay_calls{}, framework11{}, framework12{}, post11{}, post12{};

static void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
static void checked(HRESULT hr, const char* text) {
    if (FAILED(hr)) { std::ostringstream out; out << text << " hr=0x" << std::hex << unsigned(hr); throw std::runtime_error(out.str()); }
}
static void pump(unsigned ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    do {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(1);
    } while (std::chrono::steady_clock::now() < end);
}
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"WuWaPresentRecoveryOwnProcess";
        require(RegisterClassW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "RegisterClass");
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Present recovery hidden test",
            WS_OVERLAPPEDWINDOW, 0, 0, 96, 64, nullptr, nullptr, wc.hInstance, nullptr);
        require(hwnd && !IsWindowVisible(hwnd), "Hidden window");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); }
};
struct Chain {
    ComPtr<IDXGISwapChain> base;
    ComPtr<IDXGISwapChain1> one;
    ComPtr<IDXGISwapChain3> three;
    ComPtr<ID3D12Device> device12;
    ComPtr<ID3D12CommandQueue> queue;
};
static Chain make_chain(HWND hwnd, bool dx12) {
    Chain result;
    if (dx12) {
        ComPtr<IDXGIFactory4> factory; checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "Factory");
        checked(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&result.device12)), "D3D12 device");
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        checked(result.device12->CreateCommandQueue(&q, IID_PPV_ARGS(&result.queue)), "Queue");
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = 96; desc.Height = 64; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.Scaling = DXGI_SCALING_STRETCH;
        checked(factory->CreateSwapChainForHwnd(result.queue.Get(), hwnd, &desc, nullptr, nullptr, &result.one), "DX12 chain");
        checked(result.one.As(&result.base), "Base interface"); checked(result.one.As(&result.three), "Chain3 interface");
    } else {
        DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferDesc.Width = 96; desc.BufferDesc.Height = 64;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 1; desc.OutputWindow = hwnd;
        desc.Windowed = TRUE; desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        checked(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &result.base, &device, nullptr, &context), "DX11 chain");
        checked(result.base.As(&result.one), "Chain1 interface");
    }
    return result;
}
static void** slot(IUnknown* object, unsigned n) { return &(*reinterpret_cast<void***>(object))[n]; }
static UINT presents(Chain& chain) {
    UINT count{}; checked(chain.base->GetLastPresentCount(&count), "Native GetLastPresentCount"); return count;
}

// Valve is not loaded here. This controlled shape models an inline native
// entry detour that has retained a previously installed UEVR callback.
static HRESULT STDMETHODCALLTYPE overlay_present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    if (++overlay_calls > 32) return E_UNEXPECTED; // bounded baseline failure, never exhaust the fixture's stack
    return (cross_class_reentry ? cross_class_reentry : captured_present)(chain, interval, flags);
}
static HRESULT STDMETHODCALLTYPE overlay_present1(IDXGISwapChain1* chain, UINT interval, UINT flags,
                                                   const DXGI_PRESENT_PARAMETERS* params) {
    if (++overlay_calls > 32) return E_UNEXPECTED;
    return captured_present1(chain, interval, flags, params);
}
// A valid cross-method delegation is NOT recursion: original Present1 wrapper
// delegates to hooked Present, which then reaches the actual native Present.
static HRESULT STDMETHODCALLTYPE delegate_present1(IDXGISwapChain1* chain, UINT interval, UINT flags,
                                                    const DXGI_PRESENT_PARAMETERS*) {
    ++overlay_calls;
    return chain->Present(interval, flags);
}

struct EntryPatch {
    void* target{};
    void* relay{};
    std::array<unsigned char, 14> before{};
    std::array<unsigned char, 14> patched{};
    static void* allocate_near(void* source) {
        SYSTEM_INFO info{}; GetSystemInfo(&info);
        const auto address = reinterpret_cast<uintptr_t>(source);
        const auto aligned = address - address % info.dwAllocationGranularity;
        // Only this fixture's address space, at most 4096 attempts within
        // +/-128MiB on Windows' 64KiB allocation granularity. Never overwrite
        // an allocation: VirtualAlloc at an occupied candidate simply fails.
        for (uintptr_t i = 1; i <= 2048; ++i) {
            const auto delta = i * info.dwAllocationGranularity;
            for (int sign : {-1, 1}) {
                if (sign < 0 && aligned < delta) continue;
                const auto candidate = sign < 0 ? aligned - delta : aligned + delta;
                if (candidate < reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress) ||
                    candidate > reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress)) continue;
                if (auto page = VirtualAlloc(reinterpret_cast<void*>(candidate), 4096,
                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) return page;
            }
        }
        throw std::runtime_error("No bounded near relay allocation available");
    }
    EntryPatch(void* source, void* destination, bool relative = false) : target(source) {
        std::memcpy(before.data(), target, before.size());
        if (relative) {
            relay = allocate_near(source);
            std::array<unsigned char, 14> jump{0xff, 0x25, 0, 0, 0, 0};
            std::memcpy(jump.data() + 6, &destination, sizeof(destination));
            std::memcpy(relay, jump.data(), jump.size());
            DWORD ignored{};
            require(VirtualProtect(relay, 4096, PAGE_EXECUTE_READ, &ignored), "Protect own near relay");
            require(FlushInstructionCache(GetCurrentProcess(), relay, jump.size()), "Flush own near relay");
            const auto distance = static_cast<int64_t>(reinterpret_cast<uintptr_t>(relay)) -
                static_cast<int64_t>(reinterpret_cast<uintptr_t>(source) + 5);
            require(distance >= std::numeric_limits<int32_t>::min() && distance <= std::numeric_limits<int32_t>::max(),
                "Allocated relay is outside E9 range");
            patched = before; patched[0] = 0xe9;
            const auto displacement = static_cast<int32_t>(distance);
            std::memcpy(patched.data() + 1, &displacement, sizeof(displacement));
        } else {
            patched = {0xff, 0x25, 0, 0, 0, 0};
            std::memcpy(patched.data() + 6, &destination, sizeof(destination));
        }
        write(patched.data());
    }
    void write(const unsigned char* bytes) {
        DWORD previous{}; require(VirtualProtect(target, before.size(), PAGE_EXECUTE_READWRITE, &previous), "Protect own native entry");
        std::memcpy(target, bytes, before.size()); FlushInstructionCache(GetCurrentProcess(), target, before.size());
        DWORD ignored{}; require(VirtualProtect(target, before.size(), previous, &ignored), "Restore own entry protection");
    }
    void reapply() { write(patched.data()); }
    bool restored() const { return std::memcmp(target, before.data(), before.size()) == 0; }
    ~EntryPatch() { write(before.data()); if (relay) VirtualFree(relay, 0, MEM_RELEASE); }
};

static void accept_window(HWND hwnd) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (WindowFilter::get().is_filtered(hwnd) && std::chrono::steady_clock::now() < end) pump(5);
    require(!WindowFilter::get().is_filtered(hwnd), "Window did not become accepted");
}
static void callbacks(D3D11Hook& hook) {
    hook.on_present([](D3D11Hook&) { ++framework11; });
    hook.on_post_present([](D3D11Hook&) { ++post11; });
}
static void callbacks(D3D12Hook& hook) {
    hook.on_present([](D3D12Hook&) { ++framework12; });
    hook.on_post_present([](D3D12Hook&) { ++post12; });
}

int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3, "Use case-name [--baseline]");
        const std::string mode = argv[1]; const bool baseline = argc == 3 && std::string(argv[2]) == "--baseline";
        const bool one = mode.find("present1") != std::string::npos;
        const bool retired = mode.find("retired") != std::string::npos;
        const bool stable = mode == "stable";
        const bool delegation = mode == "delegation";
        const bool unrecoverable = mode == "unrecoverable";
        const bool relative_relay = mode == "e9-relay";
        const bool cross_class = mode == "cross-class";
        const bool repatch = mode == "repatch";
        const bool dx12 = mode.starts_with("12-") || stable || delegation || cross_class;
        require(mode == "11-pending" || mode == "12-pending" || mode == "12-present1-pending" ||
            mode == "11-retired" || mode == "12-retired" || mode == "12-present1-retired" ||
            stable || delegation || unrecoverable || relative_relay || cross_class || repatch, "Unknown case");
        spdlog::set_pattern("[%l] %v");
        std::cout << "scope\tactual_production_hooks=true\tFramework=mutex_only_stub\tcontrolled_inline_overlay_model=true"
            "\tSteam_loaded=false\tgame_touched=false\tcase=" << mode << "\tbaseline=" << baseline << std::endl;
        Window window; auto chain = make_chain(window.hwnd, dx12);
        native_present = reinterpret_cast<Present>(*slot(chain.base.Get(), 8));
        native_present1 = reinterpret_cast<Present1>(*slot(chain.one.Get(), 22));
        const auto calibration_before = presents(chain);
        const auto native_result = native_present(chain.base.Get(), 0, 0);
        checked(native_result, "Unhooked native calibration");
        const auto count_before = presents(chain);
        const bool frame_counter = count_before == calibration_before + 1;
        const auto native_invalid = native_present(chain.base.Get(), 5, 0);
        require(FAILED(native_invalid), "Native invalid-interval calibration must fail");
        std::cout << "calibration\tnative_hr=0x" << std::hex << unsigned(native_result)
            << "\tinvalid_interval_hr=0x" << unsigned(native_invalid) << std::dec
            << "\tnative_present_count_before=" << calibration_before << "\tafter=" << count_before
            << "\tframe_counter_usable=" << frame_counter << std::endl;
        std::unique_ptr<PointerHook> unknown_original, delegation_original;
        if (unrecoverable) unknown_original = std::make_unique<PointerHook>(slot(chain.base.Get(), 8), reinterpret_cast<void*>(&overlay_present));
        if (delegation) delegation_original = std::make_unique<PointerHook>(slot(chain.one.Get(), 22), reinterpret_cast<void*>(&delegate_present1));
        std::unique_ptr<D3D11Hook> hook11;
        std::unique_ptr<D3D12Hook> hook12;
        if (!dx12 || stable) {
            hook11 = std::make_unique<D3D11Hook>(); callbacks(*hook11); require(hook11->hook(), "Install real DX11 hook");
        } else {
            hook12 = std::make_unique<D3D12Hook>(); callbacks(*hook12); require(hook12->hook(), "Install real DX12 hook");
        }
        if (cross_class) {
            cross_class_reentry = reinterpret_cast<Present>(*slot(chain.base.Get(), 8));
            require(cross_class_reentry != native_present, "DX12 callback was not installed on fixture chain");
            require(hook12->unhook(), "Retire DX12 before cross-class cached callback");
            hook11 = std::make_unique<D3D11Hook>(); callbacks(*hook11); require(hook11->hook(), "Install cross-class DX11 probe");
            require(*slot(chain.base.Get(), 8) != reinterpret_cast<void*>(cross_class_reentry) &&
                *slot(chain.base.Get(), 8) != reinterpret_cast<void*>(native_present), "Cross-class outer entry must be the new DX11 hook");
        }
        if (stable) {
            accept_window(window.hwnd);
            checked(chain.base->Present(0, DXGI_PRESENT_TEST), "Positively observe DX12 through11");
            require(framework11 == 1, "Need one verified DX11 dispatch observation");
            captured_present = reinterpret_cast<Present>(*slot(chain.base.Get(), 8));
            require(hook11->unhook(), "Retire observed DX11");
            hook12 = std::make_unique<D3D12Hook>(); callbacks(*hook12); require(hook12->hook(), "Install stable DX12");
            require(*slot(chain.base.Get(), 8) == reinterpret_cast<void*>(captured_present), "Verified DX12 did not choose working11 entry");
            framework11 = framework12 = post11 = post12 = 0;
        }
        captured_present = reinterpret_cast<Present>(*slot(chain.base.Get(), 8));
        captured_present1 = reinterpret_cast<Present1>(*slot(chain.one.Get(), 22));
        if (retired) {
            require(dx12 ? hook12->unhook() : hook11->unhook(), "Retire before cached dispatch");
        }
        auto invoke = [&](bool use_one, UINT interval = 0) {
            DXGI_PRESENT_PARAMETERS params{};
            return use_one ? captured_present1(chain.one.Get(), interval, 0, &params) : captured_present(chain.base.Get(), interval, 0);
        };
        if (delegation) {
            accept_window(window.hwnd);
            const auto hr = invoke(true); checked(hr, "Present1 delegating to actual Present");
            require(overlay_calls == 1, "Legal delegation must forward once");
            require(!frame_counter || presents(chain) == count_before + 1, "Legal cross-method delegation must reach native exactly once");
            require(framework12 == 1 && post12 == 1 && framework11 == 0, "Accepted legal delegation must notify renderer exactly once");
            require(*slot(chain.one.Get(), 22) == reinterpret_cast<void*>(captured_present1), "Delegation changed hooks");
            std::cout << "PASS\tlegal_Present1_to_Present\trenderer_pre_post_once=true\tno_recursion_recovery_required=true" << std::endl;
        } else if (unrecoverable) {
            const auto hr = invoke(false);
            if (baseline) require(hr == E_UNEXPECTED && overlay_calls == 33, "Baseline unbounded EXE original not reproduced");
            else require(hr == S_OK && overlay_calls == 1, "Unknown original must end the cycle once without a code patch");
            require(presents(chain) == count_before, "Failed original must not invent a real Present");
            std::cout << (baseline ? "REPRODUCED" : "PASS") << "\tunrecoverable_original\tnative_present_delta=0\toverlay_calls=" << overlay_calls << std::endl;
        } else {
            EntryPatch patch(one ? reinterpret_cast<void*>(native_present1) : reinterpret_cast<void*>(native_present),
                one ? reinterpret_cast<void*>(&overlay_present1) : reinterpret_cast<void*>(&overlay_present), relative_relay);
            const auto hr = invoke(one);
            if (baseline) {
                require(hr == E_UNEXPECTED && overlay_calls == 33 && presents(chain) == count_before,
                    "Baseline pending/retired forwarding loop was not reproduced");
                std::cout << "REPRODUCED\tunguarded_early_forward_cycle\tbounded_fixture_cap=33\tnative_present_delta=0" << std::endl;
            } else {
                checked(hr, "Recovered native Present");
                require(patch.restored(), "Production recovery did not restore the original DXGI entry");
                require(overlay_calls == 1, "Repaired entry must enter cyclic overlay only once");
                require(!frame_counter || presents(chain) == count_before + 1, "Recovery native frame count differs from calibrated original");
                if (stable) require(framework11 == 0 && framework12 == 1 && post12 == 1, "Bridge duplicated or lost renderer callbacks");
                else require(framework11 == 0 && framework12 == 0, "Pending/retired chain must not initialize a renderer");
                for (unsigned i = 0; i < 3; ++i) {
                    checked(invoke(one), "Continued native Present");
                    require(!frame_counter || presents(chain) == count_before + i + 2, "Later frame count differs from calibrated original");
                }
                require(invoke(one, 5) == native_invalid, "Native invalid-argument result must propagate; fabricated success is forbidden");
                std::cout << "PASS\trecovered_native_Present\tnative_frame_counter_usable=" << frame_counter << "\toverlay_calls=1\tcontinued_frames=3"
                    << "\tinvalid_argument_native_result=true\tentry_restored=true"
                    "\tframework11=" << framework11 << "\tframework12=" << framework12 << std::endl;
                if (relative_relay) std::cout << "PASS\tE9_to_allocated_near_relay\trelay_contains_FF25_abs64=true" << std::endl;
                if (cross_class) std::cout << "PASS\tDX11_outer_to_retired_DX12_inner\tsame_chain_and_method=true" << std::endl;
                if (repatch) {
#ifdef WUWA_GUARD_AVAILABLE
                    for (unsigned recovery = 2; recovery <= wuwa_present_guard::max_restores; ++recovery) {
#else
                    for (unsigned recovery = 2; recovery <= 4; ++recovery) {
#endif
                        patch.reapply();
                        const auto before_repair = presents(chain), before_overlay = overlay_calls;
                        checked(invoke(false), "Re-applied recognized entry recovery");
                        require(patch.restored() && overlay_calls == before_overlay + 1,
                            "Each permitted re-patch must restore once without recurring");
                        require(!frame_counter || presents(chain) == before_repair + 1,
                            "Re-patch recovery native count differs from calibrated original");
                        require(invoke(false, 5) == native_invalid, "Re-patch recovery must retain the actual native invalid-argument result");
                    }
                    patch.reapply();
                    const auto before_retry = presents(chain), before_overlay = overlay_calls;
                    const auto retry_result = invoke(false);
                    require(retry_result == S_OK && overlay_calls == before_overlay + 1,
                        "Exhausted restore budget must end the cycle once without recurring");
                    require(!patch.restored() && presents(chain) == before_retry, "Rejected re-patch must not pretend native recovery");
                    std::cout << "PASS\trepatch_after_success\tsuccessful_restorations=8"
                        "\tninth_restore_refused_bounded=true\tno_native_present_claim_after_refusal=true" << std::endl;
                }
            }
        }
        if (hook12) require(hook12->unhook(), "Unhook12");
        if (hook11) require(hook11->unhook(), "Unhook11");
        hook12.reset(); hook11.reset(); delegation_original.reset(); unknown_original.reset();
        require(*slot(chain.base.Get(), 8) == reinterpret_cast<void*>(native_present) &&
            *slot(chain.one.Get(), 22) == reinterpret_cast<void*>(native_present1), "Original slots not restored");
        require(!IsWindowVisible(window.hwnd), "Window became visible");
        if (!frame_counter) std::cout << "LIMIT\thidden_HWND_native_frame_counter_does_not_advance\tnative_entry_bytes_and_results_verified"
            "\texact_native_execution_count_not_measured\tcontrolled_overlay_model_not_Steam" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL_OR_UNSUPPORTED\t" << e.what() << std::endl;
        return 1;
    }
}
