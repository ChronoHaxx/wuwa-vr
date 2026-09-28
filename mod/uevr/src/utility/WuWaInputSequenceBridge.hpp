#pragma once
#include "WuWaInputSequence.hpp"
#include "WuWaInputTrace.hpp"
#include "WuWaMenuSignals.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <nlohmann/json.hpp>

// Developer-only XInput leases. No game calls, hooks, configuration writes or
// default activation. The control thread owns JSON; polling uses fixed storage.
namespace wuwa_input_sequence_bridge {
using Json=nlohmann::json;
namespace policy=wuwa_input_sequence;
struct RuntimeGates { bool focused{},menu{},passthrough{},motion_active{}; int slot_filter{}; };
struct Protected {
    uint64_t at{},script_at{};
    bool valid{},focused{},menu{},hud_mouse{},passthrough{},motion_active{};
    int slot_filter{};
    bool game_menu{};
};
struct Proof { uint64_t at{}; policy::RawPad raw{}; bool eligible{}; };
struct Poll {
    uint64_t at{},epoch{};
    uint32_t api{},slot{},raw_result{},result{};
    uintptr_t caller{};
    bool game_reader{},generated{},decision_available{},complete{},packet_adjusted{};
    XINPUT_STATE raw{},synthetic{},delivered{};
    std::array<char,65> id{};
    policy::Phase phase{};
    policy::Reason reason{};
    policy::Context context=policy::Context::Gameplay;
    uint16_t synthetic_buttons{};
    uint8_t synthetic_lt{},synthetic_rt{};
    bool trigger_axes{};
};
inline std::mutex mutex;
inline policy::Sequence sequence;
inline Protected protected_state;
inline std::array<Proof,4> proofs{};
inline std::array<Poll,16> polls{}; // API, slot and game/non-game reader stay distinct.
// Only armed by delivered synthetic input. A collision at handoff retains a
// fixed packet correction until natural numbering can safely resume. No input
// fields are retained, generated or swallowed on ordinary physical polls.
struct PacketContinuity {
    bool armed{},synthetic{};
    uint32_t source_packet{},delivered_packet{},offset{};
    XINPUT_GAMEPAD delivered_pad{};
};
inline std::array<PacketContinuity,4> packet_continuity{};
inline std::array<std::array<char,65>,64> used_ids{};
inline size_t used_count{};
inline std::atomic<bool> active{};
inline std::atomic<uint64_t> cancel_epoch{},lock_losses{},record_losses{},next_guard_refresh{};
inline uint64_t seen_cancel_epoch{},generated_polls{};
inline uint32_t synthetic_packet{};

inline policy::RawPad raw_pad(uint32_t result,const XINPUT_STATE& value) noexcept {
    const auto& p=value.Gamepad;
    return {result==ERROR_SUCCESS,p.wButtons,p.bLeftTrigger,p.bRightTrigger,
        p.sThumbLX,p.sThumbLY,p.sThumbRX,p.sThumbRY};
}
inline bool game_reader(uintptr_t caller) noexcept {
    if(!caller || !wuwa_test::is_wuwa()) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if(VirtualQuery(reinterpret_cast<const void*>(caller),&memory,sizeof(memory))!=sizeof(memory) ||
        memory.State!=MEM_COMMIT || memory.Type!=MEM_IMAGE ||
        memory.AllocationBase!=GetModuleHandleW(nullptr) || (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const auto access=memory.Protect&0xff;
    return access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ ||
        access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
inline void sync_cancel(uint64_t now) {
    const auto epoch=cancel_epoch.load(std::memory_order_acquire);
    if(epoch!=seen_cancel_epoch) {
        sequence.cancel(now,policy::Reason::Contention);
        seen_cancel_epoch=epoch;
    }
    active.store(sequence.status().active,std::memory_order_release);
}
inline policy::Gates gates(uint64_t now,const RuntimeGates* live=nullptr,uint32_t slot=0) {
    const auto& p=protected_state;
    policy::Gates result{};
    result.protected_status_fresh=p.valid && p.at && now>=p.at && now-p.at<=500 &&
        p.script_at && now>=p.script_at && now-p.script_at<=500;
    result.focused=p.focused && (!live || live->focused);
    result.menu=p.menu || (live && live->menu);
    result.game_menu=p.game_menu;
    // Re-read the actual 250-ms native-render observation. Reusing the boolean
    // from a 500-ms-old protected snapshot would extend a closed menu's lease.
    result.native_game_menu=wuwa_menu::detected(now,false);
    result.hud_mouse=p.hud_mouse;
    result.passthrough=p.passthrough || (live && live->passthrough);
    result.motion_active=p.motion_active || (live && live->motion_active);
    result.slot_compatible=!wuwa_test::filter_input_slot(p.slot_filter,slot) &&
        (!live || !wuwa_test::filter_input_slot(live->slot_filter,slot));
    return result;
}
inline Protected parse_protected(const Json& camera,uint64_t now) {
    Protected next{};
    try {
        const auto& c=camera.at("controls");
        for(const auto* field:{"script_fresh","game_menu","hud_mouse","strict_game_foreground",
                              "native_menu","uevr_menu","motion_active","passthrough"})
            if(!c.at(field).is_boolean()) throw std::runtime_error("invalid guard flag");
        if(!c.at("script_age_ms").is_number_integer() || !c.at("slot_filter").is_number_integer())
            throw std::runtime_error("invalid guard integer");
        const auto age=c.at("script_age_ms").get<int64_t>();
        next.slot_filter=c.at("slot_filter").get<int>();
        next.valid=c.at("script_fresh").get<bool>() && age>=0 && age<=500 &&
            uint64_t(age)<=now && next.slot_filter>=0 && next.slot_filter<=4;
        next.at=now;
        next.script_at=next.valid?now-static_cast<uint64_t>(age):0;
        next.focused=c.at("strict_game_foreground").get<bool>();
        // "native_menu" is the GAME render detector, not another UEVR/OS UI.
        // gates() reads its expiring source directly instead of caching it.
        next.menu=c.at("uevr_menu").get<bool>();
        next.game_menu=c.at("game_menu").get<bool>();
        next.hud_mouse=c.at("hud_mouse").get<bool>();
        next.motion_active=c.at("motion_active").get<bool>();
        next.passthrough=c.at("passthrough").get<bool>();
    } catch(...) { next={}; }
    return next;
}
inline bool needs_guard_refresh(uint64_t now) noexcept {
    if(!active.load(std::memory_order_acquire)) return false;
    auto next=next_guard_refresh.load();
    return now>=next && next_guard_refresh.compare_exchange_strong(next,now+100);
}
inline void refresh_guards(const Json& camera) {
    const auto now=GetTickCount64();
    const auto next=parse_protected(camera,now);
    std::scoped_lock lock{mutex};
    sync_cancel(now); protected_state=next;
    sequence.tick(now,gates(now,nullptr,sequence.status().slot));
    active.store(sequence.status().active,std::memory_order_release);
}
inline int bounded_integer(const Json& value,int minimum,int maximum) {
    int64_t result{};
    if(value.is_number_unsigned()) {
        const auto number=value.get<uint64_t>();
        if(number>static_cast<uint64_t>(maximum)) throw std::runtime_error("Input sequence integer out of range");
        result=static_cast<int64_t>(number);
    } else if(value.is_number_integer()) result=value.get<int64_t>();
    else throw std::runtime_error("Input sequence requires integer fields");
    if(result<minimum || result>maximum) throw std::runtime_error("Input sequence integer out of range");
    return static_cast<int>(result);
}
inline uint16_t parse_buttons(const Json& value) {
    if(!value.is_array()) throw std::runtime_error("Input sequence buttons require an array of names");
    struct Button { std::string_view name; uint16_t bit; };
    static constexpr std::array buttons{
        Button{"a",0x1000},Button{"b",0x2000},Button{"x",0x4000},Button{"y",0x8000},
        Button{"dpad_up",0x0001},Button{"dpad_down",0x0002},Button{"dpad_left",0x0004},Button{"dpad_right",0x0008},
        Button{"start",0x0010},Button{"back",0x0020},Button{"left_shoulder",0x0100},Button{"right_shoulder",0x0200}};
    uint16_t mask{};
    for(const auto& item:value) {
        if(!item.is_string()) throw std::runtime_error("Input sequence button names must be strings");
        const auto& text=item.get_ref<const std::string&>();
        const auto found=std::find_if(buttons.begin(),buttons.end(),[&](const auto& button){return text==button.name;});
        if(found==buttons.end() || (mask & found->bit))
            throw std::runtime_error("Input sequence button name is unknown or repeated");
        mask|=found->bit;
    }
    return mask;
}
struct StatusSnapshot {
    policy::Status state{};
    std::array<char,65> id{};
    uint64_t generated{},lock_loss{},record_loss{};
};
inline StatusSnapshot snapshot_locked(uint64_t now) {
    sync_cancel(now); sequence.tick(now,gates(now,nullptr,sequence.status().slot));
    const auto state=sequence.status();
    active.store(state.active,std::memory_order_release);
    StatusSnapshot result{state,{},generated_polls,lock_losses.load(),record_losses.load()};
    const auto id=sequence.id(); std::copy(id.begin(),id.end(),result.id.begin());
    return result;
}
inline Json status_json(const StatusSnapshot& snapshot) {
    const auto& state=snapshot.state;
    return {{"protocol_version",1},{"button_context_version",1},{"trigger_axis_version",1},
        {"id",snapshot.id.data()},{"active",state.active},{"phase",policy::name(state.phase)},
        {"reason",policy::name(state.reason)},{"user_index",state.slot},{"elapsed_ms",state.elapsed_ms},
        {"duration_ms",state.phase==policy::Phase::Idle?0:state.duration_ms},{"generated_polls",snapshot.generated},
        {"input_sent",snapshot.generated!=0},{"lead_neutral_observed",state.lead_neutral_observed},
        {"tail_neutral_observed",state.final_neutral_observed},{"completed",state.completed},
        {"eligible_polls",state.eligible_polls},{"segment_mask",state.segment_mask},
        {"lock_losses",snapshot.lock_loss},{"record_losses",snapshot.record_loss},
        {"context",policy::name(state.context)},{"synthetic_buttons",state.synthetic_buttons},
        {"trigger_axes",state.trigger_axes},{"synthetic_lt",state.synthetic_lt},{"synthetic_rt",state.synthetic_rt},
        {"source","bounded synthetic controller input before normal mod chain"}};
}
inline Json status() {
    StatusSnapshot snapshot;
    { std::scoped_lock lock{mutex}; snapshot=snapshot_locked(GetTickCount64()); }
    return status_json(snapshot);
}
inline Json request(const Json& request,const Json& camera) {
    const auto action=request.at("action").get<std::string>();
    const auto now=GetTickCount64();
    const auto next=parse_protected(camera,now);
    std::string id;
    uint32_t slot{};
    policy::Context context=policy::Context::Gameplay;
    std::array<policy::Segment,8> segments{};
    size_t count{};
    if(action=="begin") {
        id=request.at("id").get<std::string>();
        if(id.empty() || id.size()>64 || id.find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid input sequence identity");
        slot=static_cast<uint32_t>(bounded_integer(request.at("user_index"),0,3));
        if(request.contains("context")) {
            const auto requested=request.at("context").get<std::string>();
            if(requested=="game-menu") context=policy::Context::GameMenu;
            else if(requested!="gameplay") throw std::runtime_error("Input context must be gameplay or game-menu");
        }
        const auto& plan=request.at("segments");
        if(!plan.is_array() || plan.empty() || plan.size()>8) throw std::runtime_error("Input sequence needs 1..8 segments");
        count=plan.size();
        for(size_t i=0;i<plan.size();++i) {
            const auto& item=plan[i];
            if(!item.is_object()) throw std::runtime_error("Input segments must be objects");
            for(const auto& [key,value]:item.items()) {
                if(key!="duration_ms" && key!="lx" && key!="ly" && key!="rx" && key!="ry" && key!="buttons" && key!="lt" && key!="rt")
                    throw std::runtime_error("Unknown input segment field");
            }
            const auto axis=[&](const char* key){return item.contains(key)?bounded_integer(item.at(key),-16384,16384):0;};
            const auto trigger=[&](const char* key){return item.contains(key)?bounded_integer(item.at(key),0,255):0;};
            segments[i]={static_cast<uint32_t>(bounded_integer(item.at("duration_ms"),1,12000)),
                axis("lx"),axis("ly"),axis("rx"),axis("ry"),
                item.contains("buttons")?parse_buttons(item.at("buttons")):uint16_t{0},
                trigger("lt"),trigger("rt"),item.contains("lt") || item.contains("rt")};
        }
    } else if(action=="heartbeat" || action=="stop") id=request.at("lease_id").get<std::string>();
    else throw std::runtime_error("Unknown input sequence action");
    StatusSnapshot snapshot;
    {
        std::scoped_lock lock{mutex};
        sync_cancel(now); protected_state=next;
        if(action=="begin") {
        for(size_t i=0;i<used_count;++i) if(id==used_ids[i].data())
            throw std::runtime_error("Input sequence identity was already used");
        if(used_count==used_ids.size()) throw std::runtime_error("Input sequence identity capacity reached");
        const auto& proof=proofs[slot];
        if(!proof.eligible) throw std::runtime_error("No verified selected-slot game poll");
        const auto guards=gates(now,nullptr,slot);
        const auto blocked=policy::guard_reason(guards,context);
        if(blocked!=policy::Reason::None)
            throw std::runtime_error(std::string("Input sequence protected guard: ")+policy::name(blocked));
        const auto error=sequence.begin(now,id,slot,std::span<const policy::Segment>{segments.data(),count},proof.raw,proof.at,context);
        if(error!=policy::BeginError::None) throw std::runtime_error(policy::name(error));
        std::copy(id.begin(),id.end(),used_ids[used_count++].begin());
        generated_polls=0; synthetic_packet=0;
        active.store(sequence.status().active,std::memory_order_release);
        next_guard_refresh=now;
        sync_cancel(now); // A concurrent missed poll during begin invalidates it.
        } else {
            // A matching terminal lease remains observable, but cannot be
            // renewed or restarted. Wrong and never-started IDs still fail.
            if(id.empty() || id!=sequence.id())
                throw std::runtime_error("Input sequence lease identity changed");
            if(sequence.active()) {
                if(action=="heartbeat") sequence.heartbeat(now,id);
                else sequence.stop(now,id);
            }
        }
        snapshot=snapshot_locked(now);
    }
    return status_json(snapshot);
}

// Called after the real XInput result and slot filter, before the ordinary mod
// chain. No wait, allocation, JSON, file I/O or component-lock acquisition.
inline Poll before_mods_verified(uint64_t now,bool verified_reader,uint32_t api,uint32_t slot,
                        uint32_t raw_result,const XINPUT_STATE& raw,uint32_t filtered_result,
                        XINPUT_STATE* state,uintptr_t caller,const RuntimeGates& live) noexcept {
    Poll result{};
    result.at=now; result.api=api; result.slot=slot; result.raw_result=raw_result;
    result.raw=raw; result.caller=caller; result.game_reader=verified_reader;
    if(slot>=4) return result;
    std::unique_lock lock{mutex,std::try_to_lock};
    if(!lock.owns_lock()) {
        ++lock_losses; cancel_epoch.fetch_add(1,std::memory_order_release);
        result.reason=policy::Reason::Contention; return result;
    }
    sync_cancel(result.at);
    result.decision_available=true;
    const auto physical=raw_pad(raw_result,raw);
    if(policy::physical_reason(physical)!=policy::Reason::None) proofs[slot].eligible=false;
    const bool eligible_reader=result.game_reader && filtered_result==ERROR_SUCCESS && state &&
        !live.passthrough && !wuwa_test::filter_input_slot(live.slot_filter,slot);
    if(result.game_reader) proofs[slot]={result.at,physical,eligible_reader};
    const auto decision=sequence.sample(result.at,slot,physical,
        gates(result.at,&live,sequence.status().slot),eligible_reader);
    const auto snapshot=sequence.status();
    result.phase=snapshot.phase; result.reason=snapshot.reason; result.context=snapshot.context;
    const auto identity=sequence.id();
    if(identity.size()<result.id.size()) std::copy(identity.begin(),identity.end(),result.id.begin());
    result.epoch=seen_cancel_epoch;
    if(decision.override_axes && filtered_result==ERROR_SUCCESS && state && result.game_reader &&
        result.epoch==cancel_epoch.load(std::memory_order_acquire)) {
        auto& pad=state->Gamepad;
        pad.sThumbLX=decision.lx; pad.sThumbLY=decision.ly; pad.sThumbRX=decision.rx; pad.sThumbRY=decision.ry;
        pad.wButtons=decision.synthetic_buttons;
        if(decision.override_triggers) {
            pad.bLeftTrigger=decision.synthetic_lt;pad.bRightTrigger=decision.synthetic_rt;
            result.trigger_axes=true;result.synthetic_lt=decision.synthetic_lt;result.synthetic_rt=decision.synthetic_rt;
        }
        const auto& continuity=packet_continuity[slot];
        synthetic_packet=continuity.armed?continuity.delivered_packet:raw.dwPacketNumber;
        state->dwPacketNumber=++synthetic_packet;
        result.synthetic=*state; result.generated=true; result.synthetic_buttons=decision.synthetic_buttons; ++generated_polls;
    }
    active.store(sequence.status().active,std::memory_order_release);
    return result;
}
inline Poll before_mods(uint32_t api,uint32_t slot,uint32_t raw_result,const XINPUT_STATE& raw,
                        uint32_t filtered_result,XINPUT_STATE* state,uintptr_t caller,const RuntimeGates& live) noexcept {
    const auto error=GetLastError();
    const auto result=before_mods_verified(GetTickCount64(),game_reader(caller),api,slot,raw_result,raw,
        filtered_result,state,caller,live);
    SetLastError(error);
    return result;
}
inline bool preserve_packet(PacketContinuity& previous,bool generated,XINPUT_STATE& state) noexcept {
    if(!generated && !previous.armed)return false;
    const auto candidate=state.dwPacketNumber;
    const bool changed=previous.armed && !wuwa_test::same_gamepad(previous.delivered_pad,state.Gamepad);
    if(generated) {
        if(previous.armed && candidate==previous.delivered_packet && changed)++state.dwPacketNumber;
        previous.armed=true;
        previous.offset=0;
    } else if(previous.synthetic || candidate!=previous.source_packet) {
        if(candidate==previous.delivered_packet && changed) {
            previous.offset=1;
            state.dwPacketNumber=candidate+previous.offset;
        } else {
            previous.armed=false;
            previous.offset=0;
        }
    } else {
        // Repeated identical physical packets must keep the corrected number,
        // not alternate between it and the previously colliding raw number.
        state.dwPacketNumber=candidate+previous.offset;
        if(state.dwPacketNumber==previous.delivered_packet && changed) {
            ++previous.offset;
            ++state.dwPacketNumber;
        }
    }
    previous.synthetic=generated;
    previous.source_packet=candidate;
    previous.delivered_packet=state.dwPacketNumber;
    previous.delivered_pad=state.Gamepad;
    return state.dwPacketNumber!=candidate;
}
inline void after_mods(Poll poll,uint32_t result,XINPUT_STATE* state) noexcept {
    if(poll.slot>=4) return;
    std::unique_lock lock{mutex,std::try_to_lock};
    if(!lock.owns_lock()) { ++record_losses; cancel_epoch.fetch_add(1,std::memory_order_release); return; }
    sync_cancel(GetTickCount64());
    if(poll.game_reader) {
        auto& continuity=packet_continuity[poll.slot];
        if(result==ERROR_SUCCESS && state && poll.decision_available) {
            poll.packet_adjusted=preserve_packet(continuity,poll.generated,*state);
        } else if(result!=ERROR_SUCCESS)continuity={};
    }
    poll.result=result; poll.delivered=result==ERROR_SUCCESS && state?*state:XINPUT_STATE{}; poll.complete=true;
    polls[(poll.game_reader?0:8)+(poll.api==14?0:4)+poll.slot]=poll;
}
inline Json pad_json(const XINPUT_STATE& value) {
    const auto& p=value.Gamepad;
    return {{"packet",value.dwPacketNumber},{"buttons",p.wButtons},{"lt",p.bLeftTrigger},{"rt",p.bRightTrigger},
        {"lx",p.sThumbLX},{"ly",p.sThumbLY},{"rx",p.sThumbRX},{"ry",p.sThumbRY}};
}
inline Json recording_snapshot(uint64_t now) {
    StatusSnapshot snapshot;
    std::array<Poll,16> saved;
    {
        std::unique_lock lock{mutex,std::try_to_lock};
        if(!lock.owns_lock()) return {{"available",false},{"reason","snapshot_busy"}};
        snapshot=snapshot_locked(now); saved=polls;
    }
    // Ordinary recordings already carry physical/controller samples. Keep the
    // optional test-feed capability visible without duplicating those samples
    // until a lease has actually started in this process.
    if(!snapshot.id[0]) return {{"protocol_version",1},{"button_context_version",1},{"trigger_axis_version",1},{"available",true},{"id",""},
        {"active",false},{"phase","idle"},{"context","gameplay"},{"synthetic_buttons",0},
        {"trigger_axes",false},{"synthetic_lt",0},{"synthetic_rt",0},{"input_sent",false},{"samples",Json::array()}};
    auto result=status_json(snapshot);
    result["available"]=true; result["samples"]=Json::array();
    for(const auto& p:saved) {
        if(!p.complete || !p.at || now<p.at || now-p.at>1000) continue;
        result["samples"].push_back({{"age_ms",now-p.at},{"api",p.api},{"slot",p.slot},
            {"caller",p.caller},{"game_reader",p.game_reader},{"lease_id",p.id.data()},
            {"phase",policy::name(p.phase)},{"reason",policy::name(p.reason)},
            {"context",policy::name(p.context)},{"synthetic_buttons",p.synthetic_buttons},
            {"trigger_axes",p.trigger_axes},{"synthetic_lt",p.synthetic_lt},{"synthetic_rt",p.synthetic_rt},
            {"generated",p.generated},{"decision_available",p.decision_available},
            {"packet_adjusted",p.packet_adjusted},
            {"raw_result",p.raw_result},{"result",p.result},
            {"raw",pad_json(p.raw)},{"synthetic",p.generated?pad_json(p.synthetic):Json(nullptr)},
            {"delivered",pad_json(p.delivered)}});
    }
    result["scope"]="recent individual XInput polls; generated is before normal mods, delivered is after; not proof of gameplay consumption";
    return result;
}
} // namespace wuwa_input_sequence_bridge
