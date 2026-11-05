#define NOMINMAX
#include "WuWaShortcutSheet.hpp"
#include "WuWaLocalization.hpp"
#include "WuWaKeyboard.hpp"
#include "WuWaBrand.hpp"
#include "WuWaPad3D.hpp"
#include "uevr-imgui/font_robotomedium.hpp"
#include <algorithm>
#include <cctype>
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
// set_key_names: the VR key and the freecam keys as this keyboard layout names them.
std::string vr_key_label{"`"};
std::array<std::string, 4> move_labels{"W", "A", "S", "D"};

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
    bool keyboard{}; // {tokens} are keyboard keys: no controller colours or VR controller names
    bool playstation{}; // PlayStation names: L1 R2 Create Options, the face buttons as their shapes (1.2.7)
    ImVec2 at(float x, float y) const { return {origin.x + x * s, origin.y + y * s}; }
    float width(float px, const std::string& text) const { return font->CalcTextSizeA(px * s, FLT_MAX, 0.0f, text.c_str()).x / s; }
    void text(float x, float y, float px, ImU32 color, const std::string& visual) const {
        list->AddText(font, px * s, at(x, y), color, visual.c_str());
    }
};

Canvas canvas(ImDrawList* list, ImVec2 size, ImFont* font, bool vr = false, bool keyboard = false, bool playstation = false) {
    Canvas c;
    c.list = list;
    c.font = font;
    if (owner == ImGui::GetIO().Fonts && fallback) c.font = fallback;
    if (auto* localized = wuwa_l10n::sheet_font()) c.font = localized;
    c.s = (std::min)(size.x / 1600.0f, size.y / 900.0f);
    c.origin = {(size.x - 1600.0f * c.s) * 0.5f, (size.y - 900.0f * c.s) * 0.5f};
    c.rtl = wuwa_l10n::right_to_left();
    c.vr = vr && !keyboard;
    c.keyboard = keyboard;
    c.playstation = playstation && !c.vr && !keyboard;
    return c;
}

// What a {token} names, as printed on its key cap. VR controllers (the mod's sightseeing pad, 1.2.7): the
// triggers are LT / RT, the grips LB / RB, the stick clicks L3 / R3, A B X Y the buttons of those names, the left
// Menu button Menu and the left grip + Menu View. PlayStation: L1 R1 L2 R2, Create, Options, and the face
// buttons' shapes (glyph names, drawn by chip()).
std::string key_name(const Canvas& c, const std::string& token) {
    if (c.keyboard) {
        // {VR}: the VR key; {W} {A} {S} {D}: the freecam keys by position; named keys are translated.
        if (token == "VR") return vr_key_label;
        if (token == "W") return move_labels[0];
        if (token == "A") return move_labels[1];
        if (token == "S") return move_labels[2];
        if (token == "D") return move_labels[3];
        static constexpr const char* named[]{N_("Wheel"), N_("Space"), N_("Ctrl"), N_("Shift"), N_("Insert")};
        for (const char* name : named) if (token == name) return tr(token);
        return token;
    }
    const bool vr = c.vr;
    if (vr) {
        if (token == "LT") return tr("L trigger");
        if (token == "RT") return tr("R trigger");
        if (token == "LB") return tr("L grip");
        if (token == "RB") return tr("R grip");
        if (token == "L3") return tr("L stick");
        if (token == "R3") return tr("R stick");
        if (token == "View") return c.rtl ? tr("Menu") + " + " + tr("L grip") : tr("L grip") + " + " + tr("Menu");
    }
    if (c.playstation) {
        if (token == "LT") return "L2";
        if (token == "RT") return "R2";
        if (token == "LB") return "L1";
        if (token == "RB") return "R1";
        if (token == "A") return "cross";
        if (token == "B") return "circle";
        if (token == "X") return "square";
        if (token == "Y") return "triangle";
        if (token == "View") return tr("Create");
        if (token == "Menu") return tr("Options");
    }
    if (token == "Down") return tr("D-pad");
    if (token == "View" || token == "Menu") return tr(token);
    return token;
}
ImU32 key_color(const Canvas& c, const std::string& token) {
    if (c.vr || c.keyboard) return gold_hi;
    if (c.playstation) {
        return token == "A" ? IM_COL32(124, 178, 238, 255) : token == "B" ? red : token == "X" ? IM_COL32(236, 146, 204, 255)
            : token == "Y" ? IM_COL32(88, 222, 170, 255) : gold_hi;
    }
    return token == "A" ? green : token == "B" ? red : token == "X" ? blue : token == "Y" ? yellow : gold_hi;
}

// A key cap: the name on a dark cap with a heavier lower edge, gold when lit.
bool glyph_cap(const Canvas& c, const std::string& name) { return !c.keyboard && wuwa_pad3d::is_glyph(name); }
float chip_width(const Canvas& c, float px, const std::string& token) {
    const auto name = key_name(c, token);
    if (glyph_cap(c, name)) return px * 1.6f;
    float w = c.width(px, name) + px * 0.64f;
    if (token == "Down") w += px * 0.7f;
    return (std::max)(w, px * 1.6f);
}
float chip(const Canvas& c, float x, float middle, float px, const std::string& token, bool lit = false) {
    const auto name = key_name(c, token);
    const float w = chip_width(c, px, token), h = px * 1.45f, top = middle - h * 0.5f;
    c.list->AddRectFilled(c.at(x, top), c.at(x + w, top + h), lit ? gold : key_bg, 7.0f * c.s);
    c.list->AddRect(c.at(x, top), c.at(x + w, top + h), lit ? lit_line : key_line, 7.0f * c.s, 0, 1.2f * c.s);
    c.list->AddLine(c.at(x + 5.0f, top + h - 1.0f), c.at(x + w - 5.0f, top + h - 1.0f), lit ? lit_line : key_line, 2.5f * c.s);
    const ImU32 color = lit ? lit_text : key_color(c, token);
    if (glyph_cap(c, name)) {
        wuwa_pad3d::glyph(c.list, c.at(x + w * 0.5f, middle), px * 0.46f * c.s, name, color, (std::max)(1.5f, px * 0.09f * c.s));
        return w;
    }
    if (c.keyboard && token == "VR" && name.size() == 1 && !std::isalnum(static_cast<unsigned char>(name[0]))) {
        c.text(x + (w - c.width(px * 1.5f, name)) * 0.5f, middle - px * 0.45f, px * 1.5f, color, name);
        return w;
    }
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
    // A faint warm light from the top, as on the menu, and a hairline under the header.
    c.list->AddRectFilledMultiColor(c.at(22, 1), c.at(1578, 160), IM_COL32(214, 175, 91, 24), IM_COL32(214, 175, 91, 24),
        IM_COL32(214, 175, 91, 0), IM_COL32(214, 175, 91, 0));
    c.list->AddRect(c.at(0, 0), c.at(1600, 900), frame_line, 22.0f * c.s, 0, 1.5f * c.s);
    c.list->AddLine(c.at(58, 116), c.at(1542, 116), row_line, 1.0f * c.s);
}

// The crest and the name, then the card's title and a hint. Without the crest in this font atlas, the name alone.
void header(const Canvas& c, const std::string& title, const std::string& hint) {
    float x = 58.0f;
    const float crest = wuwa_brand::draw_crest(c.list, c.at(46, 28), 72.0f * c.s) / c.s;
    if (crest > 0.0f) x = 46.0f + crest + 14.0f;
    c.text(x, 48, 32, accent, "WuWa VR");
    const float divider = x + c.width(32, "WuWa VR") + 24.0f;
    c.list->AddLine(c.at(divider, 40), c.at(divider, 98), row_line, 1.2f * c.s);
    const float title_x = divider + 24.0f;
    const float hint_width = rich(c, 1542, 54, 25, muted, hint, 520, 1.0f);
    rich(c, title_x, 38, 44, ink, title, 1542 - title_x - hint_width - 40);
}

// The live 3D controller (utility/WuWaPad3D.hpp, as in the WuWa VR menu) showing what is held now and turning
// slowly; the rows' leaders run to where each button is drawn. 1.2.7 (owner, 5 Nov 2026).
wuwa_pad3d::Device pad_device(const Canvas& c) {
    return c.vr ? wuwa_pad3d::Device::vr : c.playstation ? wuwa_pad3d::Device::playstation : wuwa_pad3d::Device::xbox;
}
std::uint32_t pad_bits(unsigned xinput) {
    namespace b = wuwa_pad3d::button;
    static constexpr struct { unsigned x; std::uint32_t bit; } map[] = {{0x0001, b::Up}, {0x0002, b::Down}, {0x0004, b::Left},
        {0x0008, b::Right}, {0x0010, b::Menu}, {0x0020, b::View}, {0x0040, b::L3}, {0x0080, b::R3}, {0x0100, b::LB},
        {0x0200, b::RB}, {0x1000, b::A}, {0x2000, b::B}, {0x4000, b::X}, {0x8000, b::Y}};
    std::uint32_t bits = 0;
    for (const auto& m : map) if (xinput & m.x) bits |= m.bit;
    return bits;
}
std::uint32_t token_bits(const Canvas& c, std::string_view token) {
    namespace b = wuwa_pad3d::button;
    if (token == "LT") return b::LT;
    if (token == "RT") return b::RT;
    if (token == "LB") return b::LB;
    if (token == "RB") return b::RB;
    if (token == "L3") return b::L3;
    if (token == "R3") return b::R3;
    if (token == "A") return b::A;
    if (token == "B") return b::B;
    if (token == "X") return b::X;
    if (token == "Y" || token == "YX") return b::Y;
    if (token == "Menu") return b::Menu;
    if (token == "View") return c.vr ? b::Menu : b::View; // VR: the left grip + Menu; the leader goes to Menu
    if (token == "Down") return c.vr ? 0u : b::dpad;      // VR controllers have no D-pad
    return 0;
}
using Anchors = std::vector<wuwa_pad3d::Anchor>;
Anchors draw_pad3d(const Canvas& c, float x0, float y0, float x1, float y1, std::uint32_t held) {
    wuwa_pad3d::Moment now;
    now.held = held;
    wuwa_pad3d::Look look;
    look.sway = 0.14f;
    look.sway_seconds = 12.0f;
    look.fill = 0.94f;
    Anchors anchors;
    wuwa_pad3d::draw_moment(c.list, c.font, c.at(x0, y0), c.at(x1, y1), pad_device(c), now, ImGui::GetTime(), look, &anchors);
    return anchors;
}
// A leader from (x, y) on a row's edge to its button on the controller, stopping at the button's rim.
void draw_leader(const Canvas& c, const Anchors& anchors, std::string_view token, float x, float y, bool left, bool lit) {
    const auto bits = token_bits(c, token);
    const wuwa_pad3d::Anchor* found = nullptr;
    for (const auto& anchor : anchors) {
        if (bits && (anchor.control & bits)) { found = &anchor; break; }
    }
    if (!found) return;
    const ImVec2 start = c.at(x, y), bend = c.at(left ? x + 18.0f : x - 18.0f, y);
    ImVec2 end = found->at;
    const float dx = end.x - bend.x, dy = end.y - bend.y, length = std::sqrt(dx * dx + dy * dy);
    if (length > 1.0f) {
        const float back = (found->radius * 1.15f + 3.0f * c.s) / length;
        end = {end.x - dx * back, end.y - dy * back};
    }
    const ImU32 color = lit ? leader_lit : leader;
    c.list->AddLine(start, bend, color, 1.5f * c.s);
    c.list->AddLine(bend, end, color, 1.5f * c.s);
    c.list->AddCircleFilled(end, 4.0f * c.s, lit ? lit_line : gold, 12);
}

// One row beside the controller: key cap(s), what they do, an optional state pill. Left rows keep the cap next
// to the controller; the leader runs from the row's inner edge.
struct Row {
    const char* token;
    const char* english;
    bool lit;
    const char* state;
    ImU32 state_color;
    bool dim = false;            // does nothing in the current camera
    const char* note = nullptr;  // a smaller second line under the label
    const char* value = nullptr; // a pill with text already translated (a value), in place of state
};
constexpr float row_w = 446.0f, row_h = 66.0f, row_step = 92.0f;
void callout(const Canvas& c, const Anchors& anchors, float x, float top, bool left, const Row& row) {
    constexpr float key_px = 27.0f, pill_px = 21.0f;
    const float middle = top + row_h * 0.5f;
    c.list->AddRectFilled(c.at(x, top), c.at(x + row_w, top + row_h), row.lit ? row_lit_bg : row_bg, 9.0f * c.s);
    c.list->AddRect(c.at(x, top), c.at(x + row_w, top + row_h), row.lit ? lit_line : row_line, 9.0f * c.s, 0, (row.lit ? 2.5f : 1.0f) * c.s);
    const bool two = std::string_view{row.token} == "YX";
    const float slash = two ? c.width(key_px, "/") + 8.0f : 0.0f;
    const float keys = two ? chip_width(c, key_px, "Y") + chip_width(c, key_px, "X") + slash : chip_width(c, key_px, row.token);
    float key_x = left ? x + row_w - 16.0f - keys : x + 16.0f;
    const float upper = row.note ? middle - 13.0f : middle;
    if (two) {
        key_x += chip(c, key_x, middle, key_px, "Y", row.lit);
        c.text(key_x + 4.0f, middle - key_px * 0.56f, key_px, muted, "/");
        chip(c, key_x + slash, middle, key_px, "X", row.lit);
    } else {
        chip(c, key_x, middle, key_px, row.token, row.lit);
    }
    const std::string state = row.value ? std::string{row.value} : row.state ? tr(row.state) : std::string{};
    const float state_w = state.empty() ? 0.0f : pill_text_width(c, state, pill_px) + 12.0f;
    const float text_w = row_w - 32.0f - keys - 16.0f - state_w;
    const float text_x = left ? x + 16.0f + state_w : x + 16.0f + keys + 16.0f;
    const float align = left ? 1.0f : 0.0f;
    if (row.note) {
        draw_fit(c, fit(c, row.english, 26.0f, text_w, false), text_x, upper, row.dim ? muted : ink, align, text_w);
        const float note_x = left ? x + 16.0f : text_x, note_w = text_w + state_w;
        draw_fit(c, fit(c, row.note, 21.0f, note_w, false), note_x, middle + 15.0f, row.dim ? muted : accent, align, note_w);
    } else {
        draw_fit(c, fit(c, row.english, 28.0f, text_w), text_x, middle, row.dim ? muted : ink, align, text_w);
    }
    if (!state.empty()) pill_text(c, left ? x + 16.0f : x + row_w - 16.0f - state_w + 12.0f, upper, pill_px, state, row.state_color);
    draw_leader(c, anchors, row.token, left ? x + row_w : x, middle, left, row.lit);
}

void l3_card(const Canvas& c, const CardState& st) {
    header(c, "Hold {L3}, then press", "Hold {LT}+{RT} for views");
    const auto held = [&](unsigned mask) { return (st.buttons & mask) != 0; };
    const bool camera_moves = st.first_person || st.fixed_camera;
    // The story camera nudge in use now, as on the Camera page's status line.
    std::string nudge;
    if (st.story_nudge && std::fabs(st.story_nudge_cm) >= 1.0f) {
        char text[48]{};
        std::snprintf(text, sizeof(text), wuwa_l10n::translate("%+.0f cm").c_str(), st.story_nudge_cm);
        nudge = wuwa_l10n::visual(text);
    }
    // VR controllers have no D-pad, so no full-animation row there.
    const Row left[] = {
        {"LT", N_("Portal window"), st.lt, st.portal ? N_("On") : nullptr, green},
        {"LB", N_("HUD / mouse mode"), held(lb), st.mouse_mode ? N_("On") : nullptr, green},
        {"View", N_("First person"), held(view), st.first_person ? N_("On") : nullptr, green},
        {"Down", N_("First person: full animation"), held(dpad_down), nullptr, 0, !st.first_person},
    };
    const Row right[] = {
        {"RT", N_("Diorama (10\xC3\x97)"), st.rt, st.diorama ? N_("On") : nullptr, green},
        {"RB", N_("Game / fixed camera"), held(rb), st.fixed_camera ? N_("Fixed") : nullptr, green},
        {"Menu", N_("Keep this card open"), held(menu), st.pinned ? N_("Pinned") : nullptr, green},
        // Y / X move the camera up and down in first person and the fixed camera, and along the shot in story
        // scenes on the game camera (the story camera nudge); both together reset it.
        {"YX", st.story_nudge ? N_("Story camera: nearer / back") : N_("Camera up / down"), held(button_x | button_y), nullptr, green,
            !camera_moves && !st.story_nudge,
            camera_moves || st.story_nudge ? N_("Both together: reset") : N_("Not in this camera"),
            nudge.empty() ? nullptr : nudge.c_str()},
        {"B", N_("Show / hide game UI"), held(button_b), st.ui_hidden ? N_("Hidden") : nullptr, red},
        {"A", N_("Recenter view"), held(button_a), nullptr, 0},
        {"R3", N_("WuWa VR menu"), held(r3), nullptr, 0},
    };
    // The controller in the middle with L3 held; the rows either side, leaders to their buttons.
    std::uint32_t lit = wuwa_pad3d::button::L3 | pad_bits(st.buttons);
    if (st.lt) lit |= wuwa_pad3d::button::LT;
    if (st.rt) lit |= wuwa_pad3d::button::RT;
    ImDrawListSplitter layers;
    layers.Split(c.list, 2);
    layers.SetCurrentChannel(c.list, 1);
    const auto anchors = draw_pad3d(c, 522, 156, 1082, 744, lit);
    layers.SetCurrentChannel(c.list, 0);
    constexpr float right_top = 140.0f, middle_y = right_top + (7 * row_h + 6 * (row_step - row_h)) * 0.5f;
    const int left_count = c.vr ? 3 : 4;
    const float left_top = middle_y - (left_count * row_h + (left_count - 1) * (row_step - row_h)) * 0.5f;
    for (int i = 0; i < left_count; ++i) callout(c, anchors, 50, left_top + i * row_step, true, left[i]);
    for (int i = 0; i < 7; ++i) callout(c, anchors, 1104, right_top + i * row_step, false, right[i]);
    layers.Merge(c.list);

    c.list->AddLine(c.at(50, 794), c.at(1550, 794), row_line, 1.2f * c.s);
    float x = 50.0f;
    const auto twice = tr("Click twice");
    c.text(x, 820, 25, accent, twice);
    x += c.width(25, twice) + 30.0f;
    for (const auto& [token, english] : {std::pair{"R3", N_("Freecam")}, std::pair{"L3", N_("Screenshot")}}) {
        x += chip(c, x, 836, 25, token) + 6.0f;
        c.text(x, 822, 23, accent, "\xC3\x97" "2");
        x += c.width(23, "\xC3\x97" "2") + 10.0f;
        const auto f = fit(c, english, 27, 230, false);
        c.text(x, 836 - f.px * 0.56f, f.px, ink, f.lines[0]);
        x += c.width(f.px, f.lines[0]) + 30.0f;
    }
    rich(c, 1550, 820, 26, muted, "Everything else: {L3}+{R3} opens the WuWa VR menu", 1550 - x, 1.0f);
}

// One row of the keyboard card: its key caps ("E/Q" draws two), what they do and an optional state pill.
struct KeyRow {
    const char* keys;
    const char* english;
    bool lit;
    const char* state = nullptr;
    ImU32 state_color = 0;
    bool dim = false;               // does nothing in the current camera
    const char* note = nullptr;     // a smaller second line under the label
    const char* value = nullptr;    // a pill with text already translated (a value), in place of state
};
void key_row(const Canvas& c, float x, float top, float w, const KeyRow& row) {
    constexpr float h = 62.0f, key_px = 27.0f, pill_px = 20.0f;
    const float middle = top + h * 0.5f;
    c.list->AddRectFilled(c.at(x, top), c.at(x + w, top + h), row.lit ? row_lit_bg : row_bg, 8.0f * c.s);
    c.list->AddRect(c.at(x, top), c.at(x + w, top + h), row.lit ? lit_line : row_line, 8.0f * c.s, 0, (row.lit ? 2.5f : 1.0f) * c.s);
    std::vector<std::string> caps;
    for (std::string_view rest{row.keys};;) {
        const auto slash = rest.find('/');
        caps.emplace_back(rest.substr(0, slash));
        if (slash == std::string_view::npos) break;
        rest.remove_prefix(slash + 1);
    }
    const float slash_w = c.width(key_px, "/") + 8.0f;
    float keys_w = 0.0f;
    for (size_t i = 0; i < caps.size(); ++i) keys_w += chip_width(c, key_px, caps[i]) + (i ? slash_w : 0.0f);
    // Left to right: caps, text, pill; right to left mirrored.
    float key_x = c.rtl ? x + w - 16.0f - keys_w : x + 16.0f;
    for (size_t i = 0; i < caps.size(); ++i) {
        if (i) { c.text(key_x + 4.0f, middle - key_px * 0.56f, key_px, muted, "/"); key_x += slash_w; }
        key_x += chip(c, key_x, middle, key_px, caps[i], row.lit);
    }
    const std::string state = row.value ? std::string{row.value} : row.state ? tr(row.state) : std::string{};
    const float state_w = state.empty() ? 0.0f : pill_text_width(c, state, pill_px) + 12.0f;
    const float text_w = w - 32.0f - keys_w - 16.0f - state_w;
    const float text_x = c.rtl ? x + 16.0f + state_w : x + 16.0f + keys_w + 16.0f;
    const float align = c.rtl ? 1.0f : 0.0f;
    if (row.note) {
        draw_fit(c, fit(c, row.english, 26.0f, text_w, false), text_x, middle - 13.0f, row.dim ? muted : ink, align, text_w);
        draw_fit(c, fit(c, row.note, 21.0f, text_w, false), text_x, middle + 15.0f, row.dim ? muted : accent, align, text_w);
    } else {
        draw_fit(c, fit(c, row.english, 28.0f, text_w), text_x, middle, row.dim ? muted : ink, align, text_w);
    }
    if (!state.empty()) pill_text(c, c.rtl ? x + 16.0f : x + w - 16.0f - state_w + 12.0f, middle, pill_px, state, row.state_color);
}

// Hold the VR key (1.2.7): the keyboard version of the L3 and views cards together.
void keyboard_card(const Canvas& c, const CardState& st) {
    header(c, "Hold {VR}, then press", "{Insert} opens the WuWa VR menu");
    using wuwa_keyboard::Action;
    const auto held = [&](Action a) { return (st.keys & (1u << static_cast<int>(a))) != 0; };
    const bool up = (st.keys & wuwa_keyboard::State::up_bit) != 0, down = (st.keys & wuwa_keyboard::State::down_bit) != 0;
    const bool camera_moves = st.first_person || st.fixed_camera;
    std::string nudge;
    if (!st.mouse_mode && st.story_nudge && std::fabs(st.story_nudge_cm) >= 1.0f) {
        char text[48]{};
        std::snprintf(text, sizeof(text), wuwa_l10n::translate("%+.0f cm").c_str(), st.story_nudge_cm);
        nudge = wuwa_l10n::visual(text);
    }
    // E / Q follow the camera in use, as Y / X do on the L3 card; in HUD / mouse mode they move the HUD.
    const char* eq_label = st.mouse_mode ? N_("HUD up / down") : st.story_nudge && !camera_moves ? N_("Story camera: nearer / back") : N_("Camera up / down");
    const char* eq_note = st.mouse_mode ? nullptr : st.story_nudge && !camera_moves ? N_("E + Q together: back to the game's spot")
        : camera_moves ? N_("E + Q together: back to default") : N_("Only in first person, the fixed camera and story scenes");
    const bool wheel_works = st.mouse_mode || st.fixed_camera;
    const KeyRow left[] = {
        {"1", N_("Full VR / stereo screen"), held(Action::full_or_stereo), st.view == 1 ? N_("Stereo screen") : nullptr, green},
        {"2", N_("Mono theatre"), held(Action::mono), st.view == 2 ? N_("On") : nullptr, green},
        {"3", N_("Portal window"), held(Action::portal), st.portal ? N_("On") : nullptr, green},
        {"4", N_("Diorama (10\xC3\x97)"), held(Action::diorama), st.diorama ? N_("On") : nullptr, green},
        {"B", N_("Blur (depth of field)"), held(Action::blur), st.blur ? N_("Blur on") : N_("Blur off"), st.blur ? green : muted},
        {"N", N_("Hide characters at your eyes"), held(Action::hide_near), st.hide_near ? N_("Hiding on") : N_("Hiding off"), st.hide_near ? green : muted},
        {"H", N_("Show / hide game UI"), held(Action::ui), st.ui_hidden ? N_("Hidden") : nullptr, red},
        {"R", N_("Recenter view"), held(Action::recenter)},
        {"P", N_("Screenshot"), held(Action::screenshot)},
    };
    const KeyRow right[] = {
        {"F", N_("First person"), held(Action::first_person), st.first_person ? N_("On") : nullptr, green},
        {"T", N_("Game / fixed camera"), held(Action::fixed_camera), st.fixed_camera ? N_("Fixed") : nullptr, green},
        {"C", N_("Freecam"), held(Action::freecam), st.freecam ? N_("On") : nullptr, green},
        {"G", N_("First person: full animation"), held(Action::animation), nullptr, 0, !st.first_person},
        {"E/Q", eq_label, up || down, nullptr, green, !camera_moves && !st.story_nudge && !st.mouse_mode, eq_note,
            nudge.empty() ? nullptr : nudge.c_str()},
        {"Wheel", st.mouse_mode ? N_("HUD nearer / farther") : N_("Fixed camera closer / farther"), st.wheel_lit, nullptr, 0,
            !wheel_works, wheel_works ? nullptr : N_("In the fixed camera and HUD / mouse mode")},
        {"M", N_("HUD / mouse mode"), held(Action::adjust), st.mouse_mode ? N_("On") : nullptr, green},
        {"K", N_("Keep this card open"), held(Action::pin_card), st.pinned ? N_("Pinned") : nullptr, green},
    };
    constexpr float column_w = 730.0f, top = 132.0f, step = 72.0f;
    // Views and the screen on one side, cameras and the card on the other (mirrored right to left).
    const float first_x = c.rtl ? 820.0f : 50.0f, second_x = c.rtl ? 50.0f : 820.0f;
    for (size_t i = 0; i < std::size(left); ++i) key_row(c, first_x, top + i * step, column_w, left[i]);
    for (size_t i = 0; i < std::size(right); ++i) key_row(c, second_x, top + i * step, column_w, right[i]);
    c.list->AddLine(c.at(50, 796), c.at(1550, 796), row_line, 1.2f * c.s);
    rich(c, c.rtl ? 1550.0f : 50.0f, 818, 26, st.freecam ? ink : muted,
        "Freecam: {W}{A}{S}{D} move, the mouse looks, {Space} / {Ctrl} up / down, {Shift} faster (twice: turbo)", 1500, c.rtl ? 1.0f : 0.0f);
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
        const float x0 = 192.0f + i * 414.0f, x1 = x0 + 330.0f, y0 = now ? 138.0f : 148.0f, y1 = now ? 392.0f : 382.0f;
        if (now) c.list->AddRect(c.at(x0 - 6, y0 - 6), c.at(x1 + 6, y1 + 6), IM_COL32(249, 232, 180, 70), 16.0f * c.s, 0, 5.0f * c.s);
        c.list->AddRectFilled(c.at(x0, y0), c.at(x1, y1), now ? IM_COL32(40, 32, 16, 235) : row_bg, 12.0f * c.s);
        c.list->AddRect(c.at(x0, y0), c.at(x1, y1), now ? lit_line : IM_COL32(214, 178, 98, 64), 12.0f * c.s, 0, (now ? 2.0f : 1.0f) * c.s);
        icon(c, i, x0 + 165.0f, y0 + 68.0f);
        draw_fit(c, fit(c, names[i], 34, 300, false), x0 + 15.0f, y0 + 148.0f, ink, 0.5f, 300.0f);
        draw_fit(c, fit(c, notes[i], 23, 296), x0 + 17.0f, y0 + 204.0f, muted, 0.5f, 296.0f);
        if (now) {
            const float w = pill_width(c, "now", 19.0f);
            pill(c, x0 + 165.0f - w * 0.5f, y0, 19.0f, "now", gold, true);
        }
    }
    // Click R3: full VR <-> stereo screen. Hold R3: mono theatre and back (from either view).
    arrow2(c, 564, 284);
    arrow2(c, 978, 284);
    // The caps fit the 84-unit gap between tiles (VR names are longer); the hold time sits under its arrow.
    const float r3_px = (std::min)(21.0f, 21.0f * 78.0f / chip_width(c, 21, "R3"));
    for (const float x : {564.0f, 978.0f}) chip(c, x - chip_width(c, r3_px, "R3") * 0.5f, 244, r3_px, "R3");
    c.text(978.0f - c.width(19, "0.8 s") * 0.5f, 304, 19, accent, "0.8 s");
    const bool r3_held = (st.buttons & r3) != 0, l3_held = (st.buttons & l3) != 0, y_held = (st.buttons & button_y) != 0;
    const struct { const char* token; const char* extra; const char* english; bool lit; int kind; } moves[]{
        {"Y", nullptr, N_("Press: hide characters at your eyes"), y_held, 2},
        {"L3", nullptr, N_("Click: blur on / off"), l3_held, 1},
        {"R3", nullptr, N_("Click: full VR / stereo screen"), r3_held, 0},
        {"R3", "0.8 s", N_("Hold: mono theatre on / off"), r3_held, 0},
    };
    // The controller with both triggers held at the lower left, drawn over the leaders; the moves at the right.
    ImDrawListSplitter layers;
    layers.Split(c.list, 2);
    layers.SetCurrentChannel(c.list, 1);
    const auto anchors = draw_pad3d(c, 50, 418, 636, 784, wuwa_pad3d::button::LT | wuwa_pad3d::button::RT | pad_bits(st.buttons));
    layers.SetCurrentChannel(c.list, 0);
    constexpr float x0 = 690.0f, x1 = 1550.0f, key_px = 28.0f, extra_px = 21.0f;
    float keys_w = 0.0f;
    for (const auto& m : moves) keys_w = (std::max)(keys_w, chip_width(c, key_px, m.token) + (m.extra ? 8.0f + c.width(extra_px, m.extra) : 0.0f));
    const float text_x = x0 + 22.0f + keys_w + 22.0f;
    for (int i = 0; i < 4; ++i) {
        const auto& m = moves[i];
        const float top = 424.0f + i * 88.0f, middle = top + 37.0f;
        c.list->AddRectFilled(c.at(x0, top), c.at(x1, top + 74), m.lit ? row_lit_bg : row_bg, 9.0f * c.s);
        c.list->AddRect(c.at(x0, top), c.at(x1, top + 74), m.lit ? lit_line : row_line, 9.0f * c.s, 0, (m.lit ? 2.5f : 1.0f) * c.s);
        const float x = x0 + 22.0f + chip(c, x0 + 22.0f, middle, key_px, m.token, m.lit);
        if (m.extra) c.text(x + 8.0f, middle - extra_px * 0.56f, extra_px, accent, m.extra);
        const bool blur = m.kind == 1, hide = m.kind == 2;
        const bool on = blur ? st.blur : st.hide_near;
        const char* state = blur ? (st.blur ? N_("Blur on") : N_("Blur off")) :
            hide ? (st.hide_near ? N_("Hiding on") : N_("Hiding off")) : nullptr;
        const float state_w = state ? pill_width(c, state, 21.0f) + 20.0f : 0.0f;
        const auto f = fit(c, m.english, 30, x1 - text_x - 22.0f - state_w, false);
        c.text(text_x, middle - f.px * 0.56f, f.px, ink, f.lines[0]);
        if (state) pill(c, text_x + c.width(f.px, f.lines[0]) + 20.0f, middle, 21.0f, state, on ? green : muted);
        draw_leader(c, anchors, m.token, x0, middle, false, m.lit);
    }
    layers.Merge(c.list);
    c.list->AddLine(c.at(50, 800), c.at(1550, 800), row_line, 1.2f * c.s);
    rich(c, 1550, 822, 26, muted, "Keep both triggers held to click or hold {R3} again.", 1500, 1.0f);
}

void centered_text(const Canvas& c, ImVec2 size, float y, float px, ImU32 color, const std::string& value) {
    const float initial = c.font->CalcTextSizeA(px * c.s, FLT_MAX, 0.0f, value.c_str()).x;
    const float fitted = px * c.s * (std::min)(1.0f, 1350.0f * c.s / (std::max)(initial, 1.0f));
    const float w = c.font->CalcTextSizeA(fitted, FLT_MAX, 0.0f, value.c_str()).x;
    c.list->AddText(c.font, fitted, {size.x * 0.5f - w * 0.5f, c.origin.y + y * c.s}, color, value.c_str());
}
void centered(const Canvas& c, ImVec2 size, float y, float px, ImU32 color, const char* english) {
    centered_text(c, size, y, px, color, wuwa_l10n::text(english));
}
// English with one %s for the VR key's name: formatted after translating, then put in visual order.
// A one-character name (` on US / UK layouts, ^ on German) is easy to miss in a line of text, so it shows as [`].
void centered_key(const Canvas& c, ImVec2 size, float y, float px, ImU32 color, const char* english) {
    const auto lead = static_cast<unsigned char>(vr_key_label.empty() ? 0 : vr_key_label[0]);
    const size_t one_character = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
    const std::string name = vr_key_label.size() == one_character ? "[" + vr_key_label + "]" : vr_key_label;
    char text[256]{};
    std::snprintf(text, sizeof(text), wuwa_l10n::translate(english).c_str(), name.c_str());
    centered_text(c, size, y, px, color, wuwa_l10n::visual(text));
}
}

void reserve_font(ImFontAtlas* atlas) {
    owner = atlas;
    fallback = atlas->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, 32.0f);
}

void set_key_names(const std::string& vr_key, const std::array<std::string, 4>& move) {
    if (!vr_key.empty()) vr_key_label = vr_key;
    move_labels = move;
}

void draw_card(ImDrawList* draw, ImVec2 size, ImFont* font, const CardState& state) {
    const bool keyboard = state.card == Card::keyboard;
    const auto c = canvas(draw, size, font, state.vr_controllers, keyboard, state.playstation);
    if (!c.font) return;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    frame(c);
    if (keyboard) keyboard_card(c, state);
    else if (state.card == Card::views) views_card(c, state);
    else l3_card(c, state);
    draw->PopClipRect();
}

void draw_tip(ImDrawList* draw, ImVec2 size, ImFont* font, const std::string& lead, const std::string& line, bool vr_controllers,
    const std::string& note, bool keyboard, bool playstation) {
    const auto c = canvas(draw, size, font, vr_controllers, keyboard, playstation);
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

void draw_hidden_ui_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool shortcut_available, bool menu_detected, bool keyboard) {
    const auto c = canvas(draw, size, font);
    if (!c.font) return;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    if (!menu_detected) {
        // Controller-operated menus may never show the game's mouse cursor.
        // Keep a smaller recovery hint while HUD hiding is active, so absence
        // of that cursor signal cannot strand the player in a blurred menu.
        draw->AddRectFilled(c.at(175, 535), c.at(1425, 680), IM_COL32(55, 8, 12, 232), 18 * c.s);
        centered(c, size, 552, 42, IM_COL32(255, 100, 104, 255), "GAME UI HIDDEN - MENUS ARE HIDDEN TOO");
        if (shortcut_available && keyboard) centered_key(c, size, 609, 34, IM_COL32(255, 255, 255, 255), "%s + H  =  SHOW UI");
        else centered(c, size, 609, 34, IM_COL32(255, 255, 255, 255), shortcut_available ? "L3 + B  =  SHOW UI" : "INSERT > Home > Game UI");
        draw->PopClipRect();
        return;
    }
    draw->AddRectFilled(c.at(100, 230), c.at(1500, 670), IM_COL32(68, 8, 12, 250), 28 * c.s);
    draw->AddRect(c.at(100, 230), c.at(1500, 670), IM_COL32(255, 65, 72, 255), 28 * c.s, 0, 5 * c.s);
    centered(c, size, 269, 84, IM_COL32(255, 90, 96, 255), "GAME UI IS HIDDEN");
    centered(c, size, 373, 39, IM_COL32(255, 235, 236, 255), "Show UI to see this menu");
    if (shortcut_available && keyboard) {
        centered_key(c, size, 454, 52, IM_COL32(255, 255, 255, 255), "%s + H  =  SHOW UI");
        centered_key(c, size, 523, 28, IM_COL32(255, 206, 210, 255), "Hold %s, then press H.");
    } else {
        centered(c, size, 454, 52, IM_COL32(255, 255, 255, 255), shortcut_available ? "L3 + B  =  SHOW UI" : "INSERT  =  WUWA VR MENU");
        centered(c, size, 523, 28, IM_COL32(255, 206, 210, 255), shortcut_available ? "L3 means click and hold the left stick, then press B." : "Then Home > Game UI");
    }
    centered(c, size, 601, 26, IM_COL32(255, 206, 210, 255), shortcut_available ? "Or: WuWa VR menu (L3 + R3 or Insert) > Home > Game UI" : "Polar shortcuts are disabled or bypassed.");
    draw->PopClipRect();
}

void draw_recording_stopped_warning(ImDrawList* draw, ImVec2 size, ImFont* font, int min_fps, int seconds) {
    const auto c = canvas(draw, size, font);
    if (!c.font) return;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    draw->AddRectFilled(c.at(110, 200), c.at(1490, 520), IM_COL32(68, 8, 12, 250), 26 * c.s);
    draw->AddRect(c.at(110, 200), c.at(1490, 520), IM_COL32(255, 65, 72, 255), 26 * c.s, 0, 5 * c.s);
    centered(c, size, 228, 70, IM_COL32(255, 90, 96, 255), "RECORDING STOPPED");
    char line[256]{};
    std::snprintf(line, sizeof(line), wuwa_l10n::translate("The game ran below %d fps for %d seconds.").c_str(), min_fps, seconds);
    centered_text(c, size, 322, 38, IM_COL32(255, 255, 255, 255), wuwa_l10n::visual(line));
    centered(c, size, 385, 28, IM_COL32(255, 220, 222, 255), "What was recorded is saved.");
    centered(c, size, 432, 26, IM_COL32(255, 206, 210, 255), "Lower the SteamVR resolution for WuWa, or use the stereo screen for a while.");
    draw->PopClipRect();
}

void draw_mouse_mode_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool adjustment, bool hidden_menu, bool keyboard) {
    const auto c = canvas(draw, size, font);
    if (!c.font) return;
    const float top = hidden_menu ? 700.0f : 590.0f;
    draw->PushClipRect(c.at(0, 0), c.at(1600, 900), true);
    draw->AddRectFilled(c.at(90, top), c.at(1510, top + 180), IM_COL32(43, 31, 7, 246), 18 * c.s);
    draw->AddRect(c.at(90, top), c.at(1510, top + 180), IM_COL32(255, 192, 60, 255), 18 * c.s, 0, 3 * c.s);
    centered(c, size, top + 15, 43, IM_COL32(255, 208, 99, 255), "HUD / MOUSE MODE ON");
    if (adjustment && keyboard) {
        centered_key(c, size, top + 70, 36, IM_COL32(255, 255, 255, 255), "%s + M  =  RETURN TO GAME CONTROLS");
        centered(c, size, top + 124, 27, IM_COL32(245, 224, 181, 255), "Or: WuWa VR menu > Interface > HUD / mouse mode: Off");
    } else {
        centered(c, size, top + 70, 36, IM_COL32(255, 255, 255, 255), adjustment ? "L3 + LB  =  RETURN TO GAME CONTROLS" : "WuWa VR menu > Interface > HUD / mouse mode: Off");
        centered(c, size, top + 124, 27, IM_COL32(245, 224, 181, 255), adjustment ? "Click left stick + left bumper, then release all controls." : "Activated automatically in this menu: cursor only. L3 + R3 opens the WuWa VR menu.");
    }
    draw->PopClipRect();
}
}
