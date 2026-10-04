#include "../mod/uevr/src/utility/WuWaSightseeingInput.hpp"

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>

using namespace wuwa_sightseeing;

namespace {
int checks{};
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}
void unchanged(const Result& result, Pad physical, bool connected, const char* message) {
    check(result.pad == physical && result.connected == connected && !result.vr_active, message);
}
void inactive(const Result& result, const char* message) { unchanged(result, {}, false, message); }
Result call(Mixer& mixer, int mode, Pad physical, bool connected, Pad vr, bool valid = true,
    std::uint64_t time = 1000, bool ui = false) {
    return mixer.apply(mode, physical, connected, vr, valid, time, time, ui);
}
}

int main() {
    const Pad physical{0x1000, 21, 170, -23000, 18000, 150, -400};
    const Pad vr{0x2000, 230, 30, 28000, -27000, 17000, -12000};
    check(target_slot(0) == -1 && target_slot(1) == 0 && target_slot(-1) == -1 && target_slot(6) == -1,
        "off/virtual/unknown mode target mapping");
    for (int mode = 2; mode <= 5; ++mode) check(target_slot(mode) == mode - 2, "explicit merge source slot");
    for (const auto held : {Pad{1}, Pad{0, 1}, Pad{0, 0, 1}, Pad{0, 0, 0, 1},
        Pad{0, 0, 0, 0, -1}, Pad{0, 0, 0, 0, 0, 1}, Pad{0, 0, 0, 0, 0, 0, -1}}) {
        Mixer mixer;
        inactive(call(mixer, 1, {}, false, held), "a held mapped control bypassed neutral rearm");
        check(call(mixer, 1, {}, false, {}).vr_active, "all-zero mapped state did not rearm");
        check(call(mixer, 1, {}, false, held).pad == held, "armed mapped control was changed");
    }
    {
        Mixer mixer;
        unchanged(call(mixer, 0, physical, true, vr), physical, true, "off altered connected physical input");
        unchanged(call(mixer, 0, physical, false, vr, false), physical, false, "off altered raw disconnected bytes");
        unchanged(call(mixer, 99, physical, true, vr), physical, true, "unknown mode enabled VR");
        inactive(call(mixer, 1, physical, true, vr), "virtual-only started while controls held");
        const auto ready = call(mixer, 1, {}, false, {});
        check(ready.connected && ready.vr_active && ready.pad == Pad{}, "neutral did not arm virtual-only");
        const auto active = call(mixer, 1, physical, true, vr);
        check(active.connected && active.vr_active && active.pad == vr, "virtual-only mixed physical state");
        const auto release = call(mixer, 1, physical, true, {});
        check(release.pad == Pad{} && release.connected && release.vr_active, "virtual buttons/triggers stuck after release");
        call(mixer, 0, physical, true, vr);
        inactive(call(mixer, 1, physical, true, vr), "off-on did not require neutral again");
    }
    for (int mode = 2; mode <= 5; ++mode) {
        Mixer mixer;
        unchanged(call(mixer, mode, physical, true, vr), physical, true, "unarmed merge changed physical input");
        call(mixer, mode, physical, true, {});
        const auto mixed = call(mixer, mode, physical, true, vr);
        check(mixed.connected && mixed.vr_active, "merge did not activate");
        check(mixed.pad.lx == physical.lx && mixed.pad.ly == physical.ly, "merge changed treadmill movement");
        check(mixed.pad.rx == vr.rx && mixed.pad.ry == vr.ry, "neutral physical look did not allow VR look");
        check(mixed.pad.buttons == 0x3000 && mixed.pad.lt == 230 && mixed.pad.rt == 170, "buttons/triggers did not use OR/max");
        const auto released = call(mixer, mode, physical, true, {});
        check(released.pad.buttons == physical.buttons && released.pad.lt == physical.lt && released.pad.rt == physical.rt,
            "released VR buttons/triggers persisted");
        Pad stationary = physical; stationary.lx = stationary.ly = 0;
        const auto standing = call(mixer, mode, stationary, true, vr);
        check(standing.pad.lx == 0 && standing.pad.ly == 0, "VR left stick moved a stationary treadmill");
        inactive(call(mixer, mode, physical, false, vr), "merge invented a missing physical controller");
        unchanged(call(mixer, mode, physical, true, vr), physical, true, "reconnect replayed held VR input");
        call(mixer, mode, physical, true, {});
        check(call(mixer, mode, physical, true, vr).vr_active, "reconnect could not rearm");
    }
    {
        Mixer mixer; call(mixer, 2, physical, true, {});
        for (const auto edge : {std::int16_t(-32768), std::int16_t(-8001), std::int16_t(8001), std::int16_t(32767)}) {
            Pad p = physical; p.rx = edge; p.ry = 0;
            const auto x = call(mixer, 2, p, true, vr);
            check(x.pad.rx == edge && x.pad.ry == 0, "physical X priority mixed in VR Y");
            p.rx = 0; p.ry = edge;
            const auto y = call(mixer, 2, p, true, vr);
            check(y.pad.rx == 0 && y.pad.ry == edge, "physical Y priority mixed in VR X");
        }
        for (const auto edge : {std::int16_t(-8000), std::int16_t(0), std::int16_t(8000)}) {
            Pad p = physical; p.rx = edge; p.ry = edge;
            const auto result = call(mixer, 2, p, true, vr);
            check(result.pad.rx == vr.rx && result.pad.ry == vr.ry, "deadzone boundary did not choose VR look pair");
        }
        Pad zero{};
        check(call(mixer, 2, zero, true, zero).pad == zero, "zero-vector merge produced movement");
    }
    {
        Mixer mixer; call(mixer, 2, physical, true, {});
        const auto invalid = call(mixer, 2, physical, true, vr, false);
        unchanged(invalid, physical, true, "tracking/focus invalidity altered raw physical input");
        unchanged(call(mixer, 2, physical, true, vr), physical, true, "tracking recovery replayed held VR input");
        call(mixer, 2, physical, true, {});
        check(call(mixer, 2, physical, true, vr).vr_active, "tracking recovery could not rearm");
        unchanged(call(mixer, 3, physical, true, vr), physical, true, "physical target change reused prior arming");
        call(mixer, 3, physical, true, {});
        check(call(mixer, 3, physical, true, vr).vr_active, "new physical source could not rearm");
        inactive(call(mixer, 1, physical, true, vr), "merge-to-virtual mode change reused arming");
        call(mixer, 1, physical, true, {});
        inactive(call(mixer, 1, physical, true, vr, false), "invalid virtual source remained connected");
    }
    for (int mode : {1, 2}) {
        Mixer mixer; call(mixer, mode, physical, true, {});
        const auto boundary = mixer.apply(mode, physical, true, vr, true, 1000, 1250, false);
        check(boundary.vr_active, "250ms sample was incorrectly stale");
        const auto stale = mixer.apply(mode, physical, true, vr, true, 1000, 1251, false);
        if (mode == 1) inactive(stale, "stale virtual input not disconnected");
        else unchanged(stale, physical, true, "stale merge altered physical input");
        const auto held = call(mixer, mode, physical, true, vr, true, 1252);
        check(!held.vr_active, "fresh held state rearmed after stale sample");
        call(mixer, mode, physical, true, {}, true, 1253);
        check(call(mixer, mode, physical, true, vr, true, 1254).vr_active, "stale recovery failed");
        check(!call(mixer, mode, physical, true, {}, true, 1200).vr_active, "backwards clock armed even neutral input");
        check(!call(mixer, mode, physical, true, vr, true, 1201).vr_active, "backwards clock replayed held input");
        call(mixer, mode, physical, true, {}, true, 1202);
        check(call(mixer, mode, physical, true, vr, true, 1203).vr_active, "new clock epoch could not rearm");
        check(!mixer.apply(mode, physical, true, vr, true, 1202, 1204, false).vr_active, "regressed sample timestamp accepted");
        check(!mixer.apply(mode, physical, true, {}, true, 1300, 1299, false).vr_active, "future neutral timestamp armed input");
    }
    {
        Mixer mixer; call(mixer, 1, {}, false, {});
        check(call(mixer, 1, {}, false, vr).vr_active, "virtual source did not activate");
        inactive(call(mixer, 1, {}, false, vr, true, 1000, true), "opening UI replayed held buttons");
        const auto navigate = call(mixer, 1, {}, false, {}, true, 1000, true);
        check(navigate.vr_active, "neutral UI navigation did not arm");
        check(call(mixer, 1, {}, false, vr, true, 1000, true).pad == vr, "open UI could not navigate after neutral");
        inactive(call(mixer, 1, {}, false, vr), "closing UI leaked held navigation into game");
        call(mixer, 1, {}, false, {});
        check(call(mixer, 1, {}, false, vr).vr_active, "closed UI could not rearm");
        mixer.reset();
        inactive(call(mixer, 1, {}, false, vr), "explicit runtime reset replayed held input");
    }
    {
        PacketCounter counter;
        check(counter.stamp(0, true, physical, 900) == 0, "first delivered state lacks deterministic packet seed");
        check(counter.stamp(0, true, physical, 2) == 0 && counter.stamp(0, true, physical, 901) == 0,
            "API/raw packet changes advanced unchanged delivery");
        check(counter.stamp(0, true, vr, 2) == 1, "delivered controls did not advance packet");
        check(counter.stamp(0, true, vr, 900) == 1, "other API advanced identical delivery");
        check(counter.stamp(0, false, {}, 0) == 2, "disconnect did not advance packet");
        check(counter.stamp(0, false, {}, 999) == 2, "unchanged disconnect advanced packet");
        check(counter.stamp(0, true, {}, 0) == 3, "reconnect did not advance packet");
        check(counter.stamp(1, true, physical, std::numeric_limits<std::uint32_t>::max()) == 0,
            "another physical slot inherited counter state");
        check(counter.stamp(9, true, vr, 77) == 77, "out-of-range slot mishandled raw packet");
        check(counter.stamp(0, true, {}, 0) == 3, "out-of-range slot corrupted tracked state");
        PacketCounter reversed;
        check(reversed.stamp(0, true, physical, 2) == 0 && reversed.stamp(0, true, physical, 900) == 0,
            "API polling order altered delivered packet sequence");
    }
    std::cout << "PASS: " << checks << " sightseeing input checks (pure C++ values; no devices or Windows APIs)\n";
}
