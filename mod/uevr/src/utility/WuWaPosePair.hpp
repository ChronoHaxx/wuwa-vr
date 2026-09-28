#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>

// CPU observation policy only: one viewport-draw scope, no allocation, clocks,
// locks, game pointers, JSON, or pose writes. The caller owns thread confinement.
namespace wuwa_pose_pair {
inline constexpr size_t call_capacity=8;
inline constexpr size_t constructor_capacity=4;
template<class T> struct Field {T value{};bool valid{};};
using Matrix=std::array<float,16>;
template<class T,class Read> void field(uintptr_t address,uintptr_t offset,Field<T>& value,Read& read) noexcept {
    value={};
    if (address && address<=UINTPTR_MAX-offset && address+offset<=UINTPTR_MAX-sizeof(T))
        value.valid=read(address+offset,value.value);
}
struct InitView {
    uintptr_t address{};
    Field<std::array<float,3>> origin{};
    Field<Matrix> rotation{},projection{},alternate_projection{};
    Field<uint8_t> alternate_flag{};
    Field<uintptr_t> family{},state{};
    Field<int32_t> pass{};
};
template<class Read> InitView init_view(uintptr_t address,Read read) noexcept {
    InitView v{};v.address=address;
    field(address,0,v.origin,read);field(address,0x10,v.rotation,read);
    field(address,0x50,v.projection,read);field(address,0xb0,v.alternate_projection,read);
    field(address,0xf0,v.alternate_flag,read);field(address,0xf8,v.family,read);
    field(address,0x100,v.state,read);field(address,0x150,v.pass,read);
    return v;
}
// Verified offsets within each matrix object, not a claim about which object
// an eventual renderer pass selects. In particular +7e0 is not labelled previous.
inline constexpr std::array<uint32_t,5> matrix_offsets{0,0xc0,0x200,0x280,0x300};
struct CompletedView {
    uintptr_t address{};
    Field<uintptr_t> family{},state{};
    Field<int32_t> pass{};
    std::array<Field<Matrix>,5> object_320{},object_7e0{};
};
template<class Read> CompletedView completed_view(uintptr_t address,Read read) noexcept {
    CompletedView v{};v.address=address;
    field(address,0,v.family,read);field(address,8,v.state,read);field(address,0xc90,v.pass,read);
    for(size_t i=0;i<matrix_offsets.size();++i) {
        field(address,0x320+matrix_offsets[i],v.object_320[i],read);
        field(address,0x7e0+matrix_offsets[i],v.object_7e0[i],read);
    }
    return v;
}
struct Pose {
    std::array<double,3> position{}, rotation{};
    bool valid{};
};
inline bool finite(const Pose& pose) noexcept {
    if (!pose.valid) return false;
    for (const auto v:pose.position) if (!std::isfinite(v)) return false;
    for (const auto v:pose.rotation) if (!std::isfinite(v)) return false;
    return true;
}
struct Frames {
    uint32_t runtime_frame{}, game_frame{}, thread{};
    bool operator==(const Frames&) const = default;
};
struct Scope {
    uint64_t recording_session{}, draw_sequence{}, parent_draw_sequence{}, begin_ms{};
    Frames frames{};
};
struct Call {
    uint32_t ordinal{};
    int32_t raw_index{}, logical_eye{-1};
    uint64_t begin_ms{}, end_ms{};
    Frames input_frames{}, after_pre_frames{}, output_frames{};
    Pose input_game{}, after_pre_callbacks{}, output{};
    bool has_after_pre{}, finished{};
    uint32_t callsite_rva{}, stereo_base_source_ordinal{};
    uint8_t stereo_base_action{}, stereo_base_reason{};
    bool stereo_base_observed{};
};
struct Constructor {
    uint32_t ordinal{},preceding_call_ordinal{};
    uintptr_t destination{};
    uint64_t begin_ms{},end_ms{};
    Frames input_frames{},output_frames{};
    InitView input{};
    CompletedView output{};
    bool layout_verified{},finished{},result_matches_destination{},valid{};
};
template<size_t N> bool finite_field(const Field<std::array<float,N>>& value) noexcept {
    if(!value.valid) return false;
    for(const auto number:value.value) if(!std::isfinite(number)) return false;
    return true;
}
inline bool constructor_valid(const Constructor& c) noexcept {
    if(!c.layout_verified || !c.finished || !c.result_matches_destination || !(c.input_frames==c.output_frames)) return false;
    const auto& a=c.input;const auto& b=c.output;
    if(!finite_field(a.origin) || !finite_field(a.rotation) || !finite_field(a.projection) ||
        !a.alternate_flag.valid || (a.alternate_flag.value && !finite_field(a.alternate_projection)) || !a.family.valid ||
        !a.state.valid || !a.pass.valid || !b.family.valid || !b.state.valid || !b.pass.valid ||
        a.family.value!=b.family.value || a.state.value!=b.state.value || a.pass.value!=b.pass.value) return false;
    for(size_t i=0;i<matrix_offsets.size();++i)
        if(!finite_field(b.object_320[i]) || !finite_field(b.object_7e0[i])) return false;
    return true;
}
struct Pair {
    Scope scope{};
    std::array<Call,call_capacity> calls{};
    std::array<Constructor,constructor_capacity> constructors{};
    uint64_t end_ms{};
    uint32_t call_count{}, stored_calls{};
    uint32_t constructor_count{},stored_constructors{};
    std::array<uint32_t,2> eye_calls{};
    bool available{}, closed{}, recording_continued{}, valid{};
    bool overflow{}, missing_eye{}, duplicate_eye{}, invalid_eye{};
    bool frame_mismatch{}, invalid_pose{}, incomplete_call{};
    bool constructor_overflow{};
};
struct Token {
    uint64_t draw_sequence{};
    size_t index{call_capacity};
};
struct ConstructorToken {uint64_t draw_sequence{};size_t index{constructor_capacity};};
class Draw {
public:
    void begin(bool enabled, Scope scope) noexcept {
        data={};
        open=enabled && scope.recording_session && scope.draw_sequence;
        if (open) {data.scope=scope;data.available=true;}
    }
    uint64_t sequence() const noexcept {return data.scope.draw_sequence;}
    template<class Read>
    Token input(bool recording_active, int32_t raw_index, int32_t logical_eye,
                Frames frames, uint64_t now, Read read) noexcept {
        if (!open || !recording_active) return {};
        if (data.call_count!=UINT32_MAX) ++data.call_count;
        if (data.stored_calls==call_capacity) {data.overflow=true;return {};}
        const auto index=data.stored_calls++;
        auto& call=data.calls[index];
        call.ordinal=data.call_count;call.raw_index=raw_index;call.logical_eye=logical_eye;
        call.input_frames=frames;call.begin_ms=now;call.input_game=read();
        return {data.scope.draw_sequence,index};
    }
    template<class Read>
    void after_pre(Token token, bool recording_active, Frames frames, Read read) noexcept {
        if (auto* call=find(token);call && recording_active) {
            call->after_pre_frames=frames;call->after_pre_callbacks=read();call->has_after_pre=true;
        }
    }
    void stereo_base(Token token, uint32_t callsite_rva, uint8_t action, uint8_t reason,
                     uint32_t source_ordinal) noexcept {
        if (auto* call=find(token)) {
            call->callsite_rva=callsite_rva;call->stereo_base_action=action;
            call->stereo_base_reason=reason;call->stereo_base_source_ordinal=source_ordinal;
            call->stereo_base_observed=true;
        }
    }
    template<class Read>
    void output(Token token, bool recording_active, Frames frames, uint64_t now, Read read) noexcept {
        if (auto* call=find(token);call && recording_active) {
            call->output_frames=frames;call->output=read();call->end_ms=now;call->finished=true;
        }
    }
    template<class Read>
    ConstructorToken constructor_input(bool recording_active,bool layout_verified,uintptr_t destination,
            uintptr_t init,Frames frames,uint64_t now,Read read) noexcept {
        if (!open || !recording_active) return {};
        if(data.constructor_count!=UINT32_MAX) ++data.constructor_count;
        if(data.stored_constructors==constructor_capacity) {data.constructor_overflow=true;return {};}
        const auto index=data.stored_constructors++;
        auto& c=data.constructors[index];c.ordinal=data.constructor_count;
        // Temporal adjacency only; the native constructor has no callback token.
        c.preceding_call_ordinal=data.call_count;c.destination=destination;
        c.begin_ms=now;c.input_frames=frames;c.layout_verified=layout_verified;
        if(layout_verified) c.input=init_view(init,read);
        return {data.scope.draw_sequence,index};
    }
    template<class Read>
    void constructor_output(ConstructorToken token,bool recording_active,uintptr_t view,
            Frames frames,uint64_t now,Read read) noexcept {
        if(!open || !recording_active || token.draw_sequence!=data.scope.draw_sequence || token.index>=data.stored_constructors) return;
        auto& c=data.constructors[token.index];c.finished=true;c.end_ms=now;c.output_frames=frames;
        c.result_matches_destination=view && view==c.destination;
        if(c.layout_verified && c.result_matches_destination) c.output=completed_view(view,read);
        c.valid=constructor_valid(c);
    }
    Pair finish(bool recording_active, uint64_t now) noexcept {
        if (!open) return {};
        open=false;data.closed=true;data.recording_continued=recording_active;data.end_ms=now;
        for (size_t i=0;i<data.stored_calls;++i) {
            const auto& call=data.calls[i];
            if (call.logical_eye>=0 && call.logical_eye<2) ++data.eye_calls[call.logical_eye];
            else data.invalid_eye=true;
            data.incomplete_call |= !call.has_after_pre || !call.finished;
            data.invalid_pose |= !finite(call.input_game) || (call.has_after_pre && !finite(call.after_pre_callbacks)) ||
                (call.finished && !finite(call.output));
            data.frame_mismatch |= !(call.input_frames==data.scope.frames) ||
                (call.has_after_pre && !(call.after_pre_frames==call.input_frames)) ||
                (call.finished && !(call.output_frames==call.input_frames));
        }
        data.missing_eye=!data.eye_calls[0] || !data.eye_calls[1];
        data.duplicate_eye=data.eye_calls[0]>1 || data.eye_calls[1]>1;
        data.valid=data.recording_continued && !data.overflow && !data.missing_eye &&
            !data.duplicate_eye && !data.invalid_eye && !data.frame_mismatch &&
            !data.invalid_pose && !data.incomplete_call;
        return data;
    }
private:
    Call* find(Token token) noexcept {
        return open && token.draw_sequence==data.scope.draw_sequence && token.index<data.stored_calls ?
            &data.calls[token.index] : nullptr;
    }
    Pair data{};
    bool open{};
};
} // namespace wuwa_pose_pair
