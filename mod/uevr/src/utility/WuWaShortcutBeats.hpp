#pragma once

// What a shortcut is, device by device, and how its playback runs: beats of held and pressed controls on a
// looping timeline. Plain data with no ImGui or Windows, so the VR menu model (and its unit test) can carry
// a shortcut per row; utility/WuWaPad3D.hpp draws it. 1.2.7.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wuwa_pad3d {
enum class Device : std::uint8_t { xbox, playstation, vr, keyboard };

// Pad controls by their Xbox names.
namespace button {
constexpr std::uint32_t A = 1u << 0, B = 1u << 1, X = 1u << 2, Y = 1u << 3, LB = 1u << 4, RB = 1u << 5, LT = 1u << 6,
    RT = 1u << 7, L3 = 1u << 8, R3 = 1u << 9, View = 1u << 10, Menu = 1u << 11, Up = 1u << 12, Down = 1u << 13,
    Left = 1u << 14, Right = 1u << 15;
constexpr std::uint32_t dpad = Up | Down | Left | Right;
}

// One beat of a shortcut. Pads: `hold` goes down at this beat and stays down to the end; `press` is pressed in
// it (tapped `taps` times, or held `long_press` seconds). The keyboard uses key names: "VR" (the VR key),
// "Insert", or a letter / digit as the layout prints it.
struct Beat {
    std::uint32_t hold{}, press{};
    std::string key_hold, key_press;
    int taps{1};
    float long_press{};
};
using Beats = std::vector<Beat>;

// A shortcut on every device. Empty `vr` uses the pad's beats (the mapping above) unless they need the D-pad;
// `vr_only` shortcuts exist only on VR controllers; an empty `keyboard` has no key.
struct Shortcut {
    Beats pad, vr, keyboard;
    bool vr_only{};
    const Beats& beats(Device device) const {
        static const Beats none{};
        if (device == Device::keyboard) return keyboard;
        if (device == Device::vr) {
            if (!vr.empty()) return vr;
            for (const auto& beat : pad) {
                if ((beat.hold | beat.press) & button::dpad) return none;
            }
            return pad;
        }
        return vr_only ? none : pad;
    }
    bool any() const { return !pad.empty() || !vr.empty() || !keyboard.empty(); }
};

// Builders for the shortcut tables.
inline Beat hold(std::uint32_t controls) { Beat beat; beat.hold = controls; return beat; }
inline Beat press(std::uint32_t controls, int taps = 1) { Beat beat; beat.press = controls; beat.taps = taps; return beat; }
inline Beat long_press(std::uint32_t controls, float seconds) { Beat beat; beat.press = controls; beat.long_press = seconds; return beat; }
inline Beat key_hold(std::string key) { Beat beat; beat.key_hold = std::move(key); return beat; }
inline Beat key_press(std::string key, int taps = 1) { Beat beat; beat.key_press = std::move(key); beat.taps = taps; return beat; }
inline Beat key_long_press(std::string key, float seconds) { Beat beat; beat.key_press = std::move(key); beat.long_press = seconds; return beat; }
// Hold L3, then press; hold LT + RT, then press; hold the VR key, then press.
inline Beats l3_then(std::uint32_t control) { return {hold(button::L3), press(control)}; }
inline Beats triggers_then(Beat beat) { return {hold(button::LT | button::RT), std::move(beat)}; }
inline Beats vr_key_then(std::string key) { return {key_hold("VR"), key_press(std::move(key))}; }

// The device a shortcut is shown on: the one used last when it has the shortcut there; a VR-only shortcut on
// the VR controllers; a pad shortcut without a key on the Xbox pad.
inline Device shown_on(const Shortcut& shortcut, Device last) {
    if (!shortcut.beats(last).empty()) return last;
    if (!shortcut.beats(Device::vr).empty() && (shortcut.vr_only || last == Device::vr)) return Device::vr;
    if (last == Device::keyboard && !shortcut.pad.empty()) return Device::xbox;
    return last;
}

// ---- Playback ----

struct Moment {
    int beat{-1};                  // the beat playing, -1 the rest between loops
    std::uint32_t held{}, down{}, ripple_mask{};
    std::string key_held, key_down, ripple_key;
    float press{};                 // 0..1 how far `down` is pressed
    float ripple{-1.0f};           // 0..1 a ring spreading from what just went down; -1 none
    float progress{-1.0f};         // 0..1 a long press filling up; -1 none
};

constexpr float lead_seconds = 0.25f, tap_seconds = 0.42f, tail_seconds = 0.55f, rest_seconds = 1.1f;
inline float beat_seconds(const Beat& beat) {
    if (!beat.press && beat.key_press.empty()) return 1.0f;
    if (beat.long_press > 0.0f) return lead_seconds + beat.long_press + 0.6f;
    return lead_seconds + beat.taps * tap_seconds + tail_seconds;
}
inline float loop_seconds(const Beats& beats) {
    float total = rest_seconds;
    for (const auto& beat : beats) total += beat_seconds(beat);
    return total;
}

inline Moment moment(const Beats& beats, double time) {
    Moment out;
    if (beats.empty()) return out;
    float t = static_cast<float>(std::fmod((std::max)(time, 0.0), static_cast<double>(loop_seconds(beats))));
    for (int i = 0; i < static_cast<int>(beats.size()); ++i) {
        const auto& beat = beats[i];
        const float length = beat_seconds(beat);
        out.held |= beat.hold;
        if (!beat.key_hold.empty()) out.key_held = beat.key_hold;
        if (t >= length) {
            t -= length;
            continue;
        }
        out.beat = i;
        if (t < 0.5f && (beat.hold || !beat.key_hold.empty())) {
            out.ripple_mask = beat.hold;
            out.ripple_key = beat.key_hold;
            out.ripple = t / 0.5f;
        }
        if (!beat.press && beat.key_press.empty()) return out;
        const float local = t - lead_seconds;
        if (local < 0.0f) return out;
        const auto pressed = [&](float amount) {
            out.down = beat.press;
            out.key_down = beat.key_press;
            out.press = amount;
        };
        const auto ripple = [&](float since) {
            if (since < 0.0f || since >= 0.5f) return;
            out.ripple_mask = beat.press;
            out.ripple_key = beat.key_press;
            out.ripple = since / 0.5f;
        };
        if (beat.long_press > 0.0f) {
            if (local < beat.long_press) {
                pressed((std::min)(local / 0.1f, 1.0f));
                out.progress = local / beat.long_press;
            }
            ripple(local - beat.long_press); // the shortcut fires when the hold completes
            return out;
        }
        for (int tap = 0; tap < beat.taps; ++tap) {
            const float into = local - tap * tap_seconds;
            if (into < 0.0f) break;
            ripple(into);
            if (into < 0.24f) pressed(into < 0.06f ? into / 0.06f : into > 0.18f ? (0.24f - into) / 0.06f : 1.0f);
        }
        return out;
    }
    out.held = 0;
    out.key_held.clear();
    return out;
}
}
