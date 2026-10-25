// Keyboard and mouse shortcuts (1.2.7): who owns each key, what reaches the game, and what the
// mod acts on. Offline; no system calls.
#include "../mod/uevr/src/utility/WuWaKeyboard.hpp"
#include <cassert>
#include <iostream>
using namespace wuwa_keyboard;

namespace {
constexpr uint16_t grave = 0x29, w = 0x11, a = 0x1e, d = 0x20, space = 0x39, e_key = 0x12, q_key = 0x10,
    h_key = 0x23, c_key = 0x2e, one = 0x02, right_ctrl = 0x11d, left_ctrl = 0x1d, shift = 0x2a;
constexpr uint8_t VK_CONTROL_TEST = 0x11, VK_SHIFT_TEST = 0x10;
Result down(State& s, uint16_t id, uint8_t vk, const Context& c) { return s.key(id, vk, true, false, c); }
Result repeat(State& s, uint16_t id, uint8_t vk, const Context& c) { return s.key(id, vk, true, true, c); }
Result up(State& s, uint16_t id, uint8_t vk, const Context& c) { return s.key(id, vk, false, false, c); }
}

int main() {
    const Context play{1, true, false, false}, fly{1, true, true, false}, menu_open{1, false, false, false};
    const Context fixed{1, true, false, true}, off{0, true, true, true}, rctrl{2, true, false, false};
    // The VR key alone never reaches the game (the game would open its console, or type it).
    {
        State s;
        auto r = down(s, grave, 0xc0, play);
        assert(r.swallow && r.action == Action::none && s.vr_held() && s.vr_presses() == 1);
        assert(repeat(s, grave, 0xc0, play).swallow);
        assert(up(s, grave, 0xc0, play).swallow && !s.vr_held());
    }
    // VR key + H: show / hide game UI, once per press; H never reaches the game, its repeats neither.
    {
        State s;
        down(s, grave, 0xc0, play);
        auto r = down(s, h_key, 'H', play);
        assert(r.swallow && r.action == Action::ui && s.presses(Action::ui) == 1);
        assert(s.chords() == State::chord_bit('H'));
        r = repeat(s, h_key, 'H', play);
        assert(r.swallow && r.action == Action::none && s.presses(Action::ui) == 1);
        // Letting go of the VR key first: H stays the mod's until it is released.
        assert(up(s, grave, 0xc0, play).swallow && s.chords() == 0);
        assert(repeat(s, h_key, 'H', play).swallow);
        assert(up(s, h_key, 'H', play).swallow);
        // Afterwards H is the game's again.
        assert(!down(s, h_key, 'H', play).swallow && !up(s, h_key, 'H', play).swallow);
    }
    // An unmapped key with the VR key held still reaches the game (walking while holding it).
    {
        State s;
        down(s, grave, 0xc0, play);
        assert(!down(s, w, 'W', play).swallow && !up(s, w, 'W', play).swallow);
    }
    // A key held before the VR key: its repeats and release go to the game, and it fires nothing.
    {
        State s;
        assert(!down(s, h_key, 'H', play).swallow);
        down(s, grave, 0xc0, play);
        auto r = repeat(s, h_key, 'H', play);
        assert(!r.swallow && r.action == Action::none && s.presses(Action::ui) == 0);
        assert(!up(s, h_key, 'H', play).swallow);
    }
    // E / Q: held bits for the camera height, both together for the reset; no press counted.
    {
        State s;
        down(s, grave, 0xc0, play);
        assert(down(s, e_key, 'E', play).swallow && s.chords() == State::up_bit);
        assert(down(s, q_key, 'Q', play).swallow && s.chords() == (State::up_bit | State::down_bit));
        up(s, e_key, 'E', play);
        assert(s.chords() == State::down_bit);
    }
    // With the UEVR window open nothing is taken, but a key the mod owns is still released into nothing.
    {
        State s;
        down(s, grave, 0xc0, play);
        down(s, one, '1', play);
        assert(s.presses(Action::full_or_stereo) == 1);
        assert(!down(s, h_key, 'H', menu_open).swallow);  // chords pause while the menu is open
        assert(up(s, one, '1', menu_open).swallow);
        assert(up(s, grave, 0xc0, menu_open).swallow && !s.vr_held());
        assert(!down(s, grave, 0xc0, menu_open).swallow && !s.vr_held());
        assert(!up(s, grave, 0xc0, menu_open).swallow);
    }
    // VR key off: ` is an ordinary key.
    {
        State s;
        assert(!down(s, grave, 0xc0, off).swallow && !s.vr_held());
    }
    // Right Ctrl as the VR key; Left Ctrl is a different key.
    {
        State s;
        assert(!down(s, left_ctrl, VK_CONTROL_TEST, rctrl).swallow);
        assert(down(s, right_ctrl, VK_CONTROL_TEST, rctrl).swallow && s.vr_held());
        assert(down(s, c_key, 'C', rctrl).action == Action::freecam);
    }
    // Freecam flight: W A S D, Space / E up, Ctrl / Q down, Shift faster; by position, kept from the game.
    {
        State s;
        assert(down(s, w, 'W', fly).swallow && s.flight() == fly_forward);
        assert(down(s, space, ' ', fly).swallow && down(s, e_key, 'E', fly).swallow);
        assert(s.flight() == (fly_forward | fly_up));
        // Space released while E is held: still rising.
        up(s, space, ' ', fly);
        assert(s.flight() == (fly_forward | fly_up));
        up(s, e_key, 'E', fly);
        assert(s.flight() == fly_forward);
        assert(down(s, shift, VK_SHIFT_TEST, fly).swallow && (s.flight() & fly_fast));
        // Freecam ends with W held: W stays the mod's until it is released (the game never saw it go down).
        assert(repeat(s, w, 'W', play).swallow && up(s, w, 'W', play).swallow);
        // Freecam starts with A held: A stays the game's, so the game sees its release.
        State g;
        assert(!down(g, a, 'A', play).swallow);
        assert(!repeat(g, a, 'A', fly).swallow && g.flight() == 0 && !up(g, a, 'A', fly).swallow);
        // With the VR key held in freecam, E is the camera-up chord, not flight.
        State v;
        down(v, grave, 0xc0, fly);
        assert(down(v, e_key, 'E', fly).swallow && v.flight() == 0 && v.chords() == State::up_bit);
        assert(!down(v, d, 'D', fly).swallow);  // D is not a chord: the game's
    }
    // Mouse: looks in freecam and is kept from the game; button packets always pass; paused with the VR key.
    {
        State s;
        assert(!s.mouse(5, 3, false, play) && s.mouse_x() == 0);
        assert(s.mouse(5, -3, false, fly) && s.mouse_x() == 5 && s.mouse_y() == -3);
        assert(!s.mouse(2, 0, true, fly) && s.mouse_x() == 7);
        down(s, grave, 0xc0, fly);
        assert(s.mouse(9, 9, false, fly) && s.mouse_x() == 7);
    }
    // Wheel: only with the VR key held, and only while it does something.
    {
        State s;
        assert(!s.wheel(120, fixed));
        down(s, grave, 0xc0, fixed);
        assert(!s.wheel(120, play) && s.wheel(-240, fixed) && s.wheel_total() == -240 && s.wheel_events() == 1);
    }
    // Focus lost: everything the mod held is up, the generation moves on, presses are kept.
    {
        State s;
        down(s, grave, 0xc0, fly);
        down(s, h_key, 'H', fly);
        const auto generation = s.generation();
        s.reset();
        assert(!s.vr_held() && s.chords() == 0 && s.flight() == 0 && s.generation() == generation + 1);
        assert(s.presses(Action::ui) == 1);
        assert(!up(s, h_key, 'H', play).swallow);
    }
    // Every binding is unique and none uses a flight letter or E / Q.
    for (const auto& b : bindings) {
        int same = 0;
        for (const auto& o : bindings) same += o.vk == b.vk;
        assert(same == 1 && b.vk != 'W' && b.vk != 'A' && b.vk != 'S' && b.vk != 'D' && b.vk != up_vk && b.vk != down_vk);
    }
    // lParam: scan code in bits 16-23, extended in bit 24.
    assert(key_id(0x00290001u) == 0x29 && key_id(0x011d0001u) == 0x11d);
    std::cout << "keyboard shortcuts: all checks passed\n";
    return 0;
}
