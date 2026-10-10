#pragma once

// Hosts the VR menu (utility/WuWaVrMenu) in UEVR's UI: binds its pages to the
// real settings and feeds it gamepad, keyboard and pointer input. UI thread only.
#include <imgui.h>

namespace wuwa_menu_host {
// While UEVR's UI is open. Draws the VR menu and returns true, or returns
// false when UEVR's classic window should draw instead.
bool draw(const ImVec2& target_size);
// Inside the classic window: the way back to the VR menu.
void draw_back_button();
// When UEVR's UI closes; the next open shows the VR menu again.
void on_closed();
}
