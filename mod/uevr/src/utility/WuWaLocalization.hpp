#pragma once
#include <windows.h>
#include <imgui.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Mod-owned presentation only. Does not translate UEVR or change any input,
// camera, rendering, game-language or configuration-key semantics.
// Marks English kept in a table and translated where it is drawn, so
// dev/i18n/mod_strings.py finds it for the catalogs. Expands to the text itself.
#ifndef N_
#define N_(english) english
#endif

namespace wuwa_l10n {
struct Language { std::string id, name; };
void request(std::string_view language, const std::filesystem::path& overrides, bool reload=false);
// Called before NewFrame, never from a draw callback. True requests atlas rebuild.
bool apply_pending(HMODULE module);
void reserve_fonts(ImFontAtlas* atlas, HMODULE module, float menu_size);
ImFont* menu_font();
ImFont* sheet_font();
std::string language();
// The language last asked for; it becomes language() once it settles and loads.
std::string requested_language();
std::string status();
std::vector<Language> languages();
// A language's own name (日本語, Deutsch...), ready to draw in the current menu.
std::string native_name(std::string_view id);
std::string translate(std::string_view english);
// Like translate, but never recorded as missing: text built at run time (status lines
// with values) that may or may not be a catalog entry.
std::string translate_quiet(std::string_view english);
// Text with letters that reached translate() in a non-English language and had no
// translation, since the language loaded (at most 4000). For test sessions and checks:
// write_missing() saves them to a file when the set changed; true when it wrote.
std::vector<std::string> missing_strings();
bool write_missing(const std::filesystem::path& file);
std::string visual(std::string_view logical);
std::string text(std::string_view english);
std::string label(std::string_view english);
bool valid_translation(std::string_view english, std::string_view translated);
bool rtl_available();
bool right_to_left();
// Rendering diagnostics for the offscreen check, independent of game acceptance.
std::vector<std::string> translated_strings();
}
