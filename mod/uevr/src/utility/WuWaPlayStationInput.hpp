#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Independently implemented input-only decoder. Protocol references and bounds:
// mod/PLAYSTATION-SHORTCUTS.md. No feature/output reports or virtual gamepad.
namespace wuwa_ps {
enum class Model { unsupported, ds4, dualsense };
inline Model model(uint16_t vendor, uint16_t product) {
    if (vendor != 0x054c) return Model::unsupported;
    if (product == 0x05c4 || product == 0x09cc) return Model::ds4;
    if (product == 0x0ce6 || product == 0x0df2) return Model::dualsense;
    return Model::unsupported;
}
struct Pad {
    uint16_t buttons{};
    uint8_t lt{}, rt{};
    int16_t lx{}, ly{}, rx{}, ry{};
};
inline bool neutral(const Pad& p) {
    return !p.buttons && p.lt < 30 && p.rt < 30 &&
        std::abs(int(p.lx)) < 8000 && std::abs(int(p.ly)) < 8000 &&
        std::abs(int(p.rx)) < 8000 && std::abs(int(p.ry)) < 8000;
}
inline int16_t axis(uint8_t v, bool invert = false) {
    // Both 127 and 128 occur as resting values. Keep both exactly neutral.
    int n = v < 127 ? (int(v) - 127) * 32768 / 127 :
        v > 128 ? (int(v) - 128) * 32767 / 127 : 0;
    if (invert) n = n == -32768 ? 32767 : -n;
    return static_cast<int16_t>(n);
}
inline uint32_t crc_byte(uint32_t crc, uint8_t value) {
    crc ^= value;
    for (int i = 0; i < 8; ++i) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    return crc;
}
inline bool bluetooth_crc(const uint8_t* bytes, size_t size) {
    if (size != 78) return false;
    uint32_t crc = crc_byte(0xffffffffu, 0xa1);
    for (size_t i = 0; i < size - 4; ++i) crc = crc_byte(crc, bytes[i]);
    crc ^= 0xffffffffu;
    const auto* end = bytes + size - 4;
    const uint32_t expected = uint32_t(end[0]) | (uint32_t(end[1]) << 8) |
        (uint32_t(end[2]) << 16) | (uint32_t(end[3]) << 24);
    return crc == expected;
}
inline bool decode(Model kind, const uint8_t* data, size_t size, Pad& out) {
    if (!data || kind == Model::unsupported || size < 10) return false;
    // Windows DS4 Bluetooth uses the descriptor's 547-byte transport capacity.
    // The recognized report still has its usual payload/CRC in the first bytes;
    // descriptor padding is not a new report format. See DS4Windows reference.
    if (kind == Model::ds4 && size == 547) {
        if (data[0] == 1) size = 64;
        else if (data[0] >= 0x11 && data[0] <= 0x19) size = 78;
        else return false;
    }
    size_t base{}, buttons{}, triggers{};
    if (kind == Model::ds4) {
        if (data[0] == 1 && (size == 10 || size == 64 || size == 78 || size == 128)) base = 1;
        else if (data[0] >= 0x11 && data[0] <= 0x19 && size == 78 &&
                 (data[1] & 0x80) && bluetooth_crc(data, size)) base = 3;
        else return false;
        buttons = base + 4; triggers = base + 7;
    } else {
        if (data[0] == 1 && (size == 10 || size == 78)) {
            base = 1; buttons = 5; triggers = 8;
        } else if (data[0] == 1 && size == 64) {
            base = 1; buttons = 8; triggers = 5;
        } else if (data[0] == 0x31 && size == 78 && bluetooth_crc(data, size)) {
            base = 2; buttons = 9; triggers = 6;
        } else return false;
    }
    if ((data[buttons] & 15) > 8) return false;
    Pad p{};
    // Xbox-compatible bit positions are an internal shortcut vocabulary only.
    const uint16_t hats[] = {1, 1|8, 8, 8|2, 2, 2|4, 4, 4|1, 0};
    p.buttons = hats[data[buttons] & 15];
    if (data[buttons] & 0x10) p.buttons |= 0x4000; // Square / X
    if (data[buttons] & 0x20) p.buttons |= 0x1000; // Cross / A
    if (data[buttons] & 0x40) p.buttons |= 0x2000; // Circle / B
    if (data[buttons] & 0x80) p.buttons |= 0x8000; // Triangle / Y
    const auto b = data[buttons + 1];
    if (b & 0x01) p.buttons |= 0x0100; // L1
    if (b & 0x02) p.buttons |= 0x0200; // R1
    if (b & 0x10) p.buttons |= 0x0020; // Share / Create
    if (b & 0x20) p.buttons |= 0x0010; // Options
    if (b & 0x40) p.buttons |= 0x0040; // L3
    if (b & 0x80) p.buttons |= 0x0080; // R3
    p.lt = data[triggers]; p.rt = data[triggers + 1];
    p.lx = axis(data[base]); p.ly = axis(data[base + 1], true);
    p.rx = axis(data[base + 2]); p.ry = axis(data[base + 3], true);
    out = p;
    return true;
}
inline bool recent(uint64_t stamp, uint64_t now, uint64_t age) {
    return stamp != 0 && now >= stamp && now - stamp <= age;
}
inline bool fallback_allowed(uint64_t started, uint64_t last_xinput, uint64_t report, uint64_t now) {
    return now >= started && now - started >= 2000 &&
        !recent(last_xinput, now, 2000) && recent(report, now, 250);
}
// HID menu gesture is edge-driven, including the optional one-second hold.
// Focus loss, disconnect and input-source changes require a neutral report.
struct MenuGesture {
    bool armed{}, fired{};
    uint64_t began{};
    void reset() { *this = {}; }
    bool update(const Pad& p, uint64_t now, bool enabled, bool long_press) {
        if (!enabled) { reset(); return false; }
        if (!armed) { armed = neutral(p); return false; }
        const bool both = (p.buttons & 0xc0) == 0xc0;
        if (!both) { began = 0; fired = false; return false; }
        if (!began || now < began) began = now;
        if (fired || (long_press && now - began < 1000)) return false;
        fired = true; return true;
    }
};
}
