#include "WuWaVrMenu.hpp"

#include <imgui.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <vector>

#include "WuWaBrand.hpp"
#include "WuWaLocalization.hpp"
#include "WuWaPad3D.hpp"
#include "WuWaStepPlan.hpp"

namespace wuwa_menu {
namespace {
// Black and gold, matching the launcher and the site.
constexpr ImU32 backdrop = IM_COL32(10, 10, 14, 244);
constexpr ImU32 header = IM_COL32(20, 18, 13, 255);
constexpr ImU32 row = IM_COL32(26, 25, 22, 240);
constexpr ImU32 row_focus = IM_COL32(48, 41, 25, 250);
constexpr ImU32 gold = IM_COL32(230, 186, 88, 255);
constexpr ImU32 gold_bright = IM_COL32(255, 216, 128, 255);
constexpr ImU32 gold_dim = IM_COL32(112, 90, 46, 255);
constexpr ImU32 ink = IM_COL32(243, 237, 224, 255);
constexpr ImU32 ink_dark = IM_COL32(24, 18, 6, 255);
constexpr ImU32 muted = IM_COL32(172, 162, 142, 255);
constexpr ImU32 faint = IM_COL32(104, 99, 90, 255);
constexpr ImU32 track = IM_COL32(64, 61, 56, 255);
constexpr ImU32 help_ink = IM_COL32(222, 212, 192, 255);
constexpr ImU32 status_ink = IM_COL32(236, 204, 140, 255);

// Design space: 1200 x 780 (the host keeps that shape). The page rail runs down the left; rows and the help
// strip fill the column to its right. Rows are 62 tall with a 6 gap: seven fit above a help strip tall enough for
// four lines of help at a readable size, the shortcut's steps and the 3D controller.
constexpr float width = 1200.0f, header_h = 80.0f, help_h = 196.0f, row_h = 62.0f, row_gap = 6.0f;
constexpr float left = 28.0f, right = 1172.0f;              // the setup uses the full width
constexpr float rail_left = 20.0f, rail_right = 212.0f;
constexpr float content_left = 234.0f;
constexpr float widget_right = 1148.0f, widget_left = 812.0f;
constexpr float shortcut_w = 330.0f;                          // the 3D controller in the help strip

std::string tr(const std::string& english) { return wuwa_l10n::text(english); }

std::string value_of(const Item& item) {
    char buffer[64]{};
    switch (item.kind) {
    case Kind::Choice:
        if (item.get_int && !item.choices.empty()) {
            const int index = std::clamp(item.get_int(), 0, static_cast<int>(item.choices.size()) - 1);
            return item.choice_text ? item.choice_text(index) : tr(item.choices[index]);
        }
        return {};
    case Kind::Slider:
        // The unit (cm, m, deg, x) is translated with its format; the loader keeps only
        // translations whose specifiers match. Format the logical text, then shape it.
        if (item.get_float) std::snprintf(buffer, sizeof(buffer), wuwa_l10n::translate(item.format).c_str(), item.get_float());
        return wuwa_l10n::visual(buffer);
    case Kind::Toggle: {
        auto word = item.value_text ? item.value_text() : std::string{}; // live state, e.g. "REC 1:23"
        if (word.empty()) word = tr(item.get_bool && item.get_bool() ? "On" : "Off");
        return word;
    }
    default:
        return item.value_text ? item.value_text() : std::string{};
    }
}

// Cut at the end (on a character boundary) with "..." to fit.
std::string clip(ImFont* font, float px, std::string text, float room) {
    if (font->CalcTextSizeA(px, FLT_MAX, 0.0f, text.c_str()).x <= room) return text;
    while (!text.empty() && font->CalcTextSizeA(px, FLT_MAX, 0.0f, (text + "...").c_str()).x > room) {
        unsigned char last{};
        do { last = static_cast<unsigned char>(text.back()); text.pop_back(); } while (!text.empty() && (last & 0xC0) == 0x80);
    }
    return text + "...";
}

// The crest and the name at the left of a header 80 tall; returns where they end (design units).
float draw_brand(ImDrawList* list, ImFont* font, ImVec2 pos, float s, const std::string& subtitle = {}) {
    constexpr float crest_h = 60.0f;
    float x = left - 8.0f;
    const float crest_w = wuwa_brand::draw_crest(list, ImVec2{pos.x + x * s, pos.y + (header_h - crest_h) * 0.5f * s}, crest_h * s) / s;
    x += crest_w > 0.0f ? crest_w + 10.0f : 8.0f;
    list->AddText(font, 34.0f * s, ImVec2{pos.x + x * s, pos.y + 20.0f * s}, gold, "WuWa VR");
    x += font->CalcTextSizeA(34.0f * s, FLT_MAX, 0.0f, "WuWa VR").x / s;
    if (!subtitle.empty()) {
        x += 20.0f;
        list->AddText(font, 26.0f * s, ImVec2{pos.x + x * s, pos.y + 28.0f * s}, muted, subtitle.c_str());
        x += font->CalcTextSizeA(26.0f * s, FLT_MAX, 0.0f, subtitle.c_str()).x / s;
    }
    return x;
}

// The row's shortcuts available on (or shown for) the device used last, in order.
struct Playing { const Item::Keys* keys{}; wuwa_pad3d::Device device{}; const wuwa_pad3d::Beats* beats{}; };
std::vector<Playing> playable(const Item& item, wuwa_pad3d::Device last) {
    std::vector<Playing> out;
    for (const auto& keys : item.shortcuts) {
        const auto device = wuwa_pad3d::shown_on(keys.shortcut, last);
        const auto& beats = keys.shortcut.beats(device);
        if (!beats.empty()) out.push_back({&keys, device, &beats});
    }
    return out;
}
}

void draw(std::vector<Page>& pages, State& state, const Moves& moves, const ImVec2& pos, const ImVec2& size, const Look& look) {
    apply_moves(pages, state, moves);
    if (pages.empty()) return;
    auto& io = ImGui::GetIO();
    auto* list = ImGui::GetWindowDrawList();
    ImFont* font = look.font ? look.font : ImGui::GetFont();
    const float s = size.x / width * look.scale;
    const float height = size.y / s;
    const auto at = [&](float x, float y) { return ImVec2{pos.x + x * s, pos.y + y * s}; };
    const auto measure = [&](float px, const std::string& text) { return font->CalcTextSizeA(px * s, FLT_MAX, 0.0f, text.c_str()); };
    const auto write = [&](ImVec2 where, float px, ImU32 color, const std::string& text, float wrap = 0.0f) {
        list->AddText(font, px * s, where, color, text.c_str(), nullptr, wrap * s);
    };
    // Text at up to px, smaller (to min_px) to fit room, then cut.
    const auto write_fit = [&](float x, float middle, float px, float min_px, ImU32 color, std::string text, float room, float align = 0.0f) {
        float size_px = px, w = measure(size_px, text).x / s;
        if (w > room && w > 0.0f) { size_px = (std::max)(min_px, px * room / w); w = measure(size_px, text).x / s; }
        if (w > room) { text = clip(font, size_px * s, text, room * s); w = measure(size_px, text).x / s; }
        write(at(x + (room - w) * align, middle - size_px * 0.5f), size_px, color, text);
    };
    const auto button = [&](const char* id, ImVec2 min, ImVec2 max) {
        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton(id, ImVec2{(std::max)(max.x - min.x, 1.0f), (std::max)(max.y - min.y, 1.0f)});
    };

    // A pointer that moves, clicks or scrolls takes over from the buttons.
    const float moved = io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y;
    if (ImGui::IsWindowHovered() && (moved > 9.0f * s * s || io.MouseClicked[0] || io.MouseWheel != 0.0f)) {
        state.source = Source::Pointer;
    }
    const bool pointer = state.source == Source::Pointer;

    list->AddRectFilled(pos, ImVec2{pos.x + size.x, pos.y + size.y}, backdrop, 20.0f * s);
    list->AddRect(pos, ImVec2{pos.x + size.x, pos.y + size.y}, gold_dim, 20.0f * s, 0, 2.0f * s);

    // Header: the crest and name, how to drive the menu, a badge (REC, NOT SAVING), close.
    list->AddRectFilled(pos, at(width, header_h), header, 20.0f * s, ImDrawFlags_RoundCornersTop);
    const float brand_end = draw_brand(list, font, pos, s);
    const float close_left = right - 52.0f;
    float limit = close_left - 18.0f;
    if (!look.badge.empty()) {
        const float badge_w = measure(24.0f, look.badge).x / s + 34.0f;
        const float bx = close_left - 18.0f - badge_w;
        list->AddCircleFilled(at(bx + 10.0f, 40.0f), 9.0f * s, IM_COL32(235, 64, 64, 255));
        write(at(bx + 28.0f, 28.0f), 24.0f, IM_COL32(255, 120, 120, 255), look.badge);
        limit = bx - 18.0f;
    }
    {
        using wuwa_pad3d::Device;
        const auto hint = tr(pointer ? "Point and pull the trigger, or click. Scroll to see more."
            : look.device == Device::keyboard ? "Enter select  ·  Left / right change  ·  Tab pages  ·  Esc close"
            : look.device == Device::playstation ? "Cross select  ·  Left / right change  ·  L1 / R1 pages  ·  Circle close"
            : look.device == Device::vr ? "A select  ·  Left / right change  ·  Grips pages  ·  B close"
                                        : "A select  ·  Left / right change  ·  LB / RB pages  ·  B close");
        const float room = limit - brand_end - 36.0f;
        if (room > 120.0f) write_fit(brand_end + 36.0f, 40.0f, 22.0f, 17.0f, gold_dim, hint, room, 1.0f);
    }
    {
        const auto min = at(close_left, 14.0f), max = at(right, 66.0f);
        button("close", min, max);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) state.close_requested = true;
        list->AddRect(min, max, hovered ? gold : gold_dim, 10.0f * s, 0, 2.0f * s);
        const float inset = 16.0f * s;
        list->AddLine(ImVec2{min.x + inset, min.y + inset}, ImVec2{max.x - inset, max.y - inset}, hovered ? gold_bright : muted, 3.0f * s);
        list->AddLine(ImVec2{max.x - inset, min.y + inset}, ImVec2{min.x + inset, max.y - inset}, hovered ? gold_bright : muted, 3.0f * s);
    }

    // The page rail: every page at full size, LB / RB above and below it while buttons drive the menu.
    {
        const int count = static_cast<int>(pages.size());
        float top = header_h + 16.0f, bottom = height - 16.0f;
        const bool hints = !pointer && look.device != wuwa_pad3d::Device::keyboard;
        if (hints) { top += 30.0f; bottom -= 30.0f; }
        const float slot = (std::min)(70.0f, (bottom - top) / (std::max)(count, 1)), tab_h = slot - 8.0f;
        if (hints) {
            const char* up = look.device == wuwa_pad3d::Device::playstation ? "L1" : look.device == wuwa_pad3d::Device::vr ? "L grip" : "LB";
            const char* down = look.device == wuwa_pad3d::Device::playstation ? "R1" : look.device == wuwa_pad3d::Device::vr ? "R grip" : "RB";
            write_fit(rail_left + 14.0f, top - 18.0f, 21.0f, 16.0f, muted, look.device == wuwa_pad3d::Device::vr ? tr(up) : up, rail_right - rail_left - 28.0f);
            write_fit(rail_left + 14.0f, top + count * slot + 10.0f, 21.0f, 16.0f, muted, look.device == wuwa_pad3d::Device::vr ? tr(down) : down,
                rail_right - rail_left - 28.0f);
        }
        for (int index = 0; index < count; ++index) {
            const float y = top + index * slot;
            const auto min = at(rail_left, y), max = at(rail_right, y + tab_h);
            ImGui::PushID(index);
            button("tab", min, max);
            if (ImGui::IsItemClicked()) set_page(pages, state, index);
            const bool current = index == state.page;
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();
            if (current) {
                list->AddRectFilled(min, max, row_focus, 10.0f * s);
                list->AddRectFilled(at(rail_left, y + 8.0f), at(rail_left + 5.0f, y + tab_h - 8.0f), gold, 2.5f * s);
            } else if (hovered) {
                list->AddRect(min, max, gold_dim, 10.0f * s, 0, 2.0f * s);
            }
            write_fit(rail_left + 18.0f, y + tab_h * 0.5f, 25.0f, 16.0f, current ? gold_bright : hovered ? ink : muted, tr(pages[index].title),
                rail_right - rail_left - 30.0f);
        }
        list->AddLine(at(rail_right + 11.0f, header_h + 16.0f), at(rail_right + 11.0f, height - 16.0f), IM_COL32(112, 90, 46, 120), 1.5f * s);
    }

    auto& page = pages[state.page];
    const float top = header_h + 12.0f, strip = height - help_h, bottom = strip - 8.0f, area = bottom - top;
    const auto content_min = at(content_left, top), content_max = at(width, bottom);
    const bool over_content = ImGui::IsWindowHovered() && io.MousePos.y >= content_min.y && io.MousePos.y < content_max.y &&
        io.MousePos.x >= content_min.x;
    const float mouse_x = (io.MousePos.x - pos.x) / s;
    if (!io.MouseDown[0]) state.dragging = -1;

    if (page.tiles) {
        // Home: big tiles for what you switch while playing, readable at arm's length. A press changes one.
        const int count = static_cast<int>(page.items.size());
        const int rows = (std::max)(1, (count + tile_columns - 1) / tile_columns);
        constexpr float gap = 14.0f;
        const float tile_w = (right - content_left - gap * (tile_columns - 1)) / tile_columns;
        const float tile_h = (std::min)(232.0f, (area - gap * (rows - 1)) / rows);
        for (int index = 0; index < count; ++index) {
            auto& item = page.items[index];
            if (!shown(item)) continue;
            const float x = content_left + (index % tile_columns) * (tile_w + gap), y = top + (index / tile_columns) * (tile_h + gap);
            const auto min = at(x, y), max = at(x + tile_w, y + tile_h);
            const bool enabled = !item.enabled || item.enabled();
            ImGui::PushID(state.page * 1000 + index);
            button("tile", min, max);
            const bool hovered = ImGui::IsItemHovered() && enabled;
            const bool clicked = ImGui::IsItemClicked() && enabled;
            ImGui::PopID();
            if (pointer && hovered) state.focus = index;
            const bool focused = state.focus == index && (!pointer || hovered);
            if (clicked) activate(item);
            const bool on = item.kind == Kind::Toggle && item.get_bool && item.get_bool();
            list->AddRectFilled(min, max, focused ? row_focus : row, 16.0f * s);
            if (focused) list->AddRect(min, max, gold, 16.0f * s, 0, 3.0f * s);
            else if (on) list->AddRect(min, max, gold_dim, 16.0f * s, 0, 2.0f * s);
            // Label, the value large, and the shortcut on the device used last.
            write_fit(x + 20.0f, y + 32.0f, 25.0f, 17.0f, enabled ? (focused ? ink : muted) : faint, tr(item.label), tile_w - 40.0f);
            auto value = value_of(item);
            if (value.empty() && item.kind == Kind::Action) value = tr("Now");
            write_fit(x + 20.0f, y + tile_h * 0.5f + 2.0f, 36.0f, 22.0f, !enabled ? faint : (item.kind != Kind::Toggle || on) ? gold_bright : ink,
                value, tile_w - 40.0f);
            if (item.kind == Kind::Toggle) {
                const auto dot = at(x + tile_w - 26.0f, y + 32.0f);
                if (on) list->AddCircleFilled(dot, 8.0f * s, gold_bright, 20);
                else list->AddCircle(dot, 8.0f * s, gold_dim, 20, 2.0f * s);
            }
            const auto shortcuts = playable(item, look.device);
            if (!shortcuts.empty()) {
                wuwa_pad3d::draw_keys(list, font, at(x + 20.0f, y + tile_h - 50.0f), 21.0f * s, (tile_w - 40.0f) * s, shortcuts[0].device,
                    *shortcuts[0].beats);
            } else if (item.session_only) {
                write_fit(x + 20.0f, y + tile_h - 30.0f, 20.0f, 16.0f, faint, tr("This launch only"), tile_w - 40.0f);
            }
        }
    } else {
        // Rows, scrolled so a button-focused row is always in view.
        const auto text_of = [&](const Item& item) {
            auto text = item.label.empty() ? std::string{} : tr(item.label);
            const auto extra = item.value_text ? item.value_text() : std::string{};
            if (!extra.empty()) text += (text.empty() ? "" : item.wrap ? "\n" : "  ") + extra;
            return text;
        };
        const float paragraph_px = 25.0f, paragraph_wrap = right - content_left - 16.0f;
        std::vector<float> tops, heights;
        float content = 0.0f;
        for (const auto& item : page.items) {
            float h = row_h;
            if (!shown(item)) h = 0.0f;
            else if (item.kind == Kind::Text) {
                const auto text = text_of(item);
                if (text.empty()) h = 0.0f; // an empty status line takes no space
                else if (item.wrap) h = font->CalcTextSizeA(paragraph_px * s, FLT_MAX, paragraph_wrap * s, text.c_str()).y / s + 14.0f;
                else h = item.emphasis ? 52.0f : 44.0f;
            }
            tops.push_back(content);
            heights.push_back(h);
            if (h > 0.0f) content += h + row_gap;
        }
        content = (std::max)(0.0f, content - row_gap);
        if (over_content && io.MouseWheel != 0.0f) state.scroll -= io.MouseWheel * (row_h + row_gap);
        if (!pointer && state.focus >= 0 && state.focus < static_cast<int>(tops.size())) {
            const float row_top = tops[state.focus], row_bottom = row_top + heights[state.focus];
            // Keep the paragraph just above the focused row in view too, when it fits.
            const float lead = state.focus > 0 && page.items[state.focus - 1].kind == Kind::Text && heights[state.focus - 1] > 0.0f ?
                tops[state.focus - 1] : row_top;
            if (lead < state.scroll && row_bottom - lead <= area) state.scroll = lead;
            if (row_top < state.scroll) state.scroll = row_top;
            if (row_bottom > state.scroll + area) state.scroll = row_bottom - area;
        }
        state.scroll = std::clamp(state.scroll, 0.0f, (std::max)(0.0f, content - area));

        list->PushClipRect(content_min, content_max, true);
        for (int index = 0; index < static_cast<int>(page.items.size()); ++index) {
            auto& item = page.items[index];
            const float y = top + tops[index] - state.scroll, h = heights[index];
            if (h <= 0.0f || y + h < top || y > bottom) continue;
            const bool enabled = !item.enabled || item.enabled();
            const auto min = at(content_left, y), max = at(right, y + row_h);
            const float mid = y + row_h * 0.5f;

            if (item.kind == Kind::Text) {
                if (item.wrap) write(at(content_left + 8.0f, y + 6.0f), paragraph_px, help_ink, text_of(item), paragraph_wrap);
                else if (item.emphasis) write(at(content_left + 8.0f, y + h * 0.5f - 16.0f), 32.0f, gold, text_of(item));
                else write(at(content_left + 8.0f, y + h * 0.5f - 12.0f), 24.0f, muted, text_of(item));
                continue;
            }

            ImGui::PushID(state.page * 1000 + index);
            button("row", min, max);
            const bool hovered = ImGui::IsItemHovered() && over_content && enabled;
            const bool clicked = ImGui::IsItemClicked() && over_content && enabled;
            ImGui::PopID();
            if (pointer && hovered) state.focus = index;
            const bool focused = state.focus == index && (!pointer || hovered || state.dragging == index);

            list->AddRectFilled(min, max, focused ? row_focus : row, 12.0f * s);
            if (focused) list->AddRect(min, max, gold, 12.0f * s, 0, 3.0f * s);
            // The label shrinks to fit beside the control (long translations), then wraps onto two lines.
            {
                const float room = widget_left - 24.0f - (content_left + 22.0f);
                const auto label = tr(item.label);
                const float natural = measure(30.0f, label).x / s;
                const ImU32 color = enabled ? ink : faint;
                if (natural <= room * 1.33f) {
                    write_fit(content_left + 22.0f, mid, 30.0f, 22.0f, color, label, room);
                } else {
                    const float wrap_w = room * s;
                    const float px = 23.0f;
                    const float tall = font->CalcTextSizeA(px * s, FLT_MAX, wrap_w, label.c_str()).y / s;
                    write(at(content_left + 22.0f, mid - (std::min)(tall, 56.0f) * 0.5f), px, color, label, room);
                }
            }

            switch (item.kind) {
            case Kind::Toggle: {
                const bool on = item.get_bool && item.get_bool();
                if (clicked) activate(item);
                const auto t0 = at(widget_right - 96.0f, mid - 22.0f), t1 = at(widget_right, mid + 22.0f);
                list->AddRectFilled(t0, t1, !enabled ? track : on ? gold : track, 22.0f * s);
                const float knob = on ? widget_right - 22.0f : widget_right - 74.0f;
                list->AddCircleFilled(at(knob, mid), 17.0f * s, on ? ink_dark : ink);
                write_fit(widget_left - 10.0f, mid, 24.0f, 17.0f, on ? gold_bright : muted, value_of(item), widget_right - 112.0f - widget_left + 10.0f, 1.0f);
                break;
            }
            case Kind::Choice: {
                const int last = static_cast<int>(item.choices.size()) - 1;
                const int current = item.get_int ? item.get_int() : 0;
                if (clicked) {
                    if (mouse_x < widget_left + 56.0f) nudge(item, -1);
                    else if (mouse_x > widget_right - 56.0f) nudge(item, +1);
                    else activate(item);
                }
                const auto arrow = [&](float x0, bool live, bool pointing_left) {
                    const auto a = at(x0, mid - 22.0f), b = at(x0 + 44.0f, mid + 22.0f);
                    list->AddRect(a, b, live ? gold : gold_dim, 10.0f * s, 0, 2.0f * s);
                    const float cx = x0 + 22.0f, d = pointing_left ? 6.0f : -6.0f;
                    list->AddTriangleFilled(at(cx - d, mid), at(cx + d, mid - 9.0f), at(cx + d, mid + 9.0f), live ? gold_bright : faint);
                };
                arrow(widget_left, enabled && current > 0, true);
                arrow(widget_right - 44.0f, enabled && current < last, false);
                // The value fits between the arrows: smaller type for a long one (a device name, a long translation),
                // then cut at the end.
                constexpr float room = widget_right - widget_left - 2.0f * 44.0f - 20.0f;
                write_fit(widget_left + 54.0f, mid, 26.0f, 18.0f, enabled ? gold_bright : faint, value_of(item), room, 0.5f);
                break;
            }
            case Kind::Slider: {
                const float bar0 = widget_left, bar1 = widget_right - 92.0f;
                if (clicked && mouse_x >= bar0 - 12.0f && mouse_x <= bar1 + 12.0f) state.dragging = index;
                if (state.dragging == index && enabled && item.set_float) {
                    const float t = std::clamp((mouse_x - bar0) / (bar1 - bar0), 0.0f, 1.0f);
                    item.set_float(snap(item, item.min + t * (item.max - item.min)));
                }
                const float value = item.get_float ? item.get_float() : item.min;
                const float t = item.max > item.min ? std::clamp((value - item.min) / (item.max - item.min), 0.0f, 1.0f) : 0.0f;
                list->AddRectFilled(at(bar0, mid - 5.0f), at(bar1, mid + 5.0f), track, 5.0f * s);
                list->AddRectFilled(at(bar0, mid - 5.0f), at(bar0 + t * (bar1 - bar0), mid + 5.0f), enabled ? gold : faint, 5.0f * s);
                list->AddCircleFilled(at(bar0 + t * (bar1 - bar0), mid), 14.0f * s, enabled ? gold_bright : faint);
                write_fit(bar1 + 14.0f, mid, 24.0f, 17.0f, enabled ? ink : faint, value_of(item), widget_right - bar1 - 14.0f, 1.0f);
                break;
            }
            case Kind::Action: {
                if (clicked) activate(item);
                const auto text = value_of(item);
                if (!text.empty()) write_fit(widget_left - 40.0f, mid, 24.0f, 17.0f, muted, text, widget_right - 40.0f - widget_left + 40.0f, 1.0f);
                const float cx = widget_right - 14.0f;
                list->AddTriangleFilled(at(cx - 6.0f, mid - 10.0f), at(cx + 6.0f, mid), at(cx - 6.0f, mid + 10.0f), enabled ? gold : faint);
                break;
            }
            default:
                break;
            }
        }
        list->PopClipRect();
        if (content > area) {
            const float bar = area * area / content, offset = (area - bar) * state.scroll / (content - area);
            list->AddRectFilled(at(right + 8.0f, top + offset), at(right + 14.0f, top + offset + bar), gold_dim, 3.0f * s);
        }
    }
    settle_focus(page, state);

    // Help strip: the focused row's explanation, its live status, whether it is saved, and its shortcuts
    // playing on a live 3D controller (the device used last).
    list->AddLine(at(content_left, strip), at(right, strip), gold_dim, 2.0f * s);
    const Item* focused = state.focus >= 0 && state.focus < static_cast<int>(page.items.size()) ? &page.items[state.focus] : nullptr;
    if (!focused) return;
    const auto shortcuts = playable(*focused, look.device);
    const float text_right = shortcuts.empty() ? right : right - shortcut_w - 22.0f;
    // From the bottom: the default line, the live status, the shortcut's steps; the help fills the rest, smaller when long.
    std::string note;
    if (focused->session_only) {
        note = tr("This launch only: not saved.");
    } else if (focused->default_text) {
        // Format the logical text, then shape it once: right-to-left shaping reorders the
        // characters, so a shaped format could move or break its %s. The loader keeps a
        // translation only when its specifiers match the English (valid_translation).
        char text[256]{};
        std::snprintf(text, sizeof(text), wuwa_l10n::translate("Default: %s. Saved for next time.").c_str(),
            wuwa_l10n::translate_quiet(focused->default_text()).c_str());
        std::string logical = text;
        // Also with a pointer: Delete (keyboard) or X (Xbox / VR controller) resets the row under it.
        if (focused->is_default && !focused->is_default()) {
            logical += "  " + wuwa_l10n::translate(look.device == wuwa_pad3d::Device::playstation ? "Square or Delete: back to the default."
                : "X or Delete: back to the default.");
        }
        note = wuwa_l10n::visual(logical);
    }
    float text_bottom = height - 12.0f;
    if (!note.empty()) {
        write_fit(content_left, height - 26.0f, 21.0f, 16.0f, gold_dim, note, text_right - content_left);
        text_bottom = height - 42.0f;
    }
    const auto status = focused->status ? focused->status() : std::string{};
    if (!status.empty()) {
        write_fit(content_left, text_bottom - 13.0f, 23.0f, 17.0f, status_ink, status, text_right - content_left);
        text_bottom -= 32.0f;
    }
    if (!shortcuts.empty()) {
        // Several shortcuts play one after another, each a whole loop.
        double total = 0.0;
        for (const auto& entry : shortcuts) total += wuwa_pad3d::loop_seconds(*entry.beats);
        double t = std::fmod((std::max)(look.time, 0.0), (std::max)(total, 0.1));
        size_t which = 0;
        for (; which + 1 < shortcuts.size(); ++which) {
            const double length = wuwa_pad3d::loop_seconds(*shortcuts[which].beats);
            if (t < length) break;
            t -= length;
        }
        const auto& entry = shortcuts[which];
        const float x0 = right - shortcut_w;
        list->AddRectFilled(at(x0, strip + 8.0f), at(right, height - 8.0f), IM_COL32(8, 8, 10, 200), 12.0f * s);
        list->AddRect(at(x0, strip + 8.0f), at(right, height - 8.0f), IM_COL32(112, 90, 46, 110), 12.0f * s, 0, 1.5f * s);
        for (size_t i = 0; i < shortcuts.size() && shortcuts.size() > 1; ++i) {
            const auto dot = at(right - 16.0f - (shortcuts.size() - 1 - i) * 14.0f, strip + 22.0f);
            if (i == which) list->AddCircleFilled(dot, 4.5f * s, gold_bright, 12);
            else list->AddCircle(dot, 4.0f * s, gold_dim, 12, 1.5f * s);
        }
        // The beats run on this shortcut's own clock; the slow turn on the menu's, so it never jumps.
        const auto now = wuwa_pad3d::moment(*entry.beats, t);
        wuwa_pad3d::draw_moment(list, font, at(x0 + 8.0f, strip + 12.0f), at(right - 8.0f, height - 12.0f), entry.device, now, look.time);
        // Its steps on a line in the text column, after its name when it has one: "Nearer:  Hold [L3] > then press [Y]".
        const float middle = text_bottom - 16.0f;
        float x = content_left;
        if (!entry.keys->label.empty()) {
            const auto name = tr(entry.keys->label) + ":";
            write_fit(x, middle, 22.0f, 16.0f, gold, name, 220.0f);
            x += (std::min)(220.0f, measure(22.0f, name).x / s) + 12.0f;
        }
        const ImVec2 line = at(x, middle - 22.0f * 1.16f * 0.5f); // the caption line is 1.16 of its size tall
        wuwa_pad3d::draw_captions(list, font, line, 22.0f * s, (text_right - x) * s, entry.device, *entry.beats, now.beat, false);
        text_bottom -= 40.0f;
    }
    if (!focused->help.empty() || !focused->help_more.empty()) {
        auto help = focused->help.empty() ? std::string{} : tr(focused->help);
        if (!focused->help_more.empty()) help += (help.empty() ? "" : "\n") + tr(focused->help_more);
        const float room = text_bottom - (strip + 12.0f), wrap = text_right - content_left;
        float px = 24.0f;
        // Never below 19 (about 2 cm at 2 m in the headset): a longer help is cut at the strip's edge instead.
        while (px > 19.0f && font->CalcTextSizeA(px * s, FLT_MAX, wrap * s, help.c_str()).y / s > room) px -= 1.0f;
        list->PushClipRect(at(content_left, strip + 4.0f), at(text_right, text_bottom), true);
        write(at(content_left, strip + 12.0f), px, help_ink, help, wrap);
        list->PopClipRect();
    }
}

void draw_setup(Setup& setup, const Moves& moves, const ImVec2& pos, const ImVec2& size, const Look& look) {
    setup_apply_moves(setup, moves);
    if (setup.result != SetupResult::Open) return;
    auto& io = ImGui::GetIO();
    auto* list = ImGui::GetWindowDrawList();
    ImFont* font = look.font ? look.font : ImGui::GetFont();
    const float s = size.x / width * look.scale;
    const float height = size.y / s;
    const auto at = [&](float x, float y) { return ImVec2{pos.x + x * s, pos.y + y * s}; };
    const auto tall = [&](float px, const std::string& text, float wrap) {
        return text.empty() ? 0.0f : font->CalcTextSizeA(px * s, FLT_MAX, wrap * s, text.c_str()).y / s;
    };
    const auto wide = [&](float px, const std::string& text) { return font->CalcTextSizeA(px * s, FLT_MAX, 0.0f, text.c_str()).x / s; };
    const auto write = [&](ImVec2 where, float px, ImU32 color, const std::string& text, float wrap = 0.0f) {
        list->AddText(font, px * s, where, color, text.c_str(), nullptr, wrap * s);
    };
    const auto button = [&](const char* id, ImVec2 min, ImVec2 max) {
        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton(id, ImVec2{(std::max)(max.x - min.x, 1.0f), (std::max)(max.y - min.y, 1.0f)});
    };
    const float moved = io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y;
    if (ImGui::IsWindowHovered() && (moved > 9.0f * s * s || io.MouseClicked[0] || io.MouseWheel != 0.0f)) {
        setup.source = Source::Pointer;
    }
    const bool pointer = setup.source == Source::Pointer;

    list->AddRectFilled(pos, ImVec2{pos.x + size.x, pos.y + size.y}, backdrop, 20.0f * s);
    list->AddRect(pos, ImVec2{pos.x + size.x, pos.y + size.y}, gold_dim, 20.0f * s, 0, 2.0f * s);
    list->AddRectFilled(pos, at(width, header_h), header, 20.0f * s, ImDrawFlags_RoundCornersTop);
    draw_brand(list, font, pos, s, tr("VR setup"));
    // One dot a question: answered, this one (larger), still to come.
    const float close_left = right - 52.0f;
    if (!setup.welcome()) {
        for (int i = 0; i < setup.count(); ++i) {
            const auto centre = at(close_left - 34.0f - (setup.count() - 1 - i) * 30.0f, 42.0f);
            if (i == setup.screen - 1) list->AddCircleFilled(centre, 9.0f * s, gold_bright);
            else if (i < setup.screen - 1) list->AddCircleFilled(centre, 7.0f * s, gold);
            else list->AddCircle(centre, 7.0f * s, gold_dim, 0, 2.0f * s);
        }
    }
    bool skip = false, back = false;
    {
        const auto min = at(close_left, 16.0f), max = at(right, 68.0f);
        button("close", min, max);
        const bool hovered = ImGui::IsItemHovered();
        skip |= ImGui::IsItemClicked();
        list->AddRect(min, max, hovered ? gold : gold_dim, 10.0f * s, 0, 2.0f * s);
        const float inset = 16.0f * s;
        list->AddLine(ImVec2{min.x + inset, min.y + inset}, ImVec2{max.x - inset, max.y - inset}, hovered ? gold_bright : muted, 3.0f * s);
        list->AddLine(ImVec2{max.x - inset, min.y + inset}, ImVec2{min.x + inset, max.y - inset}, hovered ? gold_bright : muted, 3.0f * s);
    }

    // This screen's words, cards and (on the summary) the answers.
    struct Card { std::string label, detail; bool recommended{}, current{}; };
    std::string title, text;
    std::vector<Card> cards;
    std::vector<std::pair<std::string, std::string>> answers;
    if (setup.welcome()) {
        title = tr("Welcome to VR");
        text = tr("A few quick choices for playing in VR. Each one takes effect at once, and you can change it later in this menu.");
        cards.push_back({tr("Set up now"), tr("About a minute.")});
        cards.push_back({tr("Skip"), tr("Keep the current settings. Run this later from the Interface page: VR setup.")});
    } else if (setup.summary()) {
        title = tr("All set");
        text = tr("Change any of these later in this menu. Run this setup again from the Interface page.");
        for (const auto& question : setup.questions) {
            const int now = question.get ? question.get() : -1;
            answers.emplace_back(tr(question.title), now >= 0 && now < static_cast<int>(question.choices.size()) ?
                tr(question.choices[now].label) : tr("Your own setting"));
        }
        cards.push_back({tr("Done"), setup.done_detail.empty() ? std::string{} : tr(setup.done_detail)});
    } else {
        const auto& question = setup.questions[setup.screen - 1];
        title = tr(question.title);
        text = tr(question.text);
        const int now = question.get ? question.get() : -1;
        for (int i = 0; i < static_cast<int>(question.choices.size()); ++i) {
            const auto& choice = question.choices[i];
            cards.push_back({tr(choice.label), choice.detail.empty() ? std::string{} : tr(choice.detail), choice.recommended, i == now});
        }
    }

    // Long translations shrink the type (to three quarters at most) rather than run into the footer.
    const float footer = height - 88.0f, inner = right - left - 56.0f;
    float name_w = 0.0f;
    for (const auto& [name, value] : answers) name_w = (std::max)(name_w, wide(26.0f, name));
    const auto card_h = [&](const Card& card, float k) {
        const float pad = 18.0f * k;
        const float detail = card.detail.empty() ? 0.0f : 6.0f * k + tall(24.0f * k, card.detail, inner);
        return (std::max)(84.0f * k, pad + tall(32.0f * k, card.label, inner) + detail + pad);
    };
    const auto content_h = [&](float k) {
        float y = 108.0f + tall(44.0f * k, title, right - left) + 14.0f * k + tall(26.0f * k, text, right - left) + 26.0f * k;
        if (!answers.empty()) y += answers.size() * 44.0f * k + 16.0f * k;
        for (const auto& card : cards) y += card_h(card, k) + 14.0f * k;
        return y;
    };
    float k = 1.0f;
    if (const float full = content_h(1.0f); full > footer) k = (std::max)(0.75f, (footer - 108.0f) / (full - 108.0f));

    float y = 108.0f;
    write(at(left, y), 44.0f * k, ink, title, right - left);
    y += tall(44.0f * k, title, right - left) + 14.0f * k;
    write(at(left, y), 26.0f * k, IM_COL32(222, 214, 198, 255), text, right - left);
    y += tall(26.0f * k, text, right - left) + 26.0f * k;
    for (const auto& [name, value] : answers) {
        write(at(left + 8.0f, y), 26.0f * k, muted, name);
        write(at(left + 8.0f + (std::min)(name_w * k + 40.0f, 600.0f), y), 26.0f * k, gold_bright, value);
        y += 44.0f * k;
    }
    if (!answers.empty()) y += 16.0f * k;
    int picked = -1;
    for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
        const auto& card = cards[i];
        const float h = card_h(card, k), pad = 18.0f * k;
        const auto min = at(left, y), max = at(right, y + h);
        ImGui::PushID(i);
        button("card", min, max);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) picked = i;
        ImGui::PopID();
        if (pointer && hovered) setup.focus = i;
        const bool focused = setup.focus == i && (!pointer || hovered);
        list->AddRectFilled(min, max, focused ? row_focus : row, 14.0f * s);
        if (focused) list->AddRect(min, max, gold, 14.0f * s, 0, 3.0f * s);
        else if (card.current) list->AddRect(min, max, gold_dim, 14.0f * s, 0, 2.0f * s);
        write(at(left + 28.0f, y + pad), 32.0f * k, ink, card.label);
        // Badges at the end of the label's line: Current at the edge, Recommended before it.
        float x = right - 24.0f;
        const auto pill = [&](const std::string& word, bool filled) {
            const float w = wide(22.0f * k, word) + 28.0f * k;
            const auto a = at(x - w, y + pad), b = at(x, y + pad + 34.0f * k);
            if (filled) list->AddRectFilled(a, b, gold, 17.0f * k * s);
            else list->AddRect(a, b, gold_dim, 17.0f * k * s, 0, 2.0f * s);
            write(at(x - w + 14.0f * k, y + pad + 5.0f * k), 22.0f * k, filled ? ink_dark : gold, word);
            x -= w + 12.0f;
        };
        if (card.current) pill(tr("Current"), false);
        if (card.recommended) pill(tr("Recommended"), true);
        if (!card.detail.empty()) write(at(left + 28.0f, y + pad + tall(32.0f * k, card.label, inner) + 6.0f * k), 24.0f * k, muted, card.detail, inner);
        y += h + 14.0f * k;
    }
    // The welcome greets with the crest, large, in the room under its two cards.
    if (setup.welcome()) {
        const float room = footer - 24.0f - y;
        if (room > 120.0f && wuwa_brand::crest_aspect() > 0.0f) {
            const float h = (std::min)(230.0f, room), w = h * wuwa_brand::crest_aspect();
            wuwa_brand::draw_crest(list, at(width * 0.5f - w * 0.5f, y + (room - h) * 0.5f), h * s);
        }
    }

    // Footer: Back, how to answer, Skip setup.
    list->AddLine(at(left, footer), at(right, footer), gold_dim, 2.0f * s);
    const auto footer_button = [&](const char* id, float x0, float x1, const std::string& word, bool arrow) {
        const auto min = at(x0, footer + 16.0f), max = at(x1, footer + 68.0f);
        button(id, min, max);
        const bool hovered = ImGui::IsItemHovered();
        list->AddRect(min, max, hovered ? gold : gold_dim, 10.0f * s, 0, 2.0f * s);
        const float text_x = arrow ? x0 + 46.0f : x0 + 20.0f;
        if (arrow) list->AddTriangleFilled(at(x0 + 20.0f, footer + 42.0f), at(x0 + 32.0f, footer + 33.0f), at(x0 + 32.0f, footer + 51.0f), hovered ? gold_bright : gold);
        write(at(text_x, footer + 29.0f), 24.0f, hovered ? ink : muted, word);
        return ImGui::IsItemClicked();
    };
    float hint_left = left, hint_right = right;
    if (setup.screen > setup.first) {
        const auto word = tr("Back");
        const float w = wide(24.0f, word) + 66.0f;
        back |= footer_button("back", left, left + w, word, true);
        hint_left = left + w + 20.0f;
    }
    if (!setup.welcome() && !setup.summary()) {
        const auto word = tr("Skip setup");
        const float w = wide(24.0f, word) + 40.0f;
        skip |= footer_button("skip", right - w, right, word, false);
        hint_right = right - w - 20.0f;
    }
    const auto hint = tr(pointer ? "Point and pull the trigger, or click." : "A pick  ·  B back");
    const float hint_w = wide(22.0f, hint);
    if (hint_w <= hint_right - hint_left) write(at((hint_left + hint_right - hint_w) * 0.5f, footer + 31.0f), 22.0f, gold_dim, hint);

    if (picked >= 0) setup_pick(setup, picked);
    else if (back) setup_back(setup);
    else if (skip) setup.result = SetupResult::Skipped;
}

void draw_step_card(ImDrawList* list, const ImVec2& size, ImFont* font, const wuwa_steps::Plan& plan, int index,
    const std::string& recording, const std::string& error) {
    if (!list || !font) return;
    const bool finished = index >= static_cast<int>(plan.steps.size());
    const auto* step = finished ? nullptr : &plan.steps[index];
    // 1600 x 900 design, like the controls card whose place this takes.
    const float s = size.x / 1600.0f;
    const auto at = [&](float x, float y) { return ImVec2{x * s, y * s}; };
    const auto tall = [&](float px, const std::string& text, float wrap) {
        return font->CalcTextSizeA(px * s, FLT_MAX, wrap * s, text.c_str()).y / s;
    };
    const auto write = [&](float x, float y, float px, ImU32 color, const std::string& text, float wrap = 0.0f) {
        list->AddText(font, px * s, at(x, y), color, text.c_str(), nullptr, wrap * s);
    };
    list->AddRectFilled(at(30, 30), at(1570, 870), IM_COL32(10, 10, 14, 238), 24 * s);
    list->AddRect(at(30, 30), at(1570, 870), IM_COL32(150, 118, 56, 255), 24 * s, 0, 3 * s);

    write(70, 58, 34, gold, tr("TEST") + "  ·  " + plan.title);
    const auto counter = finished ? tr("Finished") : std::to_string(index + 1) + " / " + std::to_string(plan.steps.size());
    const float counter_w = font->CalcTextSizeA(34 * s, FLT_MAX, 0.0f, counter.c_str()).x / s;
    write(1530 - counter_w, 58, 34, muted, counter);
    if (!recording.empty()) {
        const auto rec = "REC " + recording;
        const float rec_w = font->CalcTextSizeA(34 * s, FLT_MAX, 0.0f, rec.c_str()).x / s;
        list->AddCircleFilled(at(1530 - counter_w - 60, 76), 13 * s, IM_COL32(235, 64, 64, 255));
        write(1530 - counter_w - 90 - rec_w, 58, 34, IM_COL32(235, 90, 90, 255), rec);
    }

    float y = 120;
    if (step) {
        write(70, y, 62, ink, step->title, 1460);
        y += tall(62, step->title, 1460) + 24;
        if (!step->action.empty()) {
            write(70, y, 28, gold, tr("DO"));
            write(70, y + 36, 40, ink, step->action, 1460);
            y += 36 + tall(40, step->action, 1460) + 22;
        }
        if (!step->expect.empty()) {
            write(70, y, 28, gold, tr("EXPECT"));
            write(70, y + 36, 36, muted, step->expect, 1460);
        }
        std::string answers;
        for (const auto& answer : step->answers) answers += (answers.empty() ? "" : "   ·   ") + answer;
        write(70, 742, 30, muted, answers, 1460);
    } else {
        write(70, y, 56, ink, tr("Plan finished. Thank you!"), 1460);
        write(70, y + 90, 36, muted, tr("Your answers are saved. Restart it from the menu's Test page."), 1460);
    }
    if (!error.empty()) write(70, 690, 26, IM_COL32(255, 120, 110, 255), "plan.json: " + error, 1460);
    write(70, 800, 32, gold, tr("Answer in the menu: L3 + R3 (or Insert), Test page."));
}

void draw_note(ImDrawList* list, const ImVec2& size, ImFont* font, const std::string& title, const std::string& line1,
    const std::string& line2, bool compact) {
    if (!list || !font) return;
    // Low in view, so the scene behind it stays visible. Compact: short lines at the top edge,
    // about 10-12 degrees above the line of sight (test 16: the low note was in the way of a
    // dialogue). Head-locked at 2 m, 1 px is about 1 mm: under 24 px is hard to read.
    const float s = (std::min)(size.x / 1600.0f, size.y / 900.0f);
    const ImVec2 origin{(size.x - 1600.0f * s) * 0.5f, (size.y - 900.0f * s) * 0.5f};
    const auto at = [&](float x, float y) { return ImVec2{origin.x + x * s, origin.y + y * s}; };
    const float room = compact ? 820.0f : 1300.0f;
    const auto centered = [&](float y, float px, ImU32 color, const std::string& english) {
        const auto text = tr(english);
        const float natural = font->CalcTextSizeA(px * s, FLT_MAX, 0.0f, text.c_str()).x;
        const float fitted = px * s * (std::min)(1.0f, room * s / (std::max)(natural, 1.0f));
        const float w = font->CalcTextSizeA(fitted, FLT_MAX, 0.0f, text.c_str()).x;
        list->AddText(font, fitted, ImVec2{size.x * 0.5f - w * 0.5f, origin.y + y * s}, color, text.c_str());
    };
    if (compact) {
        list->AddRectFilled(at(370, 12), at(1230, 136), IM_COL32(10, 10, 14, 220), 16 * s);
        list->AddRect(at(370, 12), at(1230, 136), gold, 16 * s, 0, 2 * s);
        centered(20, 32, gold, title);
        if (!line1.empty()) centered(60, 28, ink, line1);
        if (!line2.empty()) centered(98, 24, muted, line2);
        return;
    }
    list->AddRectFilled(at(120, 560), at(1480, 790), IM_COL32(10, 10, 14, 236), 22 * s);
    list->AddRect(at(120, 560), at(1480, 790), gold, 22 * s, 0, 3 * s);
    centered(584, 46, gold, title);
    if (!line1.empty()) centered(652, 32, ink, line1);
    if (!line2.empty()) centered(712, 27, muted, line2);
}
}
