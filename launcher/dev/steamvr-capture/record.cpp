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
    void open(const std::filesystem::path& path,UINT width,UINT height,UINT fps) {
        w=width; h=height;
        ComPtr<IMFAttributes> attrs; check(MFCreateAttributes(&attrs,2));
        check(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE));
        check(MFCreateSinkWriterFromURL(path.c_str(),nullptr,attrs.Get(),&writer));
        ComPtr<IMFMediaType> out,in; check(MFCreateMediaType(&out));
        check(out->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video)); check(out->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
        check(out->SetUINT32(MF_MT_AVG_BITRATE,16000000)); check(out->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive));
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
int wmain(int argc,wchar_t** argv) {
    HMODULE library{}; void(__cdecl* shutdown)(){}; bool initialized=false,media=false,com=false;
    Encoder encoder; Json result; std::filesystem::path out; bool output_created=false;
    try {
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
        if (argc!=7 && argc!=8) throw std::runtime_error("Expected openvr_api.dll, game PID, new output folder, seconds (1..300), FPS (10..60), eye width (640..1920), optional stop file");
        const UINT pid=std::stoul(argv[2]), seconds=std::stoul(argv[4]), fps=std::stoul(argv[5]), ew=std::stoul(argv[6]); out=argv[3];
        if (!pid||seconds<1||seconds>300||fps<10||fps>60||ew<640||ew>1920||ew%2) throw std::runtime_error("Invalid recording bounds");
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
        {
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
            while (true) {
                const auto current=std::chrono::steady_clock::now();
                const double t=std::chrono::duration<double>(current-start).count();
                if (t>=seconds || std::filesystem::exists(out/L"STOP") || (argc==8&&std::filesystem::exists(argv[7]))) break;
                if (compositor->GetCurrentSceneFocusProcess()!=pid) throw std::runtime_error("Scene changed; recording stopped before capturing another application");
                const auto capture_begin=utc();
                vr::Compositor_FrameTiming timing{sizeof(timing)}; const bool timed=compositor->GetFrameTiming(&timing);
                resize.read(context.Get(),eyes[0].view,eyes[1].view,eyes[0].srgb,eyes[1].srgb,pixels);
                if (compositor->GetCurrentSceneFocusProcess()!=pid) throw std::runtime_error("Scene changed during readback; frame discarded");
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
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            result={{"status","recorded"},{"pid",pid},{"started_unix_ms",start_ms},{"frames",count},{"width",ew*2},{"height",eh},
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
