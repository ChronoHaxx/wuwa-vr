#include "../upstream/UEVR/src/utility/WuWaInputTrace.hpp"
#include "../upstream/UEVR/src/utility/WuWaMenuSignals.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>

// Exercise the production bridge with a deterministic clock and explicit
// caller-verification result. No hooked API, game process or device is used.
static uint64_t fixture_clock=1000;
static bool fixture_game_menu=false;
static uint64_t fixture_now() { return fixture_clock; }
#define GetTickCount64 fixture_now
#include "../upstream/UEVR/src/utility/WuWaInputSequenceBridge.hpp"
#undef GetTickCount64
namespace bridge=wuwa_input_sequence_bridge;
namespace policy=wuwa_input_sequence;
using Json=nlohmann::json;

Json camera() {
    return {{"controls",{{"script_fresh",true},{"script_age_ms",0},{"game_menu",fixture_game_menu},
        {"hud_mouse",false},{"strict_game_foreground",true},{"native_menu",false},
        {"uevr_menu",false},{"motion_active",false},{"passthrough",false},{"slot_filter",0}}}};
}
void reset() {
    fixture_clock=1000;
    fixture_game_menu=false;
    bridge::sequence={}; bridge::protected_state={}; bridge::proofs={}; bridge::polls={};bridge::packet_continuity={};
    bridge::used_ids={}; bridge::used_count=0; bridge::active=false;
    bridge::cancel_epoch=0; bridge::lock_losses=0; bridge::record_losses=0;
    bridge::next_guard_refresh=0; bridge::seen_cancel_epoch=0;
    bridge::generated_polls=0; bridge::synthetic_packet=0;
    wuwa_menu::screen_overlay_at=0; wuwa_menu::transient_menu_at=0;
    bridge::refresh_guards(camera());
}
Json plan(const char* id="lease-1",int duration=1000) {
    return {{"action","begin"},{"id",id},{"user_index",2},
        {"segments",Json::array({{{"duration_ms",duration},{"lx",0},{"ly",0},{"rx",8192},{"ry",0}}})}};
}
Json command(const char* action,const char* id="lease-1") {
    return {{"action",action},{"lease_id",id}};
}
template<class F> void rejects(F action) {
    bool rejected=false; try { action(); } catch(const std::exception&) { rejected=true; }
    assert(rejected);
}
XINPUT_STATE neutral() { XINPUT_STATE value{}; value.dwPacketNumber=42; return value; }
bridge::Poll poll(uint64_t now,bool game=true,uint32_t slot=2,
        XINPUT_STATE raw=neutral(),bridge::RuntimeGates live={true,false,false,false,0},
        bool refresh=true,uint32_t raw_result=ERROR_SUCCESS,uint32_t filtered_result=ERROR_SUCCESS,
        bool writable=true) {
    fixture_clock=now;
    if(refresh) bridge::refresh_guards(camera());
    auto output=raw;
    const auto value=bridge::before_mods_verified(now,game,14,slot,raw_result,raw,
        filtered_result,writable?&output:nullptr,0x1234,live);
    bridge::after_mods(value,filtered_result,writable?&output:nullptr);
    return value;
}
void begin(const char* id="lease-1",int duration=1000) {
    poll(fixture_clock);
    const auto status=bridge::request(plan(id,duration),camera());
    assert(status["active"]==true && status["id"]==id && status["protocol_version"]==1);
}
Json button_plan(const char* context="gameplay") {
    return {{"action","begin"},{"id","buttons-1"},{"user_index",2},{"context",context},
        {"segments",Json::array({
            {{"duration_ms",100},{"buttons",Json::array({"y"})}},
            {{"duration_ms",100}},
            {{"duration_ms",100},{"buttons",Json::array({"a","dpad_right"})}}})}};
}
void buttons_and_context() {
    reset();poll(1000);
    auto receipt=bridge::request(button_plan(),camera());
    assert(receipt["context"]=="gameplay" && receipt["button_context_version"]==1 && receipt["synthetic_buttons"]==0);
    assert(poll(1000).synthetic.Gamepad.wButtons==0);
    assert(!poll(1450,false).generated && bridge::status()["synthetic_buttons"]==0);
    auto pressed=poll(1500);
    assert(pressed.generated && pressed.synthetic_buttons==XINPUT_GAMEPAD_Y && pressed.raw.Gamepad.wButtons==0);
    assert(pressed.synthetic.Gamepad.wButtons==XINPUT_GAMEPAD_Y);
    auto changed=pressed.synthetic;changed.Gamepad.wButtons=XINPUT_GAMEPAD_X;
    bridge::after_mods(pressed,ERROR_SUCCESS,&changed);
    const auto evidence=bridge::recording_snapshot(1500);
    bool found=false;
    for(const auto& sample:evidence["samples"]) if(sample["generated"]==true && sample["age_ms"]==0) {
        found=true;
        assert(sample["context"]=="gameplay" && sample["synthetic_buttons"]==XINPUT_GAMEPAD_Y);
        assert(sample["raw"]["buttons"]==0 && sample["synthetic"]["buttons"]==XINPUT_GAMEPAD_Y);
        assert(sample["delivered"]["buttons"]==XINPUT_GAMEPAD_X);
    }
    assert(found);
    assert(poll(1600).synthetic_buttons==0);
    assert(poll(1700).synthetic_buttons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_DPAD_RIGHT));
    assert(poll(1800).synthetic_buttons==0 && bridge::status()["tail_neutral_observed"]==true);
    assert(bridge::status()["completed"]==false);
    poll(2050);assert(bridge::status()["completed"]==true);

    // Explicit game-menu context can use either the fresh script flag or the
    // independently expiring native game-render signal, with fresh guards.
    reset();poll(1000);
    rejects([]{bridge::request(button_plan("game-menu"),camera());});
    fixture_game_menu=true;
    receipt=bridge::request(button_plan("game-menu"),camera());
    assert(receipt["context"]=="game-menu");
    poll(1000);assert(poll(1500).synthetic_buttons==XINPUT_GAMEPAD_Y);
    fixture_game_menu=false;
    assert(!poll(1501).generated && bridge::status()["reason"]=="context_changed");
    assert(bridge::status()["synthetic_buttons"]==0 && bridge::status()["tail_neutral_observed"]==false);

    reset();poll(1000);bridge::request(button_plan(),camera());poll(1000);
    assert(poll(1500).synthetic_buttons==XINPUT_GAMEPAD_Y);
    fixture_game_menu=true;
    assert(!poll(1501).generated && bridge::status()["reason"]=="menu");

    // The native-render GAME menu is accepted only in the explicit context.
    reset();poll(1000);wuwa_menu::screen_overlay_at=1000;
    auto native=camera();native["controls"]["native_menu"]=true;
    rejects([&]{bridge::request(button_plan(),native);});
    receipt=bridge::request(button_plan("game-menu"),native);
    assert(receipt["active"]==true && receipt["context"]=="game-menu");
    poll(1000);wuwa_menu::screen_overlay_at=1250;poll(1250);
    wuwa_menu::screen_overlay_at=1500;
    assert(poll(1500).synthetic_buttons==XINPUT_GAMEPAD_Y);
    // A cached native=true snapshot must not extend the actual 250-ms signal.
    fixture_clock=1700;bridge::refresh_guards(native);
    assert(poll(1750,true,2,neutral(),{true,false,false,false,0},false).generated);
    assert(!poll(1751,true,2,neutral(),{true,false,false,false,0},false).generated);
    assert(bridge::status()["reason"]=="context_changed");
    // A fresh native observation cannot authorize stale protected guards.
    reset();poll(1000);wuwa_menu::screen_overlay_at=1000;
    auto stale_native=camera();stale_native["controls"]["script_age_ms"]=501;
    rejects([&]{bridge::request(button_plan("game-menu"),stale_native);});
    // Native-render positives still immediately cancel a gameplay sequence.
    reset();poll(1000);bridge::request(button_plan(),camera());poll(1000);poll(1500);
    wuwa_menu::transient_menu_at=1501;
    assert(!poll(1501).generated && bridge::status()["reason"]=="menu");

    for(const auto* flag:{"uevr_menu","hud_mouse","motion_active","passthrough"}) {
        reset();fixture_game_menu=true;poll(1000);
        auto guarded=camera();guarded["controls"][flag]=true;
        rejects([&]{bridge::request(button_plan("game-menu"),guarded);});
        bridge::request(button_plan("game-menu"),camera());poll(1000);poll(1500);
        bridge::refresh_guards(guarded);assert(!bridge::active.load());
        assert(!poll(1501).generated && bridge::status()["synthetic_buttons"]==0);
    }
    reset();fixture_game_menu=true;poll(1000);bridge::request(button_plan("game-menu"),camera());poll(1000);
    assert(!poll(1500,true,2,neutral(),{true,true,false,false,0}).generated);
    assert(bridge::status()["reason"]=="menu"); // live UEVR menu also wins
    reset();poll(1000);wuwa_menu::screen_overlay_at=1000;
    auto uevr=camera();uevr["controls"]["uevr_menu"]=true;uevr["controls"]["native_menu"]=true;
    rejects([&]{bridge::request(button_plan("game-menu"),uevr);});

    reset();fixture_game_menu=true;poll(1000);bridge::request(button_plan("game-menu"),camera());poll(1000);
    poll(1400,true,2,neutral(),{true,false,false,false,0},false);
    assert(!poll(1501,true,2,neutral(),{true,false,false,false,0},false).generated);
    assert(bridge::status()["reason"]=="protected_status_stale");

    reset();fixture_game_menu=true;poll(1000);bridge::request(button_plan("game-menu"),camera());poll(1000);poll(1500);
    auto physical=neutral();physical.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    auto output=physical;
    const auto takeover=bridge::before_mods_verified(1501,true,14,2,ERROR_SUCCESS,physical,ERROR_SUCCESS,
        &output,0x1234,{true,false,false,false,0});
    bridge::after_mods(takeover,ERROR_SUCCESS,&output);
    assert(!takeover.generated && takeover.synthetic_buttons==0 && output.Gamepad.wButtons==XINPUT_GAMEPAD_A);
    assert(bridge::status()["reason"]=="physical_buttons");

    for(const auto& invalid:std::vector<Json>{1,true,nullptr,"a",Json::array({"A"}),Json::array({"guide"}),
            Json::array({"left_thumb"}),Json::array({"lt"}),Json::array({"a","a"}),Json::array({1})}) {
        reset();poll(1000);auto bad=button_plan();bad["segments"][0]["buttons"]=invalid;
        rejects([&]{bridge::request(bad,camera());});assert(!bridge::active.load());
    }
    for(const auto& context:std::vector<Json>{nullptr,1,"menu","GAMEPLAY"}) {
        reset();poll(1000);auto bad=button_plan();bad["context"]=context;
        rejects([&]{bridge::request(bad,camera());});assert(!bridge::active.load());
    }
    reset();fixture_game_menu=true;poll(1000);auto bad=button_plan("game-menu");bad["segments"][0]["lx"]=1;
    rejects([&]{bridge::request(bad,camera());});
    bad=button_plan("game-menu");bad["segments"][0]["trigger"]=0;
    rejects([&]{bridge::request(bad,camera());});
    auto stale=camera();stale["controls"]["script_age_ms"]=501;
    rejects([&]{bridge::request(button_plan("game-menu"),stale);});
    // Verify every allowed name against the platform's actual bit definitions.
    assert(bridge::parse_buttons(Json::array({"a","b","x","y","dpad_up","dpad_down","dpad_left","dpad_right",
        "start","back","left_shoulder","right_shoulder"}))==
        (XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y|XINPUT_GAMEPAD_DPAD_UP|
        XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_START|
        XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER));
}
void packet_handoff() {
    reset();
    auto raw=neutral();raw.dwPacketNumber=40;
    poll(1000,true,2,raw);bridge::request(button_plan(),camera());
    const auto lead=poll(1000,true,2,raw);
    assert(lead.generated && lead.synthetic.dwPacketNumber==41);
    raw.dwPacketNumber=41;raw.Gamepad.sThumbLX=1;
    const auto held=poll(1500,true,2,raw);
    assert(held.generated && held.synthetic.dwPacketNumber==42 && held.synthetic_buttons==XINPUT_GAMEPAD_Y);
    raw.dwPacketNumber=42;raw.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    const auto takeover=poll(1501,true,2,raw);
    const auto delivered=bridge::polls[2].delivered;
    assert(!takeover.generated && takeover.raw.dwPacketNumber==42 && takeover.raw.Gamepad.wButtons==XINPUT_GAMEPAD_A);
    assert(delivered.Gamepad.wButtons==XINPUT_GAMEPAD_A);
    // Same packet must not describe different buttons to a packet-aware caller.
    assert(delivered.dwPacketNumber!=held.synthetic.dwPacketNumber);
    assert(delivered.dwPacketNumber==43 && bridge::polls[2].packet_adjusted);
    assert(bridge::polls[2].synthetic_buttons==0 && !bridge::polls[2].generated);
    poll(1502,true,2,raw);
    assert(bridge::polls[2].delivered.dwPacketNumber==43); // stable, no repeated increments
    raw.dwPacketNumber=43;raw.Gamepad.wButtons=XINPUT_GAMEPAD_B;
    poll(1503,true,2,raw);
    assert(bridge::polls[2].delivered.dwPacketNumber==44 && bridge::polls[2].raw.dwPacketNumber==43);
    assert(bridge::polls[2].delivered.Gamepad.wButtons==XINPUT_GAMEPAD_B);
    poll(1504,true,2,raw);assert(bridge::polls[2].delivered.dwPacketNumber==44);
    raw.dwPacketNumber=45;raw.Gamepad.wButtons=XINPUT_GAMEPAD_X;
    poll(1505,true,2,raw);
    assert(bridge::polls[2].delivered.dwPacketNumber==45 && !bridge::packet_continuity[2].armed);
    assert(!bridge::polls[2].packet_adjusted); // safe return to normal numbering

    // A stale protected snapshot cancels and releases a held button, even if
    // the raw packet happened to catch up to the last synthetic packet.
    reset();raw=neutral();raw.dwPacketNumber=40;
    poll(1000,true,2,raw);bridge::request(button_plan(),camera());poll(1000,true,2,raw);
    raw.dwPacketNumber=41;poll(1500,true,2,raw);
    raw.dwPacketNumber=42;
    fixture_clock=1501;auto stale=camera();stale["controls"]["script_fresh"]=false;
    bridge::refresh_guards(stale);
    const auto released=poll(1501,true,2,raw,{true,false,false,false,0},false);
    assert(!released.generated && bridge::status()["reason"]=="protected_status_stale");
    assert(bridge::polls[2].delivered.Gamepad.wButtons==0 && bridge::polls[2].delivered.dwPacketNumber==43);
    poll(1502,true,2,raw);assert(bridge::polls[2].delivered.dwPacketNumber==43);
    // A subsequent lease continues from the corrected output, not the older raw
    // counter. Caller/API wrappers cannot undo the game's handoff bookkeeping.
    poll(1502,false,2,raw);assert(bridge::packet_continuity[2].delivered_packet==43);
    auto next=button_plan();next["id"]="buttons-2";
    bridge::request(next,camera());
    assert(poll(1502,true,2,raw).synthetic.dwPacketNumber==44);
    auto disconnection=poll(1503,true,2,raw,{true,false,false,false,0},true,
        ERROR_DEVICE_NOT_CONNECTED,ERROR_DEVICE_NOT_CONNECTED);
    assert(!disconnection.generated && !bridge::packet_continuity[2].armed);

    // The final collision check uses actual post-mod packets and buttons.
    reset();poll(1000);bridge::request(button_plan(),camera());poll(1000);
    const auto pre=poll(1500);
    auto modified=pre.synthetic;modified.dwPacketNumber=77;modified.Gamepad.wButtons=XINPUT_GAMEPAD_X;
    bridge::after_mods(pre,ERROR_SUCCESS,&modified);
    raw=neutral();raw.dwPacketNumber=77;raw.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    poll(1501,true,2,raw);
    assert(bridge::polls[2].delivered.dwPacketNumber==78 && bridge::polls[2].raw.dwPacketNumber==77);
    assert(bridge::polls[2].delivered.Gamepad.wButtons==XINPUT_GAMEPAD_A);
    bool recorded=false;
    const auto handoff_recording=bridge::recording_snapshot(1501);
    for(const auto& row:handoff_recording["samples"]) {
        if(row["game_reader"]==true && row["slot"]==2 && row["age_ms"]==0) {
            recorded=true;assert(row["packet_adjusted"]==true && row["generated"]==false);
            assert(row["raw"]["packet"]==77 && row["delivered"]["packet"]==78);
            assert(row["synthetic"].is_null() && row["raw"]["buttons"]==XINPUT_GAMEPAD_A);
        }
    }
    assert(recorded);

    // DWORD wrapping is modulo arithmetic, including the first neutral lead.
    reset();raw=neutral();raw.dwPacketNumber=UINT32_MAX-2;
    poll(1000,true,2,raw);bridge::request(button_plan(),camera());
    assert(poll(1000,true,2,raw).synthetic.dwPacketNumber==UINT32_MAX-1);
    raw.dwPacketNumber=UINT32_MAX-1;
    assert(poll(1500,true,2,raw).synthetic.dwPacketNumber==UINT32_MAX);
    raw.dwPacketNumber=UINT32_MAX;raw.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    poll(1501,true,2,raw);assert(bridge::polls[2].delivered.dwPacketNumber==0);
    poll(1502,true,2,raw);assert(bridge::polls[2].delivered.dwPacketNumber==0);
    raw.dwPacketNumber=0;raw.Gamepad.wButtons=XINPUT_GAMEPAD_B;
    poll(1503,true,2,raw);assert(bridge::polls[2].delivered.dwPacketNumber==1);
    raw.dwPacketNumber=2;raw.Gamepad.wButtons=0;
    poll(1504,true,2,raw);assert(bridge::polls[2].delivered.dwPacketNumber==2 && !bridge::packet_continuity[2].armed);
}
void trigger_axes() {
    const auto trigger_plan=[] {
        return Json{{"action","begin"},{"id","triggers-1"},{"user_index",2},
            {"segments",Json::array({{{"duration_ms",100},{"rt",255}},{{"duration_ms",100}},
                {{"duration_ms",100},{"lt",64},{"buttons",Json::array({"a"})}}})}};
    };
    reset();auto raw=neutral();raw.Gamepad.bLeftTrigger=17;raw.Gamepad.bRightTrigger=30;
    poll(1000,true,2,raw);
    assert(bridge::status()["trigger_axis_version"]==1);
    assert(bridge::recording_snapshot(1000)["trigger_axis_version"]==1);
    const auto began=bridge::request(trigger_plan(),camera());
    assert(began["trigger_axes"]==true && began["synthetic_lt"]==0 && began["synthetic_rt"]==0);
    const auto lead=poll(1000,true,2,raw);
    assert(lead.trigger_axes && lead.synthetic.Gamepad.bLeftTrigger==0 && lead.synthetic.Gamepad.bRightTrigger==0);
    assert(lead.raw.Gamepad.bLeftTrigger==17 && lead.raw.Gamepad.bRightTrigger==30);
    const auto pressed=poll(1500,true,2,raw);
    assert(pressed.generated && pressed.trigger_axes && pressed.synthetic_rt==255 && pressed.synthetic_lt==0);
    assert(pressed.synthetic.Gamepad.bRightTrigger==255 && pressed.raw.Gamepad.bRightTrigger==30);
    auto changed=pressed.synthetic;changed.Gamepad.bRightTrigger=128;
    bridge::after_mods(pressed,ERROR_SUCCESS,&changed);
    const auto evidence=bridge::recording_snapshot(1500);
    bool found=false;
    for(const auto& row:evidence["samples"]) if(row["generated"]==true && row["age_ms"]==0) {
        found=true;assert(row["trigger_axes"]==true && row["synthetic_rt"]==255);
        assert(row["raw"]["rt"]==30 && row["synthetic"]["rt"]==255 && row["delivered"]["rt"]==128);
    }
    assert(found);
    const auto released=poll(1600,true,2,raw);
    assert(released.trigger_axes && released.synthetic.Gamepad.bLeftTrigger==0 && released.synthetic.Gamepad.bRightTrigger==0);
    const auto left=poll(1700,true,2,raw);
    assert(left.synthetic_lt==64 && left.synthetic_rt==0 && left.synthetic_buttons==XINPUT_GAMEPAD_A);
    const auto tail=poll(1800,true,2,raw);
    assert(tail.trigger_axes && tail.synthetic_lt==0 && tail.synthetic_rt==0 && !bridge::status()["completed"].get<bool>());
    poll(2050,true,2,raw);assert(bridge::status()["completed"]==true && bridge::status()["tail_neutral_observed"]==true);
    poll(2051,true,2,raw);assert(bridge::polls[2].delivered.Gamepad.bLeftTrigger==17 && bridge::polls[2].delivered.Gamepad.bRightTrigger==30);

    // Optional means opt-in: old plans preserve low physical trigger pressure.
    reset();raw=neutral();raw.Gamepad.bRightTrigger=17;poll(1000,true,2,raw);
    bridge::request(plan(),camera());
    const auto legacy=poll(1000,true,2,raw);
    assert(!legacy.trigger_axes && legacy.synthetic.Gamepad.bRightTrigger==17 && bridge::status()["trigger_axes"]==false);
    // Explicit zero requires the new capability and owns/releases both axes.
    reset();fixture_game_menu=true;poll(1000,true,2,raw);
    auto zero=button_plan("game-menu");zero["segments"][0]["rt"]=0;
    bridge::request(zero,camera());
    const auto zeroed=poll(1000,true,2,raw);
    assert(zeroed.trigger_axes && zeroed.synthetic.Gamepad.bRightTrigger==0);
    for(const auto* key:{"lt","rt"}) {
        reset();fixture_game_menu=true;poll(1000);auto menu=button_plan("game-menu");menu["segments"][0][key]=1;
        rejects([&]{bridge::request(menu,camera());});assert(!bridge::active.load());
        for(const auto& invalid:std::vector<Json>{true,false,nullptr,1.5,-1,256,UINT64_MAX,"255",Json::array()}) {
            reset();poll(1000);auto bad=trigger_plan();bad["segments"][0][key]=invalid;
            rejects([&]{bridge::request(bad,camera());});assert(!bridge::active.load());
        }
    }
    for(const auto* key:{"lt","rt"}) {
        reset();poll(1000);bridge::request(trigger_plan(),camera());poll(1000);poll(1500);
        raw=neutral();if(std::string_view(key)=="lt")raw.Gamepad.bLeftTrigger=31;else raw.Gamepad.bRightTrigger=255;
        const auto takeover=poll(1501,true,2,raw);
        assert(!takeover.generated && !takeover.trigger_axes);
        assert(bridge::polls[2].delivered.Gamepad.bLeftTrigger==raw.Gamepad.bLeftTrigger);
        assert(bridge::polls[2].delivered.Gamepad.bRightTrigger==raw.Gamepad.bRightTrigger);
        assert(bridge::status()["reason"]=="physical_triggers" && bridge::status()["synthetic_rt"]==0);
    }
    for(const auto& [live,reason]:std::initializer_list<std::pair<bridge::RuntimeGates,const char*>>{
            {{false,false,false,false,0},"focus_lost"},{{true,true,false,false,0},"menu"}}) {
        reset();poll(1000);bridge::request(trigger_plan(),camera());poll(1000);poll(1500);
        const auto cancelled=poll(1501,true,2,neutral(),live);
        assert(!cancelled.generated && !cancelled.trigger_axes && bridge::polls[2].delivered.Gamepad.bRightTrigger==0);
        assert(bridge::status()["reason"]==reason && bridge::status()["synthetic_rt"]==0);
    }
}

int main() {
    reset();
    assert(!bridge::game_reader(0));
    assert(!bridge::game_reader(reinterpret_cast<uintptr_t>(&main))); // test EXE is not WuWa
    assert(bridge::status()["phase"]=="idle");
    rejects([] { bridge::request(plan(),camera()); }); // no selected-slot proof
    poll(1000,false); rejects([] { bridge::request(plan(),camera()); });
    poll(1000,true,1); rejects([] { bridge::request(plan(),camera()); });
    poll(1000);
    const auto passive=bridge::recording_snapshot(1000);
    assert(passive["available"]==true && passive["protocol_version"]==1);
    assert(passive["phase"]=="idle" && passive["id"]=="" && passive["input_sent"]==false);
    assert(passive["samples"].empty()); // legacy recorder inputs already cover passive polling
    fixture_clock=1501; rejects([] { bridge::request(plan(),camera()); });

    reset(); begin();
    assert(poll(1000).generated);
    assert(!poll(1250,false).generated);
    assert(!poll(1250,true,1).generated);
    assert(poll(1499).synthetic.Gamepad.sThumbRX==0);
    fixture_clock=1500; bridge::refresh_guards(camera());
    auto raw=neutral(); raw.Gamepad.bLeftTrigger=17; auto output=raw;
    auto generated=bridge::before_mods_verified(1500,true,14,2,ERROR_SUCCESS,raw,ERROR_SUCCESS,
        &output,0x1234,{true,false,false,false,0});
    assert(generated.generated && generated.raw.Gamepad.sThumbRX==0);
    assert(output.Gamepad.sThumbRX==8192 && output.Gamepad.bLeftTrigger==17 && output.Gamepad.wButtons==0);
    assert(output.dwPacketNumber!=raw.dwPacketNumber);
    output.Gamepad.sThumbRX=4096; // emulate the ordinary mod chain
    bridge::after_mods(generated,ERROR_SUCCESS,&output);
    const auto recording=bridge::recording_snapshot(1500);
    bool found=false;
    for(const auto& sample:recording["samples"]) if(sample["generated"]==true && sample["age_ms"]==0) {
        found=true; assert(sample["raw"]["rx"]==0 && sample["synthetic"]["rx"]==8192);
        assert(sample["delivered"]["rx"]==4096 && sample["lease_id"]=="lease-1");
    }
    assert(found && recording["input_sent"]==true);

    reset(); begin("lease-1",100);
    poll(1000); poll(1500); poll(1600); poll(1850);
    auto terminal=bridge::request(command("heartbeat"),camera());
    assert(terminal["phase"]=="completed" && terminal["active"]==false);
    assert(terminal["segment_mask"]==1 && terminal["tail_neutral_observed"]==true);
    const auto count=terminal["generated_polls"];
    fixture_clock=9000;
    terminal=bridge::request(command("stop"),camera());
    assert(terminal["phase"]=="completed" && terminal["generated_polls"]==count);
    assert(!poll(9000).generated);
    rejects([] { bridge::request(command("heartbeat","wrong"),camera()); });
    rejects([] { bridge::request(command("stop",""),camera()); });

    reset(); begin(); poll(1000);
    raw=neutral(); raw.Gamepad.wButtons=XINPUT_GAMEPAD_A;
    assert(!poll(1010,false,2,raw).generated); // non-game physical input still cancels
    terminal=bridge::request(command("heartbeat"),camera());
    assert(terminal["phase"]=="cancelled" && terminal["reason"]=="physical_buttons");
    assert(!poll(1100).generated);
    assert(bridge::request(command("stop"),camera())["reason"]=="physical_buttons");
    rejects([] { bridge::request(plan(),camera()); }); // cannot rearm the used ID
    begin("lease-2"); bridge::request(command("stop","lease-2"),camera());
    rejects([] { bridge::request(plan(),camera()); }); // cannot reuse older IDs either

    reset(); begin();
    assert(!poll(1000,true,2,neutral(),{true,false,false,false,0},true,ERROR_SUCCESS,ERROR_SUCCESS,false).generated);
    assert(bridge::status()["lead_neutral_observed"]==false);
    assert(!poll(1100,true,2,neutral(),{true,false,false,false,0},true,ERROR_SUCCESS,ERROR_DEVICE_NOT_CONNECTED).generated);
    assert(bridge::status()["eligible_polls"]==0);
    assert(!poll(1200,true,2,neutral(),{true,false,false,false,0},true,ERROR_DEVICE_NOT_CONNECTED,ERROR_DEVICE_NOT_CONNECTED).generated);
    assert(bridge::status()["reason"]=="disconnected");

    for(const auto& [live,reason]:std::initializer_list<std::pair<bridge::RuntimeGates,const char*>>{
        {{false,false,false,false,0},"focus_lost"},{{true,true,false,false,0},"menu"},
        {{true,false,true,false,0},"passthrough"},{{true,false,false,true,0},"motion_activity"},
        {{true,false,false,false,1},"slot_incompatible"}}) {
        reset(); begin(); poll(1000);
        assert(!poll(1001,true,2,neutral(),live).generated);
        assert(bridge::status()["reason"]==reason);
    }
    for(const auto* flag:{"hud_mouse","game_menu","native_menu","uevr_menu"}) {
        reset(); begin(); auto guarded=camera(); guarded["controls"][flag]=true;
        if(std::string_view(flag)=="native_menu")wuwa_menu::screen_overlay_at=fixture_clock;
        bridge::refresh_guards(guarded); assert(!bridge::active.load());
    }
    reset(); begin(); poll(1000); poll(1400,true,2,neutral(),{true,false,false,false,0},false);
    assert(!poll(1501,true,2,neutral(),{true,false,false,false,0},false).generated);
    assert(bridge::status()["reason"]=="protected_status_stale");

    reset(); begin(); poll(1000);
    // A filtered OTHER slot does not invalidate the selected slot's lease.
    assert(!poll(1100,true,1,neutral(),{true,false,false,false,3},true,ERROR_SUCCESS,ERROR_DEVICE_NOT_CONNECTED).generated);
    assert(bridge::status()["active"]==true);

    reset(); begin(); poll(1000);
    std::promise<void> locked,release; auto released=release.get_future();
    std::thread holder([&] { std::scoped_lock lock{bridge::mutex}; locked.set_value(); released.wait(); });
    locked.get_future().wait();
    raw=neutral(); output=raw;
    const auto blocked=bridge::before_mods_verified(1001,true,14,2,ERROR_SUCCESS,raw,ERROR_SUCCESS,
        &output,0x1234,{true,false,false,false,0});
    assert(!blocked.generated && !blocked.decision_available && output.dwPacketNumber==raw.dwPacketNumber);
    release.set_value(); holder.join();
    fixture_clock=1001;
    assert(bridge::status()["reason"]=="contention");
    assert(!poll(1002).generated);

    for(const auto& invalid:std::vector<Json>{true,1.5,16385,-16385,UINT64_MAX,nullptr}) {
        reset(); poll(1000); auto request=plan(); request["segments"][0]["rx"]=invalid;
        rejects([&] { bridge::request(request,camera()); });
        assert(!bridge::active.load());
    }
    reset(); poll(1000); auto request=plan(); request["segments"][0]["buttons"]=1;
    rejects([&] { bridge::request(request,camera()); });
    auto stale=camera(); stale["controls"]["script_age_ms"]=501;
    rejects([&] { bridge::request(plan(),stale); });
    assert(!bridge::active.load());
    reset(); poll(1000); bridge::used_count=bridge::used_ids.size();
    rejects([] { bridge::request(plan(),camera()); });
    buttons_and_context();
    packet_handoff();
    trigger_axes();
    std::cout << "PASS: actual input bridge proof, button press/release, context guards, caller scope, completion acknowledgement, history, contention, validation and raw/synthetic/delivered provenance\n";
}
