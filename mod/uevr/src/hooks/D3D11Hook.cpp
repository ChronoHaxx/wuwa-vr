#include <algorithm>
#include <atomic>
#include <spdlog/spdlog.h>
#include <utility/Thread.hpp>
#include <utility/Module.hpp>

#include "WindowFilter.hpp"
#include "Framework.hpp"

#include "D3D11Hook.hpp"
#include "D3D12Hook.hpp"
#include "utility/WuWaSwapchainWindow.hpp"

using namespace std;

static D3D11Hook* g_d3d11_hook = nullptr;

namespace {
std::atomic_uint64_t g_d3d11_probe_generation{};
D3D11Hook::PresentFn g_retired_d3d11_present{};
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// Covers every path, including filtered/retired forwarding and callbacks made
// by another overlay. No allocation, code patching, or guessed success result.
struct ResizeCall {
    IDXGISwapChain* chain;
    ResizeCall* previous;
    static thread_local ResizeCall* current;
    static bool contains(IDXGISwapChain* chain) {
        unsigned depth{};
        for (auto call = current; call != nullptr; call = call->previous) {
            if (call->chain == chain || ++depth >= 16) return true;
        }
        return false;
    }
    explicit ResizeCall(IDXGISwapChain* source) : chain{source}, previous{current} { current = this; }
    ~ResizeCall() { current = previous; }
};
thread_local ResizeCall* ResizeCall::current{};

ResizeBuffersFn read_resize_original(IDXGISwapChain* chain) {
    void** table{};
    ResizeBuffersFn original{};
    SIZE_T bytes{};
    if (chain == nullptr || !ReadProcessMemory(GetCurrentProcess(), chain, &table, sizeof(table), &bytes) ||
        bytes != sizeof(table) || table == nullptr) return nullptr;
    bytes = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), table + 13, &original, sizeof(original), &bytes) ||
        bytes != sizeof(original)) return nullptr;
    return original;
}

void log_probe_window(uint64_t generation, const char* stage, IDXGISwapChain* chain,
    HRESULT desc_result, HWND desc_hwnd) {
    // Additional interface queries are limited to the first three events of
    // each outcome in a probe. They never change the GetDesc-based selection.
    const auto window = wuwa_swapchain_window::resolve(chain);
    spdlog::info("[WuWaD3DWindow] api=11 probe={} stage={} chain={:x} hwnd_hr={:x} hwnd={:x} desc_hr={:x} desc_hwnd={:x} resolved={:x}",
        generation, stage, (uintptr_t)chain, (uint32_t)window.hwnd_result,
        (uintptr_t)window.hwnd, (uint32_t)desc_result, (uintptr_t)desc_hwnd, (uintptr_t)desc_hwnd);
}
}

D3D11Hook::~D3D11Hook() {
    unhook();
    if (g_d3d11_hook == this) g_d3d11_hook = nullptr;
}

bool D3D11Hook::hook() {
    spdlog::info("Hooking D3D11");

    m_probe_generation = ++g_d3d11_probe_generation;
    m_probe_callbacks = m_probe_filtered = m_probe_selected = 0;
    m_probe_device_queries = m_probe_device_ok = 0;
    m_probe_present_slot = nullptr;
    m_observed_dx12_chain.Reset();
    m_observed_dx12_device.Reset();
    m_dx12_next_source_probe = {};
    m_dx12_source_logs = 0;

    g_d3d11_hook = this;

    HWND h_wnd = GetDesktopWindow();
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC swap_chain_desc;

    ZeroMemory(&swap_chain_desc, sizeof(swap_chain_desc));

    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.BufferCount = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.OutputWindow = h_wnd;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const auto original_bytes = utility::get_original_bytes(&D3D11CreateDeviceAndSwapChain);

    // Temporarily unhook D3D11CreateDeviceAndSwapChain
    // it allows compatibility with ReShade and other overlays that hook it
    // this is just a dummy device anyways, we don't want the other overlays to be able to use it
    if (original_bytes) {
        spdlog::info("D3D11CreateDeviceAndSwapChain appears to be hooked, temporarily unhooking");

        std::vector<uint8_t> hooked_bytes(original_bytes->size());
        memcpy(hooked_bytes.data(), &D3D11CreateDeviceAndSwapChain, original_bytes->size());

        ProtectionOverride protection_override{ &D3D11CreateDeviceAndSwapChain, original_bytes->size(), PAGE_EXECUTE_READWRITE };
        memcpy(&D3D11CreateDeviceAndSwapChain, original_bytes->data(), original_bytes->size());
        
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_NULL, nullptr, 0, &feature_level, 1, D3D11_SDK_VERSION,
                &swap_chain_desc, &swap_chain, &device, nullptr, &context))) 
        {
            spdlog::error("Failed to create D3D11 device");
            memcpy(&D3D11CreateDeviceAndSwapChain, hooked_bytes.data(), hooked_bytes.size());
            return false;
        }
        
        spdlog::info("Restoring hooked bytes for D3D11CreateDeviceAndSwapChain");
        memcpy(&D3D11CreateDeviceAndSwapChain, hooked_bytes.data(), hooked_bytes.size());
    } else {
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_NULL, nullptr, 0, &feature_level, 1, D3D11_SDK_VERSION,
                &swap_chain_desc, &swap_chain, &device, nullptr, &context))) 
        {
            spdlog::error("Failed to create D3D11 device");
            return false;
        }
    }

    try {
        m_present_hook.reset();
        m_resize_buffers_hook.reset();
        m_resize_swapchain.Reset();
        m_original_resize_buffers = nullptr;

        auto& present_fn = (*(void***)swap_chain)[8];

        m_probe_present_slot = &present_fn;

        m_present_hook = std::make_unique<PointerHook>(&present_fn, (void*)&D3D11Hook::present);
        m_original_present = m_present_hook->get_original<PresentFn>();
        g_retired_d3d11_present = m_original_present;
        m_hooked = true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to hook D3D11: {}", e.what());
        m_hooked = false;
    }

    device->Release();
    context->Release();
    swap_chain->Release();
    return m_hooked;
}

bool D3D11Hook::unhook() {
    if (!m_hooked) {
        return true;
    }

    spdlog::info("Unhooking D3D11");

    void* slot_value{};
    SIZE_T bytes{};
    const bool slot_read = m_probe_present_slot != nullptr && ReadProcessMemory(GetCurrentProcess(),
        m_probe_present_slot, &slot_value, sizeof(slot_value), &bytes) && bytes == sizeof(slot_value);
    spdlog::info("[WuWaD3DProbeSummary] api=11 probe={} callbacks={} filtered={} accepted={} device_queries={} device_ok={} slot={:x} slot_read={} slot_value={:x} slot_owned={} original={:x}",
        m_probe_generation, m_probe_callbacks, m_probe_filtered, m_probe_selected,
        m_probe_device_queries, m_probe_device_ok, (uintptr_t)m_probe_present_slot, slot_read,
        (uintptr_t)slot_value, slot_read && slot_value == (void*)&D3D11Hook::present,
        (uintptr_t)m_original_present);

    // Restore the private table before retiring the global Present slot.
    // Keep its immutable original and COM identity until object replacement so
    // an already dispatched resize can forward without touching the renderer.
    if (m_resize_buffers_hook) m_resize_buffers_hook->remove();
    if (m_present_hook->remove()) {
        // The next DX12 probe may deliberately install this same thunk. Destroy
        // the removed owner now, before a newer hook can own that destination.
        // A queued callback uses m_original_present, not this hook object.
        m_present_hook.reset();
        m_hooked = false;
        return true;
    }

    return false;
}

std::optional<D3D11Hook::VerifiedDx12Dispatch> D3D11Hook::verified_dx12_dispatch(
    void** candidate_slot, void* candidate_original, const char*& reason) {
    auto old = g_d3d11_hook;
    reason = "no_retired_dx11_probe";
    if (old == nullptr || old->m_hooked || old->m_present_hook != nullptr) return std::nullopt;
    reason = "no_positive_dx12_source";
    if (old->m_observed_dx12_chain == nullptr || old->m_observed_dx12_device == nullptr) return std::nullopt;
    reason = "original_mismatch";
    if (candidate_original == nullptr || candidate_original != reinterpret_cast<void*>(old->m_original_present)) return std::nullopt;
    void** actual_table{};
    void* actual_original{};
    SIZE_T bytes{};
    reason = "actual_table_unreadable";
    if (!ReadProcessMemory(GetCurrentProcess(), old->m_observed_dx12_chain.Get(),
        &actual_table, sizeof(actual_table), &bytes) || bytes != sizeof(actual_table) || actual_table == nullptr) return std::nullopt;
    reason = "actual_slot_differs";
    if (actual_table + 8 != candidate_slot) return std::nullopt;
    bytes = 0;
    reason = "actual_original_changed";
    if (!ReadProcessMemory(GetCurrentProcess(), candidate_slot, &actual_original, sizeof(actual_original), &bytes) ||
        bytes != sizeof(actual_original) || actual_original != candidate_original) return std::nullopt;
    reason = "positive_dx12_same_slot_original";
    return VerifiedDx12Dispatch{&D3D11Hook::present, old->m_original_present,
        old->m_observed_dx12_chain, old->m_observed_dx12_device};
}

thread_local bool g_inside_d3d11_present = false;
HRESULT last_d3d11_present_result = S_OK;

HRESULT WINAPI D3D11Hook::present(IDXGISwapChain* swap_chain, UINT sync_interval, UINT flags) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    // DX12 owns the installed slot in this mode. Its retained source identity
    // survives destruction of the retired DX11 probe; there is no DX11 frame.
    if (const auto result = D3D12Hook::present_from_stable_d3d11(swap_chain, sync_interval, flags)) return *result;

    auto d3d11 = g_d3d11_hook;

    // This line must be called before calling our detour function because we might have to unhook the function inside our detour.
    auto present_fn = d3d11 != nullptr && d3d11->m_original_present != nullptr ?
        d3d11->m_original_present : g_retired_d3d11_present;
    if (present_fn == nullptr) return DXGI_ERROR_INVALID_CALL;
    if (d3d11 == nullptr) return present_fn(swap_chain, sync_interval, flags);

    // Unhook restores the slot but keeps the original callable for an already
    // dispatched callback that waited while Framework switched API probes.
    if (!d3d11->m_hooked) {
        if (swap_chain == d3d11->m_observed_dx12_chain.Get()) {
            const auto bridged = D3D12Hook::present_from_d3d11(d3d11->m_observed_dx12_chain.Get(),
                d3d11->m_observed_dx12_device.Get(), present_fn, sync_interval, flags);
            if (bridged.has_value()) return *bridged;
        }
        return present_fn(swap_chain, sync_interval, flags);
    }

    ++d3d11->m_probe_callbacks;

    DXGI_SWAP_CHAIN_DESC swap_desc{};
    const auto desc_result = swap_chain->GetDesc(&swap_desc);

    if (WindowFilter::get().is_filtered(swap_desc.OutputWindow)) {
        if (++d3d11->m_probe_filtered <= 3) {
            log_probe_window(d3d11->m_probe_generation, "filtered", swap_chain, desc_result, swap_desc.OutputWindow);
        }
        return present_fn(swap_chain, sync_interval, flags);
    }

    if (++d3d11->m_probe_selected <= 3) {
        log_probe_window(d3d11->m_probe_generation, "accepted", swap_chain, desc_result, swap_desc.OutputWindow);
        void** table{};
        void* actual_present{};
        SIZE_T bytes{};
        const bool table_read = ReadProcessMemory(GetCurrentProcess(), swap_chain, &table, sizeof(table), &bytes) && bytes == sizeof(table);
        bytes = 0;
        const bool slot_read = table_read && table != nullptr && ReadProcessMemory(GetCurrentProcess(),
            table + 8, &actual_present, sizeof(actual_present), &bytes) && bytes == sizeof(actual_present);
        spdlog::info("[WuWaD3DDispatch] api=11 probe={} chain={:x} table_read={} table={:x} actual_slot={:x} actual_read={} actual_present={:x} dummy_slot={:x}",
            d3d11->m_probe_generation, (uintptr_t)swap_chain, table_read, (uintptr_t)table,
            table_read && table ? (uintptr_t)(table + 8) : 0, slot_read, (uintptr_t)actual_present,
            (uintptr_t)d3d11->m_probe_present_slot);
    }

    d3d11->m_inside_present = true;

    if (d3d11->m_swapchain_0 == nullptr) {
        d3d11->m_swapchain_0 = swap_chain;
        d3d11->m_swap_chain = swap_chain;
    } else if (d3d11->m_swapchain_1 == nullptr && swap_chain != d3d11->m_swapchain_0) {
        d3d11->m_swapchain_1 = swap_chain;
    }

    /*if (d3d11->m_swap_chain != d3d11->m_swapchain_0) {
        d3d11->m_inside_present = false;
        return present_fn(swap_chain, sync_interval, flags);
    }*/

    const auto device_result = swap_chain->GetDevice(__uuidof(d3d11->m_device), (void**)&d3d11->m_device);
    ++d3d11->m_probe_device_queries;
    if (SUCCEEDED(device_result) && d3d11->m_device != nullptr) ++d3d11->m_probe_device_ok;
    if (d3d11->m_probe_device_queries <= 3) {
        spdlog::info("[WuWaD3DDevice] api=11 probe={} chain={:x} hr={:x} device={:x}",
            d3d11->m_probe_generation, (uintptr_t)swap_chain, (uint32_t)device_result, (uintptr_t)d3d11->m_device);
    }

    if (SUCCEEDED(device_result) && d3d11->m_device != nullptr &&
        swap_chain == d3d11->m_swap_chain && d3d11->m_resize_buffers_hook == nullptr) {
        d3d11->install_resize_hook(swap_chain);
    }

    if (FAILED(device_result) && d3d11->m_observed_dx12_chain == nullptr &&
        std::chrono::steady_clock::now() >= d3d11->m_dx12_next_source_probe) {
        // Limit failed capability queries, not the number of future chains.
        d3d11->m_dx12_next_source_probe = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
        ComPtr<ID3D12Device4> device12;
        ComPtr<IDXGISwapChain3> chain3;
        const auto device12_result = swap_chain->GetDevice(IID_PPV_ARGS(&device12));
        const auto chain3_result = swap_chain->QueryInterface(IID_PPV_ARGS(&chain3));
        const bool accepted = SUCCEEDED(device12_result) && device12 != nullptr &&
            SUCCEEDED(chain3_result) && chain3 != nullptr && chain3.Get() == swap_chain;
        if (accepted) {
            d3d11->m_observed_dx12_chain = std::move(chain3);
            d3d11->m_observed_dx12_device = std::move(device12);
        }
        if (accepted || d3d11->m_dx12_source_logs < 3) {
            ++d3d11->m_dx12_source_logs;
            spdlog::info("[WuWaD3DBridge] stage=observe probe={} chain={:x} device12_hr={:x} chain3_hr={:x} same_interface={} accepted={}",
                d3d11->m_probe_generation, (uintptr_t)swap_chain, (uint32_t)device12_result,
                (uint32_t)chain3_result, accepted || chain3.Get() == swap_chain, accepted);
        }
    }

    /*if (d3d11->m_set_render_targets_hook == nullptr) {
        ComPtr<ID3D11DeviceContext> context{};

        d3d11->m_device->GetImmediateContext(&context);
        auto& set_render_targets_fn = (*(void***)context.Get())[33];
        d3d11->m_set_render_targets_hook = std::make_unique<PointerHook>(&set_render_targets_fn, (void*)&set_render_targets);
        OutputDebugString("Hooked ID3D11DeviceContext::SetRenderTargets");
    }*/

    /*if (GetAsyncKeyState(VK_INSERT) & 1) {
        OutputDebugString(fmt::format("Depth stencil @ {:p} used", (void*)d3d11->m_last_depthstencil_used.Get()).c_str());
    }*/

    // Restore the original bytes
    // if an infinite loop occurs, this will prevent the game from crashing
    // while keeping our hook intact
    if (g_inside_d3d11_present) {
        auto original_bytes = utility::get_original_bytes(Address{present_fn});

        if (original_bytes) {
            ProtectionOverride protection_override{present_fn, original_bytes->size(), PAGE_EXECUTE_READWRITE};

            memcpy(present_fn, original_bytes->data(), original_bytes->size());

            spdlog::info("Present fixed");
        }

        return last_d3d11_present_result;
    }

    if (d3d11->m_on_present) {
        d3d11->m_on_present(*d3d11);

        if (d3d11->m_next_present_interval) {
            sync_interval = *d3d11->m_next_present_interval;
            d3d11->m_next_present_interval = std::nullopt;

            if (sync_interval == 0) {
                BOOL is_fullscreen = 0;
                swap_chain->GetFullscreenState(&is_fullscreen, nullptr);
                flags &= ~DXGI_PRESENT_DO_NOT_SEQUENCE;

                if (!is_fullscreen && (swap_desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) {
                    flags |= DXGI_PRESENT_ALLOW_TEARING;
                }
            }
        }
    }

    HRESULT result = S_OK;
    g_inside_d3d11_present = true;

    if (!d3d11->m_ignore_next_present) {
        result = present_fn(swap_chain, sync_interval, flags);
        last_d3d11_present_result = result;
    } else {
        d3d11->m_ignore_next_present = false;
        last_d3d11_present_result = S_OK;
    }

    g_inside_d3d11_present = false;

    if (d3d11->m_on_post_present) {
        d3d11->m_on_post_present(*d3d11);
    }

    d3d11->m_last_depthstencil_used.Reset();
    d3d11->m_inside_present = false;

    return result;
}

void D3D11Hook::install_resize_hook(IDXGISwapChain* swap_chain) {
    const auto original = read_resize_original(swap_chain);
    if (original == nullptr || original == &D3D11Hook::resize_buffers) return;
    // VtableHook::get_method reads the mutable old table. Save the callable now
    // so a later overlay hook cannot silently change our forward destination.
    m_resize_swapchain = swap_chain;
    m_original_resize_buffers = original;
    m_resize_buffers_hook = std::make_unique<VtableHook>(swap_chain);
    if (!m_resize_buffers_hook->hook_method(13, (uintptr_t)&D3D11Hook::resize_buffers)) {
        m_resize_buffers_hook.reset();
        m_original_resize_buffers = nullptr;
        m_resize_swapchain.Reset();
        return;
    }
    spdlog::info("[WuWaD3DResize] api=11 stage=installed chain={:x} original={:x} scope=verified_instance",
        (uintptr_t)swap_chain, (uintptr_t)original);
}

HRESULT WINAPI D3D11Hook::resize_buffers(
    IDXGISwapChain* swap_chain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags) {
    if (swap_chain == nullptr || ResizeCall::contains(swap_chain)) return DXGI_ERROR_INVALID_CALL;
    ResizeCall resize_call{swap_chain};
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    auto d3d11 = g_d3d11_hook;
    const bool owns_chain = d3d11 != nullptr && d3d11->m_resize_swapchain.Get() == swap_chain;
    const auto resize_buffers_fn = owns_chain ? d3d11->m_original_resize_buffers : read_resize_original(swap_chain);
    if (resize_buffers_fn == nullptr || resize_buffers_fn == &D3D11Hook::resize_buffers) return DXGI_ERROR_INVALID_CALL;

    if (!owns_chain || !d3d11->m_hooked) {
        return resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
    }

    DXGI_SWAP_CHAIN_DESC swap_desc{};
    swap_chain->GetDesc(&swap_desc);

    if (WindowFilter::get().is_filtered(swap_desc.OutputWindow)) {
        return resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
    }

    d3d11->m_swap_chain = swap_chain;
    d3d11->m_swapchain_0 = nullptr;
    d3d11->m_swapchain_1 = nullptr;
    d3d11->m_last_depthstencil_used.Reset();

    if (d3d11->m_on_resize_buffers) {
        d3d11->m_on_resize_buffers(*d3d11, width, height);
    }

    return resize_buffers_fn(swap_chain, buffer_count, width, height, new_format, swap_chain_flags);
}

void WINAPI D3D11Hook::set_render_targets(
    ID3D11DeviceContext* context, UINT num_views, ID3D11RenderTargetView* const* rtvs, ID3D11DepthStencilView* dsv) {
    std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

    auto d3d11 = g_d3d11_hook;

    if (dsv != nullptr) {
        //auto obj_name = fmt::format("Depthstencil @ {:p}", (void*)d3d11->m_last_depthstencil_used.Get());
        //d3d11->m_last_depthstencil_used->SetPrivateData(WKPDID_D3DDebugObjectName, obj_name.size(), obj_name.c_str());
        //OutputDebugString(fmt::format("Depth stencil @ {:p} used", (void*)d3d11->m_last_depthstencil_used.Get()).c_str());

        D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
        dsv->GetDesc(&desc);

        if (desc.Flags & D3D11_DSV_FLAG::D3D11_DSV_READ_ONLY_DEPTH) {
            dsv->GetResource((ID3D11Resource**)d3d11->m_last_depthstencil_used.GetAddressOf());

            //OutputDebugString(fmt::format("Flags: {}", desc.Flags).c_str());
            //OutputDebugString(fmt::format("Format: {}", desc.Format).c_str());
            //OutputDebugString(fmt::format("ViewDimension: {}", desc.ViewDimension).c_str());   
        }
    }

    auto set_render_targets_fn = d3d11->m_set_render_targets_hook->get_original<decltype(set_render_targets)*>();

    return set_render_targets_fn(context, num_views, rtvs, dsv);
}
