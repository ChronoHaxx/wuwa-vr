#pragma once

// A controller drawn live as a glowing low-poly mesh: wire edges over a field of soft light points (a splat
// look), turning slowly, with the buttons of a shortcut lit beat by beat ("hold LT + RT", "then click R3").
// Only ImDrawList calls, so it stays crisp at any size in the headset and needs no texture. The VR menu shows
// the focused setting's shortcut with it; the controls cards draw their controller with it. Four devices:
// an Xbox pad, a PlayStation pad, a pair of VR controllers (the mod's mapping: A/B/X/Y keep their labels,
// triggers = LT / RT, grips = LB / RB, stick clicks = L3 / R3, left Menu = Menu, left grip + Menu = View, no
// D-pad) and a keyboard (the VR key chords).
// 1.2.7 (owner, 5 Nov 2026: "3D-ish controller overview, mesh style, playback on how to do it").
// Header only: the native build adds no source file without a CMake run.
// Never name anything near / far here: windef.h defines both as macros in the backend.
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "WuWaLocalization.hpp"
#include "WuWaShortcutBeats.hpp"

namespace wuwa_pad3d {
// ---- The keyboard layout ----
// Keys are by position (scan code, + 0x100 for extended keys) and named as the player's layout prints them.
// The host fills these (WuWaControlsComponent::update_key_names); the defaults are US QWERTY.
inline std::string vr_key_label{"`"};
inline std::uint16_t vr_key_scan{0x29};
inline std::array<std::uint16_t, 128> scan_for{};  // a letter or digit (its virtual-key code) -> scan code; 0: US
inline std::array<std::string, 0x60> printed{};    // a scan code -> what the key prints; empty: US

inline std::uint16_t us_scan(char c) {
    static constexpr const char* rows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    static constexpr std::uint16_t starts[] = {0x02, 0x10, 0x1e, 0x2c};
    for (int r = 0; r < 4; ++r) {
        for (int i = 0; rows[r][i]; ++i) {
            if (rows[r][i] == c) return static_cast<std::uint16_t>(starts[r] + i);
        }
    }
    return 0;
}
inline std::uint16_t key_scan(const std::string& key) {
    if (key == "VR") return vr_key_scan;
    if (key == "Insert") return 0x152;
    if (key.size() == 1) {
        const auto c = static_cast<unsigned char>(std::toupper(static_cast<unsigned char>(key[0])));
        if (c < scan_for.size() && scan_for[c]) return scan_for[c];
        return us_scan(static_cast<char>(c));
    }
    return 0;
}
// A key's name in captions.
inline std::string key_caption(const std::string& key) {
    if (key == "VR") return vr_key_label;
    if (key.size() > 1) return wuwa_l10n::text(key);
    return key;
}

// ---- Geometry ----

struct V3 { float x{}, y{}, z{}; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0.0f ? a * (1.0f / l) : a;
}

enum class Shape : std::uint8_t { disc, stick, rect, block, cross };

// A control: a shape in the plane (u, v) through c, standing `depth` out along n. Pressing sinks it.
struct Part {
    std::uint32_t control{}; // pad control (pads and VR); 0 for keyboard keys
    std::uint16_t scan{};    // keyboard key
    Shape shape{Shape::disc};
    V3 c, u{1, 0, 0}, v{0, 1, 0}, n{0, 0, 1};
    float ru{}, rv{}, depth{0.03f};
    std::string label;       // printed on it; PlayStation face buttons: "triangle", "circle", "cross", "square"
    ImU32 tint{};            // the label's colour when unlit; 0 is the text colour
};

struct Model {
    std::vector<V3> points;
    std::vector<std::pair<int, int>> edges;
    std::vector<std::pair<V3, float>> splats; // a soft light point and its size
    std::vector<Part> parts;
    float pitch{};                            // the resting view: + looks down onto the top edge
};

inline void loop(Model& m, const std::vector<V3>& ring, bool closed = true) {
    const int base = static_cast<int>(m.points.size()), count = static_cast<int>(ring.size());
    m.points.insert(m.points.end(), ring.begin(), ring.end());
    for (int i = 0; i + 1 < count; ++i) m.edges.push_back({base + i, base + i + 1});
    if (closed && count > 2) m.edges.push_back({base + count - 1, base});
}
inline void segment(Model& m, V3 a, V3 b) {
    const int base = static_cast<int>(m.points.size());
    m.points.push_back(a);
    m.points.push_back(b);
    m.edges.push_back({base, base + 1});
}
// Moves the model so its box is centred on the origin.
inline void centre(Model& m) {
    V3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const auto& p : m.points) {
        lo = {(std::min)(lo.x, p.x), (std::min)(lo.y, p.y), (std::min)(lo.z, p.z)};
        hi = {(std::max)(hi.x, p.x), (std::max)(hi.y, p.y), (std::max)(hi.z, p.z)};
    }
    const V3 mid = (lo + hi) * 0.5f;
    for (auto& p : m.points) p = p - mid;
    for (auto& s : m.splats) s.first = s.first - mid;
    for (auto& part : m.parts) part.c = part.c - mid;
}

// The pad body from the site's drawing (viewBox 640 x 400) as a closed outline in pad units.
inline std::vector<ImVec2> pad_outline(bool playstation) {
    static constexpr float xbox[][6] = {
        {250, 82, 390, 82, 430, 92}, {505, 104, 548, 132, 572, 196}, {600, 280, 606, 352, 566, 372},
        {532, 389, 500, 368, 478, 334}, {460, 306, 438, 292, 404, 292}, {404, 292, 236, 292, 236, 292},
        {202, 292, 180, 306, 162, 334}, {140, 368, 108, 389, 74, 372}, {34, 352, 40, 280, 68, 196},
        {92, 132, 135, 104, 210, 92}};
    // A DualSense: flatter top, longer handles.
    static constexpr float sony[][6] = {
        {250, 86, 390, 86, 430, 90}, {512, 98, 552, 126, 576, 196}, {602, 290, 612, 372, 572, 392},
        {536, 410, 506, 384, 486, 344}, {468, 312, 446, 300, 404, 300}, {404, 300, 236, 300, 236, 300},
        {194, 300, 172, 312, 154, 344}, {134, 384, 104, 410, 68, 392}, {28, 372, 38, 290, 64, 196},
        {88, 126, 128, 98, 210, 90}};
    std::vector<ImVec2> out;
    float px = 210.0f, py = playstation ? 90.0f : 92.0f;
    for (const auto& s : playstation ? sony : xbox) {
        for (int i = 1; i <= 10; ++i) {
            const float t = i / 10.0f, u = 1.0f - t;
            out.push_back({u * u * u * px + 3 * u * u * t * s[0] + 3 * u * t * t * s[2] + t * t * t * s[4],
                u * u * u * py + 3 * u * u * t * s[1] + 3 * u * t * t * s[3] + t * t * t * s[5]});
        }
        px = s[4];
        py = s[5];
    }
    return out;
}
inline bool inside(const std::vector<ImVec2>& poly, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if ((poly[i].y > y) != (poly[j].y > y) &&
            x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    }
    return in;
}

// Pad units to model space: x right, y up, the face toward the viewer (+z) with a gentle dome.
inline V3 pad_point(float x, float y, float z) {
    const float mx = (x - 320.0f) / 300.0f, my = -(y - 215.0f) / 300.0f;
    const float dome = z >= 0.0f ? 0.05f * (std::max)(0.0f, 1.0f - (mx * mx * 0.9f + my * my * 2.2f)) : 0.0f;
    return {mx, my, z + dome};
}

inline Model build_pad(bool playstation) {
    Model m;
    const auto outline = pad_outline(playstation);
    std::vector<V3> face, rim, back;
    for (const auto& p : outline) {
        face.push_back(pad_point(p.x, p.y, 0.0f));
        rim.push_back(pad_point(p.x, p.y, -0.1f));
        back.push_back(pad_point(320.0f + (p.x - 320.0f) * 0.94f, 215.0f + (p.y - 215.0f) * 0.94f, -0.2f));
    }
    loop(m, face);
    loop(m, rim);
    loop(m, back);
    for (size_t i = 0; i < outline.size(); i += 5) {
        segment(m, face[i], rim[i]);
        segment(m, rim[i], back[i]);
    }
    // The light field: points over the face inside the outline, and along the edges.
    for (float y = 96.0f; y < 400.0f; y += 20.0f) {
        const bool odd = static_cast<int>((y - 96.0f) / 20.0f) % 2 != 0;
        for (float x = 40.0f; x < 610.0f; x += 20.0f) {
            const float ox = x + (odd ? 10.0f : 0.0f);
            if (inside(outline, ox, y)) m.splats.push_back({pad_point(ox, y, 0.004f), 1.0f});
        }
    }
    for (size_t i = 0; i < outline.size(); i += 2) m.splats.push_back({face[i], 1.3f});
    for (size_t i = 1; i < outline.size(); i += 3) m.splats.push_back({back[i], 0.9f});

    const V3 side{1, 0, 0}, up{0, 1, 0}, out{0, 0, 1};
    const auto put = [&](std::uint32_t control, Shape shape, float x, float y, float ru, float rv, const char* label,
                         ImU32 tint = 0) {
        Part part;
        part.control = control;
        part.shape = shape;
        part.c = pad_point(x, y, 0.0f);
        part.u = side;
        part.v = up;
        part.n = out;
        part.ru = ru / 300.0f;
        part.rv = rv / 300.0f;
        part.label = label;
        part.tint = tint;
        m.parts.push_back(part);
    };
    // Bumpers sit on the top edge, the triggers behind and above them; both face up and a little forward.
    const auto shoulder = [&](std::uint32_t control, float x, float y, float z, float w, float d, float lean, const char* label) {
        Part part;
        part.control = control;
        part.shape = Shape::block;
        part.label = label;
        part.c = pad_point(x, y, z);
        part.n = normalize(V3{0.0f, 1.0f, lean});
        part.u = side;
        part.v = normalize(cross(part.n, part.u));
        part.ru = w / 600.0f;
        part.rv = d / 600.0f;
        part.depth = 0.045f;
        m.parts.push_back(part);
    };
    if (playstation) {
        shoulder(button::LT, 168, 80, -0.15f, 84, 64, 0.2f, "L2");
        shoulder(button::RT, 472, 80, -0.15f, 84, 64, 0.2f, "R2");
        shoulder(button::LB, 172, 98, -0.045f, 112, 30, 0.5f, "L1");
        shoulder(button::RB, 468, 98, -0.045f, 112, 30, 0.5f, "R1");
        put(button::View, Shape::rect, 226, 112, 7, 13, "Create");
        put(button::Menu, Shape::rect, 414, 112, 7, 13, "Options");
        loop(m, {pad_point(250, 98, 0.03f), pad_point(390, 98, 0.03f), pad_point(384, 178, 0.03f), pad_point(256, 178, 0.03f)});
        put(button::L3, Shape::stick, 246, 252, 28, 28, "L3");
        put(button::R3, Shape::stick, 394, 252, 28, 28, "R3");
        put(button::dpad, Shape::cross, 160, 184, 13, 13, "");
        put(button::Y, Shape::disc, 480, 146, 17, 17, "triangle", IM_COL32(64, 226, 170, 255));
        put(button::X, Shape::disc, 446, 180, 17, 17, "square", IM_COL32(255, 140, 204, 255));
        put(button::B, Shape::disc, 514, 180, 17, 17, "circle", IM_COL32(255, 108, 108, 255));
        put(button::A, Shape::disc, 480, 214, 17, 17, "cross", IM_COL32(124, 178, 255, 255));
    } else {
        shoulder(button::LT, 168, 82, -0.15f, 80, 60, 0.2f, "LT");
        shoulder(button::RT, 472, 82, -0.15f, 80, 60, 0.2f, "RT");
        shoulder(button::LB, 176, 99, -0.045f, 120, 28, 0.5f, "LB");
        shoulder(button::RB, 464, 99, -0.045f, 120, 28, 0.5f, "RB");
        put(button::View, Shape::rect, 283, 158, 12, 9, "View");
        put(button::Menu, Shape::rect, 357, 158, 12, 9, "Menu");
        std::vector<V3> guide;
        for (int i = 0; i < 16; ++i) {
            const float a = i * 6.2831853f / 16.0f;
            guide.push_back(pad_point(320 + 17 * std::cos(a), 122 + 17 * std::sin(a), 0.02f));
        }
        loop(m, guide);
        put(button::L3, Shape::stick, 200, 170, 27, 27, "L3");
        put(button::dpad, Shape::cross, 262, 244, 13, 13, "");
        put(button::R3, Shape::stick, 388, 244, 25, 25, "R3");
        put(button::Y, Shape::disc, 456, 134, 17, 17, "Y", IM_COL32(255, 212, 92, 255));
        put(button::X, Shape::disc, 422, 168, 17, 17, "X", IM_COL32(111, 178, 255, 255));
        put(button::B, Shape::disc, 490, 168, 17, 17, "B", IM_COL32(255, 122, 107, 255));
        put(button::A, Shape::disc, 456, 202, 17, 17, "A", IM_COL32(139, 212, 110, 255));
    }
    for (const auto& part : m.parts) {
        if (part.shape != Shape::stick && part.shape != Shape::disc) continue;
        for (int i = 0; i < 10; ++i) {
            const float a = i * 6.2831853f / 10.0f;
            m.splats.push_back({part.c + part.u * (std::cos(a) * part.ru * 1.4f) + part.v * (std::sin(a) * part.rv * 1.4f), 0.8f});
        }
    }
    m.pitch = 0.5f;
    centre(m);
    return m;
}

// A pair of VR controllers held up face-on, framed like the pads: a rounded head with the face buttons and the
// stick (buttons on the outer side, as on Touch controllers), the trigger peeking over its front edge the way
// LT / RT do on the pad, a handle running down and back with the grip as a tab on its inner side. Each is
// turned a little outward, as hands hold them.
inline Model build_vr() {
    Model m;
    for (const float s : {-1.0f, 1.0f}) {
        // Built around the head's centre (x right, y toward the controller's front, z out of the face), then placed.
        const float turn = s * 0.22f, ct = std::cos(turn), st = std::sin(turn);
        const auto dir = [&](V3 d) { return V3{d.x * ct + d.z * st, d.y, -d.x * st + d.z * ct}; };
        const auto at = [&](V3 p) { return dir(p) + V3{s * 0.36f, 0.0f, 0.0f}; };
        const V3 u{1, 0, 0}, v{0, 1, 0}, n{0, 0, 1};
        std::vector<std::vector<V3>> shell;
        for (const auto& [ru, rv, drop] : {std::tuple{0.165f, 0.14f, 0.0f}, std::tuple{0.2f, 0.17f, 0.05f}, std::tuple{0.16f, 0.135f, 0.11f}}) {
            std::vector<V3> ring;
            for (int i = 0; i < 22; ++i) {
                const float a = i * 6.2831853f / 22.0f;
                ring.push_back(at(u * (std::cos(a) * ru) + v * (std::sin(a) * rv) - n * drop));
            }
            loop(m, ring);
            shell.push_back(ring);
        }
        for (int i = 0; i < 22; i += 2) {
            segment(m, shell[0][i], shell[1][i]);
            segment(m, shell[1][i], shell[2][i]);
        }
        for (int i = 0; i < 22; ++i) m.splats.push_back({shell[1][i], 1.1f});
        for (int i = 0; i < 22; i += 2) m.splats.push_back({shell[0][i], 0.8f});
        for (float x = -0.14f; x <= 0.14f; x += 0.035f) {
            for (float y = -0.12f; y <= 0.12f; y += 0.035f) {
                if ((x * x) / (0.16f * 0.16f) + (y * y) / (0.135f * 0.135f) < 1.0f) m.splats.push_back({at(V3{x, y, 0.004f}), 0.7f});
            }
        }
        // The handle, from under the head down and back toward the wrist.
        const V3 down = normalize(V3{0.0f, -1.0f, -0.55f});
        const V3 ring_u{1, 0, 0}, ring_v = normalize(cross(down, ring_u));
        const V3 handle_top{0.0f, -0.07f, -0.1f};
        std::vector<std::vector<V3>> rings;
        for (int k = 0; k < 5; ++k) {
            const float t = k / 4.0f, radius = 0.075f - t * 0.014f;
            std::vector<V3> section;
            for (int i = 0; i < 14; ++i) {
                const float a = i * 6.2831853f / 14.0f;
                section.push_back(at(handle_top + down * (t * 0.4f) + ring_u * (std::cos(a) * radius) + ring_v * (std::sin(a) * radius * 0.9f)));
            }
            loop(m, section);
            for (int i = 0; i < 14; i += 2) m.splats.push_back({section[i], 0.8f});
            rings.push_back(section);
        }
        for (int i = 0; i < 14; i += 2) {
            for (size_t k = 0; k + 1 < rings.size(); ++k) segment(m, rings[k][i], rings[k + 1][i]);
        }
        const auto part_at = [&](std::uint32_t control, Shape shape, V3 c, V3 pu, V3 pv, V3 pn, float ru, float rv, const char* label) {
            Part part;
            part.control = control;
            part.shape = shape;
            part.label = label;
            part.c = at(c);
            part.u = dir(pu);
            part.v = dir(pv);
            part.n = dir(pn);
            part.ru = ru;
            part.rv = rv;
            m.parts.push_back(part);
            return &m.parts.back();
        };
        const auto face = [&](std::uint32_t control, Shape shape, float x, float y, float r, const char* label) {
            part_at(control, shape, u * (x * s) + v * y, u, v, n, r, r, label);
        };
        if (s > 0) {
            face(button::A, Shape::disc, 0.075f, -0.05f, 0.032f, "A");
            face(button::B, Shape::disc, 0.1f, 0.045f, 0.032f, "B");
            face(button::R3, Shape::stick, -0.06f, 0.03f, 0.048f, "");
        } else {
            face(button::X, Shape::disc, 0.075f, -0.05f, 0.032f, "X");
            face(button::Y, Shape::disc, 0.1f, 0.045f, 0.032f, "Y");
            face(button::L3, Shape::stick, -0.06f, 0.03f, 0.048f, "");
            face(button::Menu, Shape::disc, -0.045f, -0.085f, 0.02f, "");
        }
        const V3 trigger_n = normalize(V3{0.0f, 1.0f, 0.25f});
        part_at(s > 0 ? button::RT : button::LT, Shape::block, V3{0.0f, 0.13f, -0.13f}, u, normalize(cross(trigger_n, u)), trigger_n,
            0.06f, 0.045f, "")->depth = 0.045f;
        const V3 inner{-s, 0.0f, 0.0f};
        part_at(s > 0 ? button::RB : button::LB, Shape::block, handle_top + down * 0.16f + inner * 0.06f, down,
            normalize(cross(inner, down)), inner, 0.1f, 0.035f, "")->depth = 0.03f;
    }
    m.pitch = 0.45f;
    centre(m);
    return m;
}

// A compact keyboard (main block, the Insert cluster, arrows) lying on a desk, seen from the seat.
inline Model build_keyboard() {
    Model m;
    const float unit = 0.1f, gap = 0.009f;
    const auto key = [&](std::uint16_t scan, const char* name, float col, float row, float width = 1.0f) {
        Part part;
        part.scan = scan;
        part.shape = Shape::rect;
        part.label = name;
        part.c = {(col + width * 0.5f) * unit, -(row + 0.5f) * unit, 0.0f};
        part.u = {1, 0, 0};
        part.v = {0, 1, 0};
        part.n = {0, 0, 1};
        part.ru = width * unit * 0.5f - gap;
        part.rv = unit * 0.5f - gap;
        part.depth = 0.028f;
        m.parts.push_back(part);
    };
    const auto row_of = [&](float row, float col, std::uint16_t first, const char* names) {
        for (int i = 0; names[i]; ++i) {
            const char label[2] = {names[i], 0};
            key(static_cast<std::uint16_t>(first + i), label, col + i, row);
        }
    };
    key(0x29, "`", 0, 0);
    row_of(0, 1, 0x02, "1234567890-=");
    key(0x0e, "Bksp", 13, 0, 2.0f);
    key(0x0f, "Tab", 0, 1, 1.5f);
    row_of(1, 1.5f, 0x10, "QWERTYUIOP[]");
    key(0x2b, "\\", 13.5f, 1, 1.5f);
    key(0x3a, "Caps", 0, 2, 1.75f);
    row_of(2, 1.75f, 0x1e, "ASDFGHJKL;'");
    key(0x1c, "Enter", 12.75f, 2, 2.25f);
    key(0x2a, "Shift", 0, 3, 2.25f);
    row_of(3, 2.25f, 0x2c, "ZXCVBNM,./");
    key(0x36, "Shift", 12.25f, 3, 2.75f);
    key(0x1d, "Ctrl", 0, 4, 1.25f);
    key(0x15b, "Win", 1.25f, 4, 1.25f);
    key(0x38, "Alt", 2.5f, 4, 1.25f);
    key(0x39, "", 3.75f, 4, 6.25f);
    key(0x138, "Alt", 10.0f, 4, 1.25f);
    key(0x15c, "Win", 11.25f, 4, 1.25f);
    key(0x15d, "Menu", 12.5f, 4, 1.25f);
    key(0x11d, "Ctrl", 13.75f, 4, 1.25f);
    key(0x152, "Ins", 15.5f, 0);
    key(0x147, "Home", 16.5f, 0);
    key(0x149, "PgUp", 17.5f, 0);
    key(0x153, "Del", 15.5f, 1);
    key(0x14f, "End", 16.5f, 1);
    key(0x151, "PgDn", 17.5f, 1);
    key(0x148, "up", 16.5f, 3);
    key(0x14b, "left", 15.5f, 4);
    key(0x150, "down", 16.5f, 4);
    key(0x14d, "right", 17.5f, 4);
    // The case under the keys.
    const float l = -0.25f * unit, r = 18.75f * unit, t = 0.25f * unit, b = -5.25f * unit;
    loop(m, {{l, t, -0.01f}, {r, t, -0.01f}, {r, b, -0.01f}, {l, b, -0.01f}});
    loop(m, {{l + 0.02f, t - 0.01f, -0.06f}, {r - 0.02f, t - 0.01f, -0.06f}, {r - 0.02f, b + 0.01f, -0.06f}, {l + 0.02f, b + 0.01f, -0.06f}});
    for (float x = l; x <= r; x += 0.08f) {
        m.splats.push_back({{x, t, -0.01f}, 0.9f});
        m.splats.push_back({{x, b, -0.01f}, 0.9f});
    }
    for (const auto& part : m.parts) {
        for (const float sx : {-1.0f, 1.0f}) {
            for (const float sy : {-1.0f, 1.0f}) m.splats.push_back({part.c + V3{sx * part.ru, sy * part.rv, 0.0f}, 0.5f});
        }
    }
    m.pitch = -0.8f;
    centre(m);
    return m;
}

inline const Model& model(Device device) {
    static const Model xbox = build_pad(false), sony = build_pad(true), vr = build_vr(), keys = build_keyboard();
    switch (device) {
    case Device::playstation: return sony;
    case Device::vr: return vr;
    case Device::keyboard: return keys;
    default: return xbox;
    }
}

// ---- Drawing ----

struct Look {
    ImU32 line{IM_COL32(214, 175, 91, 255)};   // gold, the brand's
    ImU32 deep{IM_COL32(118, 152, 214, 255)};  // the side turned away shades toward night blue
    ImU32 lit{IM_COL32(255, 220, 140, 255)};
    ImU32 text{IM_COL32(249, 232, 180, 255)};
    ImU32 dark{IM_COL32(26, 18, 6, 255)};
    float sway{0.32f};        // radians the model turns either way
    float sway_seconds{9.0f};
    bool labels{true};        // names on unlit controls when there is room
    float fill{0.94f};        // share of the box the model fills
};

struct Projector {
    ImVec2 centre;
    float scale{1.0f}, cy{1.0f}, sy{}, cp{1.0f}, sp{}, distance{3.4f};
    void turn(float yaw, float pitch) {
        cy = std::cos(yaw); sy = std::sin(yaw); cp = std::cos(pitch); sp = std::sin(pitch);
    }
    V3 view(V3 p) const {
        const float x = p.x * cy + p.z * sy, z1 = -p.x * sy + p.z * cy;
        return {x, p.y * cp - z1 * sp, p.y * sp + z1 * cp};
    }
    ImVec2 screen(V3 v) const {
        const float f = distance / (distance - v.z);
        return {centre.x + v.x * f * scale, centre.y - v.y * f * scale};
    }
    ImVec2 at(V3 p) const { return screen(view(p)); }
};

inline ImU32 mix(ImU32 a, ImU32 b, float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [&](int shift) {
        const float x = ((a >> shift) & 0xFF) * (1.0f - t) + ((b >> shift) & 0xFF) * t;
        return static_cast<ImU32>(std::clamp(x, 0.0f, 255.0f)) << shift;
    };
    const auto al = static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) | al << IM_COL32_A_SHIFT;
}
inline ImU32 fade(ImU32 color, float alpha) { return mix(color, color, 0.0f, alpha * ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f); }

// Pad masks as the device's model shows them: on VR controllers View is the left grip + Menu, no D-pad.
inline std::uint32_t on_model(std::uint32_t mask, Device device) {
    if (device != Device::vr) return mask;
    if (mask & button::View) mask = (mask & ~button::View) | button::LB | button::Menu;
    return mask & ~button::dpad;
}

// What a part shows now: lit, how far it is pressed in (0..1), whether a ripple spreads from it.
struct PartState { bool lit{}; float sink{}; bool ripple{}; };
inline PartState part_state(const Part& part, const Moment& now, Device device) {
    PartState state;
    if (device == Device::keyboard) {
        const auto is = [&](const std::string& key) { return !key.empty() && key_scan(key) == part.scan; };
        const bool held = is(now.key_held), down = is(now.key_down);
        state.lit = held || down;
        state.sink = down ? now.press : held ? 1.0f : 0.0f;
        state.ripple = now.ripple >= 0.0f && is(now.ripple_key);
        return state;
    }
    const auto held = on_model(now.held, device) & part.control, down = on_model(now.down, device) & part.control;
    state.lit = held || down;
    state.sink = down ? now.press : held ? 1.0f : 0.0f;
    state.ripple = now.ripple >= 0.0f && (on_model(now.ripple_mask, device) & part.control);
    return state;
}

// A PlayStation face glyph, drawn (fonts may lack the symbols).
inline void glyph(ImDrawList* list, ImVec2 c, float r, const std::string& name, ImU32 color, float thick) {
    if (name == "triangle") {
        list->AddTriangle({c.x, c.y - r}, {c.x + r * 0.87f, c.y + r * 0.5f}, {c.x - r * 0.87f, c.y + r * 0.5f}, color, thick);
    } else if (name == "circle") {
        list->AddCircle(c, r * 0.8f, color, 20, thick);
    } else if (name == "cross") {
        list->AddLine({c.x - r * 0.7f, c.y - r * 0.7f}, {c.x + r * 0.7f, c.y + r * 0.7f}, color, thick);
        list->AddLine({c.x + r * 0.7f, c.y - r * 0.7f}, {c.x - r * 0.7f, c.y + r * 0.7f}, color, thick);
    } else if (name == "square") {
        list->AddRect({c.x - r * 0.65f, c.y - r * 0.65f}, {c.x + r * 0.65f, c.y + r * 0.65f}, color, 0.0f, 0, thick);
    } else if (name == "up" || name == "down" || name == "left" || name == "right") {
        const float dx = name == "left" ? -1.0f : name == "right" ? 1.0f : 0.0f, dy = name == "up" ? -1.0f : name == "down" ? 1.0f : 0.0f;
        const ImVec2 tip{c.x + dx * r * 0.6f, c.y + dy * r * 0.6f};
        const ImVec2 a{c.x - dx * r * 0.4f - dy * r * 0.55f, c.y - dy * r * 0.4f - dx * r * 0.55f};
        const ImVec2 b{c.x - dx * r * 0.4f + dy * r * 0.55f, c.y - dy * r * 0.4f + dx * r * 0.55f};
        list->AddTriangleFilled(tip, a, b, color);
    }
}
inline bool is_glyph(const std::string& name) {
    return name == "triangle" || name == "circle" || name == "cross" || name == "square" || name == "up" || name == "down" ||
        name == "left" || name == "right";
}

// Where a control was drawn, for leader lines on the cards; radius: pixels to its rim (a long part's short side).
struct Anchor { std::uint32_t control{}; std::uint16_t scan{}; ImVec2 at; float radius{}; };

// Fits the model, turned between -sway and +sway, into the box; returns the projector at `yaw`.
inline Projector fit_model(const Model& m, ImVec2 min, ImVec2 max, float yaw, float pitch, const Look& look) {
    Projector probe;
    float lo_x = FLT_MAX, lo_y = FLT_MAX, hi_x = -FLT_MAX, hi_y = -FLT_MAX;
    for (const float sample : {-look.sway, 0.0f, look.sway}) {
        probe.turn(sample, m.pitch);
        const auto grow = [&](V3 p, float r) {
            const ImVec2 s = probe.at(p);
            lo_x = (std::min)(lo_x, s.x - r); hi_x = (std::max)(hi_x, s.x + r);
            lo_y = (std::min)(lo_y, s.y - r); hi_y = (std::max)(hi_y, s.y + r);
        };
        for (const auto& p : m.points) grow(p, 0.0f);
        for (const auto& part : m.parts) grow(part.c, (std::max)(part.ru, part.rv));
    }
    const float w = max.x - min.x, h = max.y - min.y;
    Projector pr;
    pr.scale = (std::min)(w * look.fill / (std::max)(hi_x - lo_x, 0.01f), h * look.fill / (std::max)(hi_y - lo_y, 0.01f));
    pr.centre = {min.x + w * 0.5f - (lo_x + hi_x) * 0.5f * pr.scale, min.y + h * 0.5f - (lo_y + hi_y) * 0.5f * pr.scale};
    pr.turn(yaw, pitch);
    return pr;
}

// Draws the device showing `now` in [min, max]. `time` drives the slow turn and the shimmer.
inline void draw_moment(ImDrawList* list, ImFont* font, ImVec2 min, ImVec2 max, Device device, const Moment& now, double time,
    const Look& look = {}, std::vector<Anchor>* anchors = nullptr) {
    const float w = max.x - min.x, h = max.y - min.y;
    if (!list || w <= 4.0f || h <= 4.0f) return;
    const Model& m = model(device);
    const float yaw = look.sway * static_cast<float>(std::sin(time * 6.2831853 / look.sway_seconds));
    const float pitch = m.pitch + 0.04f * static_cast<float>(std::sin(time * 6.2831853 / (look.sway_seconds * 1.7)));
    const Projector pr = fit_model(m, min, max, yaw, pitch, look);
    ImFont* label_font = font ? font : ImGui::GetFont();

    float zlo = FLT_MAX, zhi = -FLT_MAX;
    std::vector<V3> viewed(m.points.size());
    for (size_t i = 0; i < m.points.size(); ++i) {
        viewed[i] = pr.view(m.points[i]);
        zlo = (std::min)(zlo, viewed[i].z);
        zhi = (std::max)(zhi, viewed[i].z);
    }
    const float zspan = (std::max)(zhi - zlo, 0.01f);
    const auto closeness = [&](float z) { return (z - zlo) / zspan; };

    // The light field: a wide faint halo, a mid glow and a bright core per point, twinkling slowly, a soft band
    // of light sweeping across now and then; points on the far side bluer and dimmer.
    const float dot_px = (std::max)(0.8f, pr.scale * 0.0075f);
    const float sweep = static_cast<float>(std::fmod(time / 5.0, 1.0)) * 3.0f - 1.5f;
    for (size_t i = 0; i < m.splats.size(); ++i) {
        const auto& [p, size] = m.splats[i];
        const V3 v = pr.view(p);
        const float close = closeness(v.z);
        const float twinkle = 0.78f + 0.22f * static_cast<float>(std::sin(time * 1.7 + i * 2.39));
        const float band = std::exp(-(v.x - sweep) * (v.x - sweep) / 0.02f);
        const float bright = twinkle * (1.0f + 0.9f * band);
        const ImVec2 s = pr.screen(v);
        list->AddCircleFilled(s, dot_px * 3.0f * size, mix(look.deep, look.line, close, 0.045f * bright), 10);
        list->AddCircleFilled(s, dot_px * 1.6f * size, mix(look.deep, look.line, close, 0.12f * bright), 8);
        list->AddCircleFilled(s, dot_px * 0.7f * size, mix(look.deep, look.lit, close, (0.3f + 0.5f * close) * bright), 6);
    }
    const float thick = (std::max)(1.0f, pr.scale * 0.004f);
    for (const auto& [a, b] : m.edges) {
        const float close = closeness((viewed[a].z + viewed[b].z) * 0.5f);
        list->AddLine(pr.screen(viewed[a]), pr.screen(viewed[b]), mix(look.deep, look.line, close, 0.18f + 0.5f * close), thick);
    }

    // Controls, the far ones first.
    std::vector<int> order(m.parts.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return pr.view(m.parts[a].c).z < pr.view(m.parts[b].c).z; });
    for (const int index : order) {
        const Part& part = m.parts[index];
        const PartState state = part_state(part, now, device);
        const float close = closeness(pr.view(part.c).z);
        const float facing = pr.view(part.n).z;              // > 0 turned toward the viewer
        const float shown = facing > 0.0f ? 1.0f : 0.45f;    // the hidden side of the model, dimmer
        const V3 top = part.c + part.n * (part.depth * (1.0f - 0.8f * state.sink));
        const ImU32 edge = state.lit ? look.lit : mix(look.deep, look.line, close, (0.3f + 0.6f * close) * shown);
        const ImU32 fill = state.lit ? fade(look.lit, 0.95f) : IM_COL32(14, 12, 9, static_cast<int>(210 * shown));
        const float line_w = state.lit ? thick * 1.8f : thick;
        const auto ring = [&](V3 centre_p, float ru, float rv, int count) {
            std::vector<ImVec2> pts;
            for (int i = 0; i < count; ++i) {
                const float a = i * 6.2831853f / count;
                pts.push_back(pr.at(centre_p + part.u * (std::cos(a) * ru) + part.v * (std::sin(a) * rv)));
            }
            return pts;
        };
        const auto quad = [&](V3 centre_p, float ru, float rv) {
            std::vector<ImVec2> pts;
            for (const auto& [a, b] : {std::pair{-1.0f, -1.0f}, std::pair{1.0f, -1.0f}, std::pair{1.0f, 1.0f}, std::pair{-1.0f, 1.0f}})
                pts.push_back(pr.at(centre_p + part.u * (a * ru) + part.v * (b * rv)));
            return pts;
        };
        const auto fill_poly = [&](const std::vector<ImVec2>& pts, ImU32 color) {
            list->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), color);
        };
        const auto outline = [&](const std::vector<ImVec2>& pts, ImU32 color, float width) {
            list->AddPolyline(pts.data(), static_cast<int>(pts.size()), color, ImDrawFlags_Closed, width);
        };
        const float r_px = (std::max)(part.ru, part.rv) * pr.scale;
        // Long parts (triggers, bumpers, grips): their short side, so halos and card leaders hug them.
        const bool long_part = part.shape == Shape::rect || part.shape == Shape::block;
        const float near_px = long_part ? (std::min)(part.ru, part.rv) * pr.scale : r_px;
        ImVec2 label_at = pr.at(top);
        if (part.shape == Shape::stick) label_at = pr.at(part.c + part.n * (0.07f - 0.035f * state.sink));
        if (state.lit) {
            list->AddCircleFilled(label_at, near_px * 1.9f, fade(look.lit, 0.06f), 28);
            list->AddCircleFilled(label_at, near_px * 1.4f, fade(look.lit, 0.09f), 24);
        }
        switch (part.shape) {
        case Shape::stick: {
            outline(ring(part.c, part.ru * 1.45f, part.rv * 1.45f, 22), mix(look.deep, look.line, close, (0.3f + 0.4f * close) * shown), thick);
            const V3 cap = part.c + part.n * (0.07f - 0.035f * state.sink);
            const auto cap_ring = ring(cap, part.ru, part.rv, 20), stem = ring(part.c + part.n * 0.01f, part.ru * 0.7f, part.rv * 0.7f, 20);
            for (int i = 0; i < 20; i += 5) list->AddLine(cap_ring[i], stem[i], edge, thick);
            fill_poly(cap_ring, fill);
            outline(cap_ring, edge, line_w);
            outline(ring(cap, part.ru * 0.62f, part.rv * 0.62f, 16), state.lit ? fade(look.dark, 0.5f) : fade(edge, 0.5f), thick);
            break;
        }
        case Shape::disc: {
            const auto pts = ring(top, part.ru, part.rv, 18);
            fill_poly(pts, fill);
            outline(pts, edge, line_w);
            break;
        }
        case Shape::rect: {
            const auto pts = quad(top, part.ru, part.rv);
            fill_poly(pts, fill);
            outline(pts, edge, line_w);
            break;
        }
        case Shape::block: {
            const auto cap = quad(top, part.ru, part.rv), base = quad(part.c, part.ru * 0.96f, part.rv * 0.96f);
            fill_poly(cap, fill);
            for (int i = 0; i < 4; ++i) list->AddLine(cap[i], base[i], edge, thick);
            outline(base, fade(edge, 0.6f), thick);
            outline(cap, edge, line_w);
            break;
        }
        case Shape::cross: {
            const std::uint32_t mask = on_model(now.held | now.down, device);
            for (const auto& [du, dv, bit] : {std::tuple{0.0f, 1.0f, button::Up}, std::tuple{0.0f, -1.0f, button::Down},
                     std::tuple{1.0f, 0.0f, button::Right}, std::tuple{-1.0f, 0.0f, button::Left}}) {
                const bool arm_lit = (mask & bit) != 0;
                const auto pts = quad(top + part.u * (du * part.ru * 1.25f) + part.v * (dv * part.rv * 1.25f), part.ru * 0.6f, part.rv * 0.6f);
                fill_poly(pts, arm_lit ? fade(look.lit, 0.95f) : IM_COL32(14, 12, 9, static_cast<int>(210 * shown)));
                outline(pts, arm_lit ? look.lit : edge, arm_lit ? line_w : thick);
            }
            break;
        }
        }
        if (state.ripple) {
            const float k = now.ripple;
            list->AddCircle(label_at, near_px * (1.15f + 1.5f * k), fade(look.lit, 0.85f * (1.0f - k)), 32, thick * (2.0f - k));
        }
        const bool pressing = device == Device::keyboard ? !now.key_down.empty() && key_scan(now.key_down) == part.scan
                                                         : (on_model(now.down, device) & part.control) != 0;
        if (state.lit && now.progress >= 0.0f && pressing) {
            const float a0 = -1.5707963f, a1 = a0 + 6.2831853f * now.progress;
            list->PathArcTo(label_at, r_px * 1.7f, a0, a1, 40);
            list->PathStroke(look.lit, 0, thick * 2.2f);
        }
        // A long part's short side runs into the depth and foreshortens: about half of it shows.
        if (anchors) anchors->push_back({part.control, part.scan, label_at, long_part ? near_px * 0.45f : near_px});

        // The name on it: always when lit, otherwise when there is room.
        std::string text = part.label;
        if (device == Device::keyboard) {
            if (part.scan == 0x29) text = vr_key_label;
            else if (part.scan < printed.size() && !printed[part.scan].empty()) text = printed[part.scan];
            else if (part.scan == 0x152) text = wuwa_l10n::text("Insert");
        }
        if (text.empty()) continue;
        const float px = std::clamp((std::min)(part.ru, part.rv) * pr.scale * 1.15f, 6.0f, 44.0f); // fits the height
        if (!state.lit && (!look.labels || px < 10.0f || facing < 0.15f)) continue;
        const ImU32 color = state.lit ? look.dark : part.tint ? fade(part.tint, shown) : fade(look.text, shown);
        if (is_glyph(text)) {
            glyph(list, label_at, r_px * 0.55f, text, color, (std::max)(1.0f, r_px * 0.12f));
            continue;
        }
        if (!state.lit && (text == "View" || text == "Menu" || text == "Create" || text == "Options" || text.size() > 4) && px < 16.0f) continue;
        if (text == "View" || text == "Menu" || text == "Create" || text == "Options") text = wuwa_l10n::text(text);
        float size = px;
        const float room = (part.shape == Shape::rect || part.shape == Shape::block) ? part.ru * 2.0f * pr.scale * 0.86f : r_px * 1.6f;
        const float tw = label_font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
        if (tw > room && tw > 0.0f) size *= room / tw;
        if (size < 7.0f) continue;
        const ImVec2 ts = label_font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
        list->AddText(label_font, size, {label_at.x - ts.x * 0.5f, label_at.y - ts.y * 0.5f}, color, text.c_str());
    }
}

// Plays `beats` (looping) on the device in [min, max]; returns the moment shown, whose beat drives the captions.
inline Moment draw(ImDrawList* list, ImFont* font, ImVec2 min, ImVec2 max, Device device, const Beats& beats, double time,
    const Look& look = {}, std::vector<Anchor>* anchors = nullptr) {
    const Moment now = moment(beats, time);
    draw_moment(list, font, min, max, device, now, time, look, anchors);
    return now;
}

// ---- Captions ----

// The name of a pad control on a device, for captions (English; translate with wuwa_l10n::text).
inline std::string control_name(Device device, std::uint32_t control) {
    struct Name { std::uint32_t c; const char* xbox; const char* ps; const char* vr; };
    static constexpr Name names[] = {
        {button::A, "A", "cross", "A"}, {button::B, "B", "circle", "B"}, {button::X, "X", "square", "X"},
        {button::Y, "Y", "triangle", "Y"}, {button::LB, "LB", "L1", N_("L grip")}, {button::RB, "RB", "R1", N_("R grip")},
        {button::LT, "LT", "L2", N_("L trigger")}, {button::RT, "RT", "R2", N_("R trigger")},
        {button::L3, "L3", "L3", N_("L stick")}, {button::R3, "R3", "R3", N_("R stick")},
        {button::View, N_("View"), N_("Create"), ""}, {button::Menu, N_("Menu"), N_("Options"), N_("Menu")},
        {button::Up, N_("D-pad up"), N_("D-pad up"), ""}, {button::Down, N_("D-pad down"), N_("D-pad down"), ""},
        {button::Left, N_("D-pad left"), N_("D-pad left"), ""}, {button::Right, N_("D-pad right"), N_("D-pad right"), ""}};
    for (const auto& name : names) {
        if (name.c == control) return device == Device::playstation ? name.ps : device == Device::vr ? name.vr : name.xbox;
    }
    return {};
}

// "Hold {1}", "then click {1} twice"... translated whole; {1} is where the key caps go.
inline const char* beat_template(const Beat& beat, bool first, Device device) {
    const bool has_press = beat.press || !beat.key_press.empty();
    if (!has_press) return first ? N_("Hold {1}") : N_("then hold {1}");
    const bool stick = device != Device::keyboard && (beat.press & (button::L3 | button::R3)) != 0;
    if (beat.long_press > 0.0f) {
        if (!first) return N_("then hold {1}");
        return beat.long_press < 0.9f ? N_("Hold {1} for half a second") : N_("Hold {1} for a second");
    }
    if (beat.taps >= 2) {
        if (stick) return first ? N_("Click {1} twice") : N_("then click {1} twice");
        return first ? N_("Press {1} twice") : N_("then press {1} twice");
    }
    if (stick) return first ? N_("Click {1}") : N_("then click {1}");
    return first ? N_("Press {1}") : N_("then press {1}");
}

// The key caps of a beat, as shown (translated where they are words; glyph names for PlayStation shapes).
inline std::vector<std::string> beat_keys(const Beat& beat, Device device) {
    std::vector<std::string> keys;
    if (device == Device::keyboard) {
        const auto& key = beat.key_press.empty() ? beat.key_hold : beat.key_press;
        if (!key.empty()) keys.push_back(key_caption(key));
        return keys;
    }
    const std::uint32_t mask = on_model(beat.press ? beat.press : beat.hold, device);
    static constexpr std::uint32_t order[] = {button::LT, button::RT, button::LB, button::RB, button::L3, button::R3,
        button::View, button::Menu, button::Up, button::Down, button::Left, button::Right, button::A, button::B, button::X, button::Y};
    for (const auto control : order) {
        if (!(mask & control)) continue;
        const auto name = control_name(device, control);
        if (name.empty()) continue;
        const bool word = name.size() > 2 && !is_glyph(name);
        keys.push_back(word ? wuwa_l10n::text(name) : name);
    }
    return keys;
}

// A key cap; returns its width. PlayStation face buttons draw their glyph.
inline float cap(ImDrawList* list, ImFont* font, ImVec2 at, float px, const std::string& name, bool bright, bool measure_only = false) {
    const bool shape = is_glyph(name);
    const float text_w = shape ? px * 0.9f : font->CalcTextSizeA(px, FLT_MAX, 0.0f, name.c_str()).x;
    const float w = (std::max)(text_w + px * 0.75f, px * 1.55f), h = px * 1.45f;
    if (measure_only) return w;
    const ImU32 line = bright ? IM_COL32(242, 206, 120, 255) : IM_COL32(214, 178, 98, 110);
    const ImU32 ink = bright ? IM_COL32(252, 238, 196, 255) : IM_COL32(170, 152, 112, 255);
    list->AddRectFilled(at, {at.x + w, at.y + h}, bright ? IM_COL32(48, 38, 18, 255) : IM_COL32(27, 24, 18, 255), px * 0.28f);
    list->AddRect(at, {at.x + w, at.y + h}, line, px * 0.28f, 0, (std::max)(1.0f, px * 0.07f));
    if (shape) glyph(list, {at.x + w * 0.5f, at.y + h * 0.5f}, px * 0.42f, name, ink, (std::max)(1.0f, px * 0.09f));
    else list->AddText(font, px, {at.x + (w - text_w) * 0.5f, at.y + (h - px) * 0.5f}, ink, name.c_str());
    return w;
}

// Just the key caps, compact, from the left: "[L3] + [B]", "[R3] ×2", "[Menu] 1 s" (tiles and the cards).
inline void draw_keys(ImDrawList* list, ImFont* font, ImVec2 at, float px, float max_width, Device device, const Beats& beats) {
    if (!list || !font || beats.empty()) return;
    struct Piece { std::string text; bool key{}; };
    std::vector<Piece> pieces;
    for (size_t i = 0; i < beats.size(); ++i) {
        if (i > 0) pieces.push_back({"+"});
        const auto keys = beat_keys(beats[i], device);
        for (size_t k = 0; k < keys.size(); ++k) {
            if (k > 0) pieces.push_back({"+"});
            pieces.push_back({keys[k], true});
        }
        if (beats[i].taps >= 2) pieces.push_back({"\xC3\x97" + std::to_string(beats[i].taps)}); // ×2
        if (beats[i].long_press > 0.0f) pieces.push_back({beats[i].long_press < 0.9f ? "0.5 s" : "1 s"});
    }
    if (wuwa_l10n::right_to_left()) std::reverse(pieces.begin(), pieces.end());
    const auto width = [&](float size) {
        float total = 0.0f;
        for (const auto& piece : pieces) {
            total += piece.key ? cap(list, font, {}, size, piece.text, true, true) : font->CalcTextSizeA(size, FLT_MAX, 0.0f, piece.text.c_str()).x;
            total += size * 0.3f;
        }
        return total;
    };
    float size = px;
    if (const float total = width(size); total > max_width && total > 0.0f) size *= max_width / total;
    float x = at.x;
    for (const auto& piece : pieces) {
        if (piece.key) {
            x += cap(list, font, {x, at.y}, size, piece.text, true);
        } else {
            list->AddText(font, size, {x, at.y + size * 0.2f}, IM_COL32(214, 175, 91, 220), piece.text.c_str());
            x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, piece.text.c_str()).x;
        }
        x += size * 0.3f;
    }
}

// The steps on one line, the one playing bright: "Hold [LT]+[RT]  ›  then click [R3]". Centred on at.x (or
// starting there), shrinking to max_width. Returns the line's height.
inline float draw_captions(ImDrawList* list, ImFont* font, ImVec2 at, float px, float max_width, Device device, const Beats& beats,
    int playing, bool centred = true) {
    if (!list || !font || beats.empty()) return 0.0f;
    struct Piece { std::string text; bool key{}; bool bright{}; bool chevron{}; };
    std::vector<Piece> pieces;
    const bool rtl = wuwa_l10n::right_to_left();
    for (int i = 0; i < static_cast<int>(beats.size()); ++i) {
        const bool hold_only = !beats[i].press && beats[i].key_press.empty();
        const bool bright = playing < 0 || i == playing || (i < playing && hold_only);
        std::vector<Piece> step;
        if (i > 0) step.push_back({"", false, false, true});
        const auto logical = wuwa_l10n::translate(beat_template(beats[i], i == 0, device));
        const auto keys = beat_keys(beats[i], device);
        const size_t slot = logical.find("{1}");
        std::vector<Piece> body;
        if (slot == std::string::npos) {
            body.push_back({wuwa_l10n::visual(logical), false, bright});
        } else {
            if (slot > 0) body.push_back({wuwa_l10n::visual(logical.substr(0, slot)), false, bright});
            std::vector<Piece> caps;
            for (size_t k = 0; k < keys.size(); ++k) {
                if (k > 0) caps.push_back({"+", false, bright});
                caps.push_back({keys[k], true, bright});
            }
            if (rtl) std::reverse(caps.begin(), caps.end());
            body.insert(body.end(), caps.begin(), caps.end());
            if (slot + 3 < logical.size()) body.push_back({wuwa_l10n::visual(logical.substr(slot + 3)), false, bright});
        }
        if (rtl) std::reverse(body.begin(), body.end());
        step.insert(step.end(), body.begin(), body.end());
        pieces.insert(pieces.end(), step.begin(), step.end());
    }
    if (rtl) std::reverse(pieces.begin(), pieces.end());
    const auto width = [&](float size) {
        float total = 0.0f;
        for (const auto& piece : pieces) {
            total += piece.chevron ? size * 1.3f
                : piece.key ? cap(list, font, {}, size * 0.8f, piece.text, piece.bright, true) + size * 0.24f
                            : font->CalcTextSizeA(size, FLT_MAX, 0.0f, piece.text.c_str()).x;
        }
        return total;
    };
    float size = px, total = width(size);
    if (total > max_width && total > 0.0f) {
        size *= max_width / total;
        total = width(size);
    }
    float x = centred ? at.x - total * 0.5f : at.x;
    const float cap_px = size * 0.8f, cap_h = cap_px * 1.45f;
    const float line_h = (std::max)(size, cap_h);
    for (const auto& piece : pieces) {
        if (piece.chevron) {
            const float cx = x + size * 0.65f, cy = at.y + line_h * 0.5f, r = size * 0.2f, d = rtl ? -1.0f : 1.0f;
            const ImVec2 arrow[3] = {{cx - d * r * 0.6f, cy - r}, {cx + d * r * 0.6f, cy}, {cx - d * r * 0.6f, cy + r}};
            list->AddPolyline(arrow, 3, IM_COL32(214, 175, 91, 200), 0, (std::max)(1.0f, size * 0.07f));
            x += size * 1.3f;
        } else if (piece.key) {
            x += size * 0.12f;
            x += cap(list, font, {x, at.y + (line_h - cap_h) * 0.5f}, cap_px, piece.text, piece.bright) + size * 0.12f;
        } else {
            const ImU32 color = piece.bright ? IM_COL32(247, 241, 227, 255) : IM_COL32(150, 140, 120, 255);
            list->AddText(font, size, {x, at.y + (line_h - size) * 0.5f}, color, piece.text.c_str());
            x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, piece.text.c_str()).x;
        }
    }
    return line_h;
}
}
