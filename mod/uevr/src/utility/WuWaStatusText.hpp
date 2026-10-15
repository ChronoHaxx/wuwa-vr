#pragma once

// Status lines the Lua scripts publish, shown translated in the WuWa VR menu.
// The scripts write English with numbers in it, which no catalog entry can match
// (test 17p, owner: "Now: Waiting for a close-up (lens now 75 degrees)" stayed
// English in Chinese). The lens status is now sent as parts (an English format
// and its values) and put together here; the HUD refresh status, technical text
// for the log, is shown as one of a few plain summaries.
#include <functional>
#include <string>
#include <string_view>

#ifndef N_
#define N_(english) english
#endif

namespace wuwa_status_text {
// Every format the camera script's lens status uses (02_WuWaVR_PolarControls.lua,
// lens.part), each value a %s, and the words it passes as values. Listed so
// mod_strings.py extract puts them in the catalog.
inline constexpr const char* lens_formats[]{
    N_("Off"), N_("Paused in the diorama"), N_("Paused: game camera in full VR only"),
    N_("Waiting for a close-up (lens now %s degrees)"),
    N_("Close-up at %s degrees: the game sets no focus in this shot, view at the game camera"),
    N_("Close-up at %s degrees: %s %s m away, moved %s m nearer"),
    N_("held behind the person in front (%s m)"), N_("stopped at the closest %s cm"),
    N_("hiding anything nearer than %s cm"), N_("depth of field forced on"),
    N_("depth of field: not in this game build"), N_("depth of field: %s"), N_("Unavailable: %s"),
    N_("game focus"), N_("subject")};

inline constexpr char part_separator = '\x1f', value_separator = '\x1e';

// Parts joined by \x1f, each an English format then its values, all joined by \x1e.
// translate gives the shown language for a format or a value (numbers find no entry
// and stay as they are). Each %s takes the next value; %% is a percent sign. Text
// without separators (an older script) comes back as it is.
inline std::string lens(std::string_view text, const std::function<std::string(std::string_view)>& translate) {
    if (text.find(part_separator) == std::string_view::npos && text.find(value_separator) == std::string_view::npos)
        return std::string{text};
    std::string out;
    size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(part_separator, start);
        const auto part = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        size_t field = part.find(value_separator);
        const auto english = part.substr(0, field);
        if (!english.empty()) {
            const auto format = translate(english);
            std::string line;
            for (size_t i = 0; i < format.size(); ++i) {
                if (format[i] == '%' && i + 1 < format.size() && format[i + 1] == '%') { line += '%'; ++i; continue; }
                if (format[i] == '%' && i + 1 < format.size() && format[i + 1] == 's') {
                    if (field != std::string_view::npos) {
                        const auto next = part.find(value_separator, field + 1);
                        line += translate(part.substr(field + 1, next == std::string_view::npos ? std::string_view::npos : next - field - 1));
                        field = next;
                    }
                    ++i;
                    continue;
                }
                line += format[i];
            }
            if (!out.empty()) out += "; ";
            out += line;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

// The HUD refresh status (wuwa_hud_refresh.lua, logged in full as "WuWaHud: ...") as a short
// line for the menu; empty when there is nothing to report.
inline const char* hud_refresh(std::string_view status) {
    const auto starts = [&](std::string_view prefix) { return status.substr(0, prefix.size()) == prefix; };
    if (status.empty() || status == "Ready" || starts("HUD aspect reset ready")) return "";
    if (starts("HUD layout refreshed") || starts("HUD viewport resynced") || starts("HUD root fitted"))
        return N_("HUD refresh: done. Check the HUD in the headset.");
    if (starts("HUD size already matches")) return N_("HUD refresh: the HUD already matches this view.");
    if (starts("HUD refresh queued") || starts("Refresh queued") || starts("Waiting for HUD dimensions"))
        return N_("HUD refresh: working on it.");
    if (starts("HUD viewport keeps changing"))
        return N_("HUD refresh: the HUD keeps changing. Open ESC or switch views to try again.");
    return N_("HUD refresh: not possible right now. Try again in a moment; the log has the details.");
}
} // namespace wuwa_status_text
