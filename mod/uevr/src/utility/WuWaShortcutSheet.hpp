#pragma once
#include <windows.h>
#include <imgui.h>

namespace wuwa_sheet {
// The artwork shares ImGui's font texture, including the existing DX11/DX12
// device-loss and font-rebuild lifecycle. No per-frame texture allocations.
void reserve_artwork(ImFontAtlas* atlas, HMODULE module);
bool fill_artwork(ImFontAtlas* atlas);
const char* artwork_status();
void draw(ImDrawList* draw, ImVec2 size, int page, ImFont* font, int flight_style=0, bool adjusting=false, bool mouse_active=false, bool ui_hidden=false);
void draw_hidden_ui_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool shortcut_available, bool menu_detected=true);
void draw_mouse_mode_warning(ImDrawList* draw, ImVec2 size, ImFont* font, bool adjustment, bool hidden_menu);
}
