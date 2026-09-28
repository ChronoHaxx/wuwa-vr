#include "../upstream/UEVR/src/utility/WuWaInputSequence.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <string>

using namespace wuwa_input_sequence;
namespace {
const RawPad neutral{true};
const Gates safe{};
const std::array path{Segment{300,0,12000,0,0},Segment{200,0,0,-8000,4000}};
Sequence started(uint64_t at=1000) {
    Sequence s;
    assert(s.begin(at,"run-1",2,path,neutral,at)==BeginError::None);
    return s;
}
bool zero(const Decision& d) {return d.override_axes && !d.lx && !d.ly && !d.rx && !d.ry && !d.synthetic_buttons && !d.synthetic_lt && !d.synthetic_rt;}
void require_cancel(const Sequence& s,Reason reason) {
    const auto state=s.status();
    assert(!state.active && !state.completed && state.phase==Phase::Cancelled && state.reason==reason);
}
bool same(const RawPad& a,const RawPad& b) {
    return a.connected==b.connected && a.buttons==b.buttons && a.lt==b.lt && a.rt==b.rt &&
        a.lx==b.lx && a.ly==b.ly && a.rx==b.rx && a.ry==b.ry;
}
void timing_and_completion() {
    auto s=started();
    assert(s.status().duration_ms==1250 && !s.status().lead_neutral_observed);
    assert(zero(s.sample(1000,2,neutral,safe,true)));
    for(int i=0;i<20;++i)assert(zero(s.sample(1000,2,neutral,safe,true)));
    assert(zero(s.sample(1499,2,neutral,safe,true)));
    const auto walk=s.sample(1500,2,neutral,safe,true);
    assert(walk.override_axes && walk.ly==12000 && walk.rx==0);
    for(int i=0;i<40;++i) {
        const auto duplicate=s.sample(1500,2,neutral,safe,true);
        assert(duplicate.ly==walk.ly && duplicate.rx==walk.rx);
    }
    assert(s.sample(1799,2,neutral,safe,true).ly==12000);
    const auto rotate=s.sample(1800,2,neutral,safe,true);
    assert(rotate.override_axes && rotate.ly==0 && rotate.rx==-8000 && rotate.ry==4000);
    assert(zero(s.sample(2000,2,neutral,safe,true)));
    assert(s.status().final_neutral_observed && s.status().active);
    assert(zero(s.sample(2249,2,neutral,safe,true)));
    assert(s.status().active);
    assert(zero(s.sample(2250,2,neutral,safe,true)));
    const auto done=s.status();
    assert(done.completed && !done.active && done.phase==Phase::Completed && done.reason==Reason::None);
    assert(done.segment_mask==3 && done.generated_polls==done.eligible_polls && done.elapsed_ms==1250);
    assert(!s.sample(2251,2,neutral,safe,true).override_axes);
    assert(!s.heartbeat(2251,"run-1"));
    assert(s.begin(2251,"run-1",2,path,neutral,2251)==BeginError::ReusedId);
    assert(s.begin(2251,"run-2",2,path,neutral,2251)==BeginError::None);
}
void caller_slot_and_poll_proof() {
    auto s=started();
    auto active=neutral;active.buttons=1;
    assert(!s.sample(1100,1,active,safe,true).override_axes);
    assert(!s.sample(1400,2,neutral,safe,false).override_axes);
    assert(s.status().eligible_polls==0 && !s.status().lead_neutral_observed);
    assert(zero(s.sample(1500,2,neutral,safe,true)));
    assert(zero(s.sample(1750,2,neutral,safe,true)));
    assert(s.sample(2000,2,neutral,safe,true).ly==12000);

    auto none=started();
    none.tick(1501,safe);
    require_cancel(none,Reason::NoPoll);
    assert(!none.status().lead_neutral_observed && !none.status().final_neutral_observed);
    assert(!none.sample(1502,2,neutral,safe,true).override_axes);

    auto lost=started();
    assert(zero(lost.sample(1000,2,neutral,safe,true)));
    assert(!lost.sample(1501,2,neutral,safe,true).override_axes);
    require_cancel(lost,Reason::GameHeartbeatLost);
    assert(!lost.heartbeat(1502,"run-1"));

    auto non_game=started();
    assert(!non_game.sample(1001,2,active,safe,false).override_axes);
    require_cancel(non_game,Reason::PhysicalButtons);
}
void all_gates() {
    struct Case {Gates gates;Reason reason;};
    std::array<Case,7> cases{};
    cases[0].gates.focused=false;cases[0].reason=Reason::FocusLost;
    cases[1].gates.menu=true;cases[1].reason=Reason::Menu;
    cases[2].gates.hud_mouse=true;cases[2].reason=Reason::HudMouse;
    cases[3].gates.passthrough=true;cases[3].reason=Reason::Passthrough;
    cases[4].gates.motion_active=true;cases[4].reason=Reason::Motion;
    cases[5].gates.protected_status_fresh=false;cases[5].reason=Reason::ProtectedStatusStale;
    cases[6].gates.slot_compatible=false;cases[6].reason=Reason::SlotIncompatible;
    for(const auto& c:cases) {
        auto s=started();
        s.sample(1000,2,neutral,safe,true);
        s.tick(1001,c.gates);
        require_cancel(s,c.reason);
        assert(!s.sample(1002,2,neutral,safe,true).override_axes);
    }
    auto c=started();c.cancel(1001,Reason::Contention);require_cancel(c,Reason::Contention);
}
void physical_priority_and_no_mutation() {
    std::array<RawPad,8> pads{};
    for(auto& p:pads)p=neutral;
    pads[0].connected=false;pads[1].buttons=0x8000;pads[2].lt=31;pads[3].rt=255;
    pads[4].lx=-3001;pads[5].ly=3001;pads[6].rx=std::numeric_limits<int16_t>::min();pads[7].ry=3001;
    const std::array reasons{Reason::Disconnected,Reason::PhysicalButtons,Reason::PhysicalTriggers,
        Reason::PhysicalTriggers,Reason::PhysicalStick,Reason::PhysicalStick,Reason::PhysicalStick,Reason::PhysicalStick};
    for(size_t i=0;i<pads.size();++i) {
        auto s=started();const auto before=pads[i];
        assert(!s.sample(1001,2,pads[i],safe,true).override_axes);
        assert(same(before,pads[i]));require_cancel(s,reasons[i]);
    }
    auto drift=neutral;drift.lt=30;drift.rt=30;drift.lx=-3000;drift.ly=3000;drift.rx=-3000;drift.ry=3000;
    const auto before=drift;
    Sequence s;assert(s.begin(1000,"drift",2,path,drift,1000)==BeginError::None);
    assert(zero(s.sample(1000,2,drift,safe,true)));
    assert(s.sample(1500,2,drift,safe,true).ly==12000);
    assert(same(drift,before));
}
void matching_id_and_deadlines() {
    auto s=started();
    assert(!s.stop(1100,"other") && !s.heartbeat(1100,"other"));
    assert(s.status().elapsed_ms==0 && s.status().active);
    assert(s.stop(1100,"run-1"));require_cancel(s,Reason::ExplicitStop);
    assert(!s.heartbeat(1101,"run-1"));

    const std::array longest{Segment{11250,0,4000,10000,0}};
    Sequence timeout;assert(timeout.begin(0,"timeout",0,longest,neutral,0)==BeginError::None);
    for(uint64_t at=0;at<=2500;at+=100)assert(timeout.sample(at,0,neutral,safe,true).override_axes);
    assert(!timeout.sample(2501,0,neutral,safe,true).override_axes);
    require_cancel(timeout,Reason::ExternalHeartbeatLost);
    assert(!timeout.heartbeat(2502,"timeout"));

    Sequence limit;assert(limit.begin(0,"limit",0,longest,neutral,0)==BeginError::None);
    // First eligible poll is delayed. Lead and tail still need real neutral
    // polls; they cannot silently extend the 12-second absolute lease.
    for(uint64_t at=100;at<=12000;at+=100) {
        if(at%2000==0)assert(limit.heartbeat(at,"limit"));
        assert(limit.sample(at,0,neutral,safe,true).override_axes);
    }
    assert(!limit.sample(12001,0,neutral,safe,true).override_axes);
    require_cancel(limit,Reason::LeaseExpired);
    assert(limit.status().final_neutral_observed && !limit.status().completed);

    auto reverse=started();reverse.sample(1000,2,neutral,safe,true);
    reverse.tick(999,safe);require_cancel(reverse,Reason::ClockReversed);
}
void tail_observation_is_real() {
    auto s=started();
    s.sample(1000,2,neutral,safe,true);s.sample(1500,2,neutral,safe,true);
    s.sample(1800,2,neutral,safe,true);
    // A late first tail poll does not count the earlier unsampled tail time.
    assert(zero(s.sample(2200,2,neutral,safe,true)));
    assert(zero(s.sample(2250,2,neutral,safe,true)) && !s.status().completed);
    assert(zero(s.sample(2450,2,neutral,safe,true)) && s.status().completed);

    auto no_tail=started();
    no_tail.sample(1000,2,neutral,safe,true);no_tail.sample(1500,2,neutral,safe,true);
    no_tail.sample(1800,2,neutral,safe,true);
    no_tail.tick(2301,safe);require_cancel(no_tail,Reason::GameHeartbeatLost);
    assert(!no_tail.status().final_neutral_observed);
}
void validation() {
    const auto check=[](std::string_view id,uint32_t slot,std::span<const Segment> segments,
            const RawPad& raw,uint64_t proof,BeginError expected,uint64_t now=1000) {
        Sequence s;assert(s.begin(now,id,slot,segments,raw,proof)==expected);
        assert(!s.status().active && s.id().empty());
    };
    check("",0,path,neutral,1000,BeginError::InvalidId);
    check(std::string(65,'a'),0,path,neutral,1000,BeginError::InvalidId);
    check("space id",0,path,neutral,1000,BeginError::InvalidId);
    check("ok",4,path,neutral,1000,BeginError::InvalidSlot);
    check("ok",0,{},neutral,1000,BeginError::InvalidSegments);
    const std::array<Segment,9> many{};check("ok",0,many,neutral,1000,BeginError::InvalidSegments);
    const std::array zero_duration{Segment{0}};check("ok",0,zero_duration,neutral,1000,BeginError::InvalidDuration);
    const std::array huge_duration{Segment{UINT32_MAX}};check("ok",0,huge_duration,neutral,1000,BeginError::InvalidDuration);
    const std::array over_total{Segment{11251}};check("ok",0,over_total,neutral,1000,BeginError::InvalidDuration);
    for(const int32_t bad:{-16385,16385,INT32_MIN,INT32_MAX}) {
        const std::array invalid{Segment{10,bad}};check("ok",0,invalid,neutral,1000,BeginError::InvalidAxes);
    }
    auto off=neutral;off.connected=false;check("ok",0,path,off,1000,BeginError::Disconnected);
    auto moving=neutral;moving.rx=3001;check("ok",0,path,moving,1000,BeginError::PhysicalActivity);
    check("ok",0,path,neutral,499,BeginError::StaleGameProof);
    check("ok",0,path,neutral,1001,BeginError::StaleGameProof);
    check("ok",0,path,neutral,UINT64_MAX,BeginError::ClockRange,UINT64_MAX);
    auto running=started();assert(running.begin(1000,"other",2,path,neutral,1000)==BeginError::AlreadyActive);
    const std::array extremes{Segment{1,-16384,16384,-16384,16384}};
    Sequence valid;assert(valid.begin(1000,"valid_64-char-id",3,extremes,neutral,500)==BeginError::None);
    valid.sample(1000,3,neutral,safe,true);
    const auto d=valid.sample(1500,3,neutral,safe,true);assert(d.lx==-16384 && d.ly==16384 && d.rx==-16384 && d.ry==16384);
}
void buttons_and_context() {
    const std::array presses{Segment{100,0,0,0,0,0x8000},Segment{100},Segment{100,0,0,0,0,0x1010}};
    Sequence s;
    assert(s.begin(1000,"buttons",2,presses,neutral,1000)==BeginError::None);
    assert(zero(s.sample(1000,2,neutral,safe,true)));
    assert(!s.sample(1450,2,neutral,safe,false).override_axes);
    assert(s.status().synthetic_buttons==0); // foreign readers cannot press
    assert(s.sample(1500,2,neutral,safe,true).synthetic_buttons==0x8000);
    assert(s.status().synthetic_buttons==0x8000);
    assert(zero(s.sample(1600,2,neutral,safe,true)));
    assert(s.sample(1700,2,neutral,safe,true).synthetic_buttons==0x1010);
    assert(zero(s.sample(1800,2,neutral,safe,true)));
    assert(!s.status().completed && s.status().synthetic_buttons==0);
    assert(zero(s.sample(2050,2,neutral,safe,true)) && s.status().completed);

    Gates in_menu=safe;in_menu.game_menu=true;
    Sequence menu;
    assert(menu.begin(1000,"menu",2,presses,neutral,1000,Context::GameMenu)==BeginError::None);
    assert(zero(menu.sample(1000,2,neutral,in_menu,true)));
    assert(menu.sample(1500,2,neutral,in_menu,true).synthetic_buttons==0x8000);
    assert(!menu.sample(1501,2,neutral,safe,true).override_axes);
    require_cancel(menu,Reason::ContextChanged);
    assert(menu.status().synthetic_buttons==0 && !menu.status().final_neutral_observed);

    auto gameplay=started();gameplay.sample(1000,2,neutral,safe,true);
    assert(!gameplay.sample(1001,2,neutral,in_menu,true).override_axes);
    require_cancel(gameplay,Reason::Menu);
    auto native=safe;native.native_game_menu=true;
    assert(guard_reason(native,Context::Gameplay)==Reason::Menu);
    assert(guard_reason(native,Context::GameMenu)==Reason::None);
    native.protected_status_fresh=false;
    assert(guard_reason(native,Context::GameMenu)==Reason::ProtectedStatusStale);

    for(const bool stale:{false,true}) {
        Sequence guarded;
        assert(guarded.begin(1000,stale?"stale":"uevr-menu",2,presses,neutral,1000,Context::GameMenu)==BeginError::None);
        guarded.sample(1000,2,neutral,in_menu,true);
        auto blocked=in_menu;
        if(stale)blocked.protected_status_fresh=false;else blocked.menu=true;
        assert(!guarded.sample(1500,2,neutral,blocked,true).override_axes);
        require_cancel(guarded,stale?Reason::ProtectedStatusStale:Reason::Menu);
    }
    Sequence takeover;
    assert(takeover.begin(1000,"takeover",2,presses,neutral,1000,Context::GameMenu)==BeginError::None);
    takeover.sample(1000,2,neutral,in_menu,true);takeover.sample(1500,2,neutral,in_menu,true);
    auto physical=neutral;physical.buttons=0x1000;
    assert(!takeover.sample(1501,2,physical,in_menu,false).override_axes);
    require_cancel(takeover,Reason::PhysicalButtons);
    assert(takeover.status().synthetic_buttons==0 && physical.buttons==0x1000);

    Sequence invalid;
    assert(invalid.begin(1000,"menu-axes",2,path,neutral,1000,Context::GameMenu)==BeginError::InvalidAxes);
    for(const uint16_t button:{uint16_t(0x0040),uint16_t(0x0080),uint16_t(0x0400),uint16_t(0x0800)}) {
        const std::array forbidden{Segment{100,0,0,0,0,button}};
        assert(invalid.begin(1000,"forbidden",2,forbidden,neutral,1000)==BeginError::InvalidButtons);
    }
    assert(invalid.begin(1000,"context",2,presses,neutral,1000,static_cast<Context>(4))==BeginError::InvalidContext);
}
void trigger_axes() {
    const std::array path{Segment{100,0,0,0,0,0,0,255,true},Segment{100},Segment{100,0,0,0,0,0,64,0,true}};
    Sequence s;assert(s.begin(1000,"triggers",2,path,neutral,1000)==BeginError::None);
    assert(s.status().trigger_axes && s.status().synthetic_lt==0 && s.status().synthetic_rt==0);
    const auto lead=s.sample(1000,2,neutral,safe,true);
    assert(zero(lead) && lead.override_triggers);
    assert(!s.sample(1499,2,neutral,safe,false).override_triggers);
    const auto press=s.sample(1500,2,neutral,safe,true);
    assert(press.override_triggers && press.synthetic_rt==255 && press.synthetic_lt==0);
    assert(s.status().synthetic_rt==255);
    const auto release=s.sample(1600,2,neutral,safe,true);
    assert(zero(release) && release.override_triggers); // omitted fields release both axes
    const auto left=s.sample(1700,2,neutral,safe,true);
    assert(left.synthetic_lt==64 && left.synthetic_rt==0);
    assert(zero(s.sample(1800,2,neutral,safe,true)) && !s.status().completed);
    assert(zero(s.sample(2050,2,neutral,safe,true)) && s.status().completed);

    Sequence legacy;
    const std::array ordinary{Segment{100}};
    assert(legacy.begin(1000,"legacy",2,ordinary,neutral,1000)==BeginError::None);
    assert(!legacy.status().trigger_axes && !legacy.sample(1000,2,neutral,safe,true).override_triggers);
    const std::array explicit_zero{Segment{100,0,0,0,0,0,0,0,true}};
    Gates menu=safe;menu.game_menu=true;
    Sequence zero_menu;
    assert(zero_menu.begin(1000,"zero-menu",2,explicit_zero,neutral,1000,Context::GameMenu)==BeginError::None);
    assert(zero_menu.sample(1000,2,neutral,menu,true).override_triggers);

    for(const int32_t bad:{-1,256,INT32_MIN,INT32_MAX}) {
        for(const bool right:{false,true}) {
            Sequence invalid;
            const std::array values{Segment{100,0,0,0,0,0,right?0:bad,right?bad:0,true}};
            assert(invalid.begin(1000,"range",2,values,neutral,1000)==BeginError::InvalidTriggers);
        }
    }
    for(const bool right:{false,true}) {
        Sequence invalid;
        const std::array values{Segment{100,0,0,0,0,0,right?0:1,right?1:0,true}};
        assert(invalid.begin(1000,"menu",2,values,neutral,1000,Context::GameMenu)==BeginError::InvalidTriggers);
    }
    for(const bool right:{false,true}) {
        Sequence takeover;
        assert(takeover.begin(1000,"physical",2,path,neutral,1000)==BeginError::None);
        takeover.sample(1000,2,neutral,safe,true);takeover.sample(1500,2,neutral,safe,true);
        auto raw=neutral;if(right)raw.rt=31;else raw.lt=255;
        const auto before=raw;
        const auto result=takeover.sample(1501,2,raw,safe,false);
        assert(!result.override_axes && !result.override_triggers && same(raw,before));
        require_cancel(takeover,Reason::PhysicalTriggers);
        assert(takeover.status().synthetic_lt==0 && takeover.status().synthetic_rt==0);
    }
    Sequence stopped;assert(stopped.begin(1000,"stop",2,path,neutral,1000)==BeginError::None);
    stopped.sample(1000,2,neutral,safe,true);stopped.sample(1500,2,neutral,safe,true);
    assert(stopped.stop(1501,"stop"));
    assert(!stopped.sample(1502,2,neutral,safe,true).override_triggers);
    assert(stopped.status().synthetic_lt==0 && stopped.status().synthetic_rt==0);
}
}
int main() {
    timing_and_completion();caller_slot_and_poll_proof();all_gates();
    physical_priority_and_no_mutation();matching_id_and_deadlines();
    tail_observation_is_real();validation();buttons_and_context();trigger_axes();
    std::cout<<"PASS: analogue/button sequence timing, context transitions, duplicate polls, caller/slot proof, neutral phases, "
        "physical takeover, guard cancellation, matching IDs, deadmen, limits and immutable raw input\n";
}
