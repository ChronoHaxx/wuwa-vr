#pragma once

// Hosts the VR menu (utility/WuWaVrMenu) in UEVR's UI: binds its pages to the
// real settings, feeds it gamepad, keyboard, VR controller and pointer input,
// and runs guided test plans (utility/WuWaStepPlan). UI thread unless noted.
#include <imgui.h>

namespace wuwa_menu_host {
// While UEVR's UI is open. Draws the VR menu and returns true, or returns
// false when UEVR's classic window should draw instead.
bool draw(const ImVec2& target_size);
// Inside the classic window: the way back to the VR menu.
void draw_back_button();
// Emergency keys: the next draw() shows the VR menu (turning "Use this menu" back
// on) or UEVR's classic window. Any thread; the caller opens UEVR's UI.
void request_open(bool classic_window);
// When UEVR's UI closes; the next open shows the VR menu again.
void on_closed();
// The VR menu (not the classic window) was drawn in the last few frames.
// Any thread.
bool showing();
// "Move menu" is on: the menu follows a controller's aim (the overlay reads
// this) or the sticks / arrow keys, until it is put down. Any thread.
bool moving();
// The share of the UI image's height the menu fills, for an image of this
// width / height (draw() sizes it the same way). Any thread.
float height_share(float aspect);

// The laser beam (OpenXR) is a thin quad textured from a small swatch that the
// VR menu draws in the top-left corner of the UI image; the menu's own quad
// leaves those rows out. Pixels.
inline constexpr int beam_swatch_width = 32;
inline constexpr int beam_swatch_rows = 4;

// Guided test plans: picks up <profile>/wuwa-steps/plan.json (at most once a
// second). Called every UI frame, menu open or not.
void tick();
// A plan is loaded, so its current step replaces the controls card.
bool overlay_wanted();
// Draws the current step into the passive overlay (the sheet's place).
void draw_step_overlay(ImDrawList* list, const ImVec2& size);

// A letterboxed cinematic camera started in full VR: a short note in view
// suggests a screen mode. Any thread.
bool hint_active();
void draw_hint(ImDrawList* list, const ImVec2& size);
}
