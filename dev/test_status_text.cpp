// Script status lines shown translated: the lens status sent as parts (format + values) and the
// HUD refresh status as a plain summary. Offline; a fake catalog stands in for the language.
#include "../mod/uevr/src/utility/WuWaStatusText.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <string>
using namespace wuwa_status_text;

int main() {
    const std::map<std::string, std::string, std::less<>> catalog{
        {"Waiting for a close-up (lens now %s degrees)", "等待特写（镜头现为 %s 度）"},
        {"depth of field forced on", "已强制景深"},
        {"Close-up at %s degrees: %s %s m away, moved %s m nearer", "特写 %s 度：%s 距 %s 米，前移 %s 米"},
        {"game focus", "游戏焦点"},
        {"hiding anything nearer than %s cm", "隐藏 %s 厘米内的物体"},
        {"Unavailable: %s", "不可用：%s"},
        {"%s at 100%%", "%s 为 100%%"},
    };
    const auto zh = [&](std::string_view english) {
        const auto found = catalog.find(english);
        return found == catalog.end() ? std::string{english} : found->second;
    };
    const auto en = [](std::string_view english) { return std::string{english}; };
    const auto parts = [](std::initializer_list<std::string> list) {
        std::string out;
        for (const auto& p : list) { if (!out.empty()) out += part_separator; out += p; }
        return out;
    };
    const auto part = [](std::string format, std::initializer_list<std::string> values) {
        for (const auto& v : values) { format += value_separator; format += v; }
        return format;
    };

    // The owner's line from test 17p, in English and in Chinese.
    const auto waiting = parts({part("Waiting for a close-up (lens now %s degrees)", {"75"}), "depth of field forced on"});
    assert(lens(waiting, en) == "Waiting for a close-up (lens now 75 degrees); depth of field forced on");
    assert(lens(waiting, zh) == "等待特写（镜头现为 75 度）; 已强制景深");

    // Several values, one of them a word the catalog translates; numbers stay.
    const auto close = parts({part("Close-up at %s degrees: %s %s m away, moved %s m nearer", {"38", "game focus", "1.2", "0.4"}),
        part("hiding anything nearer than %s cm", {"20"})});
    assert(lens(close, zh) == "特写 38 度：游戏焦点 距 1.2 米，前移 0.4 米; 隐藏 20 厘米内的物体");

    // A value with no catalog entry (an error text) is shown as it is; %% is a percent sign.
    assert(lens(part("Unavailable: %s", {"attempt to index nil"}), zh) == "不可用：attempt to index nil");
    assert(lens(part("%s at 100%%", {"blur"}), zh) == "blur 为 100%");
    // Too few values leave the field empty rather than reading past the text.
    assert(lens(parts({part("Waiting for a close-up (lens now %s degrees)", {}), "Off"}), en) ==
        "Waiting for a close-up (lens now  degrees); Off");
    // Empty parts are skipped; an older script's plain text comes back unchanged.
    assert(lens(parts({"", "depth of field forced on", ""}), zh) == "已强制景深");
    assert(lens("Off; depth of field forced on", zh) == "Off; depth of field forced on");
    assert(lens("", zh).empty());

    // HUD refresh: the script's technical status becomes a plain line; ready says nothing.
    assert(std::string{hud_refresh("")}.empty() && std::string{hud_refresh("Ready")}.empty());
    assert(std::string{hud_refresh("HUD aspect reset ready / HUD 比例重置就绪")}.empty());
    const std::string done = hud_refresh("HUD viewport resynced 2560x2560 -> 2560x1440; check the headset / HUD 视口已同步，请在头显中确认");
    assert(done == "HUD refresh: done. Check the HUD in the headset.");
    assert(std::string{hud_refresh("HUD layout refreshed (2) at 2560x1440; check the headset / ...")} == done);
    assert(std::string{hud_refresh("HUD root fitted (1) 2560x1440; check the headset")} == done);
    assert(std::string{hud_refresh("HUD size already matches the view / HUD 尺寸已匹配")} == "HUD refresh: the HUD already matches this view.");
    assert(std::string{hud_refresh("Refresh queued; requires the supplied Comfort script.")} == "HUD refresh: working on it.");
    assert(std::string{hud_refresh("HUD viewport keeps changing; open ESC or switch view to retry / ...")}.find("keeps changing") != std::string::npos);
    assert(std::string{hud_refresh("No active HUD with the supported LGUI schema")}.find("not possible right now") != std::string::npos);

    std::cout << "status text: 2 groups passed\n";
    return 0;
}
