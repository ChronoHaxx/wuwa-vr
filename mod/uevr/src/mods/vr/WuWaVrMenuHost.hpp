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
// When UEVR's UI closes; the next open shows the VR menu again.
void on_closed();
// The VR menu (not the classic window) was drawn in the last few frames.
// Any thread.
bool showing();

// Guided test plans: picks up <profile>/wuwa-steps/plan.json (at most once a
// second). Called every UI frame, menu open or not.
void tick();
// A plan is loaded, so its current step replaces the shortcut sheet.
bool overlay_wanted();
// Draws the current step into the passive overlay (the sheet's place).
void draw_step_overlay(ImDrawList* list, const ImVec2& size);

// A letterboxed cinematic camera started in full VR: a short note in view
// suggests a screen mode. Any thread.
bool hint_active();
void draw_hint(ImDrawList* list, const ImVec2& size);
}
