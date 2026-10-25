#pragma once

// Keyboard and mouse shortcuts (1.2.7; owner, 25 Oct 2026: keyboard players had no equivalent of the
// controller shortcuts or cards). Hold the VR key (by default the key left of 1: ` on US and UK layouts)
// and press a key, as L3 + a button on a controller; hold it half a second for the keyboard card. In
// freecam, WASD and the mouse fly the camera.
//
// Each key belongs to whoever saw it go down: a fresh press while the VR key is held, or a flight key in
// freecam, is the mod's, and its repeats and release never reach the game; any other press is the game's
// until it is released, so a key held when a mode starts or ends is never left stuck down in the game.
// Pure logic: WuWaControlsComponent feeds it the game window's messages and acts on what it returns.
#include <array>
#include <cstdint>

namespace wuwa_keyboard {
enum class Action : uint8_t {
    none, full_or_stereo, mono, portal, diorama, blur, hide_near, ui, recenter,
    first_person, fixed_camera, freecam, animation, adjust, pin_card, screenshot, count
};
constexpr int action_count = static_cast<int>(Action::count);

// With the VR key held, by virtual-key code: the letter printed on the key, so the card's mnemonics
// (R recenter, H HUD, F first person...) hold on any layout.
struct Binding { uint8_t vk; Action action; };
inline constexpr Binding bindings[] = {
    {'1', Action::full_or_stereo}, {'2', Action::mono}, {'3', Action::portal}, {'4', Action::diorama},
    {'B', Action::blur}, {'N', Action::hide_near}, {'H', Action::ui}, {'R', Action::recenter},
    {'F', Action::first_person}, {'T', Action::fixed_camera}, {'C', Action::freecam}, {'G', Action::animation},
    {'M', Action::adjust}, {'K', Action::pin_card}, {'P', Action::screenshot},
};
// Held with the VR key: camera up / down (first person, fixed camera), nearer / back in story shots,
// HUD up / down in HUD / mouse mode; both together put it back.
inline constexpr uint8_t up_vk = 'E', down_vk = 'Q';
inline Action action_for(uint8_t vk) noexcept {
    for (const auto& b : bindings) if (b.vk == vk) return b.action;
    return Action::none;
}

// A key by position: its scan code, plus 0x100 for the extended ones (Right Ctrl, arrows...).
inline uint16_t key_id(uint32_t lparam) noexcept {
    return static_cast<uint16_t>(((lparam >> 16) & 0xff) | ((lparam & (1u << 24)) ? 0x100 : 0));
}
// The VR key choices: 0 off, 1 the key left of 1 (scan 0x29: ` on US / UK, ^ on German, the squared sign on French), 2 Right Ctrl.
inline constexpr uint16_t vr_keys[] = {0, 0x29, 0x11d};

// Freecam flight keys, by position so the same keys work on AZERTY (ZQSD) and others.
enum Flight : uint16_t {
    fly_forward = 1, fly_left = 2, fly_back = 4, fly_right = 8, fly_up = 16, fly_down = 32, fly_fast = 64,
};
struct FlightKey { uint16_t id; uint16_t bit; };
inline constexpr FlightKey flight_keys[] = {
    {0x11, fly_forward}, {0x1e, fly_left}, {0x1f, fly_back}, {0x20, fly_right},
    {0x39, fly_up}, {0x12, fly_up}, {0x1d, fly_down}, {0x10, fly_down}, {0x2a, fly_fast}, {0x36, fly_fast},
};
inline uint16_t flight_bit(uint16_t id) noexcept {
    for (const auto& k : flight_keys) if (k.id == id) return k.bit;
    return 0;
}

// What the component knows as a message arrives.
struct Context {
    uint8_t vr_choice{1};  // which VR key (vr_keys), 0 off
    bool shortcuts{};      // shortcuts on and no UEVR window open
    bool flight{};         // freecam with keyboard flight on, no game menu or cursor, no HUD / mouse mode
    bool wheel{};          // the VR key + mouse wheel does something now (fixed camera, HUD / mouse mode)
};

struct Result {
    bool swallow{};               // keep the message from the game
    Action action{Action::none};  // a shortcut pressed just now
};

enum class Owner : uint8_t { game, vr_key, chord, flight };

class State {
public:
    // A WM_KEYDOWN / WM_KEYUP (or the SYS forms). repeat: lParam bit 30 on a down (the key was already down).
    Result key(uint16_t id, uint8_t vk, bool down, bool repeat, const Context& c) noexcept {
        auto& owner = m_owner[id & 0x1ff];
        if (!down) {
            const auto was = owner;
            owner = Owner::game;
            if (was == Owner::vr_key) { m_vr = false; m_chords = 0; }
            if (was == Owner::chord) m_chords &= ~chord_bit(vk);
            if (was == Owner::flight) m_flight &= ~held_flight_bit(id);
            return {was != Owner::game, Action::none};
        }
        // A repeat, or a down for a key already seen down, goes where its first press went.
        if (repeat || owner != Owner::game) return {owner != Owner::game, Action::none};
        const uint16_t vr = c.vr_choice < 3 ? vr_keys[c.vr_choice] : 0;
        if (vr && id == vr && c.shortcuts) {
            owner = Owner::vr_key; m_vr = true; m_chords = 0; ++m_vr_presses;
            return {true, Action::none};
        }
        if (m_vr && c.shortcuts) {
            const auto action = action_for(vk);
            if (action != Action::none || vk == up_vk || vk == down_vk) {
                owner = Owner::chord; m_chords |= chord_bit(vk);
                if (action != Action::none) ++m_presses[static_cast<int>(action)];
                return {true, action};
            }
            return {};
        }
        if (c.flight && !m_vr) {
            if (const auto bit = flight_bit(id)) {
                owner = Owner::flight; m_flight |= bit;
                return {true, Action::none};
            }
        }
        return {};
    }
    // WM_MOUSEWHEEL: swallowed (and added up) only with the VR key held while it does something.
    bool wheel(int delta, const Context& c) noexcept {
        if (!m_vr || !c.shortcuts || !c.wheel) return false;
        m_wheel += delta;
        ++m_wheel_events;
        return true;
    }
    // Raw mouse movement (WM_INPUT). In freecam it looks around and is kept from the game camera; with the
    // VR key held it is kept back too, as the sticks are while L3 is. Packets carrying a button press or
    // release always reach the game, so a click is never lost.
    bool mouse(long dx, long dy, bool buttons, const Context& c) noexcept {
        if (!c.flight) return false;
        if (!m_vr) { m_mouse_x += dx; m_mouse_y += dy; }
        return !buttons;
    }
    // Focus lost or the window closed: every key the mod held is up (the game sees nothing).
    void reset() noexcept {
        m_owner.fill(Owner::game);
        m_vr = false; m_chords = 0; m_flight = 0;
        ++m_generation;
    }

    bool vr_held() const noexcept { return m_vr; }
    // Chord keys held now, one bit per Action, plus bit 30 E (up) and bit 31 Q (down).
    uint32_t chords() const noexcept { return m_chords; }
    uint16_t flight() const noexcept { return m_flight; }
    uint32_t presses(Action a) const noexcept { return m_presses[static_cast<int>(a)]; }
    uint32_t vr_presses() const noexcept { return m_vr_presses; }
    long long mouse_x() const noexcept { return m_mouse_x; }
    long long mouse_y() const noexcept { return m_mouse_y; }
    long long wheel_total() const noexcept { return m_wheel; }
    uint32_t wheel_events() const noexcept { return m_wheel_events; }
    // Changes when held keys were dropped by reset(): totals and presses carry on, but a reader resyncs.
    uint32_t generation() const noexcept { return m_generation; }

    static constexpr uint32_t up_bit = 1u << 30, down_bit = 1u << 31;
    static uint32_t chord_bit(uint8_t vk) noexcept {
        if (vk == up_vk) return up_bit;
        if (vk == down_vk) return down_bit;
        const auto a = action_for(vk);
        return a == Action::none ? 0u : 1u << static_cast<int>(a);
    }

private:
    uint16_t held_flight_bit(uint16_t id) const noexcept {
        // Two keys can share a bit (Space and E both rise): keep it while the other is still held.
        const auto bit = flight_bit(id);
        for (const auto& k : flight_keys)
            if (k.bit == bit && k.id != id && m_owner[k.id] == Owner::flight) return 0;
        return bit;
    }
    std::array<Owner, 0x200> m_owner{};
    bool m_vr{};
    uint32_t m_chords{};
    uint16_t m_flight{};
    std::array<uint32_t, action_count> m_presses{};
    uint32_t m_vr_presses{}, m_wheel_events{}, m_generation{};
    long long m_mouse_x{}, m_mouse_y{}, m_wheel{};
};
} // namespace wuwa_keyboard
