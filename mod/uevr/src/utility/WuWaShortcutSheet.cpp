#define NOMINMAX
#include "WuWaShortcutSheet.hpp"
#include "WuWaLocalization.hpp"
#include "uevr-imgui/font_robotomedium.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wuwa_sheet {
namespace {
ImFontAtlas* owner{};
ImFont* fallback{};

// The site's palette (site/style.css), so the cards match the website's card.
constexpr ImU32 card_bg = IM_COL32(13, 12, 10, 248);
constexpr ImU32 frame_line = IM_COL32(214, 178, 98, 80);
constexpr ImU32 row_bg = IM_COL32(10, 9, 7, 225);
constexpr ImU32 row_line = IM_COL32(214, 178, 98, 52);
constexpr ImU32 row_lit_bg = IM_COL32(48, 38, 18, 250);
constexpr ImU32 lit_line = IM_COL32(249, 232, 180, 255);
constexpr ImU32 accent = IM_COL32(220, 185, 103, 255);
constexpr ImU32 gold_hi = IM_COL32(249, 232, 180, 255);
constexpr ImU32 ink = IM_COL32(247, 241, 227, 255);
constexpr ImU32 muted = IM_COL32(191, 182, 163, 255);
constexpr ImU32 key_bg = IM_COL32(27, 24, 18, 255);
constexpr ImU32 key_line = IM_COL32(214, 178, 98, 140);
constexpr ImU32 leader = IM_COL32(214, 178, 98, 140);
constexpr ImU32 leader_lit = IM_COL32(249, 232, 180, 230);
constexpr ImU32 gold = IM_COL32(214, 175, 91, 255);
constexpr ImU32 gold_dim = IM_COL32(214, 175, 91, 64);
constexpr ImU32 gold_thin = IM_COL32(214, 175, 91, 178);
constexpr ImU32 pad_fill = IM_COL32(19, 17, 13, 255);
constexpr ImU32 pad_line = IM_COL32(214, 178, 98, 105);
constexpr ImU32 button_fill = IM_COL32(31, 27, 20, 255);
constexpr ImU32 button_line = IM_COL32(214, 178, 98, 128);
constexpr ImU32 well_fill = IM_COL32(11, 10, 8, 255);
constexpr ImU32 well_line = IM_COL32(214, 178, 98, 72);
constexpr ImU32 lit_text = IM_COL32(26, 18, 6, 255);
constexpr ImU32 green = IM_COL32(139, 212, 110, 255);
constexpr ImU32 red = IM_COL32(255, 122, 107, 255);
constexpr ImU32 blue = IM_COL32(111, 178, 255, 255);
constexpr ImU32 yellow = IM_COL32(255, 212, 92, 255);

// XInput buttons.
constexpr unsigned dpad_down = 0x0002, menu = 0x0010, view = 0x0020, l3 = 0x0040, r3 = 0x0080,
    lb = 0x0100, rb = 0x0200, button_a = 0x1000, button_b = 0x2000, button_x = 0x4000, button_y = 0x8000;

std::string tr(const std::string& english) { return wuwa_l10n::text(english); }

// A card in 1600 x 900 design units, centred in the target.
struct Canvas {
    ImDrawList* list{};
    ImFont* font{};
    float s{1.0f};
    ImVec2 origin{};
    bool rtl{}, vr{};
    ImVec2 at(float x, float y) const { return {origin.x + x * s, origin.y + y * s}; }
    float width(float px, const std::string& text) const { return font->CalcTextSizeA(px * s, FLT_MAX, 0.0f, text.c_str()).x / s; }
    void text(float x, float y, float px, ImU32 color, const std::string& visual) const {
        list->AddText(font, px * s, at(x, y), color, visual.c_str());
    }
};

Canvas canvas(ImDrawList* list, ImVec2 size, ImFont* font, bool vr = false) {
    Canvas c;
    c.list = list;
    c.font = font;
    if (owner == ImGui::GetIO().Fonts && fallback) c.font = fallback;
    if (auto* localized = wuwa_l10n::sheet_font()) c.font = localized;
    c.s = (std::min)(size.x / 1600.0f, size.y / 900.0f);
    c.origin = {(size.x - 1600.0f * c.s) * 0.5f, (size.y - 900.0f * c.s) * 0.5f};
    c.rtl = wuwa_l10n::right_to_left();
    c.vr = vr;
    return c;
}

// What a {token} names, as printed on its key cap. VR controllers: UEVR maps the
// triggers to LT / RT, the grips to LB / RB, the stick clicks to L3 / R3, the
// left controller's lower button to B and the right's upper button to X.
std::string key_name(const std::string& token, bool vr) {
    if (vr) {
        if (token == "LT") return tr("L trigger");
        if (token == "RT") return tr("R trigger");
        if (token == "LB") return tr("L grip");
        if (token == "RB") return tr("R grip");
        if (token == "L3") return tr("L stick");
        if (token == "R3") return tr("R stick");
        if (token == "B") return "X";
        if (token == "X") return "B";
    }
    if (token == "Down") return tr("D-pad");
    if (token == "View" || token == "Menu") return tr(token);
    return token;
}
ImU32 key_color(const std::string& token, bool vr) {
    if (vr) return gold_hi;
    return token == "A" ? green : token == "B" ? red : token == "X" ? blue : token == "Y" ? yellow : gold_hi;
}

// A key cap: the name on a dark cap with a heavier lower edge, gold when lit.
float chip_width(const Canvas& c, float px, const std::string& token) {
    float w = c.width(px, key_name(token, c.vr)) + px * 0.64f;
    if (token == "Down") w += px * 0.7f;
    return (std::max)(w, px * 1.6f);
}
float chip(const Canvas& c, float x, float middle, float px, const std::string& token, bool lit = false) {
    const auto name = key_name(token, c.vr);
    const float w = chip_width(c, px, token), h = px * 1.45f, top = middle - h * 0.5f;
    c.list->AddRectFilled(c.at(x, top), c.at(x + w, top + h), lit ? gold : key_bg, 7.0f * c.s);
    c.list->AddRect(c.at(x, top), c.at(x + w, top + h), lit ? lit_line : key_line, 7.0f * c.s, 0, 1.2f * c.s);
    c.list->AddLine(c.at(x + 5.0f, top + h - 1.0f), c.at(x + w - 5.0f, top + h - 1.0f), lit ? lit_line : key_line, 2.5f * c.s);
    const ImU32 color = lit ? lit_text : key_color(token, c.vr);
    const float tx = token == "Down" ? x + px * 0.32f : x + (w - c.width(px, name)) * 0.5f;
    c.text(tx, middle - px * 0.56f, px, color, name);
    if (token == "Down") {
        const float ax = x + w - px * 0.6f;
        c.list->AddLine(c.at(ax, middle - px * 0.42f), c.at(ax, middle + px * 0.05f), color, 2.0f * c.s);
        c.list->AddTriangleFilled(c.at(ax - px * 0.24f, middle - px * 0.02f), c.at(ax + px * 0.24f, middle - px * 0.02f),
            c.at(ax, middle + px * 0.34f), color);
    }
    return w;
}

// A translated line split into text and {key} caps, in visual order.
struct Piece { bool key{}; std::string text; };
std::vector<Piece> pieces(const std::string& english, bool rtl) {
    const auto logical = wuwa_l10n::translate(english);
    std::vector<Piece> out;
    size_t at = 0;
    while (at < logical.size()) {
        const size_t open = logical.find('{', at);
        const size_t close = open == std::string::npos ? open : logical.find('}', open);
        if (close == std::string::npos) { out.push_back({false, logical.substr(at)}); break; }
        if (open > at) out.push_back({false, logical.substr(at, open - at)});
        out.push_back({true, logical.substr(open + 1, close - open - 1)});
        at = close + 1;
    }
    for (auto& piece : out) if (!piece.key) piece.text = wuwa_l10n::visual(piece.text);
    if (rtl) std::reverse(out.begin(), out.end());
    return out;
}
float line_width(const Canvas& c, const std::vector<Piece>& line, float px) {
    float w = 0.0f;
    for (const auto& piece : line) w += piece.key ? chip_width(c, px * 0.8f, piece.text) + px * 0.16f : c.width(px, piece.text);
    return w;
}
// Text with key caps; align 0 starts at x, 0.5 centres on it, 1 ends at it.
// Shrinks to max_width. Returns the width drawn.
float rich(const Canvas& c, float x, float top, float px, ImU32 color, const std::string& english, float max_width, float align = 0.0f) {
    const auto line = pieces(english, c.rtl);
    float w = line_width(c, line, px);
    if (w > max_width && w > 0.0f) { px *= max_width / w; w = line_width(c, line, px); }
    float cursor = x - w * align;
    for (const auto& piece : line) {
        if (piece.key) {
            cursor += px * 0.08f;
            cursor += chip(c, cursor, top + px * 0.56f, px * 0.8f, piece.text) + px * 0.08f;
        } else {
            c.text(cursor, top, px, color, piece.text);
            cursor += c.width(px, piece.text);
        }
    }
    return w;
}

// A label shrunk to fit, or split over two lines at the space nearest the
// middle (anywhere, for scripts without spaces).
struct Fit { std::vector<std::string> lines; float px{}; };
Fit fit(const Canvas& c, const std::string& english, float px, float max_width, bool two_lines = true) {
    const auto logical = wuwa_l10n::translate(english);
    const auto whole = wuwa_l10n::visual(logical);
    const float w = c.width(px, whole);
    if (w <= max_width) return {{whole}, px};
    if (!two_lines || w * 0.82f <= max_width) return {{whole}, px * max_width / w};
    size_t best = std::string::npos;
    float best_score = FLT_MAX;
    const float middle = logical.size() * 0.5f;
    for (size_t i = 1; i + 1 < logical.size(); ++i) {
        const auto ch = static_cast<unsigned char>(logical[i]);
        if ((ch & 0xC0) == 0x80) continue;
        const float score = std::fabs(static_cast<float>(i) - middle) + (ch == ' ' ? 0.0f : logical.size() * 0.35f);
        if (score < best_score) { best_score = score; best = i; }
    }
    if (best == std::string::npos) return {{whole}, px * max_width / w};
    auto first = logical.substr(0, best), second = logical.substr(best);
    while (!first.empty() && first.back() == ' ') first.pop_back();
    while (!second.empty() && second.front() == ' ') second.erase(0, 1);
    Fit out{{wuwa_l10n::visual(first), wuwa_l10n::visual(second)}, px * 0.86f};
    const float widest = (std::max)(c.width(out.px, out.lines[0]), c.width(out.px, out.lines[1]));
    if (widest > max_width) out.px *= max_width / widest;
    return out;
}
void draw_fit(const Canvas& c, const Fit& f, float x, float middle, ImU32 color, float align, float box_width) {
    const float line_h = f.px * 1.12f, top = middle - line_h * f.lines.size() * 0.5f;
    for (size_t i = 0; i < f.lines.size(); ++i) {
        const float w = c.width(f.px, f.lines[i]);
        c.text(x + (box_width - w) * align, top + line_h * i, f.px, color, f.lines[i]);
    }
}

// A small status pill such as "On" or "Blur off". Returns its width. The _text forms take text that
// is already translated and in visual order (a value such as "+12 cm").
float pill_text_width(const Canvas& c, const std::string& text, float px) { return c.width(px, text) + px * 1.2f; }
float pill_width(const Canvas& c, const std::string& english, float px) { return pill_text_width(c, tr(english), px); }
void pill_text(const Canvas& c, float x, float middle, float px, const std::string& text, ImU32 color, bool filled = false) {
    const float w = pill_text_width(c, text, px), h = px * 1.45f;
    if (filled) c.list->AddRectFilled(c.at(x, middle - h * 0.5f), c.at(x + w, middle + h * 0.5f), color, h * 0.5f * c.s);
    else c.list->AddRect(c.at(x, middle - h * 0.5f), c.at(x + w, middle + h * 0.5f), color, h * 0.5f * c.s, 0, 1.4f * c.s);
    c.text(x + px * 0.6f, middle - px * 0.56f, px, filled ? lit_text : color, text);
}
void pill(const Canvas& c, float x, float middle, float px, const std::string& english, ImU32 color, bool filled = false) {
    pill_text(c, x, middle, px, tr(english), color, filled);
}

void frame(const Canvas& c) {
    c.list->AddRectFilled(c.at(0, 0), c.at(1600, 900), card_bg, 22.0f * c.s);
    c.list->AddRect(c.at(0, 0), c.at(1600, 900), frame_line, 22.0f * c.s, 0, 1.5f * c.s);
}

void header(const Canvas& c, const std::string& title, const std::string& hint) {
    c.text(58, 54, 30, accent, "WuWa VR");
    c.list->AddLine(c.at(222, 46), c.at(222, 98), row_line, 1.2f * c.s);
    const float hint_width = rich(c, 1542, 58, 24, muted, hint, 560, 1.0f);
    rich(c, 246, 42, 44, ink, title, 1542 - 246 - hint_width - 48);
}

// The controller, from the site's drawing (viewBox 640 x 400).
struct Pad {
    const Canvas& c;
    ImVec2 o;
    float k;
    ImVec2 at(float x, float y) const { return c.at(o.x + x * k, o.y + y * k); }
    float r(float v) const { return v * k * c.s; }
    void label(float x, float y, float px, ImU32 color, const char* text) const {
        const float w = c.width(px * k, text);
        c.text(o.x + x * k - w * 0.5f, o.y + y * k - px * k * 0.56f, px * k, color, text);
    }
    // A rounded rectangle turned about its centre.
    void slab(float cx, float cy, float w, float h, float radius, float degrees, bool lit, const char* name) const {
        const float a = degrees * 3.14159265f / 180.0f, ca = std::cos(a), sa = std::sin(a);
        const float hx = w * 0.5f - radius, hy = h * 0.5f - radius;
        const struct { float x, y, start; } corners[] = {{hx, -hy, -90.0f}, {hx, hy, 0.0f}, {-hx, hy, 90.0f}, {-hx, -hy, 180.0f}};
        ImVec2 points[28];
        int n = 0;
        for (const auto& corner : corners) {
            for (int i = 0; i <= 6; ++i) {
                const float t = (corner.start + i * 15.0f) * 3.14159265f / 180.0f;
                const float lx = corner.x + radius * std::cos(t), ly = corner.y + radius * std::sin(t);
                points[n++] = at(cx + lx * ca - ly * sa, cy + lx * sa + ly * ca);
            }
        }
        c.list->AddConvexPolyFilled(points, n, lit ? gold : button_fill);
        c.list->AddPolyline(points, n, lit ? lit_line : button_line, ImDrawFlags_Closed, (lit ? 2.5f : 1.5f) * c.s);
        label(cx, cy, 17.0f, lit ? lit_text : gold_hi, name);
    }
    void face(float cx, float cy, float radius, bool lit, ImU32 text, const char* name) const {
        c.list->AddCircleFilled(at(cx, cy), r(radius), lit ? gold : button_fill, 40);
        c.list->AddCircle(at(cx, cy), r(radius), lit ? lit_line : button_line, 40, (lit ? 2.5f : 1.5f) * c.s);
        label(cx, cy, 17.0f, lit ? lit_text : text, name);
    }
    void stick(float cx, float cy, float outer, float inner, bool lit, const char* name) const {
        c.list->AddCircleFilled(at(cx, cy), r(outer), well_fill, 48);
        c.list->AddCircle(at(cx, cy), r(outer), lit ? leader_lit : well_line, 48, 1.5f * c.s);
        face(cx, cy, inner, lit, gold_hi, name);
    }
    void dpad_key(float x, float y, float w, float h, bool lit) const {
        c.list->AddRectFilled(at(x, y), at(x + w, y + h), lit ? gold : button_fill, r(4));
        c.list->AddRect(at(x, y), at(x + w, y + h), lit ? lit_line : button_line, r(4), 0, (lit ? 2.5f : 1.5f) * c.s);
    }
    void body() const {
        // M210 92, then cubic segments (and one line) around the outline.
        static constexpr float path[][6] = {
            {250, 82, 390, 82, 430, 92}, {505, 104, 548, 132, 572, 196}, {600, 280, 606, 352, 566, 372},
            {532, 389, 500, 368, 478, 334}, {460, 306, 438, 292, 404, 292}, {404, 292, 236, 292, 236, 292},
            {202, 292, 180, 306, 162, 334}, {140, 368, 108, 389, 74, 372}, {34, 352, 40, 280, 68, 196},
            {92, 132, 135, 104, 210, 92}};
        std::vector<ImVec2> outline;
        float px = 210, py = 92;
        for (const auto& s : path) {
            for (int i = 1; i <= 12; ++i) {
                const float t = i / 12.0f, u = 1.0f - t;
                const float x = u * u * u * px + 3 * u * u * t * s[0] + 3 * u * t * t * s[2] + t * t * t * s[4];
                const float y = u * u * u * py + 3 * u * u * t * s[1] + 3 * u * t * t * s[3] + t * t * t * s[5];
                outline.push_back(at(x, y));
            }
            px = s[4]; py = s[5];
        }
        // The outline is not convex, but every point of it is visible from here.
        const ImVec2 hub = at(320, 150);
        for (size_t i = 0; i < outline.size(); ++i)
            c.list->AddTriangleFilled(hub, outline[i], outline[(i + 1) % outline.size()], pad_fill);
        c.list->AddPolyline(outline.data(), static_cast<int>(outline.size()), pad_line, ImDrawFlags_Closed, 2.0f * c.s);
    }
};

void controller(const Canvas& c, ImVec2 o, float k, const CardState& st, bool l3_modifier) {
    const Pad pad{c, o, k};
    const auto held = [&](unsigned mask) { return (st.buttons & mask) != 0; };
    pad.slab(182, 39, 84, 34, 14, -8, st.lt, "LT");
    pad.slab(458, 39, 84, 34, 14, 8, st.rt, "RT");
    pad.slab(182, 74, 128, 24, 12, -8, held(lb), "LB");
    pad.slab(458, 74, 128, 24, 12, 8, held(rb), "RB");
    pad.body();
    c.list->AddCircleFilled(pad.at(320, 124), pad.r(17), IM_COL32(12, 11, 9, 255), 32);
    c.list->AddCircle(pad.at(320, 124), pad.r(17), well_line, 32, 1.5f * c.s);
    // View (two squares) and Menu (three lines).
    for (const bool is_menu : {false, true}) {
        const float x = is_menu ? 344.0f : 270.0f;
        const bool lit = held(is_menu ? menu : view);
        c.list->AddRectFilled(pad.at(x, 150), pad.at(x + 26, 167), lit ? gold : button_fill, pad.r(6));
        c.list->AddRect(pad.at(x, 150), pad.at(x + 26, 167), lit ? lit_line : button_line, pad.r(6), 0, (lit ? 2.5f : 1.5f) * c.s);
        const ImU32 glyph = lit ? lit_text : gold_hi;
        if (is_menu) {
            for (const float y : {155.0f, 158.5f, 162.0f}) c.list->AddLine(pad.at(351, y), pad.at(363, y), glyph, 1.5f * c.s);
        } else {
            c.list->AddRect(pad.at(277, 155), pad.at(284, 161), glyph, 0.0f, 0, 1.5f * c.s);
            c.list->AddLine(pad.at(281, 161), pad.at(281, 164), glyph, 1.5f * c.s);
            c.list->AddLine(pad.at(281, 164), pad.at(288, 164), glyph, 1.5f * c.s);
            c.list->AddLine(pad.at(288, 164), pad.at(288, 158), glyph, 1.5f * c.s);
            c.list->AddLine(pad.at(288, 158), pad.at(284, 158), glyph, 1.5f * c.s);
        }
        pad.label(x + 13, 184, 14, muted, is_menu ? "Menu" : "View");
    }
    pad.stick(200, 170, 40, 27, l3_modifier || held(l3), "L3");
    pad.dpad_key(253, 217, 18, 22, held(0x0001));
    pad.dpad_key(235, 235, 22, 18, held(0x0004));
    pad.dpad_key(267, 235, 22, 18, held(0x0008));
    pad.dpad_key(253, 249, 18, 22, held(dpad_down));
    pad.stick(388, 244, 37, 25, held(r3), "R3");
    // VR controllers: the letters of the controller button that presses each.
    pad.face(456, 134, 17, held(button_y), c.vr ? gold_hi : yellow, "Y");
    pad.face(422, 168, 17, held(button_x), c.vr ? gold_hi : blue, c.vr ? "B" : "X");
    pad.face(490, 168, 17, held(button_b), c.vr ? gold_hi : red, c.vr ? "X" : "B");
    pad.face(456, 202, 17, held(button_a), c.vr ? gold_hi : green, "A");
}

// One row beside the controller: key cap(s), what they do, an optional state
// pill, and a leader to the button. Left rows keep the cap next to the controller.
struct Row {
    const char* token;
    const char* english;
    bool lit;
    float tx, ty, radius;   // the button, in pad units
    const char* state;
    ImU32 state_color;
    bool dim = false;       // does nothing in the current camera
    const char* note = nullptr; // a smaller second line under the label
    const char* value = nullptr; // a pill with text already translated (a value), in place of state
};
void callout(const Canvas& c, const Pad& pad, float x, float top, bool left, const Row& row) {
    constexpr float w = 440.0f, h = 62.0f, key_px = 26.0f;
    const float middle = top + h * 0.5f;
    c.list->AddRectFilled(c.at(x, top), c.at(x + w, top + h), row.lit ? row_lit_bg : row_bg, 8.0f * c.s);
    c.list->AddRect(c.at(x, top), c.at(x + w, top + h), row.lit ? lit_line : row_line, 8.0f * c.s, 0, (row.lit ? 2.5f : 1.0f) * c.s);
    const bool two = std::string_view{row.token} == "YX";
    const float slash = two ? c.width(key_px, "/") + 8.0f : 0.0f;
    const float keys = two ? chip_width(c, key_px, "Y") + chip_width(c, key_px, "X") + slash : chip_width(c, key_px, row.token);
    float key_x = left ? x + w - 16.0f - keys : x + 16.0f;
    if (two) {
        key_x += chip(c, key_x, middle, key_px, "Y", row.lit);
        c.text(key_x + 4.0f, middle - key_px * 0.56f, key_px, muted, "/");
        chip(c, key_x + slash, middle, key_px, "X", row.lit);
    } else {
        chip(c, key_x, middle, key_px, row.token, row.lit);
    }
    const std::string state = row.value ? std::string{row.value} : row.state ? tr(row.state) : std::string{};
    const float state_w = state.empty() ? 0.0f : pill_text_width(c, state, 18.0f) + 12.0f;
    const float text_w = w - 32.0f - keys - 16.0f - state_w;
    const float text_x = left ? x + 16.0f + state_w : x + 16.0f + keys + 16.0f;
    if (row.note) {
        draw_fit(c, fit(c, row.english, 25.0f, text_w, false), text_x, middle - 12.0f, row.dim ? muted : ink, left ? 1.0f : 0.0f, text_w);
        draw_fit(c, fit(c, row.note, 19.0f, text_w, false), text_x, middle + 14.0f, row.dim ? muted : accent, left ? 1.0f : 0.0f, text_w);
    } else {
        draw_fit(c, fit(c, row.english, 27.0f, text_w), text_x, middle, row.dim ? muted : ink, left ? 1.0f : 0.0f, text_w);
    }
    if (!state.empty()) pill_text(c, left ? x + 16.0f : x + w - 16.0f - state_w + 12.0f, middle, 18.0f, state, row.state_color);
    // Leader: out from the box, then straight to the button's edge.
    const ImVec2 start = c.at(left ? x + w : x, middle), bend = c.at(left ? x + w + 16.0f : x - 16.0f, middle);
    ImVec2 end = pad.at(row.tx, row.ty);
    const float dx = end.x - bend.x, dy = end.y - bend.y, length = std::sqrt(dx * dx + dy * dy);
    if (length > 1.0f && row.radius > 0.0f) {
        const float back = pad.r(row.radius + 3.0f) / length;
        end = {end.x - dx * back, end.y - dy * back};
    }
    const ImU32 color = row.lit ? leader_lit : leader;
    c.list->AddLine(start, bend, color, 1.4f * c.s);
    c.list->AddLine(bend, end, color, 1.4f * c.s);
    c.list->AddCircleFilled(end, 3.5f * c.s, row.lit ? lit_line : gold, 12);
}

void l3_card(const Canvas& c, const CardState& st) {
    header(c, "Hold {L3}, then press", "Hold {LT}+{RT} for views");
    const ImVec2 o{532.0f, 278.0f};
    const float k = 0.834f;
    const Pad pad{c, o, k};
    const auto held = [&](unsigned mask) { return (st.buttons & mask) != 0; };
    const bool camera_moves = st.first_person || st.fixed_camera;
    // The story camera nudge in use now, as on the Cinema page's status line.
    std::string nudge;
    if (st.story_nudge && std::fabs(st.story_nudge_cm) >= 1.0f) {
        char text[48]{};
        std::snprintf(text, sizeof(text), wuwa_l10n::translate("%+.0f cm").c_str(), st.story_nudge_cm);
        nudge = wuwa_l10n::visual(text);
    }
    const Row left[] = {
        {"LT", N_("Portal window"), st.lt, 182, 39, 17, st.portal ? N_("On") : nullptr, green},
        {"LB", N_("HUD / mouse mode"), held(lb), 182, 74, 12, st.mouse_mode ? N_("On") : nullptr, green},
        {"View", N_("First person"), held(view), 283, 158.5f, 9, st.first_person ? N_("On") : nullptr, green},
        {"Down", N_("First person: full animation"), held(dpad_down), 262, 260, 9, nullptr, 0, !st.first_person},
    };
    const Row right[] = {
        {"RT", N_("Diorama (10\xC3\x97)"), st.rt, 458, 39, 17, st.diorama ? N_("On") : nullptr, green},
        {"RB", N_("Game / fixed camera"), held(rb), 458, 74, 12, st.fixed_camera ? N_("Fixed") : nullptr, green},
        {"Menu", N_("Keep this card open"), held(menu), 357, 158.5f, 9, st.pinned ? N_("Pinned") : nullptr, green},
        // Y / X move the camera up and down in first person and the fixed camera, and along the shot in story
        // scenes on the game camera (the story camera nudge); both together reset it.
        {"YX", st.story_nudge ? N_("Story camera: nearer / back") : N_("Camera up / down"), held(button_x | button_y),
            439, 151, 0, nullptr, green, !camera_moves && !st.story_nudge,
            st.story_nudge ? N_("Y + X together: back to the game's spot")
                : camera_moves ? N_("Y + X together: back to default") : N_("Only in first person, the fixed camera and story scenes"),
            nudge.empty() ? nullptr : nudge.c_str()},
        {"B", N_("Show / hide game UI"), held(button_b), 490, 168, 17, st.ui_hidden ? N_("Hidden") : nullptr, red},
        {"A", N_("Recenter view"), held(button_a), 456, 202, 17, nullptr, 0},
        {"R3", N_("WuWa VR menu"), held(r3), 388, 244, 25, nullptr, 0},
    };
    // The leaders run over the controller to each button, as on the site.
    controller(c, o, k, st, true);
    for (int i = 0; i < 4; ++i) callout(c, pad, 58, 272.0f + i * 90.0f, true, left[i]);
    for (int i = 0; i < 7; ++i) callout(c, pad, 1102, 152.0f + i * 90.0f, false, right[i]);

    c.list->AddLine(c.at(58, 806), c.at(1542, 806), row_line, 1.2f * c.s);
    float x = 58.0f;
    const auto twice = tr("Click twice");
    c.text(x, 832, 22, accent, twice);
    x += c.width(22, twice) + 34.0f;
    for (const auto& [token, english] : {std::pair{"R3", N_("Freecam")}, std::pair{"L3", N_("Screenshot")}}) {
        x += chip(c, x, 846, 22, token) + 6.0f;
        c.text(x, 834, 20, accent, "\xC3\x97" "2");
        x += c.width(20, "\xC3\x97" "2") + 10.0f;
        const auto f = fit(c, english, 25, 220, false);
        c.text(x, 846 - f.px * 0.56f, f.px, ink, f.lines[0]);
        x += c.width(f.px, f.lines[0]) + 34.0f;
    }
    rich(c, 1542, 830, 25, muted, "Everything else: {L3}+{R3} opens the WuWa VR menu", 1542 - x, 1.0f);
}

// Both ways: R3 switches between two views and back (test 17k).
void arrow2(const Canvas& c, float x, float y) {
    c.list->AddLine(c.at(x - 14, y), c.at(x + 14, y), accent, 3.0f * c.s);
    c.list->AddTriangleFilled(c.at(x + 24, y), c.at(x + 10, y - 11), c.at(x + 10, y + 11), accent);
    c.list->AddTriangleFilled(c.at(x - 24, y), c.at(x - 10, y + 11), c.at(x - 10, y - 11), accent);
}
// The view icons (viewBox 120 x 80) around a centre.
void icon(const Canvas& c, int which, float cx, float cy) {
    constexpr float k = 1.25f;
    const auto at = [&](float x, float y) { return c.at(cx + (x - 60.0f) * k, cy + (y - 40.0f) * k); };
    const float thick = 3.0f * c.s, thin = 2.0f * c.s;
    if (which == 0) {
        c.list->AddLine(at(10, 70), at(60, 40), gold, thick);
        c.list->AddLine(at(60, 40), at(110, 70), gold, thick);
        c.list->AddLine(at(60, 10), at(60, 40), gold, thick);
        c.list->AddCircleFilled(at(60, 22), 9.0f * k * c.s, gold, 24);
        c.list->AddBezierQuadratic(at(20, 70), at(60, 52), at(100, 70), gold_thin, thin, 24);
        return;
    }
    c.list->AddRect(at(16, 12), at(104, 62), gold, 4.0f * k * c.s, 0, thick);
    c.list->AddRectFilled(at(26, 20), at(94, 54), gold_dim);
    if (which == 1) {
        const ImVec2 hills[] = {at(30, 50), at(50, 34), at(62, 44), at(76, 30), at(90, 50)};
        c.list->AddPolyline(hills, 5, gold_thin, 0, thin);
        c.list->AddLine(at(36, 72), at(84, 72), gold_thin, thin);
    } else {
        c.list->AddLine(at(60, 72), at(60, 62), gold_thin, thin);
        c.list->AddLine(at(44, 72), at(76, 72), gold_thin, thin);
    }
}

void views_card(const Canvas& c, const CardState& st) {
    header(c, "Hold {LT}+{RT}, then", "Hold {L3} for everything else");
    static constexpr const char* names[]{N_("Full VR"), N_("Stereo screen"), N_("Mono theatre")};
    static constexpr const char* notes[]{N_("You are in the scene"), N_("A big 3D screen; best for cutscenes"), N_("Flat screen; easiest on the eyes")};
    for (int i = 0; i < 3; ++i) {
        const bool now = st.view == i;
        const float x0 = 192.0f + i * 414.0f, x1 = x0 + 330.0f, y0 = now ? 150.0f : 166.0f, y1 = now ? 432.0f : 416.0f;
        if (now) c.list->AddRect(c.at(x0 - 6, y0 - 6), c.at(x1 + 6, y1 + 6), IM_COL32(249, 232, 180, 70), 16.0f * c.s, 0, 5.0f * c.s);
        c.list->AddRectFilled(c.at(x0, y0), c.at(x1, y1), now ? IM_COL32(40, 32, 16, 235) : row_bg, 12.0f * c.s);
        c.list->AddRect(c.at(x0, y0), c.at(x1, y1), now ? lit_line : IM_COL32(214, 178, 98, 64), 12.0f * c.s, 0, (now ? 2.0f : 1.0f) * c.s);
        icon(c, i, x0 + 165.0f, y0 + 78.0f);
        draw_fit(c, fit(c, names[i], 34, 300, false), x0 + 15.0f, y0 + 166.0f, ink, 0.5f, 300.0f);
        draw_fit(c, fit(c, notes[i], 22, 290), x0 + 20.0f, y0 + 222.0f, muted, 0.5f, 290.0f);
        if (now) {
            const float w = pill_width(c, "now", 18.0f);
            pill(c, x0 + 165.0f - w * 0.5f, y0, 18.0f, "now", gold, true);
        }
    }
    // Click R3: full VR <-> stereo screen. Hold R3: mono theatre and back (from either view).
    arrow2(c, 564, 291);
    arrow2(c, 978, 291);
    chip(c, 564.0f - chip_width(c, 20, "R3") * 0.5f, 252, 20, "R3");
    {
        const float w = chip_width(c, 20, "R3") + 8.0f + c.width(18, "0.8 s");
        const float x = 978.0f - w * 0.5f;
        chip(c, x, 252, 20, "R3");
        c.text(x + chip_width(c, 20, "R3") + 8.0f, 252.0f - 18.0f * 0.56f, 18, accent, "0.8 s");
    }
    const bool r3_held = (st.buttons & r3) != 0, l3_held = (st.buttons & l3) != 0, y_held = (st.buttons & button_y) != 0;
    const struct { const char* token; const char* extra; const char* english; bool lit; } moves[]{
        {"R3", nullptr, N_("Click: full VR / stereo screen"), r3_held},
        {"R3", "0.8 s", N_("Hold: mono theatre on / off"), r3_held},
        {"L3", nullptr, N_("Click: blur on / off"), l3_held},
        {"Y", nullptr, N_("Press: hide characters at your eyes"), y_held},
    };
    for (int i = 0; i < 4; ++i) {
        const auto& m = moves[i];
        const float top = 468.0f + i * 82.0f, middle = top + 34.0f;
        c.list->AddRectFilled(c.at(351, top), c.at(1249, top + 68), m.lit ? row_lit_bg : row_bg, 8.0f * c.s);
        c.list->AddRect(c.at(351, top), c.at(1249, top + 68), m.lit ? lit_line : row_line, 8.0f * c.s, 0, (m.lit ? 2.5f : 1.0f) * c.s);
        float x = 372.0f + chip(c, 372, middle, 26, m.token, m.lit);
        if (m.extra) c.text(x + 8.0f, middle - 20.0f * 0.56f, 20, accent, m.extra);
        const bool blur = i == 2, hide = i == 3;
        const bool on = blur ? st.blur : st.hide_near;
        const char* state = blur ? (st.blur ? N_("Blur on") : N_("Blur off")) :
            hide ? (st.hide_near ? N_("Hiding on") : N_("Hiding off")) : nullptr;
        const float state_w = state ? pill_width(c, state, 20.0f) + 20.0f : 0.0f;
        const auto f = fit(c, m.english, 29, 1249.0f - 543.0f - 24.0f - state_w, false);
        c.text(543, middle - f.px * 0.56f, f.px, ink, f.lines[0]);
        if (state) pill(c, 543.0f + c.width(f.px, f.lines[0]) + 20.0f, middle, 20.0f, state, on ? green : muted);
    }
    c.list->AddLine(c.at(58, 810), c.at(1542, 810), row_line, 1.2f * c.s);
    const auto f = fit(c, "Keep both triggers held to click or hold R3 again.", 25, 1480, false);
    c.text(1542.0f - c.width(f.px, f.lines[0]), 832, f.px, muted, f.lines[0]);
}

void centered(const Canvas& c, ImVec2 size, float y, float px, ImU32 color, const char* english) {
    const auto value = wuwa_l10n::text(english);
    const float initial = c.font->CalcTextSizeA(px * c.s, FLT_MAX, 0.0f, value.c_str()).x;
    const float fitted = px * c.s * (std::min)(1.0f, 1350.0f * c.s / (std::max)(initial, 1.0f));
    const float w = c.font->CalcTextSizeA(fitted, FLT_MAX, 0.0f, value.c_str()).x;
    c.list->AddText(c.font, fitted, {size.x * 0.5f - w * 0.5f, c.origin.y + y * c.s}, color, value.c_str());
}
}

void reserve_font(ImFontAtlas* atlas) {
    owner = atlas;
    fallback = atlas->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, 32.0f);
}

void draw_card(ImDrawList* draw, ImVec2 size, ImFont* font, const CardState& state) {
    const auto c = canvas(draw, size, font, state.vr_controllers);
    if (!c.font) return;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    frame(c);
    if (state.card == Card::views) views_card(c, state);
    else l3_card(c, state);
    draw->PopClipRect();
}

void draw_tip(ImDrawList* draw, ImVec2 size, ImFont* font, const std::string& lead, const std::string& line, bool vr_controllers,
    const std::string& note) {
    const auto c = canvas(draw, size, font, vr_controllers);
    if (!c.font) return;
    constexpr float px = 26.0f, top = 618.0f, h = 58.0f;
    const auto lead_text = lead.empty() ? std::string{} : tr(lead);
    const float lead_w = lead.empty() ? 0.0f : c.width(px, lead_text) + 14.0f;
    const float max_line = 1480.0f - 48.0f - lead_w;
    const float line_w = (std::min)(line_width(c, pieces(line, c.rtl), px), max_line);
    const float w = lead_w + line_w + 48.0f, x0 = 800.0f - w * 0.5f;
    draw->AddRectFilled(c.at(x0, top), c.at(x0 + w, top + h), IM_COL32(8, 7, 5, 222), h * 0.5f * c.s);
    draw->AddRect(c.at(x0, top), c.at(x0 + w, top + h), IM_COL32(214, 178, 98, 118), h * 0.5f * c.s, 0, 1.5f * c.s);
    const float text_top = top + (h - px) * 0.5f - 2.0f;
    if (c.rtl) {
        rich(c, x0 + 24.0f, text_top, px, ink, line, max_line);
        if (!lead.empty()) c.text(x0 + w - 24.0f - lead_w + 14.0f, text_top, px, accent, lead_text);
    } else {
        if (!lead.empty()) c.text(x0 + 24.0f, text_top, px, accent, lead_text);
        rich(c, x0 + 24.0f + lead_w, text_top, px, ink, line, max_line);
    }
    if (!note.empty()) {
        constexpr float note_px = 20.0f;
        const float scale = (std::min)(1.0f, 1480.0f / (std::max)(c.width(note_px, note), 1.0f));
        const float note_w = c.width(note_px * scale, note);
        c.text(800.0f - note_w * 0.5f, top + h + 12.0f, note_px * scale, muted, note);
    }
}

void draw_hidden_ui_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool shortcut_available, bool menu_detected) {
    const auto c = canvas(draw, size, font);
    if (!c.font) return;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    if (!menu_detected) {
        // Controller-operated menus may never show the game's mouse cursor.
        // Keep a smaller recovery hint while HUD hiding is active, so absence
        // of that cursor signal cannot strand the player in a blurred menu.
        draw->AddRectFilled(c.at(175, 535), c.at(1425, 680), IM_COL32(55, 8, 12, 232), 18 * c.s);
        centered(c, size, 552, 42, IM_COL32(255, 100, 104, 255), "GAME UI HIDDEN - MENUS ARE HIDDEN TOO");
        centered(c, size, 609, 34, IM_COL32(255, 255, 255, 255), shortcut_available ? "L3 + B  =  SHOW UI" : "INSERT > WuWa Controls > Show game UI now");
        draw->PopClipRect();
        return;
    }
    draw->AddRectFilled(c.at(100, 230), c.at(1500, 670), IM_COL32(68, 8, 12, 250), 28 * c.s);
    draw->AddRect(c.at(100, 230), c.at(1500, 670), IM_COL32(255, 65, 72, 255), 28 * c.s, 0, 5 * c.s);
    centered(c, size, 269, 84, IM_COL32(255, 90, 96, 255), "GAME UI IS HIDDEN");
    centered(c, size, 373, 39, IM_COL32(255, 235, 236, 255), "Show UI to see this menu");
    centered(c, size, 454, 52, IM_COL32(255, 255, 255, 255), shortcut_available ? "L3 + B  =  SHOW UI" : "INSERT  =  OPEN UEVR");
    centered(c, size, 523, 28, IM_COL32(255, 206, 210, 255), shortcut_available ? "L3 means click and hold the left stick, then press B." : "WuWa Controls > Show game UI now");
    centered(c, size, 601, 26, IM_COL32(255, 206, 210, 255), shortcut_available ? "Or: UEVR > WuWa Controls > Show game UI now" : "Polar shortcuts are disabled or bypassed.");
    draw->PopClipRect();
}

void draw_mouse_mode_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool adjustment, bool hidden_menu) {
    const auto c = canvas(draw, size, font);
    if (!c.font) return;
    const float top = hidden_menu ? 700.0f : 590.0f;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    draw->AddRectFilled(c.at(90, top), c.at(1510, top + 180), IM_COL32(43, 31, 7, 246), 18 * c.s);
    draw->AddRect(c.at(90, top), c.at(1510, top + 180), IM_COL32(255, 192, 60, 255), 18 * c.s, 0, 3 * c.s);
    centered(c, size, top + 15, 43, IM_COL32(255, 208, 99, 255), "HUD / MOUSE MODE ON");
    centered(c, size, top + 70, 36, IM_COL32(255, 255, 255, 255), adjustment ? "L3 + LB  =  RETURN TO GAME CONTROLS" : "UEVR > WuWa Controls > Exit mouse mode now");
    centered(c, size, top + 124, 27, IM_COL32(245, 224, 181, 255), adjustment ? "Click left stick + left bumper, then release all controls." : "Activated automatically in this menu: cursor only. L3 + R3 opens the WuWa VR menu.");
    draw->PopClipRect();
}
}
