#define NOMINMAX
#include "../mod/uevr/src/utility/WuWaHandDemoRenderer.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <dxgi1_4.h>
#include <cstdlib>

using namespace wuwa_hand;
namespace {
int creates{},destroys{},locates{},fail_create{};
bool active[2]{true,true},optical[2]{true,true},locate_error[2]{};
bool invalid{},untracked{};
XRAPI_ATTR XrResult XRAPI_CALL create(XrSession,const XrHandTrackerCreateInfoEXT* info,XrHandTrackerEXT* handle) {
    ++creates;
    assert(info->type==XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT && info->handJointSet==XR_HAND_JOINT_SET_DEFAULT_EXT);
    const auto* source=static_cast<const XrHandTrackingDataSourceInfoEXT*>(info->next);
    assert(source && source->type==XR_TYPE_HAND_TRACKING_DATA_SOURCE_INFO_EXT && source->requestedDataSourceCount==1);
    assert(source->requestedDataSources[0]==XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT);
    if (creates==fail_create) return XR_ERROR_RUNTIME_FAILURE;
    *handle=reinterpret_cast<XrHandTrackerEXT>(uintptr_t(info->hand==XR_HAND_LEFT_EXT?1:2)); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL destroy(XrHandTrackerEXT) { ++destroys; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL locate(XrHandTrackerEXT handle,const XrHandJointsLocateInfoEXT* info,XrHandJointLocationsEXT* out) {
    ++locates; const auto side=reinterpret_cast<uintptr_t>(handle)-1;
    assert(info->baseSpace!=XR_NULL_HANDLE && info->time>0 && out->jointCount==26);
    if (locate_error[side]) return XR_ERROR_RUNTIME_FAILURE;
    auto* source=static_cast<XrHandTrackingDataSourceStateEXT*>(out->next);
    assert(source && source->type==XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT);
    source->isActive=active[side]; source->dataSource=optical[side]?
        XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT:XR_HAND_TRACKING_DATA_SOURCE_CONTROLLER_EXT;
    out->isActive=active[side];
    for(size_t i=0;i<26;++i) {
        auto& j=out->jointLocations[i];
        j.locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT | (untracked?0:XR_SPACE_LOCATION_POSITION_TRACKED_BIT);
        j.pose.position={static_cast<float>(i)*0.008f+(side==0?-0.2f:0.1f),static_cast<float>(i)*0.002f,-0.6f};
        if (invalid) j.pose.position.x=std::numeric_limits<float>::quiet_NaN();
    }
    return XR_SUCCESS;
}
std::array<XrView,2> views() {
    std::array<XrView,2> result{};
    for (auto& v:result) { v.type=XR_TYPE_VIEW; v.pose.orientation.w=1;
        v.fov={-0.785398f,0.785398f,0.785398f,-0.785398f}; }
    result[0].pose.position.x=-0.032f; result[1].pose.position.x=0.032f; return result;
}
void components() {
    Tracking tracker; const auto session=reinterpret_cast<XrSession>(1); const auto space=reinterpret_cast<XrSpace>(2);
    const Api api{create,locate,destroy}; const auto now=Clock::now();
    tracker.configure(session,false,true,api); tracker.enable(true);
    assert(!tracker.snapshot().enabled && tracker.snapshot().state==State::Unsupported);
    tracker.configure(session,true,false,api); tracker.enable(true);
    assert(!tracker.snapshot().enabled && tracker.snapshot().state==State::SourceUnavailable);
    tracker.configure(session,true,true,{}); tracker.enable(true); assert(!tracker.snapshot().enabled);
    tracker.configure(session,true,true,api); assert(tracker.snapshot().state==State::Off && creates==0);
    tracker.enable(true); assert(creates==0); tracker.update(100,space,views(),true,now);
    auto s=tracker.snapshot(now); assert(creates==2 && s.state==State::Active && s.hands[0].optical && s.hands[1].optical);
    assert(geometry(s,Layout::DoubleWide,400,200,now).count==100);
    assert(geometry(s,Layout::Left,200,200,now).count==50);
    assert(geometry(s,Layout::Right,200,200,now).count==50);
    assert(!fresh(s,now+std::chrono::milliseconds(201)) && !fresh(s,now-std::chrono::milliseconds(1)));
    assert(tracker.snapshot(now+std::chrono::milliseconds(201)).state==State::Waiting);
    assert(geometry(s,Layout::DoubleWide,400,200,now+std::chrono::milliseconds(201)).count==0);
    assert(geometry(s,Layout::DoubleWide,400,200,now-std::chrono::milliseconds(1)).count==0);
    assert(geometry(s,Layout::Left,0,100,now).count==0);
    Point left{},right{};
    assert(project({0,0,-1},views()[0],200,200,left) && project({0,0,-1},views()[1],200,200,right));
    assert(left.x>100 && right.x<100 && std::abs(left.y-100)<0.001f);
    auto rotated=views()[0]; rotated.pose.position={0,0,0};
    rotated.pose.orientation={0,0.70710678f,0,0.70710678f};
    assert(project({-1,0,0},rotated,200,200,left) && std::abs(left.x-100)<0.01f);
    assert(!project({0,0,1},views()[0],200,200,left));
    auto bad=views()[0]; bad.fov.angleLeft=std::numeric_limits<float>::quiet_NaN(); assert(!project({0,0,-1},bad,200,200,left));
    bad=views()[0]; bad.pose.orientation.w=0; assert(!project({0,0,-1},bad,200,200,left));
    auto wide=geometry(s,Layout::DoubleWide,400,200,now);
    for(size_t i=0;i<wide.count;++i) for(size_t v=0;v<6;++v) {
        const auto x=wide.segments[i].xy[v*2]; assert(std::isfinite(x));
        assert(i<50?x<=0:x>=0); // each eye remains in its own half
    }
    auto cropped=s; cropped.bounds={{{{0.2f,0.8f,0.1f,0.7f}},{{0.1f,0.9f,0.2f,0.9f}}}};
    cropped.views[0].fov.angleRight=1.0f; cropped.views[1].fov.angleLeft=-1.0f;
    auto crop=geometry(cropped,Layout::DoubleWide,401,200,now); assert(crop.count==100);
    for(size_t i=0;i<crop.count;++i) for(size_t v=0;v<6;++v) {
        const float px=(crop.segments[i].xy[v*2]+1)*401/2;
        const float py=(1-crop.segments[i].xy[v*2+1])*200/2;
        assert(i<50?(px>=39.99f&&px<=160.01f&&py>=19.99f&&py<=140.01f):
            (px>=219.99f&&px<=380.01f&&py>=39.99f&&py<=180.01f));
    }
    cropped.bounds[0][0]=std::numeric_limits<float>::quiet_NaN();
    assert(geometry(cropped,Layout::Left,200,200,now).count==0);
    optical[0]=false; tracker.update(101,space,views(),true,now); s=tracker.snapshot(now);
    assert(!s.hands[0].optical && s.hands[1].optical && geometry(s,Layout::Left,200,200,now).count==25);
    active[1]=false; tracker.update(102,space,views(),true,now); assert(tracker.snapshot(now).state==State::Waiting);
    active[1]=true; optical[0]=true; invalid=true;
    tracker.update(103,space,views(),true,now); assert(tracker.snapshot(now).state==State::Waiting);
    invalid=false; untracked=true; tracker.update(104,space,views(),true,now); assert(tracker.snapshot(now).state==State::Waiting);
    untracked=false; locate_error[0]=true; tracker.update(105,space,views(),true,now);
    assert(!tracker.snapshot(now).hands[0].optical && tracker.snapshot(now).hands[1].optical);
    locate_error[1]=true; tracker.update(106,space,views(),true,now); assert(tracker.snapshot(now).state==State::Error);
    locate_error[0]=locate_error[1]=false;
    tracker.suspend(); assert(destroys==2 && tracker.snapshot(now).state==State::Waiting);
    tracker.update(107,space,views(),false,now); assert(creates==2);
    tracker.update(108,space,views(),true,now); assert(creates==4);
    tracker.enable(false); assert(destroys==4 && !tracker.snapshot(now).enabled);
    fail_create=6; tracker.enable(true); tracker.update(109,space,views(),true,now);
    assert(creates==6 && destroys==5 && !tracker.snapshot(now).enabled && tracker.snapshot(now).state==State::Error);
    fail_create=0; tracker.enable(true); tracker.update(110,space,views(),true,now);
    assert(creates==8); tracker.update(0,space,views(),true,now); assert(destroys==7);
    tracker.update(111,space,views(),true,now); tracker.reset(); assert(creates==10 && destroys==9 && !tracker.snapshot(now).enabled);
    std::cout<<"PASS optical capability, lifecycle, source rejection, loss/stale/failure, projection and eye layouts\n";
}
void reservation() {
    struct Packet { int tag{}; Snapshot sample{}; };
    FrameReservation<Packet> frame;
    Packet first{7},newer{8}; first.sample.views=views(); newer.sample.views=views();
    first.sample.views[0].fov.angleLeft=-0.6f; first.sample.views[1].pose.position.x=0.07f;
    newer.sample.views[0].fov.angleLeft=-1.1f; newer.sample.views[1].pose.position.x=0.12f;
    assert(!frame.reserve(first) && !frame.consume());
    frame.begin(); const auto old_generation=frame.generation();
    assert(frame.reserve(first)->tag==7); frame.drawn(old_generation);
    // A new game-thread packet arrives between left/right draws and end_frame.
    assert(frame.reserve(newer)->tag==7 && frame.peek()->sample.views[0].fov.angleLeft==-0.6f);
    first.sample.views[0].fov.angleLeft=-0.2f; // reservation owns its immutable copy
    auto submitted=frame.consume(); assert(submitted && submitted->tag==7);
    assert(submitted->sample.views[0].fov.angleLeft==-0.6f && submitted->sample.views[1].pose.position.x==0.07f);
    assert(!frame.consume() && !frame.reserve(newer)); // consumed exactly once
    frame.begin(); assert(frame.reserve(newer)); frame.drawn(old_generation);
    assert(!frame.consume()); // late completion from a previous frame is rejected
    frame.begin(); assert(frame.reserve(newer)); frame.drawn(frame.generation()); frame.abort();
    assert(!frame.consume() && !frame.peek()); // abort cannot leak into a reused tag
    frame.begin(); assert(frame.reserve(newer)); assert(!frame.consume()); // renderer failed/no draw
    frame.begin(); const auto generation=frame.generation(); assert(frame.reserve(newer)); frame.drawn(generation);
    submitted=frame.consume(); assert(submitted && submitted->tag==8);
    std::cout<<"PASS immutable per-frame submission, newer enqueue, single consume, stale generation, abort and failed draw\n";
}
void warp11() {
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    const auto result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
    assert(SUCCEEDED(result));
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=400; desc.Height=200; desc.ArraySize=1; desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target; assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&target)));
    ComPtr<ID3D11RenderTargetView> rtv; assert(SUCCEEDED(device->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
    const float black[4]{}; context->ClearRenderTargetView(rtv.Get(),black);
    // A distinctive viewport and target must be restored after the overlay.
    const D3D11_VIEWPORT original{7,9,13,17,0,1}; context->RSSetViewports(1,&original);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    auto* old_target=rtv.Get(); context->OMSetRenderTargets(1,&old_target,nullptr);
    Tracking tracker; tracker.configure(reinterpret_cast<XrSession>(1),true,true,{create,locate,destroy});
    tracker.enable(true); tracker.update(200,reinterpret_cast<XrSpace>(2),views(),true);
    auto s=tracker.snapshot();
    assert(Renderer::get().draw11(context.Get(),target.Get(),s,Layout::DoubleWide));
    D3D11_VIEWPORT restored{}; UINT count=1; context->RSGetViewports(&count,&restored);
    assert(count==1 && restored.TopLeftX==7 && restored.TopLeftY==9 && restored.Width==13);
    ComPtr<ID3D11RenderTargetView> restored_target; context->OMGetRenderTargets(1,&restored_target,nullptr);
    assert(restored_target.Get()==rtv.Get());
    D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
    assert(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback; assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&readback)));
    context->CopyResource(readback.Get(),target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{}; assert(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)));
    size_t lit[2]{}; for(UINT y=0;y<200;++y) for(UINT x=0;x<400;++x) {
        const auto* p=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
        if(p[0]||p[1]||p[2]) ++lit[x/200];
    }
    context->Unmap(readback.Get(),0); assert(lit[0]>30 && lit[1]>30);
    tracker.enable(false); assert(!Renderer::get().draw11(context.Get(),target.Get(),tracker.snapshot(),Layout::DoubleWide));
    Renderer::get().reset11(); tracker.reset();
    std::cout<<"PASS headless D3D11 WARP stereo pixels and RTV/viewport/topology restoration ("<<lit[0]<<", "<<lit[1]<<")\n";
}
void warp12() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory; assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
    ComPtr<IDXGIAdapter> adapter; assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))));
    ComPtr<ID3D12Device> device; assert(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC queue_desc{};
    assert(SUCCEEDED(device->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator> allocator; assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
    ComPtr<ID3D12GraphicsCommandList> commands;
    assert(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands))));
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=400; desc.Height=200; desc.DepthOrArraySize=1; desc.MipLevels=1; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ComPtr<ID3D12Resource> target;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&target))));
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{}; heap_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap_desc.NumDescriptors=1;
    ComPtr<ID3D12DescriptorHeap> descriptors; assert(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc,IID_PPV_ARGS(&descriptors))));
    const auto rtv=descriptors->GetCPUDescriptorHandleForHeapStart(); device->CreateRenderTargetView(target.Get(),nullptr,rtv);
    const float black[4]{}; commands->ClearRenderTargetView(rtv,black,0,nullptr);
    Tracking tracker; tracker.configure(reinterpret_cast<XrSession>(1),true,true,{create,locate,destroy});
    tracker.enable(true); tracker.update(300,reinterpret_cast<XrSpace>(2),views(),true);
    // Reject unsupported MSAA before altering this shared list. A normal draw
    // on the same list must still execute and read back successfully below.
    auto msaa_desc=desc; msaa_desc.SampleDesc.Count=4;
    ComPtr<ID3D12Resource> msaa;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&msaa_desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&msaa))));
    assert(!Renderer::get().draw12(commands.Get(),msaa.Get(),rtv,desc.Format,tracker.snapshot(),Layout::DoubleWide));
    assert(renderer_error==12);
    Renderer::get().reset12(); // No hand draw commands/resources exist yet.
    tracker.update(301,reinterpret_cast<XrSpace>(2),views(),true);
    assert(Renderer::get().draw12(commands.Get(),target.Get(),rtv,desc.Format,tracker.snapshot(),Layout::DoubleWide));
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=target.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1,&barrier);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 size{};
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&size);
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=size;
    buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    assert(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
    D3D12_TEXTURE_COPY_LOCATION dst{},src{}; dst.pResource=readback.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint=footprint; src.pResource=target.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commands->CopyTextureRegion(&dst,0,0,0,&src,nullptr); assert(SUCCEEDED(commands->Close()));
    ID3D12CommandList* lists[]={commands.Get()}; queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence; assert(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));
    const auto event=CreateEventW(nullptr,FALSE,FALSE,nullptr); assert(event);
    assert(SUCCEEDED(queue->Signal(fence.Get(),1))); assert(SUCCEEDED(fence->SetEventOnCompletion(1,event)));
    assert(WaitForSingleObject(event,5000)==WAIT_OBJECT_0); CloseHandle(event);
    void* mapped{}; D3D12_RANGE range{0,static_cast<SIZE_T>(size)}; assert(SUCCEEDED(readback->Map(0,&range,&mapped)));
    size_t lit[2]{}; for(UINT y=0;y<200;++y) for(UINT x=0;x<400;++x) {
        const auto* p=static_cast<const unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;
        if(p[0]||p[1]||p[2]) ++lit[x/200];
    }
    const D3D12_RANGE no_write{0,0}; readback->Unmap(0,&no_write); assert(lit[0]>30 && lit[1]>30);
    tracker.enable(false); assert(!Renderer::get().draw12(commands.Get(),target.Get(),rtv,desc.Format,tracker.snapshot(),Layout::DoubleWide));
    Renderer::get().reset12(); tracker.reset();
    std::cout<<"PASS headless D3D12 WARP MSAA rejection, stereo pixels and fenced resource release ("<<lit[0]<<", "<<lit[1]<<")\n";
}
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    components(); reservation(); warp11(); warp12();
    std::cout<<"Hand demo tests passed. No OpenXR runtime, game or visible window started.\n";
}
