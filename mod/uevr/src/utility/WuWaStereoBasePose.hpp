#pragma once
#include "WuWaPosePair.hpp"
#include <cstdint>
#include <cmath>
#include <limits>

// One native viewport draw's game-camera base, before callbacks and XR offsets.
// No addresses, allocation, clocks, process APIs, projection or history writes.
// Caller owns a stack-scoped instance and masks it across nested draws. Only a
// code-verified native projection-data callsite may set eligible_site=true.
namespace wuwa_stereo_base_pose {
using Pose = wuwa_pose_pair::Pose;
using Frames = wuwa_pose_pair::Frames;
enum class Action { ignored, cached, reused, refused };
enum class Reason {
    none, disabled, ineligible_site, auxiliary, sequence, frame_mismatch,
    invalid_scale, scale_mismatch, invalid_pose, invalidated, ordinal_overflow, device_mismatch
};
constexpr const char* action_name(Action value) noexcept {
    switch(value) {
    case Action::ignored:return "ignored";
    case Action::cached:return "cached";
    case Action::reused:return "reused";
    case Action::refused:return "refused";
    }
    return "unknown";
}
constexpr const char* reason_name(Reason value) noexcept {
    switch(value) {
    case Reason::none:return "none";
    case Reason::disabled:return "disabled";
    case Reason::ineligible_site:return "ineligible_site";
    case Reason::auxiliary:return "auxiliary";
    case Reason::sequence:return "sequence";
    case Reason::frame_mismatch:return "frame_mismatch";
    case Reason::invalid_scale:return "invalid_scale";
    case Reason::scale_mismatch:return "scale_mismatch";
    case Reason::invalid_pose:return "invalid_pose";
    case Reason::invalidated:return "invalidated";
    case Reason::ordinal_overflow:return "ordinal_overflow";
    case Reason::device_mismatch:return "device_mismatch";
    }
    return "unknown";
}
struct Result {
    Action action{Action::ignored};
    Reason reason{Reason::none};
    uint32_t call_ordinal{},source_ordinal{};
    bool applied() const noexcept {return action==Action::reused;}
};
class Pair {
public:
    void begin(Frames frames,bool valid_scope) noexcept {
        expected=frames;base={};scale=0;ordinal=0;source_ordinal=0;source_device=0;
        enabled=valid_scope;invalid=false;have_source=false;consumed=false;
    }
    Result apply(int32_t raw_index,bool eligible_site,Frames frames,
                 float world_to_meters,Pose& pose,uintptr_t device=1) noexcept {
        if(ordinal==std::numeric_limits<uint32_t>::max()) {
            invalid=true;return result(Action::refused,Reason::ordinal_overflow);
        }
        ++ordinal;
        if(!enabled)return result(Action::ignored,Reason::disabled);
        // Non-native/auxiliary callbacks are untouched. In particular raw 1
        // must not replace the base captured from the native pass-2 call.
        if(!eligible_site)return result(Action::ignored,Reason::ineligible_site);
        if(raw_index!=2 && raw_index!=3)return result(Action::ignored,Reason::auxiliary);
        if(invalid)return result(Action::refused,Reason::invalidated);
        if(!device || (source_device && device!=source_device))return refuse(Reason::device_mismatch);
        if(!(frames==expected))return refuse(Reason::frame_mismatch);
        if(!std::isfinite(world_to_meters) || world_to_meters<=0)return refuse(Reason::invalid_scale);
        if(!wuwa_pose_pair::finite(pose))return refuse(Reason::invalid_pose);
        if(raw_index==2) {
            if(have_source || consumed)return refuse(Reason::sequence);
            base=pose;scale=world_to_meters;have_source=true;source_ordinal=ordinal;source_device=device;
            return result(Action::cached,Reason::none);
        }
        if(!have_source || consumed)return refuse(Reason::sequence);
        if(world_to_meters!=scale)return refuse(Reason::scale_mismatch);
        // Copy the untransformed game pose only. Each callback still applies
        // its own mod/HMD/eye transform and projection after this operation.
        pose=base;consumed=true;
        return result(Action::reused,Reason::none);
    }
private:
    Result result(Action action,Reason reason) const noexcept {
        return {action,reason,ordinal,source_ordinal};
    }
    Result refuse(Reason reason) noexcept {
        invalid=true;return result(Action::refused,reason);
    }
    Frames expected{};
    Pose base{};
    float scale{};
    uint32_t ordinal{},source_ordinal{};
    uintptr_t source_device{};
    bool enabled{},invalid{},have_source{},consumed{};
};
} // namespace wuwa_stereo_base_pose
