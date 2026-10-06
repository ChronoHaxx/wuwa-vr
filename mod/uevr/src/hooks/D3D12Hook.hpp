#pragma once

#include <iostream>
#include <functional>
#include <cstdint>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi")

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>

#include "utility/PointerHook.hpp"
#include "utility/VtableHook.hpp"

class D3D12Hook
{
public:
	typedef std::function<void(D3D12Hook&)> OnPresentFn;
	typedef std::function<void(D3D12Hook&, uint32_t w, uint32_t h)> OnResizeBuffersFn;
    typedef std::function<void(D3D12Hook&, uint32_t w, uint32_t h)> OnResizeTargetFn;
    typedef std::function<void(D3D12Hook&)> OnCreateSwapChainFn;

	D3D12Hook() = default;
	virtual ~D3D12Hook();

	bool hook();
	bool unhook();

    bool is_hooked() {
        return m_hooked;
    }

    using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
    // nullopt means the retired caller must forward its original normally.
    static std::optional<HRESULT> present_from_d3d11(IDXGISwapChain3* observed,
        ID3D12Device4* proven_device, PresentFn original, UINT sync_interval, UINT flags);
    static std::optional<HRESULT> present_from_stable_d3d11(
        IDXGISwapChain* source, UINT sync_interval, UINT flags);

    void on_present(OnPresentFn fn) {
        m_on_present = fn;
    }

    void on_post_present(OnPresentFn fn) {
        m_on_post_present = fn;
    }

    void on_resize_buffers(OnResizeBuffersFn fn) {
        m_on_resize_buffers = fn;
    }

    void on_resize_target(OnResizeTargetFn fn) {
        m_on_resize_target = fn;
    }

    /*void on_create_swap_chain(OnCreateSwapChainFn fn) {
        m_on_create_swap_chain = fn;
    }*/

    ID3D12Device4* get_device() const {
        return m_device;
    }

    IDXGISwapChain3* get_swap_chain() const {
        return m_swap_chain;
    }

    auto get_swapchain_0() { return m_swapchain_0; }
    auto get_swapchain_1() { return m_swapchain_1; }

    ID3D12CommandQueue* get_command_queue() const {
        return m_command_queue;
    }

    UINT get_display_width() const {
        return m_display_width;
    }

    UINT get_display_height() const {
        return m_display_height;
    }

    UINT get_render_width() const {
        return m_render_width;
    }

    UINT get_render_height() const {
        return m_render_height;
    }

    bool is_inside_present() const {
        return m_inside_present;
    }

    bool is_proton_swapchain() const {
        return m_using_proton_swapchain;
    }

    bool is_framegen_swapchain() const {
        return m_using_frame_generation_swapchain;
    }

    void ignore_next_present() {
        m_ignore_next_present = true;
    }

    void set_next_present_interval(uint32_t interval) {
        m_next_present_interval = interval;
    }

protected:
    ID3D12Device4* m_device{ nullptr };
    IDXGISwapChain3* m_swap_chain{ nullptr };
    IDXGISwapChain3* m_swapchain_0{};
    IDXGISwapChain3* m_swapchain_1{};
    ID3D12CommandQueue* m_command_queue{ nullptr };
    UINT m_display_width{ NULL };
    UINT m_display_height{ NULL };
    UINT m_render_width{ NULL };
    UINT m_render_height{ NULL };

    uint32_t m_command_queue_offset{};
    uint32_t m_proton_swapchain_offset{};

    std::optional<uint32_t> m_next_present_interval{};

    bool m_using_proton_swapchain{ false };
    bool m_using_frame_generation_swapchain{ false };
    bool m_hooked{ false };
    bool m_is_phase_1{ true };
    bool m_inside_present{false};
    bool m_ignore_next_present{false};

    // Bootstrap evidence only; callers serialize through Framework's hook mutex.
    uint64_t m_probe_generation{};
    uint64_t m_probe_callbacks{}, m_probe_filtered{}, m_probe_selected{}, m_probe_other_instance{};
    uint64_t m_probe_device_queries{}, m_probe_device_ok{};
    unsigned m_probe_filtered_logs{}, m_probe_selected_logs{}, m_probe_other_logs{}, m_probe_resize_logs{};
    void** m_probe_present_slot{};
    void** m_probe_present1_slot{};
    void* m_present_destination{};
    uint64_t m_probe_raw_entry_start{}, m_probe_raw_entry1_start{};
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_dispatch_swapchain{};
    Microsoft::WRL::ComPtr<ID3D12Device4> m_dispatch_device{};
    PresentFn m_dispatch_original{};
    bool m_bridge_attempted{};
    uint64_t m_probe_bridge_calls{};
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_bridge_swapchain{};
    Microsoft::WRL::ComPtr<ID3D12Device4> m_bridge_device{};
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_bridge_queue{};

    std::unique_ptr<PointerHook> m_present_hook{};
    std::unique_ptr<PointerHook> m_present1_hook{};
    std::unique_ptr<VtableHook> m_swapchain_hook{};
    //std::unique_ptr<FunctionHook> m_create_swap_chain_hook{};

    OnPresentFn m_on_present{ nullptr };
    OnPresentFn m_on_post_present{ nullptr };
    OnResizeBuffersFn m_on_resize_buffers{ nullptr };
    OnResizeTargetFn m_on_resize_target{ nullptr };
    //OnCreateSwapChainFn m_on_create_swap_chain{ nullptr };
    
    static HRESULT present_internal(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params, bool present1 = false, PresentFn original_override = nullptr);
    // A queued callback can outlive its hook object. Resolve only this exact
    // chain's selected original, or its restored live vtable after retirement.
    static void* resize_original(IDXGISwapChain3* swap_chain, unsigned index);
    static bool is_selected_resize_chain(IDXGISwapChain3* swap_chain);

    static HRESULT WINAPI present(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags);
    static HRESULT WINAPI present1(IDXGISwapChain3* swap_chain, UINT sync_interval, UINT flags, DXGI_PRESENT_PARAMETERS* params);
    static HRESULT WINAPI resize_buffers(IDXGISwapChain3* swap_chain, UINT buffer_count, UINT width, UINT height, DXGI_FORMAT new_format, UINT swap_chain_flags);
    static HRESULT WINAPI resize_target(IDXGISwapChain3* swap_chain, const DXGI_MODE_DESC* new_target_parameters);
    //static HRESULT WINAPI create_swap_chain(IDXGIFactory4* factory, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p_fullscreen_desc, IDXGIOutput* p_restrict_to_output, IDXGISwapChain** swap_chain);
};

