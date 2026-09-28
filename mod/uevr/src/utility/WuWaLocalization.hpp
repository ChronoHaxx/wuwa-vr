#pragma once
#include <windows.h>
#include <imgui.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Mod-owned presentation only. Does not translate UEVR or change any input,
// camera, rendering, game-language or configuration-key semantics.
namespace wuwa_l10n {
struct Language { std::string id, name; };
void request(std::string_view language, const std::filesystem::path& overrides, bool reload=false);
// Called before NewFrame, never from a draw callback. True requests atlas rebuild.
bool apply_pending(HMODULE module);
void reserve_fonts(ImFontAtlas* atlas, HMODULE module, float menu_size);
ImFont* menu_font();
ImFont* sheet_font();
std::string language();
std::string status();
std::vector<Language> languages();
std::string translate(std::string_view english);
std::string visual(std::string_view logical);
std::string text(std::string_view english);
std::string label(std::string_view english);
bool valid_translation(std::string_view english, std::string_view translated);
bool rtl_available();
bool right_to_left();
// Rendering diagnostics for the offscreen check, independent of game acceptance.
std::vector<std::string> translated_strings();
}
