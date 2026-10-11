#pragma once

// WuWa test 3k: reads the scene depth for close-up match (see
// utility/WuWaDepthProbe.hpp). About 16 times a second, while the camera script
// asks, a sprite batch shrinks the part of the eye's depth around the 2D
// frame's centre into a 32 x 32 float target, which is read back once its
// fence completes (a frame or two later). The depth stays in the engine's own
// end-of-frame state (readable by pixel shaders); nothing the game draws is
// touched. Present thread only.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <d3d12.h>
#include <DirectXColors.h>
#include <wrl/client.h>
#include <spdlog/spdlog.h>
#include <utility/Logging.hpp>

#include <../../directxtk12-src/Inc/SpriteBatch.h>

#include "CommandContext.hpp"
#include "TextureContext.hpp"
#include "../../../utility/WuWaDepthProbe.hpp"

namespace wuwa_depth {
class Sampler {
public:
    static constexpr UINT side = 32;
    static constexpr UINT row_pitch = 256; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
    // Where to look. The view sits at the top left of the depth target.
    struct View {
        float eye_w{}, eye_h{};                  // one view's size in pixels
        float left{}, right{}, top{}, bottom{};  // tangents of the eye's edges (left, bottom negative)
        float x{}, y{}, z{};                     // the 2D camera's forward in the eye (x right, y up, -z ahead)
        float near_cm{};                         // near plane (reversed, infinite depth: z = near / distance)
    };

    // The batch draws into DXGI_FORMAT_R32_FLOAT (D3D12Component owns it).
    void update(ID3D12Device* device, ID3D12Resource* depth, const View& view, DirectX::DX12::SpriteBatch* batch) {
        using namespace wuwa_depth_probe;
        const auto now = GetTickCount64();
        if (m_pending) {
            if (!m_cmd.fence || m_cmd.fence->GetCompletedValue() < m_cmd.fence_value) return;
            m_cmd.wait(0);
            m_pending = false;
            read(now);
        }
        if (!wanted()) {
            // Never keep the engine's depth target (or a stale one) alive while idle.
            if (m_source.texture && now - m_last > 3000) m_source.reset();
            if (state.load() == Sampling) state.store(Idle);
            return;
        }
        // Fast enough that a cut is measured within about 0.1 s.
        if (now - m_last < 60 || device == nullptr || batch == nullptr) return;
        m_last = now;
        if (depth == nullptr) { state.store(NoDepth); return; }
        if (!ensure(device, depth)) { state.store(Unsupported); return; }

        // Region: the middle 35% of the 2D frame (16:9), around where its
        // centre lands in this eye; the head may have turned away from it.
        const float ahead = -view.z;
        const float across = view.right - view.left, down = view.top - view.bottom;
        if (ahead < 0.3f || across <= 0.0f || down <= 0.0f || view.near_cm <= 0.0f) { state.store(OutOfView); return; }
        const float cx = view.x / ahead, cy = view.y / ahead;
        const float hx = std::tan(fov.load() * 3.14159265f / 360.0f) * 0.35f, hy = hx * 9.0f / 16.0f;
        const float u0 = (cx - hx - view.left) / across, u1 = (cx + hx - view.left) / across;
        const float v0 = (view.top - (cy + hy)) / down, v1 = (view.top - (cy - hy)) / down;
        const float cu0 = std::clamp(u0, 0.0f, 1.0f), cu1 = std::clamp(u1, 0.0f, 1.0f);
        const float cv0 = std::clamp(v0, 0.0f, 1.0f), cv1 = std::clamp(v1, 0.0f, 1.0f);
        if ((cu1 - cu0) * (cv1 - cv0) < 0.25f * (u1 - u0) * (v1 - v0)) { state.store(OutOfView); return; }
        const auto desc = depth->GetDesc();
        RECT source{LONG(cu0 * view.eye_w), LONG(cv0 * view.eye_h), LONG(cu1 * view.eye_w), LONG(cv1 * view.eye_h)};
        source.right = std::clamp<LONG>(std::max<LONG>(source.right, source.left + 2), 2, LONG(desc.Width));
        source.bottom = std::clamp<LONG>(std::max<LONG>(source.bottom, source.top + 2), 2, LONG(desc.Height));
        m_view = view;
        m_region = {(cu0 + cu1) * 0.5f, (cv0 + cv1) * 0.5f};

        if (!m_cmd.ready()) {
            m_cmd.wait(0);
            if (!m_cmd.ready()) return;
        }
        auto* list = m_cmd.cmd_list.Get();
        ID3D12DescriptorHeap* heaps[] = {m_source.srv_heap->Heap()};
        list->SetDescriptorHeaps(1, heaps);
        const auto rtv = m_target.get_rtv();
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{0.0f, 0.0f, float(side), float(side), 0.0f, 1.0f};
        const D3D12_RECT scissor{0, 0, LONG(side), LONG(side)};
        list->RSSetViewports(1, &viewport);
        list->RSSetScissorRects(1, &scissor);
        batch->SetViewport(viewport);
        batch->Begin(list, DirectX::DX12::SpriteSortMode::SpriteSortMode_Immediate);
        batch->Draw(m_source.get_srv_gpu(), DirectX::XMUINT2{uint32_t(desc.Width), uint32_t(desc.Height)},
            RECT{0, 0, LONG(side), LONG(side)}, &source, DirectX::Colors::White);
        batch->End();
        barrier(list, m_target.texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{m_target.texture.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
        from.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION to{m_readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
        to.PlacedFootprint.Footprint = {DXGI_FORMAT_R32_FLOAT, side, side, 1, row_pitch};
        list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        barrier(list, m_target.texture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_cmd.has_commands = true;
        m_pending = m_cmd.execute();
        if (m_pending && state.load() != Sampling) state.store(Sampling);
    }

private:
    static void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES from,
        D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.StateBefore = from;
        b.Transition.StateAfter = to;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        list->ResourceBarrier(1, &b);
    }

    // A depth target is read through a typeless format's depth plane only.
    static DXGI_FORMAT depth_view(DXGI_FORMAT format) {
        switch (format) {
        case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }

    bool ensure(ID3D12Device* device, ID3D12Resource* depth) {
        if (!m_cmd.cmd_list && !m_cmd.setup(L"WuWa depth probe")) return false;
        if (!m_target.texture) {
            D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = side; desc.Height = side; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R32_FLOAT; desc.SampleDesc.Count = 1;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            ComPtr<ID3D12Resource> texture;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture)))) return false;
            D3D12_HEAP_PROPERTIES readback_heap{D3D12_HEAP_TYPE_READBACK};
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = row_pitch * side; buffer.Height = 1; buffer.DepthOrArraySize = 1; buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(device->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE, &buffer,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readback)))) return false;
            if (!m_target.setup(device, texture.Get(), DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_FLOAT, L"WuWa depth probe target"))
                return false;
        }
        if (m_source.texture.Get() != depth) {
            m_source.reset();
            const auto desc = depth->GetDesc();
            m_format = depth_view(desc.Format);
            if (m_format == DXGI_FORMAT_UNKNOWN || (desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
                desc.SampleDesc.Count != 1) {
                SPDLOG_WARNING_EVERY_N_SEC(10, "[WuWaDepth] scene depth not readable: format={} flags={} samples={}",
                    (int)desc.Format, (int)desc.Flags, desc.SampleDesc.Count);
                return false;
            }
            // Only a shader view: an RTV on a depth target would be invalid.
            m_source.texture = depth;
            if (!m_source.create_srv(device, m_format)) { m_source.reset(); return false; }
            spdlog::info("[WuWaDepth] reading scene depth {}x{} format {}", desc.Width, desc.Height, (int)desc.Format);
        }
        return true;
    }

    void read(uint64_t now) {
        using namespace wuwa_depth_probe;
        void* data{};
        const D3D12_RANGE range{0, row_pitch * side};
        if (FAILED(m_readback->Map(0, &range, &data)) || data == nullptr) return;
        std::vector<float> found;
        found.reserve(side * side);
        for (UINT y = 0; y < side; ++y) {
            const auto* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(data) + y * row_pitch);
            for (UINT x = 0; x < side; ++x) {
                const float z = row[x];
                // Reversed, infinite depth: 0 is the sky, 1 the near plane.
                if (z > 1e-7f && z <= 1.0f) found.push_back(m_view.near_cm / z);
            }
        }
        const D3D12_RANGE none{0, 0};
        m_readback->Unmap(0, &none);
        const float share = float(found.size()) / float(side * side);
        if (found.size() >= 16) {
            std::sort(found.begin(), found.end());
            const auto at = [&](float q) { return found[(std::min)(found.size() - 1, size_t(q * float(found.size())))]; };
            nearest.store(at(0.05f)); p25.store(at(0.25f)); p50.store(at(0.5f));
        } else {
            nearest.store(0.0f); p25.store(0.0f); p50.store(0.0f);
        }
        valid.store(share);
        seq.fetch_add(1);
        if (now - m_logged >= 5000) {
            m_logged = now;
            spdlog::info("[WuWaDepth] fov={:.1f} region centre=({:.2f},{:.2f}) valid={:.2f} near={:.0f} p25={:.0f} p50={:.0f} cm",
                fov.load(), m_region[0], m_region[1], share, nearest.load(), p25.load(), p50.load());
        }
    }

    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    d3d12::CommandContext m_cmd{};
    d3d12::TextureContext m_source{}, m_target{};
    ComPtr<ID3D12Resource> m_readback{};
    DXGI_FORMAT m_format{DXGI_FORMAT_UNKNOWN};
    View m_view{};
    std::array<float, 2> m_region{};
    uint64_t m_last{}, m_logged{};
    bool m_pending{false};
};
}
