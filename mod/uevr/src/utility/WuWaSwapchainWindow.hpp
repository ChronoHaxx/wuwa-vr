#pragma once

#include <array>
#include <cstddef>
#include <dxgi1_2.h>

namespace wuwa_swapchain_window {
struct Result {
    HWND window{};
    HWND hwnd{};
    HWND desc_hwnd{};
    HRESULT hwnd_result{E_PENDING};
    HRESULT desc_result{E_PENDING};
    bool desc_checked{};
    bool used_desc{};
};

// GetHwnd can be unavailable on a legacy/proxy swapchain even when the base
// interface describes an HWND. Never substitute a foreground/guessed window;
// callers still apply their normal WindowFilter to the resolved handle.
template <typename ReadHwnd, typename ReadDesc>
Result resolve_with(ReadHwnd read_hwnd, ReadDesc read_desc) {
    Result result{};
    result.hwnd_result = read_hwnd(&result.hwnd);
    if (SUCCEEDED(result.hwnd_result) && result.hwnd != nullptr) {
        result.window = result.hwnd;
        return result;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    result.desc_checked = true;
    result.desc_result = read_desc(&desc);
    result.desc_hwnd = desc.OutputWindow;
    if (SUCCEEDED(result.desc_result) && result.desc_hwnd != nullptr) {
        result.window = result.desc_hwnd;
        result.used_desc = true;
    }
    return result;
}

inline Result resolve(IDXGISwapChain* chain) {
    if (!chain) return {};
    return resolve_with([chain](HWND* hwnd) {
        // Present supplies the base interface. Do not assume its pointer also
        // exposes IDXGISwapChain1 slots when an overlay/proxy is involved.
        IDXGISwapChain1* extended{};
        const auto queried = chain->QueryInterface(IID_PPV_ARGS(&extended));
        if (FAILED(queried)) return queried;
        if (!extended) return E_NOINTERFACE;
        const auto result = extended->GetHwnd(hwnd);
        extended->Release();
        return result;
    }, [chain](DXGI_SWAP_CHAIN_DESC* desc) { return chain->GetDesc(desc); });
}

// Used under the hook-monitor mutex. Remember a small number of distinct
// decisions for this process instead of printing an error for every frame.
template <typename Key, std::size_t Capacity>
class DiagnosticBudget {
public:
    bool record(const Key& key) {
        for (std::size_t i = 0; i < m_count; ++i) {
            if (m_keys[i] == key) return false;
        }
        if (m_count == Capacity) return false;
        m_keys[m_count++] = key;
        return true;
    }
private:
    std::array<Key, Capacity> m_keys{};
    std::size_t m_count{};
};
}
