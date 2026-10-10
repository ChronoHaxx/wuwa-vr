#include "WuWaVrMenu.hpp"

#include <imgui.h>
#include <cfloat>
#include <cstdio>

#include "WuWaLocalization.hpp"

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

// Design space: 1200 wide. Rows are 68 tall with an 8 gap.
constexpr float width = 1200.0f, header_h = 84.0f, help_h = 116.0f, row_h = 68.0f, row_gap = 8.0f;
constexpr float left = 28.0f, right = 1172.0f, widget_left = 768.0f, widget_right = 1148.0f;

std::string tr(const std::string& english) { return wuwa_l10n::text(english); }

std::string value_of(const Item& item) {
    char buffer[64]{};
    switch (item.kind) {
    case Kind::Choice:
        if (item.get_int && !item.choices.empty()) {
            const int index = std::clamp(item.get_int(), 0, static_cast<int>(item.choices.size()) - 1);
            return tr(item.choices[index]);
        }
        return {};
    case Kind::Slider:
        if (item.get_float) std::snprintf(buffer, sizeof(buffer), item.format, item.get_float());
        return buffer;
    default:
        return item.value_text ? item.value_text() : std::string{};
    }
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

    // Header: title, page tabs (LB / RB when using buttons), close.
    list->AddRectFilled(pos, at(width, header_h), header, 20.0f * s, ImDrawFlags_RoundCornersTop);
    write(at(left, 22.0f), 34.0f, gold, "WuWa VR");
    float x = 220.0f;
    if (!pointer) {
        write(at(x, 30.0f), 22.0f, muted, "LB");
        x += 40.0f;
    }
    for (int index = 0; index < static_cast<int>(pages.size()); ++index) {
        const auto title = tr(pages[index].title);
        const float w = measure(26.0f, title).x / s + 36.0f;
        const auto min = at(x, 16.0f), max = at(x + w, 68.0f);
        ImGui::PushID(index);
        button("tab", min, max);
        if (ImGui::IsItemClicked()) set_page(pages, state, index);
        const bool current = index == state.page;
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (current) list->AddRectFilled(min, max, gold, 10.0f * s);
        else list->AddRect(min, max, hovered ? gold : gold_dim, 10.0f * s, 0, 2.0f * s);
        write(at(x + 18.0f, 28.0f), 26.0f, current ? ink_dark : hovered ? ink : muted, title);
        x += w + 10.0f;
    }
    if (!pointer) write(at(x + 6.0f, 30.0f), 22.0f, muted, "RB");
    {
        const auto min = at(right - 52.0f, 16.0f), max = at(right, 68.0f);
        button("close", min, max);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) state.close_requested = true;
        list->AddRect(min, max, hovered ? gold : gold_dim, 10.0f * s, 0, 2.0f * s);
        const float inset = 16.0f * s;
        list->AddLine(ImVec2{min.x + inset, min.y + inset}, ImVec2{max.x - inset, max.y - inset}, hovered ? gold_bright : muted, 3.0f * s);
        list->AddLine(ImVec2{max.x - inset, min.y + inset}, ImVec2{min.x + inset, max.y - inset}, hovered ? gold_bright : muted, 3.0f * s);
    }

    // Rows, scrolled so a button-focused row is always in view.
    auto& page = pages[state.page];
    const float top = header_h + 12.0f, bottom = height - help_h - 8.0f, area = bottom - top;
    const float content = page.items.size() * (row_h + row_gap) - row_gap;
    const auto content_min = at(0.0f, top), content_max = at(width, bottom);
    const bool over_content = ImGui::IsWindowHovered() && io.MousePos.y >= content_min.y && io.MousePos.y < content_max.y;
    if (over_content && io.MouseWheel != 0.0f) state.scroll -= io.MouseWheel * (row_h + row_gap);
    if (!pointer && state.focus >= 0) {
        const float row_top = state.focus * (row_h + row_gap);
        if (row_top < state.scroll) state.scroll = row_top;
        if (row_top + row_h > state.scroll + area) state.scroll = row_top + row_h - area;
    }
    state.scroll = std::clamp(state.scroll, 0.0f, (std::max)(0.0f, content - area));

    if (!io.MouseDown[0]) state.dragging = -1;
    list->PushClipRect(content_min, content_max, true);
    for (int index = 0; index < static_cast<int>(page.items.size()); ++index) {
        auto& item = page.items[index];
        const float y = top + index * (row_h + row_gap) - state.scroll;
        if (y + row_h < top || y > bottom) continue;
        const bool enabled = !item.enabled || item.enabled();
        const auto min = at(left, y), max = at(right, y + row_h);
        const float mid = y + row_h * 0.5f;

        if (item.kind == Kind::Text) {
            auto text = tr(item.label);
            if (item.value_text) text += "  " + item.value_text();
            write(at(left + 8.0f, mid - 13.0f), 24.0f, muted, text);
            continue;
        }

        ImGui::PushID(state.page * 1000 + index);
        button("row", min, max);
        const bool hovered = ImGui::IsItemHovered() && over_content && enabled;
        const bool clicked = ImGui::IsItemClicked() && over_content && enabled;
        ImGui::PopID();
        if (pointer && hovered) state.focus = index;
        const bool focused = state.focus == index && (!pointer || hovered || state.dragging == index);
        const float mouse_x = (io.MousePos.x - pos.x) / s;

        list->AddRectFilled(min, max, focused ? row_focus : row, 12.0f * s);
        if (focused) list->AddRect(min, max, gold, 12.0f * s, 0, 3.0f * s);
        write(at(left + 24.0f, mid - 15.0f), 30.0f, enabled ? ink : faint, tr(item.label));

        switch (item.kind) {
        case Kind::Toggle: {
            const bool on = item.get_bool && item.get_bool();
            if (clicked) activate(item);
            const auto t0 = at(widget_right - 96.0f, mid - 22.0f), t1 = at(widget_right, mid + 22.0f);
            list->AddRectFilled(t0, t1, !enabled ? track : on ? gold : track, 22.0f * s);
            const float knob = on ? widget_right - 22.0f : widget_right - 74.0f;
            list->AddCircleFilled(at(knob, mid), 17.0f * s, on ? ink_dark : ink);
            const auto word = tr(on ? "On" : "Off");
            write(at(widget_right - 112.0f - measure(24.0f, word).x / s, mid - 12.0f), 24.0f, on ? gold_bright : muted, word);
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
            const auto text = value_of(item);
            const float w = measure(26.0f, text).x / s;
            write(at((widget_left + widget_right) * 0.5f - w * 0.5f, mid - 13.0f), 26.0f, enabled ? gold_bright : faint, text);
            break;
        }
        case Kind::Slider: {
            const float bar0 = widget_left, bar1 = widget_right - 84.0f;
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
            const auto text = value_of(item);
            write(at(widget_right - measure(24.0f, text).x / s, mid - 12.0f), 24.0f, enabled ? ink : faint, text);
            break;
        }
        case Kind::Action: {
            if (clicked) activate(item);
            const auto text = value_of(item);
            if (!text.empty()) write(at(widget_right - 40.0f - measure(24.0f, text).x / s, mid - 12.0f), 24.0f, muted, text);
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

    // Help strip: the focused row's explanation and how to drive the menu.
    const float strip = height - help_h;
    list->AddLine(at(left, strip), at(right, strip), gold_dim, 2.0f * s);
    if (state.focus >= 0 && state.focus < static_cast<int>(page.items.size()) && !page.items[state.focus].help.empty()) {
        write(at(left, strip + 14.0f), 24.0f, IM_COL32(214, 204, 184, 255), tr(page.items[state.focus].help), right - left);
    }
    const auto hint = tr(pointer ? "Point and pull the trigger, or click. Scroll to see more."
                                 : "A select  ·  Left / right change  ·  LB / RB pages  ·  B close");
    write(at(left, height - 36.0f), 22.0f, gold_dim, hint);
}
}
