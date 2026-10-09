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
Result call(Mixer& mixer, Style style, Pad physical, bool connected, Pad vr, bool valid = true,
    std::uint64_t time = 1000, bool ui = false) {
    return mixer.apply(true, style, physical, connected, vr, valid, time, time, ui);
}
constexpr Pad button(std::uint16_t bits) { return Pad{bits}; }
constexpr Pad left_stick(std::int16_t x, std::int16_t y) { return Pad{0, 0, 0, x, y}; }
constexpr Pad right_stick(std::int16_t x, std::int16_t y) { return Pad{0, 0, 0, 0, 0, x, y}; }
}

int main() {
    const Pad physical{0x1000, 21, 170, -23000, 18000, 150, -400};
    const Pad vr{0x2000, 230, 30, 28000, -27000, 17000, -12000};
    const auto styles = {Style::Both, Style::LastUsed, Style::VrOnly};

    // Settings mapping.
    check(style_from(0) == Style::Both && style_from(1) == Style::LastUsed && style_from(2) == Style::VrOnly,
        "saved style indices changed meaning");
    check(style_from(-1) == Style::Both && style_from(9) == Style::Both, "unknown style was not Both");
    check(slot_from(0) == 0 && slot_from(3) == 3 && slot_from(4) == 0 && slot_from(-1) == 0,
        "slot outside 0-3 was accepted");

    // Off leaves the slot byte-for-byte alone, connected or not.
    {
        Mixer mixer;
        const auto on = mixer.apply(false, Style::Both, physical, true, vr, true, 1000, 1000, false);
        check(on.pad == physical && on.connected && !on.vr_ready && on.status == Status::Off,
            "off altered connected physical input");
        const auto off = mixer.apply(false, Style::VrOnly, {}, false, vr, true, 1000, 1000, false);
        check(off.pad == Pad{} && !off.connected && off.status == Status::Off, "off claimed a missing pad");
    }

    // While on, the slot always reports connected, so the game keeps polling it.
    for (const auto style : styles) {
        Mixer mixer;
        for (const bool valid : {false, true}) {
            for (const bool connected : {false, true}) {
                const auto r = call(mixer, style, connected ? physical : Pad{}, connected, {}, valid);
                check(r.connected, "enabled slot reported disconnected");
            }
        }
        const auto stale = mixer.apply(true, style, {}, false, vr, true, 1000, 5000, false);
        check(stale.connected && !stale.vr_ready && stale.status == Status::WaitingVr && neutral(stale.pad),
            "stale VR sample disconnected the slot or leaked input");
    }

    // A VR input held when VR starts is ignored until released; others work at once.
    for (const auto style : styles) {
        Mixer mixer;
        const auto start = call(mixer, style, {}, false, button(0x1000));
        check(start.vr_ready && start.held && neutral(start.pad), "held button leaked at enable");
        const auto other = call(mixer, style, {}, false, Pad{0x1000 | 0x2000});
        check(other.pad.buttons == 0x2000 && other.held, "new button waited for the held one");
        const auto released = call(mixer, style, {}, false, button(0x2000));
        check(released.pad.buttons == 0x2000 && !released.held, "released mask was not cleared");
        const auto again = call(mixer, style, {}, false, button(0x3000));
        check(again.pad.buttons == 0x3000, "re-pressed button stayed masked");
    }
    for (const auto held_pad : {Pad{0, 255}, Pad{0, 0, 255}, left_stick(20000, 0), right_stick(0, -20000)}) {
        Mixer mixer;
        check(neutral(call(mixer, Style::VrOnly, {}, false, held_pad).pad), "held trigger/stick leaked at enable");
        check(neutral(call(mixer, Style::VrOnly, {}, false, held_pad).pad), "held trigger/stick leaked while held");
        call(mixer, Style::VrOnly, {}, false, {});
        check(call(mixer, Style::VrOnly, {}, false, held_pad).pad == held_pad, "trigger/stick stayed masked");
    }
    {
        // A stick partly returned is still held; only a full return clears it.
        Mixer mixer;
        call(mixer, Style::VrOnly, {}, false, left_stick(20000, 0));
        check(neutral(call(mixer, Style::VrOnly, {}, false, left_stick(3000, 0)).pad), "partial return unmasked stick");
        call(mixer, Style::VrOnly, {}, false, {});
        check(call(mixer, Style::VrOnly, {}, false, left_stick(3000, 0)).pad.lx == 3000, "returned stick still masked");
    }

    // Losing and regaining VR (sleep, headset off, focus) masks again.
    {
        Mixer mixer;
        call(mixer, Style::VrOnly, {}, false, {});
        check(call(mixer, Style::VrOnly, {}, false, vr).pad == vr, "ready VR input was not delivered");
        const auto lost = call(mixer, Style::VrOnly, {}, false, vr, false);
        check(neutral(lost.pad) && lost.status == Status::WaitingVr && lost.connected, "lost VR leaked input");
        const auto back = call(mixer, Style::VrOnly, {}, false, vr);
        check(neutral(back.pad) && back.held, "regained VR delivered inputs held across the gap");
    }

    // Opening or closing UEVR masks what was held for the menu chord.
    {
        Mixer mixer;
        const Pad chord = button(0x0040 | 0x0080);
        call(mixer, Style::VrOnly, {}, false, {});
        check(call(mixer, Style::VrOnly, {}, false, chord).pad == chord, "L3+R3 was not delivered");
        check(neutral(call(mixer, Style::VrOnly, {}, false, chord, true, 1000, true).pad), "chord leaked into UI");
        call(mixer, Style::VrOnly, {}, false, {}, true, 1000, true);
        check(call(mixer, Style::VrOnly, {}, false, chord, true, 1000, true).pad == chord, "UI could not see a new chord");
        check(neutral(call(mixer, Style::VrOnly, {}, false, chord).pad), "closing chord leaked into the game");
    }

    // Both: buttons combine, triggers take max, sticks go whole to the larger deflection.
    {
        Mixer mixer;
        call(mixer, Style::Both, physical, true, {});
        const auto mixed = call(mixer, Style::Both, physical, true, vr);
        check(mixed.connected && mixed.vr_ready && mixed.source == Source::Both, "both did not activate");
        check(mixed.pad.buttons == 0x3000 && mixed.pad.lt == 230 && mixed.pad.rt == 170, "buttons/triggers not combined");
        check(mixed.pad.lx == 28000 && mixed.pad.ly == -27000, "larger VR left stick did not win");
        check(mixed.pad.rx == 17000 && mixed.pad.ry == -12000, "larger VR right stick did not win");
        const Pad treadmill = left_stick(0, 30000);
        const auto walking = call(mixer, Style::Both, treadmill, true, right_stick(15000, 0));
        check(walking.pad.lx == 0 && walking.pad.ly == 30000 && walking.pad.rx == 15000,
            "treadmill movement lost while VR looked around");
        check(walking.vr == right_stick(15000, 0), "VR share for menu navigation included the treadmill");
        const auto pair = call(mixer, Style::Both, left_stick(20000, 0), true, left_stick(0, 19000));
        check(pair.pad.lx == 20000 && pair.pad.ly == 0, "stick axes mixed across sources");
        const auto tie = call(mixer, Style::Both, left_stick(10000, 0), true, left_stick(0, 10000));
        check(tie.pad.lx == 10000 && tie.pad.ly == 0, "tie did not keep the physical stick");
        const auto alone = call(mixer, Style::Both, physical, true, vr, false);
        check(alone.pad == physical && alone.source == Source::Physical && alone.status == Status::WaitingVr,
            "Xbox stopped while VR was unavailable");
        const auto vr_only_now = call(mixer, Style::Both, {}, false, {});
        check(vr_only_now.connected && vr_only_now.source == Source::Vr, "missing Xbox stopped VR");
    }

    // Last used: a new engagement switches owner; holds and drift never do.
    {
        Mixer mixer;
        const auto first = call(mixer, Style::LastUsed, left_stick(0, 30000), true, {});
        check(first.source == Source::Vr && neutral(first.pad), "VR did not own the slot after enable");
        const auto press = call(mixer, Style::LastUsed, left_stick(0, 30000), true, button(0x1000));
        check(press.source == Source::Vr && press.pad == button(0x1000), "VR press did not deliver alone");
        const auto pad_press = call(mixer, Style::LastUsed, Pad{0x4000, 0, 0, 0, 30000}, true, button(0x1000));
        check(pad_press.source == Source::Physical && pad_press.pad.buttons == 0x4000 && pad_press.pad.ly == 30000,
            "Xbox press did not take over");
        const auto steady = call(mixer, Style::LastUsed, Pad{0x4000, 0, 0, 0, 30000}, true, button(0x1000));
        check(steady.source == Source::Physical, "a steady VR hold took ownership back");
        const auto drift = call(mixer, Style::LastUsed, Pad{0x4000, 0, 0, 0, 30000}, true, Pad{0x1000, 0, 0, 0, 0, 3000});
        check(drift.source == Source::Physical, "VR stick below the deadzone took ownership");
        const auto look = call(mixer, Style::LastUsed, Pad{0x4000, 0, 0, 0, 30000}, true, Pad{0x1000, 0, 0, 0, 0, 20000});
        check(look.source == Source::Vr && look.pad == Pad{0x1000, 0, 0, 0, 0, 20000}, "VR stick did not take over");
        const auto xbox_drift = call(mixer, Style::LastUsed, Pad{0x4000, 0, 0, 2000, 30000}, true, Pad{0x1000});
        check(xbox_drift.source == Source::Vr, "Xbox drift took ownership");
        const auto sleeping = call(mixer, Style::LastUsed, physical, true, {}, false);
        check(sleeping.source == Source::Physical && sleeping.pad == physical, "Xbox stopped while VR slept");
    }

    // VR only ignores the Xbox entirely.
    {
        Mixer mixer;
        call(mixer, Style::VrOnly, physical, true, {});
        const auto r = call(mixer, Style::VrOnly, physical, true, vr);
        check(r.pad == vr && r.source == Source::Vr, "VR only mixed in the Xbox");
        const auto waiting = call(mixer, Style::VrOnly, physical, true, vr, false);
        check(neutral(waiting.pad) && waiting.connected && waiting.source == Source::None, "VR only passed the Xbox");
    }

    // Turning off and on again starts fresh.
    {
        Mixer mixer;
        call(mixer, Style::VrOnly, {}, false, {});
        call(mixer, Style::VrOnly, {}, false, vr);
        mixer.apply(false, Style::VrOnly, {}, false, vr, true, 1000, 1000, false);
        check(neutral(call(mixer, Style::VrOnly, {}, false, vr).pad), "off-on did not mask held input");
    }

    // Clock reversal is treated as a missing sample.
    {
        Mixer mixer;
        call(mixer, Style::VrOnly, {}, false, {}, true, 1000);
        check(!mixer.apply(true, Style::VrOnly, {}, false, vr, true, 900, 900, false).vr_ready,
            "clock reversal was trusted");
    }

    // Menu gesture: hold 1 s toggles, a tap is a short Start or Back pulse.
    {
        MenuGesture g;
        check(!g.update(true, start_bit, false, 0) && g.holding(), "press was not tracked");
        check(!g.update(true, start_bit, false, 999), "toggled before one second");
        check(g.update(true, start_bit, false, 1000), "did not toggle at one second");
        check(!g.update(true, start_bit, true, 1500) && !g.holding(), "toggled twice in one hold");
        check(!g.update(true, 0, true, 1600) && g.take(1600) == 0, "release after a toggle sent Start");
    }
    {
        MenuGesture g;
        g.update(true, start_bit, true, 0);
        check(g.take(10) == 0, "Start sent while still holding");
        g.update(true, 0, true, 300);
        check(g.take(300) == start_bit && g.take(350) == start_bit, "tap did not pulse Start");
        check(g.take(400) == 0, "pulse outlived its time");
        g.update(true, back_bit, true, 1000);
        g.update(true, 0, true, 1200);
        check(g.take(5000) == back_bit && g.take(5001) == back_bit && g.take(5002) == 0,
            "late polls missed the Back pulse");
    }
    {
        MenuGesture g;
        g.update(true, start_bit, false, 0);
        g.update(true, 0, false, 200);
        check(g.take(200) == 0, "tap while off reached the game");
        g.update(true, start_bit, true, 1000);
        check(!g.update(false, 0, true, 1500) && !g.holding(), "lost tracking kept the hold");
        check(!g.update(true, start_bit, true, 2100), "lost tracking let an old hold toggle");
        g.update(true, 0, true, 2200);
        check(g.take(2200) == start_bit, "press after tracking returned was not a tap");
        g.cancel_pulse();
        check(g.take(2201) == 0, "cancelled pulse still sent");
    }

    // Both stick clicks held turn VR on from off (Index has no usable Menu button).
    {
        constexpr std::uint16_t sticks = 0x0040 | 0x0080;
        MenuGesture g;
        check(!g.update(true, sticks, false, 0) && g.holding(), "stick chord was not tracked while off");
        check(!g.update(true, sticks, false, 999), "stick chord fired before one second");
        check(g.update(true, sticks, false, 1000), "stick chord did not turn VR on");
        check(!g.update(true, sticks, true, 1500) && !g.holding(), "stick chord fired again once on");
        check(!g.update(true, 0, true, 1600) && g.take(1600) == 0, "stick chord release sent a pulse");
        MenuGesture on;
        check(!on.update(true, sticks, true, 0) && !on.update(true, sticks, true, 2000),
            "stick chord turned VR off (it must open UEVR settings instead)");
        MenuGesture single;
        check(!single.update(true, 0x0040, false, 0) && !single.update(true, 0x0040, false, 2000),
            "one stick click alone toggled VR");
        MenuGesture lost;
        lost.update(true, sticks, false, 0);
        lost.update(false, 0, false, 600);
        check(!lost.update(true, sticks, false, 1100), "lost tracking let an old stick chord fire");
    }

    // Readiness reports what the slot actually delivered.
    {
        Mixer mixer;
        Readiness readiness;
        check(readiness.read(false, 1000, false).status == Status::Off, "off was not reported");
        check(readiness.read(true, 1000, false).status == Status::WaitingVr, "no sample was not waiting");
        readiness.sample(Status::Active, 1000);
        check(readiness.read(true, 1000, false).status == Status::WaitingPoll, "unpolled slot looked active");
        check(!readiness.polled(1000), "missing poll was reported as polled");
        const auto r = call(mixer, Style::Both, physical, true, vr, true, 1000);
        readiness.poll(r, 1000);
        const auto snap = readiness.read(true, 1000, false);
        check(snap.status == Status::Active && snap.source == Source::Both && snap.held,
            "held-at-enable not reported");
        check(readiness.read(true, 1000, true).status == Status::MenuReady, "open menu not reported");
        check(readiness.read(true, 1300, false).status == Status::WaitingVr, "stale sample looked active");
        readiness.sample(Status::Active, 1300);
        check(readiness.read(true, 1300, false).status == Status::WaitingPoll && !readiness.polled(1300),
            "stale poll looked active");
        for (const auto blocker : {Status::Passthrough, Status::SlotFiltered, Status::InputMuted,
            Status::GameUnfocused, Status::OpenXRRequired}) {
            readiness.sample(blocker, 1300);
            check(readiness.read(true, 1300, false).status == blocker, "blocker hidden by missing polls");
        }
        readiness.sample(Status::WaitingVr, 1300);
        check(readiness.read(true, 1300, false).status == Status::WaitingPoll, "waiting VR hid missing polls");
        readiness.reset();
        check(readiness.read(true, 1300, false).status == Status::WaitingVr, "reset retained readiness");
    }

    // Packet numbers follow the delivered state.
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
    std::cout << "PASS: " << checks << " VR controller input checks (pure C++ values; no devices or Windows APIs)\n";
}
