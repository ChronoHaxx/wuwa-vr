#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

// Pure, opt-in controller test policy. The bridge owns serialization, caller
// verification and delivery. This class never reads devices or changes raw input.
namespace wuwa_input_sequence {
inline constexpr uint32_t max_segments=8, max_total_ms=12000, lead_ms=500, tail_ms=250;
inline constexpr uint32_t external_timeout_ms=2500, game_timeout_ms=500;
inline constexpr int32_t axis_limit=16384, physical_axis_limit=3000;

// Ordinary XInput buttons only: no stick clicks or guide. Trigger bytes are
// separately opt-in so legacy plans preserve subthreshold physical pressure.
inline constexpr uint16_t allowed_buttons=0xf33f;
enum class Context { Gameplay, GameMenu };
constexpr const char* name(Context v) { return v==Context::GameMenu?"game-menu":"gameplay"; }
struct Segment {
    uint32_t duration_ms{};
    int32_t lx{},ly{},rx{},ry{};
    uint16_t buttons{};
    int32_t lt{},rt{};
    bool trigger_axes{};
};
struct RawPad {
    bool connected{};
    uint16_t buttons{};
    uint8_t lt{},rt{};
    int16_t lx{},ly{},rx{},ry{};
};
struct Gates {
    bool focused=true, menu=false, hud_mouse=false, passthrough=false, motion_active=false;
    bool protected_status_fresh=true, slot_compatible=true;
    // menu protects UEVR's own UI. Both script and native-render signals refer
    // to GAME menus; the bridge supplies their independently bounded freshness.
    bool game_menu=false, native_game_menu=false;
};
enum class Phase { Idle, Lead, Running, Tail, Completed, Cancelled };
enum class Reason {
    None, ExplicitStop, NoPoll, LeaseExpired, ExternalHeartbeatLost, GameHeartbeatLost,
    FocusLost, Menu, HudMouse, Passthrough, Motion, Disconnected, PhysicalButtons,
    PhysicalTriggers, PhysicalStick, ClockReversed, ProtectedStatusStale,
    SlotIncompatible, Contention, ContextChanged
};
enum class BeginError { None, AlreadyActive, InvalidId, ReusedId, InvalidSlot, InvalidSegments,
    InvalidDuration, InvalidAxes, Disconnected, PhysicalActivity, StaleGameProof, ClockRange,
    InvalidButtons, InvalidContext, InvalidTriggers };
constexpr const char* name(Phase v) {
    switch(v) {
    case Phase::Idle:return "idle"; case Phase::Lead:return "lead";
    case Phase::Running:return "running"; case Phase::Tail:return "tail";
    case Phase::Completed:return "completed"; case Phase::Cancelled:return "cancelled";
    } return "unknown";
}
constexpr const char* name(Reason v) {
    switch(v) {
    case Reason::None:return "none"; case Reason::ExplicitStop:return "explicit_stop";
    case Reason::NoPoll:return "no_game_poll"; case Reason::LeaseExpired:return "lease_expired";
    case Reason::ExternalHeartbeatLost:return "external_heartbeat_lost";
    case Reason::GameHeartbeatLost:return "game_heartbeat_lost";
    case Reason::FocusLost:return "focus_lost"; case Reason::Menu:return "menu";
    case Reason::HudMouse:return "hud_mouse"; case Reason::Passthrough:return "passthrough";
    case Reason::Motion:return "motion_activity"; case Reason::Disconnected:return "disconnected";
    case Reason::PhysicalButtons:return "physical_buttons";
    case Reason::PhysicalTriggers:return "physical_triggers";
    case Reason::PhysicalStick:return "physical_stick"; case Reason::ClockReversed:return "clock_reversed";
    case Reason::ProtectedStatusStale:return "protected_status_stale";
    case Reason::SlotIncompatible:return "slot_incompatible"; case Reason::Contention:return "contention";
    case Reason::ContextChanged:return "context_changed";
    } return "unknown";
}
constexpr const char* name(BeginError v) {
    switch(v) {
    case BeginError::None:return "none"; case BeginError::AlreadyActive:return "already_active";
    case BeginError::InvalidId:return "invalid_id"; case BeginError::InvalidSlot:return "invalid_slot";
    case BeginError::ReusedId:return "reused_id";
    case BeginError::InvalidSegments:return "invalid_segments";
    case BeginError::InvalidDuration:return "invalid_duration"; case BeginError::InvalidAxes:return "invalid_axes";
    case BeginError::Disconnected:return "disconnected"; case BeginError::PhysicalActivity:return "physical_activity";
    case BeginError::StaleGameProof:return "stale_game_proof"; case BeginError::ClockRange:return "clock_range";
    case BeginError::InvalidButtons:return "invalid_buttons"; case BeginError::InvalidContext:return "invalid_context";
    case BeginError::InvalidTriggers:return "invalid_triggers";
    } return "unknown";
}
constexpr Reason physical_reason(const RawPad& raw) {
    if(!raw.connected)return Reason::Disconnected;
    if(raw.buttons)return Reason::PhysicalButtons;
    if(raw.lt>30 || raw.rt>30)return Reason::PhysicalTriggers;
    for(const int32_t axis : {int32_t(raw.lx),int32_t(raw.ly),int32_t(raw.rx),int32_t(raw.ry)})
        if(axis>physical_axis_limit || axis< -physical_axis_limit)return Reason::PhysicalStick;
    return Reason::None;
}
constexpr Reason guard_reason(const Gates& g,Context context) {
    if(!g.focused)return Reason::FocusLost;
    if(g.menu)return Reason::Menu;
    if(g.hud_mouse)return Reason::HudMouse;
    if(g.passthrough)return Reason::Passthrough;
    if(g.motion_active)return Reason::Motion;
    if(!g.protected_status_fresh)return Reason::ProtectedStatusStale;
    if(!g.slot_compatible)return Reason::SlotIncompatible;
    const bool in_game_menu=g.game_menu || g.native_game_menu;
    if(context==Context::Gameplay && in_game_menu)return Reason::Menu;
    if(context==Context::GameMenu && !in_game_menu)return Reason::ContextChanged;
    return Reason::None;
}
// override_axes retains its name for existing analogue callers; when true the
// bridge also replaces ordinary button bits with synthetic_buttons.
struct Decision {
    bool override_axes{};
    int16_t lx{},ly{},rx{},ry{};
    uint16_t synthetic_buttons{};
    uint8_t synthetic_lt{},synthetic_rt{};
    bool override_triggers{};
};
struct Status {
    bool active{};
    Phase phase=Phase::Idle;
    Reason reason=Reason::None;
    uint32_t slot{}, eligible_polls{};
    bool lead_neutral_observed{}, final_neutral_observed{}, completed{};
    uint32_t segment_mask{};
    uint64_t elapsed_ms{};
    uint32_t duration_ms{}, generated_polls{};
    Context context=Context::Gameplay;
    uint16_t synthetic_buttons{};
    uint8_t synthetic_lt{},synthetic_rt{};
    bool trigger_axes{};
};

class Sequence {
public:
    BeginError begin(uint64_t now,std::string_view lease_id,uint32_t slot,
            std::span<const Segment> segments,const RawPad& initial,uint64_t verified_game_poll_at,
            Context context=Context::Gameplay) {
        if(active())return BeginError::AlreadyActive;
        if(lease_id.empty() || lease_id.size()>64)return BeginError::InvalidId;
        if(lease_id==id())return BeginError::ReusedId;
        for(const char c:lease_id)if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
            (c>='0'&&c<='9') || c=='-' || c=='_'))return BeginError::InvalidId;
        if(slot>3)return BeginError::InvalidSlot;
        if(context!=Context::Gameplay && context!=Context::GameMenu)return BeginError::InvalidContext;
        if(segments.empty() || segments.size()>max_segments)return BeginError::InvalidSegments;
        uint64_t duration=lead_ms+tail_ms;
        bool trigger_axes{};
        for(const auto& s:segments) {
            if(!s.duration_ms)return BeginError::InvalidDuration;
            duration+=s.duration_ms;
            if(duration>max_total_ms)return BeginError::InvalidDuration;
            for(const int32_t axis:{s.lx,s.ly,s.rx,s.ry})
                if(axis>axis_limit || axis< -axis_limit)return BeginError::InvalidAxes;
            if(context==Context::GameMenu && (s.lx || s.ly || s.rx || s.ry))return BeginError::InvalidAxes;
            if(s.buttons & ~allowed_buttons)return BeginError::InvalidButtons;
            if(s.lt<0 || s.lt>255 || s.rt<0 || s.rt>255 ||
                (context==Context::GameMenu && (s.lt || s.rt)))return BeginError::InvalidTriggers;
            trigger_axes=trigger_axes || s.trigger_axes || s.lt || s.rt;
        }
        if(!initial.connected)return BeginError::Disconnected;
        if(physical_reason(initial)!=Reason::None)return BeginError::PhysicalActivity;
        if(now<verified_game_poll_at || now-verified_game_poll_at>game_timeout_ms)return BeginError::StaleGameProof;
        if(now>UINT64_MAX-max_total_ms)return BeginError::ClockRange;
        *this=Sequence{};
        m_id_size=lease_id.size();
        for(size_t i=0;i<m_id_size;++i)m_id[i]=lease_id[i];
        m_count=static_cast<uint32_t>(segments.size());
        for(uint32_t i=0;i<m_count;++i)m_segments[i]=segments[i];
        m_motion_duration=static_cast<uint32_t>(duration-lead_ms-tail_ms);
        m_slot=slot;m_start=m_last=now;m_external=now;m_game=verified_game_poll_at;
        m_context=context;
        m_trigger_axes=trigger_axes;
        m_phase=Phase::Lead;
        return BeginError::None;
    }

    bool heartbeat(uint64_t now,std::string_view lease_id) {
        if(!active() || lease_id!=id())return false;
        check_time(now);
        if(!active())return false;
        m_external=now;
        return true;
    }
    bool stop(uint64_t now,std::string_view lease_id) {
        if(!active() || lease_id!=id())return false;
        cancel(now,Reason::ExplicitStop);
        return true;
    }
    void cancel(uint64_t now,Reason reason) {
        if(!active())return;
        if(now>m_last)m_last=now;
        m_phase=Phase::Cancelled;m_reason=reason;m_synthetic_buttons=0;m_synthetic_lt=m_synthetic_rt=0;
    }
    void tick(uint64_t now,const Gates& g) {
        if(!active())return;
        check_time(now);
        if(!active())return;
        const auto reason=guard_reason(g,m_context);
        if(reason!=Reason::None)cancel(now,reason);
    }
    Decision sample(uint64_t now,uint32_t slot,const RawPad& raw,const Gates& g,bool game_reader) {
        tick(now,g);
        if(!active() || slot!=m_slot)return {};
        const auto physical=physical_reason(raw);
        if(physical!=Reason::None){cancel(now,physical);return {};}
        // Non-game readers can stop on physical input, but cannot manufacture
        // a game heartbeat, start the lead-in, or receive generated axes.
        if(!game_reader)return {};
        m_game=now;++m_polls;m_synthetic_buttons=0;m_synthetic_lt=m_synthetic_rt=0;
        const Decision neutral{true,0,0,0,0,0,0,0,m_trigger_axes};
        if(!m_lead_observed) {
            m_lead_observed=true;m_lead_start=now;
            return neutral;
        }
        const auto elapsed=now-m_lead_start;
        if(elapsed<lead_ms)return neutral;
        uint64_t cursor=lead_ms;
        for(uint32_t i=0;i<m_count;++i) {
            cursor+=m_segments[i].duration_ms;
            if(elapsed<cursor) {
                m_phase=Phase::Running;m_segment_mask|=1u<<i;
                const auto& s=m_segments[i];
                m_synthetic_buttons=s.buttons;
                m_synthetic_lt=static_cast<uint8_t>(s.lt);m_synthetic_rt=static_cast<uint8_t>(s.rt);
                return {true,static_cast<int16_t>(s.lx),static_cast<int16_t>(s.ly),
                    static_cast<int16_t>(s.rx),static_cast<int16_t>(s.ry),s.buttons,
                    m_synthetic_lt,m_synthetic_rt,m_trigger_axes};
            }
        }
        // Keep returning neutral through the tail. Completion needs a second
        // eligible neutral poll at/after its end, not merely elapsed wall time.
        m_phase=Phase::Tail;
        if(m_final_neutral && now-m_tail_start>=tail_ms) {
            m_phase=Phase::Completed;
        }
        if(!m_final_neutral)m_tail_start=now;
        m_final_neutral=true;
        return neutral;
    }
    bool active() const { return m_phase==Phase::Lead || m_phase==Phase::Running || m_phase==Phase::Tail; }
    std::string_view id() const { return {m_id.data(),m_id_size}; }
    Status status() const {
        return {active(),m_phase,m_reason,m_slot,m_polls,m_lead_observed,m_final_neutral,
            m_phase==Phase::Completed,m_segment_mask,m_last>=m_start?m_last-m_start:0,
            lead_ms+m_motion_duration+tail_ms,m_polls,m_context,m_synthetic_buttons,
            m_synthetic_lt,m_synthetic_rt,m_trigger_axes};
    }
private:
    void check_time(uint64_t now) {
        if(now<m_last){cancel(m_last,Reason::ClockReversed);return;}
        m_last=now;
        if(now-m_start>max_total_ms)cancel(now,Reason::LeaseExpired);
        else if(now-m_external>external_timeout_ms)cancel(now,Reason::ExternalHeartbeatLost);
        else if(now-m_game>game_timeout_ms)cancel(now,m_polls?Reason::GameHeartbeatLost:Reason::NoPoll);
    }
    std::array<char,64> m_id{};
    size_t m_id_size{};
    std::array<Segment,max_segments> m_segments{};
    uint32_t m_count{},m_motion_duration{},m_slot{},m_polls{},m_segment_mask{};
    uint64_t m_start{},m_last{},m_external{},m_game{},m_lead_start{},m_tail_start{};
    bool m_lead_observed{},m_final_neutral{};
    Phase m_phase=Phase::Idle;
    Reason m_reason=Reason::None;
    Context m_context=Context::Gameplay;
    uint16_t m_synthetic_buttons{};
    uint8_t m_synthetic_lt{},m_synthetic_rt{};
    bool m_trigger_axes{};
};
}
