#pragma once

namespace wuwa_d3d_probe {
enum class Api { None, D3D11, D3D12 };

// Called only after the existing no-present timeout. A successful dummy-device
// hook is not evidence of the game's API. Once a real matching device/swapchain
// reaches our callback, preserve that renderer through later reset/recovery.
constexpr Api next_probe(Api active, bool initialized, bool real_present_seen) {
    if (active == Api::None) return Api::D3D12;
    if (initialized || real_present_seen) return active;
    return active == Api::D3D12 ? Api::D3D11 : Api::D3D12;
}
}
