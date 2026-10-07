#include <thread>
#include <atomic>
#include <future>
#include <unordered_set>
#include <tuple>

#include <spdlog/spdlog.h>
#include <utility/Thread.hpp>
#include <utility/Module.hpp>
#include <utility/RTTI.hpp>

#include "WindowFilter.hpp"
#include "Framework.hpp"

#include "D3D12Hook.hpp"
#include "D3D11Hook.hpp"
#include "utility/WuWaSwapchainWindow.hpp"
#include "utility/WuWaPresentGuard.hpp"

static D3D12Hook* g_d3d12_hook = nullptr;

namespace {
std::atomic_uint64_t g_d3d12_probe_generation{};
std::atomic_uint64_t g_d3d12_raw_entries{}, g_d3d12_raw_entries1{};
thread_local bool g_d3d11_bridge_present{};

// The queue offset is discovered on the dummy, not trusted for another object.
// This guard probes only that one candidate. No memory scan or fallback offset.
HRESULT guarded_queue_info(IUnknown* candidate, ID3D12CommandQueue** queue,
    IUnknown** owner, D3D12_COMMAND_QUEUE_DESC* desc) {
    __try {
        auto result = candidate->QueryInterface(IID_PPV_ARGS(queue));
        if (FAILED(result) || *queue == nullptr) return FAILED(result) ? result : E_NOINTERFACE;
        result = (*queue)->GetDevice(IID_PPV_ARGS(owner));
        if (FAILED(result) || *owner == nullptr) return FAILED(result) ? result : E_NOINTERFACE;
        *desc = (*queue)->GetDesc();
        return S_OK;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return E_FAIL;
    }
}

bool read_own_pointer(const void* address, void** result) {
    SIZE_T bytes{};
    return address != nullptr && ReadProcessMemory(GetCurrentProcess(), address,
        result, sizeof(*result), &bytes) && bytes == sizeof(*result);
}

// All Present/Resize callers hold Framework's hook-monitor mutex.
void log_window_probe(uint64_t generation, unsigned& budget, const char* stage, IDXGISwapChain3* chain,
    wuwa_swapchain_window::Result window, bool present1, bool phase1,
    const void* selected = nullptr) {
    if (budget >= 3) return;
    ++budget;
    // Read both HWND descriptions for a bounded number of events. This copy is
    // diagnostic only: successful GetHwnd still selects exactly the same HWND.
    if (!window.desc_checked) {
        DXGI_SWAP_CHAIN_DESC desc{};
        window.desc_result = chain->GetDesc(&desc);
        window.desc_hwnd = desc.OutputWindow;
        window.desc_checked = true;
    }
    spdlog::info("[WuWaD3DWindow] api=12 probe={} stage={} chain={:x} present1={} phase1={} hwnd_hr={:x} hwnd={:x} desc_checked={} desc_hr={:x} desc_hwnd={:x} resolved={:x} fallback={} selected={:x}",
        generation, stage, (uintptr_t)chain, present1, phase1, (uint32_t)window.hwnd_result,
        (uintptr_t)window.hwnd, window.desc_checked, (uint32_t)window.desc_result,
        (uintptr_t)window.desc_hwnd, (uintptr_t)window.window, window.used_desc, (uintptr_t)selected);
}
}

D3D12Hook::~D3D12Hook() {
    unhook();
    if (g_d3d12_hook == this) g_d3d12_hook = nullptr;
}

bool D3D12Hook::hook() {
    spdlog::info("Hooking D3D12");

    m_probe_generation = ++g_d3d12_probe_generation;
    m_probe_callbacks = m_probe_filtered = m_probe_selected = m_probe_other_instance = 0;
    m_probe_device_queries = m_probe_device_ok = 0;
    m_probe_filtered_logs = m_probe_selected_logs = m_probe_other_logs = m_probe_resize_logs = 0;
    m_probe_present_slot = m_probe_present1_slot = nullptr;
    m_present_destination = reinterpret_cast<void*>(&D3D12Hook::present);
    m_probe_raw_entry_start = g_d3d12_raw_entries.load(std::memory_order_relaxed);
    m_probe_raw_entry1_start = g_d3d12_raw_entries1.load(std::memory_order_relaxed);
    m_dispatch_swapchain.Reset();
    m_dispatch_device.Reset();
    m_dispatch_original = nullptr;
    m_bridge_attempted = false;
    m_probe_bridge_calls = 0;
    m_bridge_swapchain.Reset();
    m_bridge_device.Reset();
    m_bridge_queue.Reset();

    g_d3d12_hook = this;

    IDXGISwapChain1* swap_chain1{ nullptr };
    IDXGISwapChain3* swap_chain{ nullptr };
    ID3D12Device* device{ nullptr };

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc1;

    ZeroMemory(&swap_chain_desc1, sizeof(swap_chain_desc1));

    swap_chain_desc1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_chain_desc1.BufferCount = 2;
    swap_chain_desc1.SampleDesc.Count = 1;
    swap_chain_desc1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swap_chain_desc1.Width = 1;
    swap_chain_desc1.Height = 1;

    // Manually get D3D12CreateDevice export because the user may be running Windows 7
    const auto d3d12_module = LoadLibraryA("d3d12.dll");
    if (d3d12_module == nullptr) {
        spdlog::error("Failed to load d3d12.dll");
        return false;
    }

    auto d3d12_create_device = (decltype(D3D12CreateDevice)*)GetProcAddress(d3d12_module, "D3D12CreateDevice");
    if (d3d12_create_device == nullptr) {
        spdlog::error("Failed to get D3D12CreateDevice export");
        return false;
    }

    spdlog::info("Creating dummy device");

    // Get the original on-disk bytes of the D3D12CreateDevice export
    const auto original_bytes = utility::get_original_bytes(d3d12_create_device);

    // Temporarily unhook D3D12CreateDevice
    // it allows compatibility with ReShade and other overlays that hook it
    // this is just a dummy device anyways, we don't want the other overlays to be able to use it
    if (original_bytes) {
        spdlog::info("D3D12CreateDevice appears to be hooked, temporarily unhooking");

        std::vector<uint8_t> hooked_bytes(original_bytes->size());
        memcpy(hooked_bytes.data(), d3d12_create_device, original_bytes->size());

        ProtectionOverride protection_override{ d3d12_create_device, original_bytes->size(), PAGE_EXECUTE_READWRITE };
        memcpy(d3d12_create_device, original_bytes->data(), original_bytes->size());
        
        if (FAILED(d3d12_create_device(nullptr, feature_level, IID_PPV_ARGS(&device)))) {
            spdlog::error("Failed to create D3D12 Dummy device");
            memcpy(d3d12_create_device, hooked_bytes.data(), hooked_bytes.size());
            return false;
        }

        spdlog::info("Restoring hooked bytes for D3D12CreateDevice");
        memcpy(d3d12_create_device, hooked_bytes.data(), hooked_bytes.size());
    } else { // D3D12CreateDevice is not hooked
        if (FAILED(d3d12_create_device(nullptr, feature_level, IID_PPV_ARGS(&device)))) {
            spdlog::error("Failed to create D3D12 Dummy device");
            return false;
        }
    }

    spdlog::info("Dummy device: {:x}", (uintptr_t)device);

    // Manually get CreateDXGIFactory export because the user may be running Windows 7
    const auto dxgi_module = LoadLibraryA("dxgi.dll");
    if (dxgi_module == nullptr) {
        spdlog::error("Failed to load dxgi.dll");
        return false;
    }

    auto create_dxgi_factory = (decltype(CreateDXGIFactory)*)GetProcAddress(dxgi_module, "CreateDXGIFactory");

    if (create_dxgi_factory == nullptr) {
        spdlog::error("Failed to get CreateDXGIFactory export");
        return false;
    }

    spdlog::info("Creating dummy DXGI factory");

    IDXGIFactory4* factory{ nullptr };
    if (FAILED(create_dxgi_factory(IID_PPV_ARGS(&factory)))) {
        spdlog::error("Failed to create D3D12 Dummy DXGI Factory");
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = 0;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0;

    spdlog::info("Creating dummy command queue");

    ID3D12CommandQueue* command_queue{ nullptr };
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)))) {
        spdlog::error("Failed to create D3D12 Dummy Command Queue");
        return false;
    }

    spdlog::info("Creating dummy swapchain");

    // used in CreateSwapChainForHwnd fallback
    HWND hwnd = 0;
    WNDCLASSEX wc{};

    auto init_dummy_window = [&]() {
        // fallback to CreateSwapChainForHwnd
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DefWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hIcon = NULL;
        wc.hCursor = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = TEXT("REFRAMEWORK_DX12_DUMMY");
        wc.hIconSm = NULL;

        ::RegisterClassEx(&wc);

        hwnd = ::CreateWindow(wc.lpszClassName, TEXT("REF DX Dummy Window"), WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

        swap_chain_desc1.BufferCount = 3;
        swap_chain_desc1.Width = 0;
        swap_chain_desc1.Height = 0;
        swap_chain_desc1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_desc1.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        swap_chain_desc1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc1.SampleDesc.Count = 1;
        swap_chain_desc1.SampleDesc.Quality = 0;
        swap_chain_desc1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc1.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        swap_chain_desc1.Scaling = DXGI_SCALING_STRETCH;
        swap_chain_desc1.Stereo = FALSE;
    };

    std::vector<std::function<bool ()>> swapchain_attempts{
        // we call CreateSwapChainForComposition instead of CreateSwapChainForHwnd
        // because some overlays will have hooks on CreateSwapChainForHwnd
        // and all we're doing is creating a dummy swapchain
        // we don't want to screw up the overlay
        [&]() {
            return !FAILED(factory->CreateSwapChainForComposition(command_queue, &swap_chain_desc1, nullptr, &swap_chain1));
        },
        [&]() {
            init_dummy_window();

            return !FAILED(factory->CreateSwapChainForHwnd(command_queue, hwnd, &swap_chain_desc1, nullptr, nullptr, &swap_chain1));
        },
        [&]() {
            return !FAILED(factory->CreateSwapChainForHwnd(command_queue, GetDesktopWindow(), &swap_chain_desc1, nullptr, nullptr, &swap_chain1));
        },
    };

    bool any_succeed = false;

    for (auto i = 0; i < swapchain_attempts.size(); i++) {
        auto& attempt = swapchain_attempts[i];
        
        try {
            spdlog::info("Trying swapchain attempt {}", i);

            if (attempt()) {
                spdlog::info("Created dummy swapchain on attempt {}", i);
                any_succeed = true;
                break;
            }
        } catch (std::exception& e) {
            spdlog::error("Failed to create dummy swapchain on attempt {}: {}", i, e.what());
        } catch(...) {
            spdlog::error("Failed to create dummy swapchain on attempt {}: unknown exception", i);
        }

        spdlog::error("Attempt {} failed", i);
    }

    if (!any_succeed) {
        spdlog::error("Failed to create D3D12 Dummy Swap Chain");

        if (hwnd) {
            ::DestroyWindow(hwnd);
        }

        if (wc.lpszClassName != nullptr) {
            ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        }

        return false;
    }

    spdlog::info("Querying dummy swapchain");

    if (FAILED(swap_chain1->QueryInterface(IID_PPV_ARGS(&swap_chain)))) {
        spdlog::error("Failed to retrieve D3D12 DXGI SwapChain");
        return false;
    }

    try {
        const auto ti = utility::rtti::get_type_info(swap_chain1);
        const auto swapchain_classname = ti != nullptr && ti->name() != nullptr ? std::string_view{ti->name()} : "unknown";
        const auto raw_name = ti != nullptr && ti->raw_name() != nullptr ? std::string_view{ti->raw_name()} : "unknown";

        spdlog::info("Swapchain type info: {}", swapchain_classname);
        spdlog::info("Swapchain raw type info: {}", raw_name);
        
        if (swapchain_classname.contains("interposer::DXGISwapChain")) { // DLSS3
            spdlog::info("Found Streamline (DLSSFG) swapchain during dummy initialization: {:x}", (uintptr_t)swap_chain1);
            m_using_frame_generation_swapchain = true;
        }
        // Need to test this one to see if it actually has the same issues - disabling it for now
        /*else if (swapchain_classname.contains("FrameInterpolationSwapChain")) { // FSR3
            spdlog::info("Found FSR3 swapchain during dummy initialization: {:x}", (uintptr_t)swap_chain1);
            m_using_frame_generation_swapchain = true;
        }*/
    } catch (const std::exception& e) {
        spdlog::error("Failed to get type info: {}", e.what());
    } catch (...) {
        spdlog::error("Failed to get type info: unknown exception");
    }

    spdlog::info("Finding command queue offset");

    m_command_queue_offset = 0;

    // Find the command queue offset in the swapchain
    for (auto i = 0; i < 512 * sizeof(void*); i += sizeof(void*)) {
        const auto base = (uintptr_t)swap_chain1 + i;

        // reached the end
        if (IsBadReadPtr((void*)base, sizeof(void*))) {
            break;
        }

        auto data = *(ID3D12CommandQueue**)base;

        if (data == command_queue) {
            m_command_queue_offset = i;
            spdlog::info("Found command queue offset: {:x}", i);
            break;
        }
    }

    auto target_swapchain = swap_chain;

    // Scan throughout the swapchain for a valid pointer to scan through
    // this is usually only necessary for Proton
    if (m_command_queue_offset == 0) {
        bool should_break = false;

        for (auto base = 0; base < 512 * sizeof(void*); base += sizeof(void*)) {
            const auto pre_scan_base = (uintptr_t)swap_chain1 + base;

            // reached the end
            if (IsBadReadPtr((void*)pre_scan_base, sizeof(void*))) {
                break;
            }

            const auto scan_base = *(uintptr_t*)pre_scan_base;

            if (scan_base == 0 || IsBadReadPtr((void*)scan_base, sizeof(void*))) {
                continue;
            }

            for (auto i = 0; i < 512 * sizeof(void*); i += sizeof(void*)) {
                const auto pre_data = scan_base + i;

                if (IsBadReadPtr((void*)pre_data, sizeof(void*))) {
                    break;
                }

                auto data = *(ID3D12CommandQueue**)pre_data;

                if (data == command_queue) {
                    // If we hook Streamline's Swapchain, the menu fails to render correctly/flickers
                    // So we switch out the swapchain with the internal one owned by Streamline
                    // Side note: Even though we are scanning for Proton here,
                    // this doubles as an offset scanner for the real swapchain inside Streamline (or FSR3)
                    if (m_using_frame_generation_swapchain) {
                        target_swapchain = (IDXGISwapChain3*)scan_base;
                    }

                    if (!m_using_frame_generation_swapchain) {
                        m_using_proton_swapchain = true;
                    }

                    m_command_queue_offset = i;
                    m_proton_swapchain_offset = base;
                    should_break = true;

                    spdlog::info("Proton potentially detected");
                    spdlog::info("Found command queue offset: {:x}", i);
                    break;
                }
            }

            if (m_using_proton_swapchain || should_break) {
                break;
            }
        }
    }

    if (m_command_queue_offset == 0) {
        spdlog::error("Failed to find command queue offset");
        return false;
    }

    try {
        spdlog::info("Initializing hooks");
        m_present_hook.reset();
        m_present1_hook.reset();
        m_swapchain_hook.reset();

        m_is_phase_1 = true;

        auto& present_fn = (*(void***)target_swapchain)[8]; // Present
        auto& present1_fn = (*(void***)target_swapchain)[22]; // Present1
        m_probe_present_slot = &present_fn;
        m_probe_present1_slot = &present1_fn;
        const char* dispatch_reason = "unsupported_queue_layout";
        // Preserve the existing native route for layouts the guarded bridge
        // cannot validate; selecting a guaranteed-refused bridge would stall it.
        if (!m_using_proton_swapchain && !m_using_frame_generation_swapchain &&
            m_command_queue_offset > 0 && m_command_queue_offset <= 0x1000) {
            if (auto dispatch = D3D11Hook::verified_dx12_dispatch(&present_fn, present_fn, dispatch_reason)) {
                m_present_destination = reinterpret_cast<void*>(dispatch->entry);
                m_dispatch_swapchain = std::move(dispatch->chain);
                m_dispatch_device = std::move(dispatch->device);
                m_dispatch_original = dispatch->original;
            }
        }
        spdlog::info("[WuWaD3DDispatch] api=12 probe={} stage=install route={} reason={} slot={:x} destination={:x} original={:x} source={:x}",
            m_probe_generation, m_dispatch_swapchain ? "verified_dx11_thunk" : "native_dx12_thunk",
            dispatch_reason, (uintptr_t)&present_fn, (uintptr_t)m_present_destination, (uintptr_t)present_fn,
            (uintptr_t)m_dispatch_swapchain.Get());
        m_present_hook = std::make_unique<PointerHook>(&present_fn, m_present_destination);
        m_present1_hook = std::make_unique<PointerHook>(&present1_fn, (void*)&D3D12Hook::present1);
        wuwa_present_guard::remember(m_present_hook->get_original<void*>(), false);
        wuwa_present_guard::remember(m_present1_hook->get_original<void*>(), true);
        m_hooked = true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize hooks: {}", e.what());
        m_hooked = false;
    }

    device->Release();
    command_queue->Release();
    factory->Release();
    swap_chain1->Release();
    swap_chain->Release();

    if (hwnd) {
        ::DestroyWindow(hwnd);
    }

    if (wc.lpszClassName != nullptr) {
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
    }

    return m_hooked;
}

bool D3D12Hook::unhook() {
    if (!m_hooked) {
        return true;
    }

    spdlog::info("Unhooking D3D12");

    void* slot_value{};
    void* slot1_value{};
    SIZE_T bytes{};
    const bool slot_read = m_probe_present_slot != nullptr && ReadProcessMemory(GetCurrentProcess(),
        m_probe_present_slot, &slot_value, sizeof(slot_value), &bytes) && bytes == sizeof(slot_value);
    bytes = 0;
    const bool slot1_read = m_probe_present1_slot != nullptr && ReadProcessMemory(GetCurrentProcess(),
        m_probe_present1_slot, &slot1_value, sizeof(slot1_value), &bytes) && bytes == sizeof(slot1_value);
    spdlog::info("[WuWaD3DProbeSummary] api=12 probe={} callbacks={} filtered={} selected={} other_instance={} device_queries={} device_ok={} slot={:x} slot_read={} slot_value={:x} slot_owned={} original={:x} slot1={:x} slot1_read={} slot1_value={:x} slot1_owned={} original1={:x} bridge_calls={} destination={:x} raw_entries={} raw_entries1={}",
        m_probe_generation, m_probe_callbacks, m_probe_filtered, m_probe_selected, m_probe_other_instance,
        m_probe_device_queries, m_probe_device_ok, (uintptr_t)m_probe_present_slot, slot_read,
        (uintptr_t)slot_value, slot_read && slot_value == m_present_destination,
        (uintptr_t)(m_present_hook ? m_present_hook->get_original<void*>() : nullptr),
        (uintptr_t)m_probe_present1_slot, slot1_read, (uintptr_t)slot1_value,
        slot1_read && slot1_value == (void*)&D3D12Hook::present1,
        (uintptr_t)(m_present1_hook ? m_present1_hook->get_original<void*>() : nullptr), m_probe_bridge_calls,
        (uintptr_t)m_present_destination,
        g_d3d12_raw_entries.load(std::memory_order_relaxed) - m_probe_raw_entry_start,
        g_d3d12_raw_entries1.load(std::memory_order_relaxed) - m_probe_raw_entry1_start);

    // A callback may already have entered and be waiting for Framework's
    // hook-monitor mutex. Restore the slots but retain their original targets
    // until this hook object is replaced; retired callbacks forward only.
    const bool present_removed = !m_present_hook || m_present_hook->remove();
    const bool present1_removed = !m_present1_hook || m_present1_hook->remove();
    if (!present_removed || !present1_removed) return false;
    if (m_swapchain_hook) m_swapchain_hook->remove();

    m_hooked = false;
    m_is_phase_1 = true;

    return true;
}

thread_local int32_t g_present_depth = 0;

std::optional<HRESULT> D3D12Hook::present_from_stable_d3d11(
    IDXGISwapChain* source, UINT sync_interval, UINT flags) {
    // Called only with Framework's hook mutex held. A different source retains
    // ordinary original forwarding; it never inherits the verified API/queue.
    auto d3d12 = g_d3d12_hook;
    if (d3d12 == nullptr || !d3d12->m_hooked || source != d3d12->m_dispatch_swapchain.Get() ||
        d3d12->m_dispatch_original == nullptr) return std::nullopt;
    const auto original = d3d12->m_dispatch_original;
    const auto result = present_from_d3d11(d3d12->m_dispatch_swapchain.Get(), d3d12->m_dispatch_device.Get(),
        original, sync_interval, flags);
    // Queue validation/reentry refusal still belongs to this exact source. Do
    // not fall through to a newer DX11 probe's potentially different original.
    if (result.has_value()) return *result;
    return wuwa_present_guard::forward(original, false,
        [&](auto target) { return target(source, sync_interval, flags); });
}

std::optional<HRESULT> D3D12Hook::present_from_d3d11(IDXGISwapChain3* observed,
    ID3D12Device4* proven_device, PresentFn original, UINT sync_interval, UINT flags) {
    // The retired DX11 callback already owns Framework's recursive mutex.
    auto d3d12 = g_d3d12_hook;
    if (d3d12 == nullptr || !d3d12->m_hooked || observed == nullptr || proven_device == nullptr ||
        original == nullptr || g_d3d11_bridge_present) return std::nullopt;

    if (!d3d12->m_bridge_attempted) {
        d3d12->m_bridge_attempted = true;
        auto rejected = [&](const char* reason, HRESULT hr = E_FAIL) {
            spdlog::info("[WuWaD3DBridge] stage=rejected probe={} chain={:x} reason={} hr={:x}",
                d3d12->m_probe_generation, (uintptr_t)observed, reason, (uint32_t)hr);
        };
        if (d3d12->m_swap_chain != nullptr && d3d12->m_swap_chain != observed) {
            rejected("different_selected_chain");
            return std::nullopt;
        }
        if (d3d12->m_using_proton_swapchain || d3d12->m_using_frame_generation_swapchain ||
            d3d12->m_command_queue_offset == 0 || d3d12->m_command_queue_offset > 0x1000) {
            rejected("unsupported_queue_layout");
            return std::nullopt;
        }
        void* candidate{};
        if (!read_own_pointer(reinterpret_cast<const char*>(observed) + d3d12->m_command_queue_offset, &candidate) || candidate == nullptr) {
            rejected("queue_pointer_unreadable");
            return std::nullopt;
        }
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<IUnknown> owner, expected;
        D3D12_COMMAND_QUEUE_DESC desc{};
        const auto queue_result = guarded_queue_info(static_cast<IUnknown*>(candidate), queue.GetAddressOf(), owner.GetAddressOf(), &desc);
        if (FAILED(queue_result) || queue == nullptr || owner == nullptr) {
            rejected("queue_interface_unverified", queue_result);
            return std::nullopt;
        }
        const auto expected_result = proven_device->QueryInterface(IID_PPV_ARGS(&expected));
        if (FAILED(expected_result) || expected == nullptr || expected.Get() != owner.Get()) {
            rejected("queue_device_mismatch", expected_result);
            return std::nullopt;
        }
        if (desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
            rejected("queue_not_direct");
            return std::nullopt;
        }
        d3d12->m_bridge_swapchain = observed;
        d3d12->m_bridge_device = proven_device;
        d3d12->m_bridge_queue = std::move(queue);

        void* table{};
        void* slot{};
        void* slot1{};
        const bool table_read = read_own_pointer(observed, &table);
        const bool slot_read = table_read && table != nullptr && read_own_pointer(static_cast<void**>(table) + 8, &slot);
        const bool slot1_read = table_read && table != nullptr && read_own_pointer(static_cast<void**>(table) + 22, &slot1);
        spdlog::info("[WuWaD3DBridge] stage=accepted probe={} chain={:x} device={:x} queue={:x} queue_offset={:x} table_read={} table={:x} slot_read={} actual_present={:x} slot1_read={} actual_present1={:x} original={:x}",
            d3d12->m_probe_generation, (uintptr_t)observed, (uintptr_t)proven_device,
            (uintptr_t)d3d12->m_bridge_queue.Get(), d3d12->m_command_queue_offset, table_read, (uintptr_t)table,
            slot_read, (uintptr_t)slot, slot1_read, (uintptr_t)slot1, (uintptr_t)original);
    }

    if (d3d12->m_bridge_swapchain.Get() != observed || d3d12->m_bridge_device.Get() != proven_device ||
        d3d12->m_bridge_queue == nullptr) return std::nullopt;
    struct BridgeScope {
        BridgeScope() { g_d3d11_bridge_present = true; }
        ~BridgeScope() { g_d3d11_bridge_present = false; }
    } scope;
    ++d3d12->m_probe_bridge_calls;
    return present_internal(observed, sync_interval, flags, nullptr, false, original);
}

HRESULT D3D12Hook::present_internal(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params, bool present1, PresentFn original_override) {
    auto d3d12 = g_d3d12_hook;

    using Present1Fn = HRESULT(*)(IDXGISwapChain3*, UINT, UINT, DXGI_PRESENT_PARAMETERS*);
    Present1Fn present_fn{nullptr};

    if (original_override != nullptr) {
        present_fn = reinterpret_cast<Present1Fn>(original_override);
    } else if (!present1) {
        present_fn = d3d12->m_present_hook->get_original<Present1Fn>();
    } else {
        present_fn = d3d12->m_present1_hook->get_original<Present1Fn>();
    }

    auto call_original = [&] {
        return original_override != nullptr ? original_override(swap_chain, sync_interval, flags) :
            present_fn(swap_chain, sync_interval, flags, params);
    };
    // Early returns forward through the loop guard (see WuWaPresentGuard).
    auto forward_original = [&] {
        void* const fn = original_override != nullptr ? reinterpret_cast<void*>(original_override) :
            reinterpret_cast<void*>(present_fn);
        return wuwa_present_guard::forward(fn, present1, [&](void* target) -> HRESULT {
            if (target == fn) return call_original();
            return reinterpret_cast<Present1Fn>(target)(swap_chain, sync_interval, flags, params);
        });
    };

    if (!d3d12->m_hooked) {
        return forward_original();
    }

    ++d3d12->m_probe_callbacks;

    const auto window = wuwa_swapchain_window::resolve(swap_chain);
    if (d3d12->m_is_phase_1 && WindowFilter::get().is_filtered(window.window)) {
        ++d3d12->m_probe_filtered;
        log_window_probe(d3d12->m_probe_generation, d3d12->m_probe_filtered_logs, "filtered", swap_chain, window, present1, true);
        return forward_original();
    }

    if (!d3d12->m_is_phase_1 && swap_chain != d3d12->m_swapchain_hook->get_instance()) {
        const auto og_instance = d3d12->m_swapchain_hook->get_instance();

        // If the original swapchain instance is invalid, then we should not proceed, and rehook the swapchain
        if (IsBadReadPtr(og_instance, sizeof(void*)) || IsBadReadPtr(og_instance.deref(), sizeof(void*))) {
            spdlog::error("Bad read pointer for original swapchain instance, re-hooking");
            d3d12->m_is_phase_1 = true;
        }

        if (!d3d12->m_is_phase_1) {
            ++d3d12->m_probe_other_instance;
            log_window_probe(d3d12->m_probe_generation, d3d12->m_probe_other_logs, "other_instance", swap_chain, window, present1, false, og_instance.as<void*>());
            return forward_original();
        }
    }

    if (d3d12->m_is_phase_1) {
        ++d3d12->m_probe_selected;
        log_window_probe(d3d12->m_probe_generation, d3d12->m_probe_selected_logs, "selected", swap_chain, window, present1, true);
        //d3d12->m_present_hook.reset();
        d3d12->m_swapchain_hook.reset();

        // vtable hook the swapchain instead of global hooking
        // this seems safer for whatever reason
        // if we globally hook the vtable pointers, it causes all sorts of weird conflicts with other hooks
        // dont hook present though via this hook so other hooks dont get confused
        d3d12->m_swapchain_hook = std::make_unique<VtableHook>(swap_chain);
        //d3d12->m_swapchain_hook->hook_method(8, (uintptr_t)&D3D12Hook::present);
        d3d12->m_swapchain_hook->hook_method(13, (uintptr_t)&D3D12Hook::resize_buffers);
        d3d12->m_swapchain_hook->hook_method(14, (uintptr_t)&D3D12Hook::resize_target);
        d3d12->m_is_phase_1 = false;
    }

    d3d12->m_inside_present = true;
    d3d12->m_swap_chain = swap_chain;

    const auto device_result = swap_chain->GetDevice(IID_PPV_ARGS(&d3d12->m_device));
    ++d3d12->m_probe_device_queries;
    if (SUCCEEDED(device_result) && d3d12->m_device != nullptr) ++d3d12->m_probe_device_ok;
    if (d3d12->m_probe_device_queries <= 3) {
        spdlog::info("[WuWaD3DDevice] api=12 probe={} chain={:x} hr={:x} device={:x} command_queue_offset={:x}",
            d3d12->m_probe_generation, (uintptr_t)swap_chain, (uint32_t)device_result,
            (uintptr_t)d3d12->m_device, d3d12->m_command_queue_offset);
    }

    if (d3d12->m_device != nullptr) {
        if (d3d12->m_bridge_swapchain.Get() == swap_chain && d3d12->m_bridge_queue != nullptr) {
            d3d12->m_command_queue = d3d12->m_bridge_queue.Get();
        } else if (d3d12->m_using_proton_swapchain) {
            const auto real_swapchain = *(uintptr_t*)((uintptr_t)swap_chain + d3d12->m_proton_swapchain_offset);
            d3d12->m_command_queue = *(ID3D12CommandQueue**)(real_swapchain + d3d12->m_command_queue_offset);
        } else {
            d3d12->m_command_queue = *(ID3D12CommandQueue**)((uintptr_t)swap_chain + d3d12->m_command_queue_offset);
        }
    }

    if (d3d12->m_swapchain_0 == nullptr) {
        d3d12->m_swapchain_0 = swap_chain;
    } else if (d3d12->m_swapchain_1 == nullptr && swap_chain != d3d12->m_swapchain_0) {
        d3d12->m_swapchain_1 = swap_chain;
    }
    
    // Restore the original bytes
    // if an infinite loop occurs, this will prevent the game from crashing
    // while keeping our hook intact
    if (g_present_depth > 0) {
        auto original_bytes = utility::get_original_bytes(Address{present_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{present_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(present_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Present fixed");
        }

        if ((uintptr_t)present_fn != (uintptr_t)D3D12Hook::present && g_present_depth == 1) {
            spdlog::info("Attempting to call real present function");

            ++g_present_depth;
            const auto result = [&] { wuwa_present_guard::Scope forwarding; return call_original(); }();
            --g_present_depth;

            if (result != S_OK) {
                spdlog::error("Present failed: {:x}", result);
            }

            return result;
        }

        spdlog::info("Just returning S_OK");
        return S_OK;
    }

    if (d3d12->m_on_present) {
        d3d12->m_on_present(*d3d12);

        if (d3d12->m_next_present_interval) {
            sync_interval = *d3d12->m_next_present_interval;
            d3d12->m_next_present_interval = std::nullopt;

            if (sync_interval == 0) {
                BOOL is_fullscreen = 0;
                swap_chain->GetFullscreenState(&is_fullscreen, nullptr);
                flags &= ~DXGI_PRESENT_DO_NOT_SEQUENCE;

                DXGI_SWAP_CHAIN_DESC swap_desc{};
                swap_chain->GetDesc(&swap_desc);

                if (!is_fullscreen && (swap_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                    flags |= DXGI_PRESENT_ALLOW_TEARING;
                }
            }
        }
    }

    ++g_present_depth;

    auto result = S_OK;
    
    if (!d3d12->m_ignore_next_present) {
        wuwa_present_guard::Scope forwarding;  // lets the early paths see a loop
        result = call_original();

        if (result != S_OK) {
            spdlog::error("Present failed: {:x}", result);
        }
    } else {
        d3d12->m_ignore_next_present = false;
    }

    --g_present_depth;

    if (d3d12->m_on_post_present) {
        d3d12->m_on_post_present(*d3d12);
    }

    d3d12->m_inside_present = false;

    return result;
}

HRESULT WINAPI D3D12Hook::present(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags) {
    g_d3d12_raw_entries.fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};
    
    return D3D12Hook::present_internal(swap_chain, sync_interval, flags, nullptr, false);
}

HRESULT WINAPI D3D12Hook::present1(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params) {
    g_d3d12_raw_entries1.fetch_add(1, std::memory_order_relaxed);
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    return D3D12Hook::present_internal(swap_chain, sync_interval, flags, params, true);
}

void* D3D12Hook::resize_original(IDXGISwapChain3* swap_chain, unsigned index) {
    const auto current = g_d3d12_hook;
    if (current != nullptr && current->m_swapchain_hook != nullptr &&
        current->m_swapchain_hook->get_instance().as<void*>() == swap_chain) {
        return current->m_swapchain_hook->get_method<void*>(index);
    }

    // unhook() restores the per-instance table before the old object is
    // destroyed. A cached/queued entry may then run with no selected instance
    // (or a different one) in the replacement. Do not borrow that instance's
    // originals or require its first Present to have happened.
    void* table{};
    void* original{};
    if (!read_own_pointer(swap_chain, &table) || table == nullptr ||
        !read_own_pointer(static_cast<void**>(table) + index, &original)) return nullptr;
    return original;
}

bool D3D12Hook::is_selected_resize_chain(IDXGISwapChain3* swap_chain) {
    const auto current = g_d3d12_hook;
    return current != nullptr && current->m_hooked && !current->m_is_phase_1 &&
        current->m_swap_chain == swap_chain && current->m_swapchain_hook != nullptr &&
        current->m_swapchain_hook->get_instance().as<void*>() == swap_chain;
}

thread_local int32_t g_resize_buffers_depth = 0;

HRESULT WINAPI D3D12Hook::resize_buffers(IDXGISwapChain3* swap_chain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    spdlog::info("D3D12 resize buffers called");
    spdlog::info(" Parameters: buffer_count {} width {} height {} new_format {} swap_chain_flags {}", buffer_count, width, height, (uint32_t)new_format, swap_chain_flags);

    const auto resize_buffers_fn = reinterpret_cast<decltype(D3D12Hook::resize_buffers)*>(resize_original(swap_chain, 13));
    // Never recurse into our own stale/private table when no original remains.
    if (resize_buffers_fn == nullptr || resize_buffers_fn == &D3D12Hook::resize_buffers) return DXGI_ERROR_INVALID_CALL;
    if (!is_selected_resize_chain(swap_chain)) {
        return resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
    }
    auto d3d12 = g_d3d12_hook;

    const auto window = wuwa_swapchain_window::resolve(swap_chain);
    if (WindowFilter::get().is_filtered(window.window)) {
        log_window_probe(d3d12->m_probe_generation, d3d12->m_probe_resize_logs, "resize_buffers_filtered", swap_chain, window, false, d3d12->m_is_phase_1);
        return resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
    }

    d3d12->m_display_width = width;
    d3d12->m_display_height = height;

    if (g_resize_buffers_depth > 0) {
        auto original_bytes = utility::get_original_bytes(Address{resize_buffers_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{resize_buffers_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(resize_buffers_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Resize buffers fixed");
        }

        if ((uintptr_t)resize_buffers_fn != (uintptr_t)&D3D12Hook::resize_buffers && g_resize_buffers_depth == 1) {
            spdlog::info("Attempting to call the real resize buffers function");

            ++g_resize_buffers_depth;
            const auto result = resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
            --g_resize_buffers_depth;

            if (result != S_OK) {
                spdlog::error("Resize buffers failed: {:x}", result);
            }

            return result;
        } else {
            spdlog::info("Just returning S_OK");
            return S_OK;
        }
    }

    if (d3d12->m_on_resize_buffers) {
        d3d12->m_on_resize_buffers(*d3d12, width, height);
    }

    ++g_resize_buffers_depth;

    const auto result = resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
    
    if (result != S_OK) {
        spdlog::error("Resize buffers failed: {:x}", result);
    }

    --g_resize_buffers_depth;

    return result;
}

thread_local int32_t g_resize_target_depth = 0;

HRESULT WINAPI D3D12Hook::resize_target(IDXGISwapChain3* swap_chain, const DXGI_MODE_DESC* new_target_parameters) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    spdlog::info("D3D12 resize target called");
    spdlog::info(" Parameters: new_target_parameters {:x}", (uintptr_t)new_target_parameters);

    const auto resize_target_fn = reinterpret_cast<decltype(D3D12Hook::resize_target)*>(resize_original(swap_chain, 14));
    if (resize_target_fn == nullptr || resize_target_fn == &D3D12Hook::resize_target) return DXGI_ERROR_INVALID_CALL;
    if (!is_selected_resize_chain(swap_chain) || new_target_parameters == nullptr) {
        return resize_target_fn(swap_chain, new_target_parameters);
    }
    auto d3d12 = g_d3d12_hook;

    const auto window = wuwa_swapchain_window::resolve(swap_chain);
    if (WindowFilter::get().is_filtered(window.window)) {
        log_window_probe(d3d12->m_probe_generation, d3d12->m_probe_resize_logs, "resize_target_filtered", swap_chain, window, false, d3d12->m_is_phase_1);
        return resize_target_fn(swap_chain, new_target_parameters);
    }

    d3d12->m_render_width = new_target_parameters->Width;
    d3d12->m_render_height = new_target_parameters->Height;

    // Restore the original code to the resize_buffers function.
    if (g_resize_target_depth > 0) {
        auto original_bytes = utility::get_original_bytes(Address{resize_target_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{resize_target_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(resize_target_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Resize target fixed");
        }

        if ((uintptr_t)resize_target_fn != (uintptr_t)&D3D12Hook::resize_target && g_resize_target_depth == 1) {
            spdlog::info("Attempting to call the real resize target function");

            ++g_resize_target_depth;
            const auto result = resize_target_fn(swap_chain, new_target_parameters);
            --g_resize_target_depth;

            if (result != S_OK) {
                spdlog::error("Resize target failed: {:x}", result);
            }

            return result;
        } else {
            spdlog::info("Just returning S_OK");
            return S_OK;
        }
    }

    if (d3d12->m_on_resize_target) {
        d3d12->m_on_resize_target(*d3d12, new_target_parameters->Width, new_target_parameters->Height);
    }

    ++g_resize_target_depth;

    const auto result = resize_target_fn(swap_chain, new_target_parameters);
    
    if (result != S_OK) {
        spdlog::error("Resize target failed: {:x}", result);
    }

    --g_resize_target_depth;

    return result;
}

/*HRESULT WINAPI D3D12Hook::create_swap_chain(IDXGIFactory4* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p_fullscreen_desc, IDXGIOutput* p_restrict_to_output, IDXGISwapChain** swap_chain)
{
    spdlog::info("D3D12 create swapchain called");

    auto d3d12 = g_d3d12_hook;

    d3d12->m_command_queue = (ID3D12CommandQueue*)device;
    
    if (d3d12->m_on_create_swap_chain) {
        d3d12->m_on_create_swap_chain(*d3d12);
    }

    auto create_swap_chain_fn = d3d12->m_create_swap_chain_hook->get_original<decltype(D3D12Hook::create_swap_chain)>();

    return create_swap_chain_fn(factory, device, hwnd, desc, p_fullscreen_desc, p_restrict_to_output, swap_chain);
}*/

