#pragma once
#include "WuWaHandDemo.hpp"
#include <d3d11_1.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include <cstring>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace wuwa_hand {
// Drawn into existing eye images after the portal mask. No depth test: this is
// explicitly an unoccluded visualization, not world collision or game geometry.
class Renderer {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    inline static constexpr char shader[]=R"(
cbuffer HandSegment : register(b0) { float4 xy[3]; float4 color; };
struct Output { float4 position : SV_Position; float4 tint : COLOR0; };
Output vs_main(uint id : SV_VertexID) {
    Output o; float4 pair=xy[id/2];
    o.position=float4((id%2)==0?pair.xy:pair.zw,0,1); o.tint=color; return o;
}
float4 ps_main(Output i) : SV_Target { return i.tint; }
)";
public:
    static Renderer& get() { static Renderer renderer; return renderer; }
    void reset11() { std::scoped_lock lock(m_mutex); clear11(); }
    // Caller must drain its command contexts before releasing DX12 objects.
    void reset12() { std::scoped_lock lock(m_mutex); clear12(); }
    bool draw11(ID3D11DeviceContext* context,ID3D11Texture2D* target,const Snapshot& sample,Layout layout) {
        if (!context || !target || !fresh(sample)) return false;
        D3D11_TEXTURE2D_DESC desc{}; target->GetDesc(&desc);
        const auto batch=geometry(sample,layout,static_cast<float>(desc.Width),static_cast<float>(desc.Height));
        if (!batch.count) return false;
        std::scoped_lock lock(m_mutex);
        Ptr<ID3D11Device> device; context->GetDevice(&device);
        Ptr<ID3D11DeviceContext1> context1;
        if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) return fail11();
        if (!ensure11(device.Get())) return false;
        auto it=m_rtvs.find(target);
        if (it==m_rtvs.end()) {
            if (m_rtvs.size()>=16) m_rtvs.clear();
            D3D11_RENDER_TARGET_VIEW_DESC info{}; info.Format=typed(desc.Format);
            info.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            Ptr<ID3D11RenderTargetView> rtv;
            if (FAILED(device->CreateRenderTargetView(target,&info,&rtv))) return fail11();
            it=m_rtvs.emplace(target,std::move(rtv)).first;
        }
        // A private context state saves/restores the entire pipeline, including
        // class instances, UAVs, predication and all render targets.
        Ptr<ID3DDeviceContextState> previous;
        context1->SwapDeviceContextState(m_state11.Get(),&previous);
        struct Restore { ID3D11DeviceContext1* context; ID3DDeviceContextState* state;
            ~Restore() { context->ClearState(); context->SwapDeviceContextState(state,nullptr); }
        } restore{context1.Get(),previous.Get()};
        auto* rtv=it->second.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
        context->OMSetBlendState(m_blend11.Get(),nullptr,UINT_MAX);
        context->OMSetDepthStencilState(m_depth11.Get(),0);
        context->RSSetState(m_raster11.Get());
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height),0,1};
        context->RSSetViewports(1,&viewport);
        context->VSSetShader(m_vs11.Get(),nullptr,0); context->PSSetShader(m_ps11.Get(),nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto* constants=m_constants11.Get(); context->VSSetConstantBuffers(0,1,&constants);
        for (size_t i=0;i<batch.count;++i) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(constants,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))) {
                fail11();
                // Issued pixels must retain their matching views/depth metadata,
                // even if a later segment cannot be uploaded.
                return i > 0;
            }
            std::memcpy(mapped.pData,&batch.segments[i],sizeof(DrawConstants)); context->Unmap(constants,0);
            context->Draw(6,0);
        }
        return true;
    }
    // Called at the tail of this dedicated OpenXR copy command list. Target is
    // already in RENDER_TARGET state; callers execute before image release.
    bool draw12(ID3D12GraphicsCommandList* list,ID3D12Resource* target,D3D12_CPU_DESCRIPTOR_HANDLE rtv,
        DXGI_FORMAT format,const Snapshot& sample,Layout layout) {
        if (!list || !target || !rtv.ptr || !fresh(sample)) return false;
        const auto desc=target->GetDesc();
        const auto batch=geometry(sample,layout,static_cast<float>(desc.Width),static_cast<float>(desc.Height));
        if (!batch.count) return false;
        std::scoped_lock lock(m_mutex);
        // This demo's PSO is single-sample. Reject unsupported targets before
        // touching the caller's command list, which also submits the eye image.
        if (desc.SampleDesc.Count != 1) return fail12();
        Ptr<ID3D12Device> device;
        if (FAILED(target->GetDevice(IID_PPV_ARGS(&device)))) return false;
        if (format==DXGI_FORMAT_UNKNOWN) format=typed(desc.Format);
        if (!ensure12(device.Get(),format)) return false;
        list->SetGraphicsRootSignature(m_root12.Get()); list->SetPipelineState(m_psos12.at(format).Get());
        list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const D3D12_VIEWPORT viewport{0,0,static_cast<float>(desc.Width),static_cast<float>(desc.Height),0,1};
        const D3D12_RECT scissor{0,0,static_cast<LONG>(desc.Width),static_cast<LONG>(desc.Height)};
        list->RSSetViewports(1,&viewport); list->RSSetScissorRects(1,&scissor);
        for (size_t i=0;i<batch.count;++i) {
            list->SetGraphicsRoot32BitConstants(0,sizeof(DrawConstants)/4,&batch.segments[i],0);
            list->DrawInstanced(6,1,0,0);
        }
        return true;
    }
private:
    static DXGI_FORMAT typed(DXGI_FORMAT format) {
        switch(format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:return format;
        }
    }
    bool compile(Ptr<ID3DBlob>& vs,Ptr<ID3DBlob>& ps) {
        return SUCCEEDED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs_main","vs_5_0",0,0,&vs,nullptr)) &&
            SUCCEEDED(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps_main","ps_5_0",0,0,&ps,nullptr));
    }
    bool fail11() { if (!m_failed11) spdlog::error("[HandDemo] D3D11 visualization unavailable until graphics reset"); m_failed11=true; renderer_error=11; return false; }
    bool fail12() { if (!m_failed12) spdlog::error("[HandDemo] D3D12 visualization unavailable until graphics reset"); m_failed12=true; renderer_error=12; return false; }
    bool ensure11(ID3D11Device* device) {
        if (m_device11.Get()!=device) { clear11(); m_device11=device; }
        if (m_failed11) return false;
        if (m_state11) return true;
        Ptr<ID3D11Device1> device1; Ptr<ID3DBlob> vs,ps;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))) || !compile(vs,ps)) return fail11();
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        if (FAILED(device1->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),nullptr,&m_state11)) ||
            FAILED(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&m_vs11)) ||
            FAILED(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&m_ps11))) return fail11();
        D3D11_BUFFER_DESC buffer{}; buffer.ByteWidth=sizeof(DrawConstants); buffer.Usage=D3D11_USAGE_DYNAMIC;
        buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER; buffer.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        D3D11_BLEND_DESC blend{}; auto& b=blend.RenderTarget[0]; b.BlendEnable=TRUE;
        b.SrcBlend=D3D11_BLEND_SRC_ALPHA; b.DestBlend=D3D11_BLEND_INV_SRC_ALPHA; b.BlendOp=D3D11_BLEND_OP_ADD;
        b.SrcBlendAlpha=D3D11_BLEND_ONE; b.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        b.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=FALSE; depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE;
        raster.DepthClipEnable=TRUE;
        if (FAILED(device->CreateBuffer(&buffer,nullptr,&m_constants11)) || FAILED(device->CreateBlendState(&blend,&m_blend11)) ||
            FAILED(device->CreateDepthStencilState(&depth,&m_depth11)) || FAILED(device->CreateRasterizerState(&raster,&m_raster11))) return fail11();
        return true;
    }
    bool ensure12(ID3D12Device* device,DXGI_FORMAT format) {
        // The integration drains command contexts before reset12(). If a new
        // device arrives first, retain the old PSOs rather than releasing objects
        // that could still be referenced by queued GPU work.
        if (m_device12 && m_device12.Get()!=device) return fail12();
        if (!m_device12) m_device12=device;
        if (m_failed12) return false;
        if (m_psos12.contains(format)) return true;
        if (!m_root12) {
            D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameter.Constants.Num32BitValues=sizeof(DrawConstants)/4; parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
            D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters=1; desc.pParameters=&parameter;
            Ptr<ID3DBlob> blob;
            if (FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,nullptr)) ||
                FAILED(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&m_root12)))) return fail12();
        }
        Ptr<ID3DBlob> vs,ps; if (!compile(vs,ps)) return fail12();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{}; desc.pRootSignature=m_root12.Get();
        desc.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; desc.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; desc.NumRenderTargets=1;
        desc.RTVFormats[0]=format; desc.SampleDesc.Count=1; desc.SampleMask=UINT_MAX;
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable=TRUE; desc.DepthStencilState.DepthEnable=FALSE;
        desc.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        auto& b=desc.BlendState.RenderTarget[0]; b.BlendEnable=TRUE;
        b.SrcBlend=D3D12_BLEND_SRC_ALPHA; b.DestBlend=D3D12_BLEND_INV_SRC_ALPHA; b.BlendOp=D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha=D3D12_BLEND_ONE; b.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA; b.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        Ptr<ID3D12PipelineState> pso;
        if (FAILED(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&pso)))) return fail12();
        m_psos12.emplace(format,std::move(pso)); return true;
    }
    void clear11() {
        m_rtvs.clear(); m_state11.Reset(); m_vs11.Reset(); m_ps11.Reset(); m_constants11.Reset();
        m_blend11.Reset(); m_depth11.Reset(); m_raster11.Reset(); m_device11.Reset(); m_failed11=false;
        if(renderer_error==11) renderer_error=0;
    }
    void clear12() { m_psos12.clear(); m_root12.Reset(); m_device12.Reset(); m_failed12=false; if(renderer_error==12) renderer_error=0; }
    std::mutex m_mutex;
    bool m_failed11{},m_failed12{};
    Ptr<ID3D11Device> m_device11;
    Ptr<ID3DDeviceContextState> m_state11;
    Ptr<ID3D11VertexShader> m_vs11; Ptr<ID3D11PixelShader> m_ps11;
    Ptr<ID3D11Buffer> m_constants11; Ptr<ID3D11BlendState> m_blend11;
    Ptr<ID3D11DepthStencilState> m_depth11; Ptr<ID3D11RasterizerState> m_raster11;
    std::unordered_map<ID3D11Texture2D*,Ptr<ID3D11RenderTargetView>> m_rtvs;
    Ptr<ID3D12Device> m_device12; Ptr<ID3D12RootSignature> m_root12;
    std::unordered_map<DXGI_FORMAT,Ptr<ID3D12PipelineState>> m_psos12;
};
}
