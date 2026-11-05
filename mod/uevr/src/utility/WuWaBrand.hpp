#pragma once

// The WuWa VR crest (Abby in a VR headset, dev/brand/abby.py mod) inside the ImGui font atlas, so the VR menu
// and the controls cards draw it with no texture of their own. Three heights (72, 144, 288 px) are raw
// straight-alpha RGBA in RCDATA 4840-4842: the game needs no image decoder. reserve() adds a custom rect for
// each before the atlas builds; fill() copies the pixels in after it builds, before the renderer uploads the
// atlas. Header only: the native build adds no source file without a CMake run. 1.2.7 (owner, 5 Nov 2026).
#include <imgui.h>
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace wuwa_brand {
struct Picture {
    int rect{-1};
    int w{}, h{};
    const unsigned char* pixels{};
};
inline ImFontAtlas* owner{};
inline std::array<Picture, 3> crests{};

inline std::string_view rcdata(HMODULE module, int id) {
    const auto found = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
    const auto handle = found ? LoadResource(module, found) : nullptr;
    const auto* bytes = handle ? static_cast<const char*>(LockResource(handle)) : nullptr;
    return bytes ? std::string_view{bytes, SizeofResource(module, found)} : std::string_view{};
}

// Before atlas->Build(). The bytes must outlive the atlas (resources stay mapped; the preview keeps its own).
inline void reserve_data(ImFontAtlas* atlas, const std::array<std::string_view, 3>& files) {
    owner = atlas;
    for (int i = 0; i < static_cast<int>(crests.size()); ++i) {
        auto& crest = crests[i];
        crest = {};
        const auto data = files[i];
        if (data.size() < 12 || std::memcmp(data.data(), "WWIM", 4) != 0) continue;
        std::uint32_t w{}, h{};
        std::memcpy(&w, data.data() + 4, 4);
        std::memcpy(&h, data.data() + 8, 4);
        if (w == 0 || h == 0 || w > 1024 || h > 1024 || data.size() < 12 + static_cast<size_t>(w) * h * 4) continue;
        crest.w = static_cast<int>(w);
        crest.h = static_cast<int>(h);
        crest.pixels = reinterpret_cast<const unsigned char*>(data.data()) + 12;
        crest.rect = atlas->AddCustomRectRegular(crest.w, crest.h);
    }
}
inline void reserve(ImFontAtlas* atlas, HMODULE module) {
    reserve_data(atlas, {rcdata(module, 4840), rcdata(module, 4841), rcdata(module, 4842)});
}

// After atlas->Build(): the renderer's GetTexDataAsRGBA32 then returns the same buffer, crest included.
inline void fill(ImFontAtlas* atlas) {
    if (!atlas || atlas != owner) return;
    unsigned char* texture{};
    int tw{}, th{};
    atlas->GetTexDataAsRGBA32(&texture, &tw, &th);
    for (auto& crest : crests) {
        if (crest.rect < 0) continue;
        const auto* rect = texture ? atlas->GetCustomRectByIndex(crest.rect) : nullptr;
        if (!rect || !rect->IsPacked() || rect->X + crest.w > tw || rect->Y + crest.h > th) {
            crest.rect = -1;
            continue;
        }
        for (int y = 0; y < crest.h; ++y) {
            std::memcpy(texture + (static_cast<size_t>(rect->Y + y) * tw + rect->X) * 4,
                crest.pixels + static_cast<size_t>(y) * crest.w * 4, static_cast<size_t>(crest.w) * 4);
        }
    }
}

// Width over height, or 0 when no crest is in the current atlas.
inline float crest_aspect() {
    if (!ImGui::GetCurrentContext() || ImGui::GetIO().Fonts != owner) return 0.0f;
    for (auto it = crests.rbegin(); it != crests.rend(); ++it) {
        if (it->rect >= 0) return static_cast<float>(it->w) / static_cast<float>(it->h);
    }
    return 0.0f;
}

// Draws the crest `height` pixels tall from its top-left corner; returns the width drawn (0: none, nothing drawn).
// Uses the smallest size at least as tall, so the atlas never minifies more than about twice.
inline float draw_crest(ImDrawList* list, ImVec2 pos, float height, ImU32 tint = IM_COL32_WHITE) {
    if (!list || height <= 0.0f || !ImGui::GetCurrentContext()) return 0.0f;
    auto* atlas = ImGui::GetIO().Fonts;
    if (atlas != owner) return 0.0f;
    const Picture* pick = nullptr;
    for (const auto& crest : crests) {
        if (crest.rect < 0) continue;
        if (!pick || (pick->h < height ? crest.h > pick->h : crest.h >= height && crest.h < pick->h)) pick = &crest;
    }
    if (!pick) return 0.0f;
    ImVec2 uv0, uv1;
    atlas->CalcCustomRectUV(atlas->GetCustomRectByIndex(pick->rect), &uv0, &uv1);
    const float width = height * static_cast<float>(pick->w) / static_cast<float>(pick->h);
    list->AddImage(atlas->TexID, pos, ImVec2{pos.x + width, pos.y + height}, uv0, uv1, tint);
    return width;
}
}
