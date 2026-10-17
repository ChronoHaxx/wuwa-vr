// Public compositor mirrors only: never capture the desktop or inject input.
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <codecapi.h>
#include <wrl/client.h>
#include <openvr.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <Xinput.h>
#include <timeapi.h>
using Microsoft::WRL::ComPtr;
using Json=nlohmann::json;
static void check(HRESULT value) { if (FAILED(value)) throw std::runtime_error("HRESULT "+std::to_string(value)); }
static int64_t utc() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
static bool steamvr_running() {
    const auto snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snap==INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W p{sizeof(p)}; bool found=false;
    if (Process32FirstW(snap,&p)) do { if (_wcsicmp(p.szExeFile,L"vrserver.exe")==0) found=true; } while (Process32NextW(snap,&p));
    CloseHandle(snap); return found;
}
struct Encoder {
    ComPtr<IMFSinkWriter> writer; DWORD stream{}; UINT w{},h{};
    // high: H.264 High profile (the showcase video); the side-by-side recording keeps the encoder default.
    void open(const std::filesystem::path& path,UINT width,UINT height,UINT fps,UINT bitrate=16000000,bool high=false) {
        w=width; h=height;
        ComPtr<IMFAttributes> attrs; check(MFCreateAttributes(&attrs,2));
        check(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
        check(MFCreateSinkWriterFromURL(path.c_str(),nullptr,attrs.Get(),&writer));
        ComPtr<IMFMediaType> out,in; check(MFCreateMediaType(&out));
        check(out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); check(out->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
        check(out->SetUINT32(MF_MT_AVG_BITRATE,bitrate));
        if (high) check(out->SetUINT32(MF_MT_MPEG2_PROFILE,eAVEncH264VProfile_High)); check(out->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(out.Get(),MF_MT_FRAME_SIZE,w,h)); check(MFSetAttributeRatio(out.Get(),MF_MT_FRAME_RATE,fps,1));
        check(MFSetAttributeRatio(out.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1)); check(writer->AddStream(out.Get(),&stream));
        check(MFCreateMediaType(&in)); check(in->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
        check(in->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32)); check(in->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(in.Get(),MF_MT_FRAME_SIZE,w,h)); check(MFSetAttributeRatio(in.Get(),MF_MT_FRAME_RATE,fps,1));
        check(MFSetAttributeRatio(in.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1));
        check(in->SetUINT32(MF_MT_DEFAULT_STRIDE,w*4));
        check(writer->SetInputMediaType(stream,in.Get(),nullptr)); check(writer->BeginWriting());
    }
    void add(const std::vector<BYTE>& pixels,LONGLONG timestamp,LONGLONG duration) {
        ComPtr<IMFMediaBuffer> buffer; check(MFCreateMemoryBuffer(static_cast<DWORD>(pixels.size()),&buffer));
        BYTE* data{}; check(buffer->Lock(&data,nullptr,nullptr)); memcpy(data,pixels.data(),pixels.size()); check(buffer->Unlock());
        check(buffer->SetCurrentLength(static_cast<DWORD>(pixels.size())));
        ComPtr<IMFSample> sample; check(MFCreateSample(&sample)); check(sample->AddBuffer(buffer.Get()));
        check(sample->SetSampleTime(timestamp)); check(sample->SetSampleDuration(duration)); check(writer->WriteSample(stream,sample.Get()));
    }
    void close() { if (writer) { const auto result=writer->Finalize(); writer.Reset(); check(result); } }
};
struct Eye {
    vr::IVRCompositor* compositor{}; ID3D11ShaderResourceView* view{};
    ComPtr<ID3D11Texture2D> texture; UINT width{},height{}; bool bgra{},srgb{};
    ~Eye() { if (view) compositor->ReleaseMirrorTextureD3D11(view); }
    void open(vr::IVRCompositor* c,ID3D11Device* device,int eye) {
        compositor=c;
        const auto result=c->GetMirrorTextureD3D11(static_cast<vr::EVREye>(eye),device,reinterpret_cast<void**>(&view));
        if (result || !view) throw std::runtime_error("Mirror unavailable "+std::to_string(result));
        ComPtr<ID3D11Resource> resource; view->GetResource(&resource); check(resource.As(&texture));
        D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc); width=desc.Width; height=desc.Height;
        D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; view->GetDesc(&vd);
        bgra=vd.Format==DXGI_FORMAT_B8G8R8A8_UNORM || vd.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        srgb=vd.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || vd.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool rgba=vd.Format==DXGI_FORMAT_R8G8B8A8_UNORM || vd.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if ((!bgra&&!rgba)||desc.SampleDesc.Count!=1||desc.ArraySize!=1||desc.MipLevels!=1||!width||!height)
            throw std::runtime_error("Unsupported mirror format");
    }
};
// Resize each eye on the GPU, then read back only the final SBS dimensions.
// The former CPU bilinear loop read full 2700x2880 mirrors and managed only
// ~10.7 new captures/sec in the owner's first 30-fps recording.
struct StereoResize {
    ComPtr<ID3D11Texture2D> target,staging;
    ComPtr<ID3D11RenderTargetView> linear,srgb;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11SamplerState> sampler;
    UINT width{},height{};
    void open(ID3D11Device* device,UINT eye_width,UINT eye_height) {
        width=eye_width*2; height=eye_height;
        D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height; d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d,nullptr,&target));
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM; check(device->CreateRenderTargetView(target.Get(),&rv,&linear));
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; check(device->CreateRenderTargetView(target.Get(),&rv,&srgb));
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&staging));
        constexpr char shader[]=R"(
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vert(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }
Texture2D eye:register(t0); SamplerState smp:register(s0);
float4 pixel(V v):SV_Target { return float4(eye.Sample(smp,v.uv).rgb,1); }
)";
        ComPtr<ID3DBlob> v,p,error;
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vert","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&error));
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error));
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));
        check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
        D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&s,&sampler));
    }
    void read(ID3D11DeviceContext* context,ID3D11ShaderResourceView* left,ID3D11ShaderResourceView* right,
              bool left_srgb,bool right_srgb,std::vector<BYTE>& output) {
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0); context->PSSetShader(ps.Get(),nullptr,0);
        auto* smp=sampler.Get(); context->PSSetSamplers(0,1,&smp);
        ID3D11ShaderResourceView* eyes[]{left,right}; const bool encoded[]{left_srgb,right_srgb};
        for (int i=0;i<2;++i) {
            D3D11_VIEWPORT vp{float(i)*float(width)/2,0,float(width)/2,float(height),0,1}; context->RSSetViewports(1,&vp);
            auto* rtv=encoded[i]?srgb.Get():linear.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
            context->PSSetShaderResources(0,1,&eyes[i]); context->Draw(3,0);
        }
        ID3D11ShaderResourceView* empty{}; context->PSSetShaderResources(0,1,&empty);
        context->OMSetRenderTargets(0,nullptr,nullptr); context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map));
        output.resize(static_cast<size_t>(width)*height*4);
        for (UINT y=0;y<height;++y) memcpy(output.data()+static_cast<size_t>(y)*width*4,
            static_cast<const BYTE*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,width*4);
        context->Unmap(staging.Get(),0);
    }
};
// Showcase video: the left eye as a 16:9 camera on its optical axis, steadied, at the game's own frame rate.
// The per-eye mirror is what the headset shows (the HUD at its place included). It changes only when the game
// submits a frame (about 30 a second), so each new game frame is written once at the time it arrived, instead of
// the uneven repeats a fixed-rate capture makes (those judder).
struct Quat { double w{1},x{},y{},z{}; };
static Quat qmul(Quat a,Quat b) {
    return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z, a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x, a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
static Quat qconj(Quat q) { return {q.w,-q.x,-q.y,-q.z}; }
static Quat qnorm(Quat q) { const double n=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z); return n>0?Quat{q.w/n,q.x/n,q.y/n,q.z/n}:Quat{}; }
static double qangle(Quat a,Quat b) { return 2*std::acos(std::min(1.0,std::abs(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z))); }
static Quat qslerp(Quat a,Quat b,double t) {
    double d=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z; if (d<0) { b={-b.w,-b.x,-b.y,-b.z}; d=-d; }
    if (d>0.9995) return qnorm({a.w+(b.w-a.w)*t,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t});
    const double th=std::acos(d),sn=std::sin(th),ka=std::sin((1-t)*th)/sn,kb=std::sin(t*th)/sn;
    return {a.w*ka+b.w*kb,a.x*ka+b.x*kb,a.y*ka+b.y*kb,a.z*ka+b.z*kb};
}
static Quat quat_from(const vr::HmdMatrix34_t& m) {
    const double a=m.m[0][0],b=m.m[0][1],c=m.m[0][2],d=m.m[1][0],e=m.m[1][1],f=m.m[1][2],g=m.m[2][0],h=m.m[2][1],i=m.m[2][2];
    const double trace=a+e+i; Quat q;
    if (trace>0) { const double k=std::sqrt(trace+1.0)*2; q={0.25*k,(h-f)/k,(c-g)/k,(d-b)/k}; }
    else if (a>e && a>i) { const double k=std::sqrt(1.0+a-e-i)*2; q={(h-f)/k,0.25*k,(b+d)/k,(c+g)/k}; }
    else if (e>i) { const double k=std::sqrt(1.0+e-a-i)*2; q={(c-g)/k,(b+d)/k,0.25*k,(f+h)/k}; }
    else { const double k=std::sqrt(1.0+i-a-e)*2; q={(d-b)/k,(c+g)/k,(f+h)/k,0.25*k}; }
    return qnorm(q);
}
// Rows of the rotation matrix of q (v' = q v q*), padded to float4 for the shader.
static void qrows(Quat q,float rows[3][4]) {
    const double w=q.w,x=q.x,y=q.y,z=q.z;
    const double m[3][3]{{1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)},{2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)},{2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)}};
    for (int r=0;r<3;++r) { for (int c=0;c<3;++c) rows[r][c]=float(m[r][c]); rows[r][3]=0; }
}
// The same rule as the plugin's steady desktop view (utility/WuWaSteadyView.hpp): follow the head with a time
// constant, and past a knee ease the lag towards the margin instead of stopping hard at it.
struct Steady {
    Quat smooth; double last{}; bool valid{};
    // The smoothed view's rotation in the actual view's frame (identity when there is nothing to smooth).
    Quat update(Quat actual,double now,double tau,double max_angle) {
        const double dt=now-last; last=now;
        if (!valid || dt>0.5 || dt<0 || qangle(smooth,actual)>0.6) { smooth=actual; valid=true; return {}; }
        if (dt>0) smooth=qnorm(qslerp(smooth,actual,1.0-std::exp(-dt/std::max(0.01,tau))));
        const double lag=qangle(smooth,actual),knee=0.6*max_angle;
        if (lag>knee) smooth=qnorm(qslerp(actual,smooth,(knee+(max_angle-knee)*std::tanh((lag-knee)/(max_angle-knee)))/lag));
        return qnorm(qmul(qconj(actual),smooth));
    }
};
// Draws one eye as seen from the steadied view into a 16:9 target (and a 32x32 fingerprint of the whole eye), and
// reads them back. Each output pixel is a ray of the steadied camera, turned into the rendered eye's frame and
// projected with the eye's tangents. OpenVR GetProjectionRaw gives left < 0 < right, and names the vertical sides
// the other way round from the picture: "top" is the downward tangent (negative) and "bottom" the upward one, which
// its own projection matrix puts on the first row (OpenXR's angleDown / angleUp, as UEVR logs them). Up to test 18
// the crop read top as up, so a headset with more view below than above recorded a view tilted down.
struct ShowcaseCrop {
    struct Constants { float tangents[4]; float crop[4]; float rows[3][4]; };
    ComPtr<ID3D11Texture2D> target,staging,print_target,print_staging;
    ComPtr<ID3D11RenderTargetView> linear,srgb,print_linear,print_srgb;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps,print_ps;
    ComPtr<ID3D11SamplerState> sampler; ComPtr<ID3D11Buffer> constants;
    UINT width{},height{};
    static void targets(ID3D11Device* device,UINT w,UINT h,ComPtr<ID3D11Texture2D>& tex,ComPtr<ID3D11Texture2D>& stage,
                        ComPtr<ID3D11RenderTargetView>& lin,ComPtr<ID3D11RenderTargetView>& enc) {
        D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_TYPELESS; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&d,nullptr,&tex));
        D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM; check(device->CreateRenderTargetView(tex.Get(),&rv,&lin));
        rv.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; check(device->CreateRenderTargetView(tex.Get(),&rv,&enc));
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&stage));
    }
    void open(ID3D11Device* device,UINT w,UINT h) {
        width=w; height=h;
        targets(device,w,h,target,staging,linear,srgb);
        targets(device,32,32,print_target,print_staging,print_linear,print_srgb);
        constexpr char shader[]=R"(
cbuffer C:register(b0) { float4 tangents; float4 crop; float4 row0; float4 row1; float4 row2; };
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vert(uint id:SV_VertexID) { V v; v.uv=float2((id<<1)&2,id&2); v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1); return v; }
Texture2D eye:register(t0); SamplerState smp:register(s0);
float4 pixel(V v):SV_Target {
    float3 ray=float3((v.uv.x*2-1)*crop.x+crop.z,(1-v.uv.y*2)*crop.y+crop.w,-1);
    float3 a=float3(dot(row0.xyz,ray),dot(row1.xyz,ray),dot(row2.xyz,ray));
    float2 t=a.xy/max(1e-4,-a.z);
    float2 uv=float2((t.x-tangents.x)/(tangents.y-tangents.x),(tangents.w-t.y)/(tangents.w-tangents.z));
    return float4(eye.Sample(smp,uv).rgb,1);
}
float4 whole(V v):SV_Target { return float4(eye.Sample(smp,v.uv).rgb,1); }
)";
        ComPtr<ID3DBlob> v,p,q,error;
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vert","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&v,&error));
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"pixel","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&p,&error));
        check(D3DCompile(shader,sizeof(shader),nullptr,nullptr,nullptr,"whole","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&q,&error));
        check(device->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vs));
        check(device->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&ps));
        check(device->CreatePixelShader(q->GetBufferPointer(),q->GetBufferSize(),nullptr,&print_ps));
        D3D11_SAMPLER_DESC sd{}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD=D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sd,&sampler));
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=sizeof(Constants); bd.Usage=D3D11_USAGE_DYNAMIC;
        bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&bd,nullptr,&constants));
    }
    static void read_back(ID3D11DeviceContext* context,ID3D11Texture2D* tex,ID3D11Texture2D* stage,UINT w,UINT h,std::vector<BYTE>& out) {
        context->CopyResource(stage,tex);
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(stage,0,D3D11_MAP_READ,0,&map));
        out.resize(static_cast<size_t>(w)*h*4);
        for (UINT y=0;y<h;++y) memcpy(out.data()+static_cast<size_t>(y)*w*4,static_cast<const BYTE*>(map.pData)+static_cast<size_t>(y)*map.RowPitch,w*4);
        context->Unmap(stage,0);
    }
    void draw(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,ID3D11PixelShader* shader,
              ID3D11RenderTargetView* lin,ID3D11RenderTargetView* enc,UINT w,UINT h) {
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.Get(),nullptr,0); context->PSSetShader(shader,nullptr,0);
        auto* smp=sampler.Get(); context->PSSetSamplers(0,1,&smp);
        auto* cb=constants.Get(); context->PSSetConstantBuffers(0,1,&cb);
        D3D11_VIEWPORT vp{0,0,float(w),float(h),0,1}; context->RSSetViewports(1,&vp);
        auto* rtv=encoded?enc:lin; context->OMSetRenderTargets(1,&rtv,nullptr);
        context->PSSetShaderResources(0,1,&eye); context->Draw(3,0);
        ID3D11ShaderResourceView* empty{}; context->PSSetShaderResources(0,1,&empty); context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    // Equal fingerprints mean the mirror still holds the same picture.
    void fingerprint(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,std::vector<BYTE>& out) {
        draw(context,eye,encoded,print_ps.Get(),print_linear.Get(),print_srgb.Get(),32,32);
        read_back(context,print_target.Get(),print_staging.Get(),32,32,out);
    }
    void read(ID3D11DeviceContext* context,ID3D11ShaderResourceView* eye,bool encoded,const Constants& c,std::vector<BYTE>& out) {
        D3D11_MAPPED_SUBRESOURCE map{}; check(context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&map));
        memcpy(map.pData,&c,sizeof(c)); context->Unmap(constants.Get(),0);
        draw(context,eye,encoded,ps.Get(),linear.Get(),srgb.Get(),width,height);
        read_back(context,target.Get(),staging.Get(),width,height,out);
    }
};
// The crop: the eye's whole width less room on each side for the steady view to turn, at most 90 degrees across
// (trimmed from the wider side first), and 16:9 centred on the axis vertically. An eye sees further outwards than
// towards the nose, so the window sits off the axis on the outer side (a shifted lens, like an off-axis camera):
// test 18 kept it centred, which cut it to the narrow nasal side (65 degrees across on a headset that shows 89).
struct ShowcaseFrame { double half_width{},half_height{},centre_x{},centre_y{}; };
static ShowcaseFrame showcase_frame(float left,float right,float top,float bottom,double room_angle) {
    ShowcaseFrame f;
    double outer_left=std::atan(-double(left))-room_angle, outer_right=std::atan(double(right))-room_angle;
    const double up=std::atan(double(bottom))-room_angle, down=std::atan(-double(top))-room_angle;
    if (outer_left<=0.03 || outer_right<=0.03 || up<=0.03 || down<=0.03) throw std::runtime_error("Eye view too narrow for a showcase crop");
    if (double excess=outer_left+outer_right-1.5707963267948966; excess>0) {
        double& wider=outer_left>outer_right?outer_left:outer_right;
        const double even=std::min(excess,std::abs(outer_left-outer_right));
        wider-=even; excess-=even; outer_left-=excess/2; outer_right-=excess/2;
    }
    const double x0=-std::tan(outer_left), x1=std::tan(outer_right), y_up=std::tan(up), y_down=std::tan(down);
    f.half_width=(x1-x0)/2; f.centre_x=(x1+x0)/2; f.half_height=f.half_width*9.0/16.0;
    if (2*f.half_height>y_up+y_down) { f.half_height=(y_up+y_down)/2; f.half_width=f.half_height*16.0/9.0; }
    f.centre_y=std::clamp(0.0,f.half_height-y_down,y_up-f.half_height);
    return f;
}
static bool same_print(const std::vector<BYTE>& a,const std::vector<BYTE>& b) {
    if (a.size()!=b.size() || a.empty()) return false;
    for (size_t i=0;i<a.size();++i) if (std::abs(int(a[i])-int(b[i]))>2) return false;
    return true;
}
int wmain(int argc,wchar_t** argv) {
    HMODULE library{}; void(__cdecl* shutdown)(){}; bool initialized=false,media=false,com=false;
    Encoder encoder; Json result; std::filesystem::path out; bool output_created=false;
    try {
        if ((argc==2 || argc==3) && std::wstring_view(argv[1])==L"--self-test-showcase") {
            if (argc==3 && std::filesystem::exists(argv[2])) throw std::runtime_error("Self-test output exists");
            // Synthetic eye: four quadrants. A centred view keeps each quadrant in place, a turned view turns the
            // picture, an unchanged eye keeps its fingerprint, and a quick head turn stays inside the margin.
            ComPtr<ID3D11Device> test_device; ComPtr<ID3D11DeviceContext> test_context;
            check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&test_device,nullptr,&test_context));
            std::vector<BYTE> eye_pixels(640*640*4);
            for (int y=0;y<640;++y) for (int x=0;x<640;++x) {
                const auto i=(y*640+x)*4; eye_pixels[i]=x>=320?255:0; eye_pixels[i+1]=y>=320?255:0; eye_pixels[i+2]=64; eye_pixels[i+3]=255;
            }
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=640; d.MipLevels=d.ArraySize=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{eye_pixels.data(),640*4,0};
            ComPtr<ID3D11Texture2D> tex; ComPtr<ID3D11ShaderResourceView> view;
            check(test_device->CreateTexture2D(&d,&data,&tex)); check(test_device->CreateShaderResourceView(tex.Get(),nullptr,&view));
            ShowcaseCrop crop; crop.open(test_device.Get(),320,180);
            {
                // An eye like the lighthouse headset of test 18: 49 degrees outwards, 40 inwards, 38 up, 50 down.
                // OpenVR order: left, right, "top" (the downward tangent), "bottom" (the upward one).
                const float l=-1.157f,r=0.829f,down=-1.215f,up=0.788f;
                const auto f=showcase_frame(l,r,down,up,0.12);
                const double across=std::atan(f.half_width-f.centre_x)+std::atan(f.half_width+f.centre_x);
                if (across<1.25 || f.centre_x>=0 || std::abs(f.centre_y)>1e-9 || f.half_height>std::tan(std::atan(up)-0.12))
                    throw std::runtime_error("Showcase crop of an asymmetric eye is narrow, centred or outside the eye");
                // The axis lies at row up/(up-down) of the eye; a marker there must land where the crop puts the axis.
                std::vector<BYTE> marked(640*640*4);
                const int axis_row=int(640.0*up/(up-down)), axis_col=int(640.0*-l/(r-l));
                for (int y=0;y<640;++y) for (int x=0;x<640;++x) {
                    const auto i=(y*640+x)*4; marked[i]=std::abs(x-axis_col)<12&&std::abs(y-axis_row)<12?255:0; marked[i+3]=255;
                }
                D3D11_TEXTURE2D_DESC md{}; md.Width=md.Height=640; md.MipLevels=md.ArraySize=1; md.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
                md.SampleDesc.Count=1; md.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA mdata{marked.data(),640*4,0};
                ComPtr<ID3D11Texture2D> mtex; ComPtr<ID3D11ShaderResourceView> mview;
                check(test_device->CreateTexture2D(&md,&mdata,&mtex)); check(test_device->CreateShaderResourceView(mtex.Get(),nullptr,&mview));
                ShowcaseCrop::Constants on_axis{{l,r,down,up},{0.02f,0.01125f,0,0}}; qrows(Quat{},on_axis.rows);
                std::vector<BYTE> seen; crop.read(test_context.Get(),mview.Get(),false,on_axis,seen);
                if (seen[(90*320+160)*4]<200) throw std::runtime_error("Showcase view of an asymmetric eye is off the optical axis");
                // The full crop shows the axis at its own spot: right of centre (the window leans outwards), mid-height.
                ShowcaseCrop::Constants full{{l,r,down,up},{float(f.half_width),float(f.half_height),float(f.centre_x),float(f.centre_y)}};
                qrows(Quat{},full.rows); crop.read(test_context.Get(),mview.Get(),false,full,seen);
                const int col=int(std::lround(160+160*(-f.centre_x)/f.half_width));
                if (seen[(90*320+col)*4]<200 || seen[(90*320+160)*4]>8) throw std::runtime_error("Off-axis showcase crop lost the optical axis");
            }
            const auto frame=showcase_frame(-1,1,-1,1,0.12);
            ShowcaseCrop::Constants c{{-1,1,-1,1},{float(frame.half_width),float(frame.half_height),0,0}};
            qrows(Quat{},c.rows);
            std::vector<BYTE> out; crop.read(test_context.Get(),view.Get(),false,c,out);
            auto at=[&](int x,int y,int ch) { return int(out[(y*320+x)*4+ch]); };
            if (at(40,20,0)>8 || at(40,20,1)>8 || at(280,160,0)<247 || at(280,160,1)<247 || at(288,63,0)<247)
                throw std::runtime_error("Centred showcase view moved or flipped the eye image");
            const double half=std::acos(-1.0)/4;  // a quarter turn about the view axis
            qrows(Quat{std::cos(half),0,0,std::sin(half)},c.rows);
            crop.read(test_context.Get(),view.Get(),false,c,out);
            if (at(288,63,0)>8 || at(288,63,1)>8) throw std::runtime_error("Turned showcase view did not turn the picture");
            std::vector<BYTE> a,b2; crop.fingerprint(test_context.Get(),view.Get(),false,a); crop.fingerprint(test_context.Get(),view.Get(),false,b2);
            if (!same_print(a,b2)) throw std::runtime_error("Unchanged eye gave a different fingerprint");
            b2[100]^=0x80; if (same_print(a,b2)) throw std::runtime_error("Changed eye gave the same fingerprint");
            Steady steady; double worst=0;
            for (int i=0;i<400;++i) {
                const double now=i/30.0, yaw=1.2*now;  // a quick 1.2 rad/s turn at a 30 fps game
                worst=std::max(worst,qangle(Quat{},steady.update(Quat{std::cos(yaw/2),0,std::sin(yaw/2),0},now,0.35,0.10)));
            }
            if (worst>=0.10 || worst<0.06) throw std::runtime_error("Steady lag left the eased margin");
            if (argc==3) {
                // The largest showcase video, with frames at uneven game times (25 and 41 ms apart).
                check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
                ShowcaseCrop big; big.open(test_device.Get(),2560,1440); qrows(Quat{},c.rows);
                std::vector<BYTE> picture; big.read(test_context.Get(),view.Get(),false,c,picture);
                encoder.open(argv[2],2560,1440,60,40000000,true);
                LONGLONG at_time=0;
                for (int i=0;i<60;++i) { const LONGLONG step=(i%2?410000:250000); encoder.add(picture,at_time,step); at_time+=step; }
                encoder.close(); MFShutdown(); media=false; CoUninitialize(); com=false;
                std::cout<<"Showcase encoder: 60 frames, 2560x1440, 40 Mbps, uneven frame times ("<<at_time/10000<<" ms).\n";
            }
            std::cout<<"Synthetic showcase view: asymmetric eye, centred, turned, fingerprint and steady checks passed; no game/VR access.\n"; return 0;
        }
        if ((argc==3 || argc==4) && std::wstring_view(argv[1])==L"--self-test") {
            const UINT test_fps=argc==4 ? std::stoul(argv[3]) : 30;
            if (test_fps!=30 && test_fps!=45 && test_fps!=60) throw std::runtime_error("Self-test FPS must be 30, 45 or 60");
            const std::filesystem::path video=argv[2];
            if (std::filesystem::exists(video)) throw std::runtime_error("Self-test output exists");
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
            encoder.open(video,640,480,test_fps);
            std::vector<BYTE> pixels(640*480*4);
            for (int y=0;y<480;++y) for (int x=0;x<640;++x) {
                const auto i=(y*640+x)*4;
                pixels[i]=x>=320?255:0; pixels[i+1]=y>=240?255:0; pixels[i+2]=(x<320&&y<240)|| (x>=320&&y>=240)?255:0; pixels[i+3]=255;
                if (x%320>100 && x%320<140 && y>80 && y<120) { pixels[i]=96; pixels[i+1]=128; pixels[i+2]=160; }
            }
            // Exercise the same two-eye GPU path, including UNORM/SRGB parity.
            ComPtr<ID3D11Device> test_device; ComPtr<ID3D11DeviceContext> test_context;
            check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&test_device,nullptr,&test_context));
            ComPtr<ID3D11Texture2D> tex[2]; ComPtr<ID3D11ShaderResourceView> views[2];
            std::vector<BYTE> eye_pixels(320*480*4);
            for (int eye=0;eye<2;++eye) {
                for(int y=0;y<480;++y) memcpy(eye_pixels.data()+y*320*4,pixels.data()+(y*640+eye*320)*4,320*4);
                D3D11_TEXTURE2D_DESC d{}; d.Width=320; d.Height=480; d.MipLevels=d.ArraySize=1;
                d.Format=eye ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
                d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA data{eye_pixels.data(),320*4,0};
                check(test_device->CreateTexture2D(&d,&data,&tex[eye])); check(test_device->CreateShaderResourceView(tex[eye].Get(),nullptr,&views[eye]));
            }
            StereoResize resize; resize.open(test_device.Get(),320,480);
            std::vector<BYTE> resized; resize.read(test_context.Get(),views[0].Get(),views[1].Get(),false,true,resized);
            for (int y : {30,450}) for (int x : {30,600}) for (int c=0;c<4;++c)
                if (std::abs(int(resized[(y*640+x)*4+c])-int(pixels[(y*640+x)*4+c]))>1)
                    throw std::runtime_error("GPU eye resize changed color, order or orientation");
            for (int x : {120,440}) for (int c=0;c<3;++c)
                if (std::abs(int(resized[(100*640+x)*4+c])-int(pixels[(100*640+x)*4+c]))>1)
                    throw std::runtime_error("GPU resize changed UNORM/SRGB midtones");
            StereoResize smaller; smaller.open(test_device.Get(),160,240);
            std::vector<BYTE> reduced_pixels; smaller.read(test_context.Get(),views[0].Get(),views[1].Get(),false,true,reduced_pixels);
            for (int y : {15,50,225}) for (int x : {15,60,220,300}) for (int c=0;c<4;++c)
                if (std::abs(int(reduced_pixels[(y*320+x)*4+c])-int(pixels[(y*2*640+x*2)*4+c]))>1)
                    throw std::runtime_error("GPU downsize changed eye color or orientation");
            pixels=std::move(resized);
            for(UINT frame=0;frame<test_fps*2;++frame) encoder.add(pixels,frame*10000000ll/test_fps,10000000/test_fps);
            encoder.close(); MFShutdown(); media=false; CoUninitialize(); com=false;
            std::cout<<"Synthetic GPU SBS resize + RGB/orientation encoder test: "<<test_fps*2<<" frames, 640x480, "<<test_fps<<"fps; no game/VR access.\n"; return 0;
        }
        // --showcase first: the left eye as a steadied 16:9 camera (showcase.mp4) instead of both eyes side by side.
        const bool showcase=argc>1 && std::wstring_view(argv[1])==L"--showcase";
        if (showcase) { ++argv; --argc; }
        if (argc!=7 && argc!=8) throw std::runtime_error("Expected [--showcase] openvr_api.dll, game PID, new output folder, seconds (1..300), FPS (10..60), eye width (640..1920), optional stop file");
        const UINT pid=std::stoul(argv[2]), seconds=std::stoul(argv[4]), fps=std::stoul(argv[5]), ew=std::stoul(argv[6]); out=argv[3];
        if (!pid||seconds<1||seconds>300||fps<10||fps>60||ew<640||ew>1920||ew%2) throw std::runtime_error("Invalid recording bounds");
        // Ends the recording cleanly when the game exits (a crash included) instead of waiting out the time.
        const HANDLE game=OpenProcess(SYNCHRONIZE,FALSE,pid);
        if (std::filesystem::exists(out)) throw std::runtime_error("Output exists; refusing to overwrite");
        if (!steamvr_running()) throw std::runtime_error("SteamVR is not running; recorder will not launch it");
        library=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!library) throw std::runtime_error("Cannot load OpenVR library");
        const auto init=reinterpret_cast<uint32_t(__cdecl*)(vr::EVRInitError*,vr::EVRApplicationType,const char*)>(GetProcAddress(library,"VR_InitInternal2"));
        const auto get=reinterpret_cast<void*(__cdecl*)(const char*,vr::EVRInitError*)>(GetProcAddress(library,"VR_GetGenericInterface"));
        shutdown=reinterpret_cast<void(__cdecl*)()>(GetProcAddress(library,"VR_ShutdownInternal"));
        if (!init||!get||!shutdown) throw std::runtime_error("OpenVR exports missing");
        vr::EVRInitError error{}; init(&error,vr::VRApplication_Background,nullptr);
        if (error) throw std::runtime_error("Background OpenVR init failed"); initialized=true;
        auto system=static_cast<vr::IVRSystem*>(get(vr::IVRSystem_Version,&error));
        if (error||!system) throw std::runtime_error("VR system unavailable");
        auto compositor=static_cast<vr::IVRCompositor*>(get(vr::IVRCompositor_Version,&error));
        if (error||!compositor||compositor->GetCurrentSceneFocusProcess()!=pid) throw std::runtime_error("Requested game is not the compositor scene");
        int adapter_index{}; system->GetDXGIOutputInfo(&adapter_index);
        ComPtr<IDXGIFactory> factory; ComPtr<IDXGIAdapter> adapter;
        check(CreateDXGIFactory(IID_PPV_ARGS(&factory))); check(factory->EnumAdapters(adapter_index,&adapter));
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        check(CoInitializeEx(nullptr,COINIT_MULTITHREADED)); com=true; check(MFStartup(MF_VERSION)); media=true;
        if (showcase) {
            Eye eye; eye.open(compositor,device.Get(),0);
            float tan_l{},tan_r{},tan_t{},tan_b{}; system->GetProjectionRaw(vr::Eye_Left,&tan_l,&tan_r,&tan_t,&tan_b);
            const Quat eye_to_head=quat_from(system->GetEyeToHeadTransform(vr::Eye_Left));
            constexpr double max_angle=0.10, tau=0.35;
            const auto frame=showcase_frame(tan_l,tan_r,tan_t,tan_b,max_angle+0.02);
            // Picture size 720 / 1024 / 1280 px per eye maps to 720p / 1080p / 1440p.
            const UINT ow=ew<=720?1280u:ew<=1024?1920u:2560u, oh=ow*9/16;
            const UINT bitrate=oh>=1440?40000000u:oh>=1080?24000000u:12000000u;
            if (!std::filesystem::create_directories(out)) throw std::runtime_error("Output appeared during startup; refusing to overwrite");
            output_created=true;
            encoder.open(out/L"showcase.mp4",ow,oh,fps,bitrate,true);
            std::ofstream frames(out/L"frames.jsonl",std::ios::binary);
            if (!frames) throw std::runtime_error("Cannot write frame sidecar");
            ShowcaseCrop crop; crop.open(device.Get(),ow,oh);
            timeBeginPeriod(1);  // 2 ms polls: a new game frame is noticed within a few ms of arriving
            Steady steady; std::vector<BYTE> pixels,pending,print,last_print;
            Sleep(250); const auto start=std::chrono::steady_clock::now();
            uint64_t count=0,unsteadied=0,too_soon=0; uint32_t last_index=0; bool have_index=false; const auto start_ms=utc();
            double pending_t=-1, waiting_since=-1;
            std::string end_reason="time_limit"; Json pauses=Json::array(); double paused_at=-1, t=0;
            while (true) {
                t=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
                if (t>=seconds) break;
                if (std::filesystem::exists(out/L"STOP") || (argc==8&&std::filesystem::exists(argv[7]))) { end_reason="stopped"; break; }
                if (game && WaitForSingleObject(game,0)==WAIT_OBJECT_0) { end_reason="game_exited"; break; }
                if (compositor->GetCurrentSceneFocusProcess()!=pid) { if (paused_at<0) paused_at=t; Sleep(100); continue; }
                // A new game frame shows as a new frame index; its timing carries the pose it was rendered with.
                vr::Compositor_FrameTiming timing{sizeof(timing)}; const bool timed=compositor->GetFrameTiming(&timing);
                if (timed && have_index && timing.m_nFrameIndex==last_index) { Sleep(2); continue; }
                // The mirror can trail the timing by a moment: wait (briefly) for the picture itself to change.
                crop.fingerprint(context.Get(),eye.view,eye.srgb,print);
                if (same_print(print,last_print)) {
                    if (waiting_since<0) waiting_since=t;
                    if (!timed || t-waiting_since<0.012) { Sleep(2); continue; }
                }
                waiting_since=-1; last_print=print;
                if (timed) { last_index=timing.m_nFrameIndex; have_index=true; }
                if (pending_t>=0 && t-pending_t<0.5/fps) { ++too_soon; continue; }  // the game runs faster than the video
                Quat view{};
                if (timed && timing.m_HmdPose.bPoseIsValid) {
                    const Quat head=quat_from(timing.m_HmdPose.mDeviceToAbsoluteTracking);
                    const Quat d=steady.update(qmul(head,eye_to_head),t,tau,max_angle);
                    view=d;
                } else ++unsteadied;
                ShowcaseCrop::Constants c{{tan_l,tan_r,tan_t,tan_b},{float(frame.half_width),float(frame.half_height),float(frame.centre_x),float(frame.centre_y)}};
                qrows(view,c.rows);
                const auto capture_begin=utc();
                crop.read(context.Get(),eye.view,eye.srgb,c,pixels);
                if (compositor->GetCurrentSceneFocusProcess()!=pid) { if (paused_at<0) paused_at=t; continue; }  // frame discarded
                if (paused_at>=0) { pauses.push_back({{"from_s",paused_at},{"to_s",t}}); paused_at=-1; }
                bool nonblack=false; for (size_t i=0;i<pixels.size();i+=128) if (pixels[i]||pixels[i+1]||pixels[i+2]) { nonblack=true; break; }
                if (!count&&!nonblack) throw std::runtime_error("Blank initial mirror; no useful video");
                // Each frame lasts until the next one arrives, so the video keeps the game's real timing.
                if (pending_t>=0) encoder.add(pending,static_cast<LONGLONG>(pending_t*10000000),static_cast<LONGLONG>((t-pending_t)*10000000));
                std::swap(pending,pixels); pending_t=t;
                frames<<Json{{"frame",count},{"video_seconds",t},{"capture_begin_ms",capture_begin},{"capture_end_ms",utc()},
                    {"clock_ms",GetTickCount64()},{"scene_frame",timed?Json(timing.m_nFrameIndex):Json(nullptr)},
                    {"steady",{view.w,view.x,view.y,view.z}}}.dump()<<'\n';
                ++count; if (count%fps==0) frames.flush();
            }
            if (pending_t>=0) encoder.add(pending,static_cast<LONGLONG>(pending_t*10000000),10000000/fps);
            encoder.close(); timeEndPeriod(1);
            if (paused_at>=0) pauses.push_back({{"from_s",paused_at},{"to_s",t}});
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            result={{"status","recorded"},{"style","showcase"},{"end_reason",end_reason},{"scene_pauses",pauses},
                {"pid",pid},{"started_unix_ms",start_ms},{"frames",count},{"frames_unsteadied",unsteadied},
                {"frames_faster_than_video",too_soon},{"width",ow},{"height",oh},{"bitrate",bitrate},
                {"fps_target",fps},{"captured_fps",count/elapsed},{"wall_seconds",elapsed},
                {"timing","one sample per new game frame at its arrival time (variable frame rate, no repeats)"},
                {"source_eye_size",{eye.width,eye.height}},{"eye_tangents",{tan_l,tan_r,tan_t,tan_b}},
                {"crop_half_tangents",{frame.half_width,frame.half_height}},{"crop_centre_tangents",{frame.centre_x,frame.centre_y}},{"steady",{{"seconds",tau},{"margin_rad",max_angle}}},
                {"method","OpenVR left-eye mirror; 16:9 view across the eye's width, level with the optical axis, steadied with the pose each frame was rendered with"},
                {"desktop_captured",false},{"input_sent",false},{"audio",false}};
            std::ofstream(out/L"recording.json")<<result.dump(2)<<'\n';
        } else {
            Eye eyes[2]; for (int i=0;i<2;++i) eyes[i].open(compositor,device.Get(),i);
            if (eyes[0].width!=eyes[1].width || eyes[0].height!=eyes[1].height) throw std::runtime_error("Eye sizes differ");
            const UINT eh=std::max(2u,static_cast<UINT>(std::lround(double(ew)*eyes[0].height/eyes[0].width/2))*2);
            if (!std::filesystem::create_directories(out)) throw std::runtime_error("Output appeared during startup; refusing to overwrite");
            output_created=true;
            encoder.open(out/L"clean-sbs.mp4",ew*2,eh,fps);
            std::ofstream frames(out/L"frames.jsonl",std::ios::binary);
            if (!frames) throw std::runtime_error("Cannot write frame sidecar");
            std::vector<BYTE> pixels(static_cast<size_t>(ew)*2*eh*4);
            StereoResize resize; resize.open(device.Get(),ew,eh);
            Sleep(250); const auto start=std::chrono::steady_clock::now();
            uint64_t count=0; const auto start_ms=utc();
            // A hitch, loading stall or hang can hand SteamVR's scene to another process. Nothing is read
            // then (another application is never captured); the video holds its last frame and resumes
            // when WuWa has the scene again, so one recording spans scene changes.
            std::string end_reason="time_limit"; Json pauses=Json::array(); double paused_at=-1, t=0;
            while (true) {
                const auto current=std::chrono::steady_clock::now();
                t=std::chrono::duration<double>(current-start).count();
                if (t>=seconds) break;
                if (std::filesystem::exists(out/L"STOP") || (argc==8&&std::filesystem::exists(argv[7]))) { end_reason="stopped"; break; }
                if (game && WaitForSingleObject(game,0)==WAIT_OBJECT_0) { end_reason="game_exited"; break; }
                if (compositor->GetCurrentSceneFocusProcess()!=pid) {
                    if (paused_at<0) paused_at=t;
                    Sleep(100); continue;
                }
                const auto capture_begin=utc();
                vr::Compositor_FrameTiming timing{sizeof(timing)}; const bool timed=compositor->GetFrameTiming(&timing);
                resize.read(context.Get(),eyes[0].view,eyes[1].view,eyes[0].srgb,eyes[1].srgb,pixels);
                if (compositor->GetCurrentSceneFocusProcess()!=pid) { if (paused_at<0) paused_at=t; continue; }  // frame discarded
                if (paused_at>=0) { pauses.push_back({{"from_s",paused_at},{"to_s",t}}); paused_at=-1; }
                bool nonblack=false; for (size_t i=0;i<pixels.size();i+=128) if (pixels[i]||pixels[i+1]||pixels[i+2]) { nonblack=true; break; }
                if (!count&&!nonblack) throw std::runtime_error("Blank initial mirror; no useful video");
                const auto capture_end=utc();
                encoder.add(pixels,static_cast<LONGLONG>(t*10000000),10000000/fps);
                frames<<Json{{"frame",count},{"video_seconds",t},{"capture_begin_ms",capture_begin},{"capture_end_ms",capture_end},
                    {"clock_ms",GetTickCount64()},{"scene_frame",timed?Json(timing.m_nFrameIndex):Json(nullptr)}}.dump()<<'\n';
                ++count; if (count%fps==0) frames.flush();
                const auto target=start+std::chrono::microseconds(static_cast<int64_t>((std::floor(t*fps)+1)*1000000/fps));
                const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(target-std::chrono::steady_clock::now()).count();
                if (remaining>0) Sleep(static_cast<DWORD>(remaining));
            }
            encoder.close();
            if (paused_at>=0) pauses.push_back({{"from_s",paused_at},{"to_s",t}});
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            result={{"status","recorded"},{"end_reason",end_reason},{"scene_pauses",pauses},
                {"pid",pid},{"started_unix_ms",start_ms},{"frames",count},{"width",ew*2},{"height",eh},
                {"fps_target",fps},{"captured_fps",count/elapsed},{"wall_seconds",elapsed},
                {"timing","wall-clock presentation timestamps; encoder may repeat frames to meet target FPS"},
                {"source_eye_size",{eyes[0].width,eyes[0].height}},{"method","OpenVR per-eye mirror; GPU SBS resize and one readback"},
                {"stereo_gpu_synchronized",false},{"desktop_captured",false},{"input_sent",false},{"audio",false}};
            std::ofstream(out/L"recording.json")<<result.dump(2)<<'\n';
        }
        shutdown(); initialized=false; FreeLibrary(library); library=nullptr; MFShutdown(); media=false; CoUninitialize(); com=false;
        std::cout<<result.dump()<<'\n'; return 0;
    } catch (const std::exception& e) {
        try { encoder.close(); } catch (...) {}
        if (output_created) std::ofstream(out/L"recording-error.txt")<<e.what()<<'\n';
        if (initialized&&shutdown) shutdown(); if (library) FreeLibrary(library); if (media) MFShutdown(); if (com) CoUninitialize();
        std::cerr<<e.what()<<'\n'; return 1;
    }
}
