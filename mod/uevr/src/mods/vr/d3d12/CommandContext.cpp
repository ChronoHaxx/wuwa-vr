#include <spdlog/spdlog.h>
#include <utility/String.hpp>
#include <utility/Logging.hpp>

#include "Framework.hpp"

#include "TextureContext.hpp"
#include "CommandContext.hpp"
#include "../../../utility/WuWaCopyBounds.hpp"

namespace {
wuwa_copy_bounds::Texture copy_layout(ID3D12Resource* resource) {
    const auto d = resource->GetDesc();
    return {uint32_t(d.Dimension), d.Width, d.Height, d.DepthOrArraySize, d.MipLevels,
        d.SampleDesc.Count, d.SampleDesc.Quality, uint32_t(d.Format)};
}
bool region_valid(ID3D12Resource* src, ID3D12Resource* dst, const D3D12_BOX* box,
    UINT x = 0, UINT y = 0, UINT z = 0) {
    if (!src || !dst || src == dst) return false;
    const auto s = copy_layout(src), d = copy_layout(dst);
    if (!box && s.width > UINT32_MAX) return false;
    const wuwa_copy_bounds::Box b = box ? wuwa_copy_bounds::Box{box->left, box->top, box->front,
        box->right, box->bottom, box->back} : wuwa_copy_bounds::Box{0, 0, 0, uint32_t(s.width), s.height,
        s.dimension == 4 ? s.depth_or_array : 1U};
    return wuwa_copy_bounds::region(s, d, b, {x, y, z});
}
}

namespace d3d12 {
bool CommandContext::setup(const wchar_t* name) {
    std::scoped_lock _{this->mtx};

    this->internal_name = name;

    auto& hook = g_framework->get_d3d12_hook();
    auto device = hook->get_device();

    this->cmd_allocator.Reset();
    this->cmd_list.Reset();
    this->fence.Reset();
    this->has_commands = false;
    this->recording_rejected = false;

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&this->cmd_allocator)))) {
        spdlog::error("[VR] Failed to create command allocator for {}", utility::narrow(name));
        return false;
    }

    this->cmd_allocator->SetName(name);

    if (FAILED(device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, this->cmd_allocator.Get(), nullptr, IID_PPV_ARGS(&this->cmd_list)))) {
        spdlog::error("[VR] Failed to create command list for {}", utility::narrow(name));
        return false;
    }
    
    this->cmd_list->SetName(name);

    if (FAILED(device->CreateFence(this->fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&this->fence)))) {
        spdlog::error("[VR] Failed to create fence for {}", utility::narrow(name));
        return false;
    }

    this->fence->SetName(name);
    this->fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    return true;
}

void CommandContext::reset() {
    std::scoped_lock _{this->mtx};
    this->wait(2000);
    //this->on_post_present(VR::get().get());

    this->cmd_allocator.Reset();
    this->cmd_list.Reset();
    this->fence.Reset();
    this->fence_value = 0;
    CloseHandle(this->fence_event);
    this->fence_event = 0;
    this->waiting_for_fence = false;
    this->has_commands = false;
    this->recording_rejected = false;
}

// A failed Close permanently invalidates that command-list object. Recreate
// only this unsubmitted recording; retain completed fence/event bookkeeping.
bool CommandContext::recreate_recording() {
    if (this->waiting_for_fence || (this->fence && this->fence->GetCompletedValue() < this->fence_value)) {
        // Never discard an allocator which may still be used by the GPU.
        this->recording_rejected = true;
        return false;
    }
    this->cmd_list.Reset();
    this->cmd_allocator.Reset();
    this->has_commands = false;
    this->recording_rejected = false;
    auto device = g_framework->get_d3d12_hook()->get_device();
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&this->cmd_allocator))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, this->cmd_allocator.Get(), nullptr,
            IID_PPV_ARGS(&this->cmd_list)))) {
        this->recording_rejected = true;
        spdlog::error("[VR] Failed to recreate discarded command list ({})", utility::narrow(this->internal_name));
        return false;
    }
    this->cmd_allocator->SetName(this->internal_name.c_str());
    this->cmd_list->SetName(this->internal_name.c_str());
    return true;
}

void CommandContext::wait(uint32_t ms) {
    std::scoped_lock _{this->mtx};

    if (!this->cmd_list && !this->waiting_for_fence && this->fence) {
        this->recreate_recording();
    }

    if (!this->waiting_for_fence) return;
    if (!this->fence || !this->fence_event) {
        SPDLOG_ERROR_EVERY_N_SEC(2, "[VR] Pending command fence is unavailable ({})", utility::narrow(this->internal_name));
        return;
    }

    auto completed = this->fence->GetCompletedValue();
    if (completed != UINT64_MAX && completed < this->fence_value) {
        const auto wait_started = GetTickCount64();
        const auto result = WaitForSingleObject(this->fence_event, ms);
        const auto waited_ms = GetTickCount64() - wait_started;
        completed = this->fence->GetCompletedValue();
        if (waited_ms > 250) {
            SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Slow GPU fence wait ({}) elapsed_ms={} requested_timeout_ms={} target_fence={} completed_fence={} result={}",
                utility::narrow(this->internal_name), waited_ms, ms, this->fence_value, completed, result);
        }
        if (result != WAIT_OBJECT_0 && completed < this->fence_value) {
            SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferring command reuse: fence wait incomplete ({}) result={} timeout_ms={}",
                utility::narrow(this->internal_name), result, ms);
            return;
        }
    }
    // UINT64_MAX signals device removal, not successful GPU completion. A
    // timeout/stale event must never release or reset an in-flight allocator.
    if (completed == UINT64_MAX || completed < this->fence_value) {
        SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferring command reuse: fence is not complete ({})", utility::narrow(this->internal_name));
        return;
    }

    ResetEvent(this->fence_event);
    this->waiting_for_fence = false;
    if (FAILED(this->cmd_allocator->Reset())) {
        spdlog::error("[VR] Failed to reset command allocator for {}", utility::narrow(this->internal_name));
        this->recreate_recording();
        return;
    }
    if (FAILED(this->cmd_list->Reset(this->cmd_allocator.Get(), nullptr))) {
        spdlog::error("[VR] Failed to reset command list for {}", utility::narrow(this->internal_name));
        this->recreate_recording();
        return;
    }
    this->has_commands = false;
    this->recording_rejected = false;
}

void CommandContext::copy(ID3D12Resource* src, ID3D12Resource* dst, D3D12_RESOURCE_STATES src_state, D3D12_RESOURCE_STATES dst_state) {
    std::scoped_lock _{this->mtx};

    if (!this->ready() || src == nullptr || dst == nullptr || src == dst ||
        !wuwa_copy_bounds::whole(copy_layout(src), copy_layout(dst))) {
        this->recording_rejected = true;
        SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferred incompatible whole-resource copy ({})", utility::narrow(this->internal_name));
        return;
    }

    // Switch src into copy source.
    D3D12_RESOURCE_BARRIER src_barrier{};

    src_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    src_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    src_barrier.Transition.pResource = src;
    src_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    src_barrier.Transition.StateBefore = src_state;
    src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    // Switch dst into copy destination.
    D3D12_RESOURCE_BARRIER dst_barrier{};
    dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    dst_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    dst_barrier.Transition.pResource = dst;
    dst_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dst_barrier.Transition.StateBefore = dst_state;
    dst_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    {
        D3D12_RESOURCE_BARRIER barriers[2]{src_barrier, dst_barrier};
        this->cmd_list->ResourceBarrier(2, barriers);
    }

    // Copy the resource.
    this->cmd_list->CopyResource(dst, src);

    // Switch back to present.
    src_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    src_barrier.Transition.StateAfter = src_state;
    dst_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    dst_barrier.Transition.StateAfter = dst_state;

    {
        D3D12_RESOURCE_BARRIER barriers[2]{src_barrier, dst_barrier};
        this->cmd_list->ResourceBarrier(2, barriers);
    }

    this->has_commands = true;
}

void CommandContext::copy_region(ID3D12Resource* src, ID3D12Resource* dst, D3D12_BOX* src_box, D3D12_RESOURCE_STATES src_state, D3D12_RESOURCE_STATES dst_state) {
    std::scoped_lock _{this->mtx};

    if (!this->ready() || !region_valid(src, dst, src_box)) {
        this->recording_rejected = true;
        SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferred out-of-bounds/incompatible region copy ({})", utility::narrow(this->internal_name));
        return;
    }

    // Switch src into copy source.
    D3D12_RESOURCE_BARRIER src_barrier{};

    src_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    src_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    src_barrier.Transition.pResource = src;
    src_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    src_barrier.Transition.StateBefore = src_state;
    src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    // Switch dst into copy destination.
    D3D12_RESOURCE_BARRIER dst_barrier{};
    dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    dst_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    dst_barrier.Transition.pResource = dst;
    dst_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dst_barrier.Transition.StateBefore = dst_state;
    dst_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    {
        D3D12_RESOURCE_BARRIER barriers[2]{src_barrier, dst_barrier};
        this->cmd_list->ResourceBarrier(2, barriers);
    }

    // Copy the resource.
    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = src;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    this->cmd_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, src_box);

    // Switch back to present.
    src_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    src_barrier.Transition.StateAfter = src_state;
    dst_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    dst_barrier.Transition.StateAfter = dst_state;

    {
        D3D12_RESOURCE_BARRIER barriers[2]{src_barrier, dst_barrier};
        this->cmd_list->ResourceBarrier(2, barriers);
    }

    this->has_commands = true;
}

void CommandContext::copy_region(ID3D12Resource* src, ID3D12Resource* dst, D3D12_BOX* src_box, UINT dst_x, UINT dst_y, UINT dst_z, D3D12_RESOURCE_STATES src_state, D3D12_RESOURCE_STATES dst_state) {
    std::scoped_lock _{this->mtx};

    if (!this->ready() || !region_valid(src, dst, src_box, dst_x, dst_y, dst_z)) {
        this->recording_rejected = true;
        SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferred out-of-bounds/incompatible offset copy ({})", utility::narrow(this->internal_name));
        return;
    }

    // Switch src into copy source.
    D3D12_RESOURCE_BARRIER src_barrier{};

    src_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    src_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    src_barrier.Transition.pResource = src;
    src_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    src_barrier.Transition.StateBefore = src_state;
    src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

    // Switch dst into copy destination.
    D3D12_RESOURCE_BARRIER dst_barrier{};
    dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    dst_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    dst_barrier.Transition.pResource = dst;
    dst_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dst_barrier.Transition.StateBefore = dst_state;
    dst_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    {
        D3D12_RESOURCE_BARRIER barriers[2]{src_barrier, dst_barrier};
        this->cmd_list->ResourceBarrier(2, barriers);
    }

    // Copy the resource.
    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = src;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = dst;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.SubresourceIndex = 0;

    this->cmd_list->CopyTextureRegion(&dst_loc, dst_x, dst_y, dst_z, &src_loc, src_box);

    // Switch back to present.
    src_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    src_barrier.Transition.StateAfter = src_state;
    dst_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    dst_barrier.Transition.StateAfter = dst_state;

    {
        D3D12_RESOURCE_BARRIER barriers[2]{src_barrier, dst_barrier};
        this->cmd_list->ResourceBarrier(2, barriers);
    }

    this->has_commands = true;
}

// More optimal than two copy_region calls.
void CommandContext::copy_region_stereo(ID3D12Resource* srcleft, ID3D12Resource* srcright, ID3D12Resource* dst, D3D12_BOX* srcleft_box, D3D12_BOX* srcright_box,
    UINT dstleft_x, UINT dstleft_y, UINT dstleft_z,
    UINT dstright_x, UINT dstright_y, UINT dstright_z,
    D3D12_RESOURCE_STATES src_state,
    D3D12_RESOURCE_STATES dst_state)
{
    std::scoped_lock _{this->mtx};
    // Validate BOTH regions before recording either barrier/copy. In a mode
    // transition the new XR target may precede the resized engine resources.
    if (!this->ready() || srcleft == srcright ||
        !region_valid(srcleft, dst, srcleft_box, dstleft_x, dstleft_y, dstleft_z) ||
        !region_valid(srcright, dst, srcright_box, dstright_x, dstright_y, dstright_z)) {
        this->recording_rejected = true;
        SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferred out-of-bounds/incompatible stereo copy ({})", utility::narrow(this->internal_name));
        return;
    }

    // Transition states to copy source / dest.
    D3D12_RESOURCE_BARRIER barriers[3]
    {
        { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {srcleft, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, src_state, D3D12_RESOURCE_STATE_COPY_SOURCE} },
        { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {srcright, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, src_state, D3D12_RESOURCE_STATE_COPY_SOURCE} },
        { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {dst, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, dst_state, D3D12_RESOURCE_STATE_COPY_DEST} }
    };
    
    this->cmd_list->ResourceBarrier(3, barriers);

    // Copy left half
    D3D12_TEXTURE_COPY_LOCATION src_loc_left = { srcleft, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };
    D3D12_TEXTURE_COPY_LOCATION dst_loc = { dst, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };

    this->cmd_list->CopyTextureRegion(&dst_loc, dstleft_x, dstleft_y, dstleft_z, &src_loc_left, srcleft_box);

    // Copy right half
    D3D12_TEXTURE_COPY_LOCATION src_loc_right = { srcright, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, 0 };

    this->cmd_list->CopyTextureRegion(&dst_loc, dstright_x, dstright_y, dstright_z, &src_loc_right, srcright_box);

    // Transition states back to original.
    barriers[0] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {srcleft, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, src_state} };
    barriers[1] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {srcright, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, src_state} };
    barriers[2] = { D3D12_RESOURCE_BARRIER_TYPE_TRANSITION, D3D12_RESOURCE_BARRIER_FLAG_NONE, {dst, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST, dst_state} };

    this->cmd_list->ResourceBarrier(3, barriers);

    this->has_commands = true;
}

void CommandContext::clear_rtv(ID3D12Resource* dst, D3D12_CPU_DESCRIPTOR_HANDLE rtv, const float* color, D3D12_RESOURCE_STATES dst_state, UINT rect_count, const D3D12_RECT* rects) {
    std::scoped_lock _{this->mtx};

    if (!this->ready() || dst == nullptr) {
        this->recording_rejected = true;
        SPDLOG_WARNING_EVERY_N_SEC(2, "[VR] Deferred clear: resource or command context is not ready ({})", utility::narrow(this->internal_name));
        return;
    }

    // Switch dst into copy destination.
    D3D12_RESOURCE_BARRIER dst_barrier{};
    dst_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    dst_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    dst_barrier.Transition.pResource = dst;
    dst_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    dst_barrier.Transition.StateBefore = dst_state;
    dst_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // No need to switch if we're already in the right state.
    if (dst_state != dst_barrier.Transition.StateAfter) {
        D3D12_RESOURCE_BARRIER barriers[1]{dst_barrier};
        this->cmd_list->ResourceBarrier(1, barriers);
    }

    // Clear the resource.
    this->cmd_list->ClearRenderTargetView(rtv, color, rect_count, rects);

    // Switch back to present.
    dst_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    dst_barrier.Transition.StateAfter = dst_state;

    if (dst_state != dst_barrier.Transition.StateBefore) {
        D3D12_RESOURCE_BARRIER barriers[1]{dst_barrier};
        this->cmd_list->ResourceBarrier(1, barriers);
    }

    this->has_commands = true;
}

void CommandContext::clear_rtv(d3d12::TextureContext& tex, const float* color, D3D12_RESOURCE_STATES dst_state, UINT rect_count, const D3D12_RECT* rects) {
    if (tex.texture == nullptr || tex.rtv_heap == nullptr) {
        return;
    }

    this->clear_rtv(tex.texture.Get(), tex.get_rtv(), color, dst_state, rect_count, rects);
}

bool CommandContext::execute() {
    std::scoped_lock _{this->mtx};

    if (this->waiting_for_fence) return false;

    if (this->recording_rejected) {
        this->recreate_recording();
        return false;
    }
    if (!this->cmd_list) return false;
    
    if (this->has_commands) {
        const auto close_result = this->cmd_list->Close();
        if (FAILED(close_result)) {
            spdlog::error("[VR] Failed to close command list; discarding recording. ({}) HRESULT={:08x}",
                utility::narrow(this->internal_name), uint32_t(close_result));
            this->recreate_recording();
            return false;
        }
        
        auto command_queue = g_framework->get_d3d12_hook()->get_command_queue();
        ID3D12CommandList* const cmd_lists[] = {this->cmd_list.Get()};
        command_queue->ExecuteCommandLists(1, cmd_lists);
        command_queue->Signal(this->fence.Get(), ++this->fence_value);
        this->fence->SetEventOnCompletion(this->fence_value, this->fence_event);
        this->waiting_for_fence = true;
        this->has_commands = false;
        return true;
    }
    return false;
}
}
