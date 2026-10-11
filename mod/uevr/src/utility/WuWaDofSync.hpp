#pragma once

// Depth of field for both eyes (experiment, test 3f). With the show flag
// forced on, test 3d blurred one eye only: the second stereo view does not
// get the camera's depth-of-field settings. Before the view family renders,
// copy the depth-of-field values of one view's final post-process settings
// into the other's.
//
// Field offsets inside FPostProcessSettings come from the engine's own
// reflection. Where that block sits inside an FSceneView is not known for this
// build, so it is found once by matching three engine defaults that the game
// does not change (sensor width 24.576 mm, minimum f-stop 1.2, 5 blades) at a
// single offset in the first view, and confirmed in the second. Any mismatch
// later stops the copy. Game thread (BeginRenderViewFamily).
#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <sdk/UObjectArray.hpp>
#include <sdk/UClass.hpp>
#include <sdk/FProperty.hpp>

#include "WuWaShadowPass.hpp"

namespace wuwa_dof_sync {
struct Layout {
    bool attempted{}, ready{};
    uint32_t base{};                 // FinalPostProcessSettings inside FSceneView
    uint32_t sensor{}, min_fstop{}, blades{}, focal{}, fstop{}, scale{};
    std::vector<uint32_t> fields;    // 4-byte depth-of-field fields, relative to the block
};
inline Layout layout{};
inline std::atomic<uint64_t> copies{}, refused{};
// Static strings only: the menu reads this from the UI thread.
inline std::atomic<const char*> status{"Off"};

inline bool read_u32(uintptr_t address, uint32_t& value) { return wuwa_shadow::checked::read(address, value); }
inline bool read_f32(uintptr_t address, float& value) {
    uint32_t bits{};
    if (!read_u32(address, bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

inline bool resolve_fields() {
    layout.attempted = true;
    const auto settings = sdk::find_uobject<sdk::UScriptStruct>(L"ScriptStruct /Script/Engine.PostProcessSettings");
    if (!settings) { status = "PostProcessSettings not found"; return false; }
    const auto offset_of = [&](const wchar_t* name) -> int32_t {
        const auto property = settings->find_property(name);
        return property ? property->get_offset() : -1;
    };
    const auto sensor = offset_of(L"DepthOfFieldSensorWidth"), min_fstop = offset_of(L"DepthOfFieldMinFstop"),
        blades = offset_of(L"DepthOfFieldBladeCount"), focal = offset_of(L"DepthOfFieldFocalDistance"),
        fstop = offset_of(L"DepthOfFieldFstop"), scale = offset_of(L"DepthOfFieldScale");
    if (sensor < 0 || min_fstop < 0 || blades < 0 || focal < 0 || fstop < 0) { status = "Depth-of-field fields not found"; return false; }
    layout.sensor = uint32_t(sensor); layout.min_fstop = uint32_t(min_fstop); layout.blades = uint32_t(blades);
    layout.focal = uint32_t(focal); layout.fstop = uint32_t(fstop); layout.scale = scale >= 0 ? uint32_t(scale) : uint32_t(focal);
    for (const auto* name : {L"DepthOfFieldFocalDistance", L"DepthOfFieldFstop", L"DepthOfFieldMinFstop", L"DepthOfFieldBladeCount",
             L"DepthOfFieldSensorWidth", L"DepthOfFieldDepthBlurAmount", L"DepthOfFieldDepthBlurRadius", L"DepthOfFieldFocalRegion",
             L"DepthOfFieldNearTransitionRegion", L"DepthOfFieldFarTransitionRegion", L"DepthOfFieldScale", L"DepthOfFieldNearBlurSize",
             L"DepthOfFieldFarBlurSize", L"DepthOfFieldOcclusion", L"DepthOfFieldSkyFocusDistance", L"DepthOfFieldVignetteSize",
             L"DepthOfFieldColorThreshold", L"DepthOfFieldSizeThreshold"}) {
        if (const auto at = offset_of(name); at >= 0) layout.fields.push_back(uint32_t(at));
    }
    return true;
}

inline bool anchors_match(uintptr_t block) {
    float sensor{}, min_fstop{};
    uint32_t blades{};
    return read_f32(block + layout.sensor, sensor) && read_f32(block + layout.min_fstop, min_fstop) &&
        read_u32(block + layout.blades, blades) && sensor == 24.576f && min_fstop == 1.2f && blades == 5;
}

inline bool find_block(uintptr_t first, uintptr_t second) {
    constexpr uint32_t search = 0x6000;
    uint32_t found{}, matches{};
    for (uint32_t base = 0; base < search; base += 4) {
        if (anchors_match(first + base)) { found = base; ++matches; }
    }
    if (matches != 1 || !anchors_match(second + found)) {
        status = matches == 0 ? "Post-process block not found in the view" :
            matches > 1 ? "Post-process block ambiguous" : "Second view does not match the first";
        spdlog::warn("[WuWaDoF] block search failed: {} matches, {}", matches, status.load());
        return false;
    }
    layout.base = found;
    layout.ready = true;
    spdlog::info("[WuWaDoF] FinalPostProcessSettings at view+0x{:x}; {} depth-of-field fields", found, layout.fields.size());
    return true;
}

// source: 0 copies the first view into the second, 1 the second into the first.
inline void sync(bool enabled, int source, const void* first_view, const void* second_view) {
    if (!enabled || !first_view || !second_view) { status = enabled ? "Waiting for a stereo frame" : "Off"; return; }
    const auto a = reinterpret_cast<uintptr_t>(first_view), b = reinterpret_cast<uintptr_t>(second_view);
    if (!layout.attempted && !resolve_fields()) return;
    if (layout.fields.empty()) return;
    if (!layout.ready && !find_block(a, b)) { layout.fields.clear(); return; } // one search per session
    const auto from = (source == 1 ? b : a) + layout.base, to = (source == 1 ? a : b) + layout.base;
    if (!anchors_match(from) || !anchors_match(to)) { ++refused; status = "Views changed shape; copy skipped"; return; }
    // Before the copy: shows which view carried the camera's depth of field.
    float fa{}, fb{}, sa{}, sb{};
    read_f32(a + layout.base + layout.focal, fa); read_f32(b + layout.base + layout.focal, fb);
    read_f32(a + layout.base + layout.fstop, sa); read_f32(b + layout.base + layout.fstop, sb);
    for (const auto offset : layout.fields) {
        uint32_t value{};
        if (!read_u32(from + offset, value) || !wuwa_shadow::write_field(to + offset, value)) {
            ++refused; status = "Copy failed"; return;
        }
    }
    ++copies;
    status = source == 1 ? "Copied into the first view" : "Copied into the second view";
    SPDLOG_INFO_EVERY_N_SEC(5, "[WuWaDoF] copy {} -> {}: focus {:.0f} / {:.0f} cm, f-stop {:.2f} / {:.2f}",
        source == 1 ? 2 : 1, source == 1 ? 1 : 2, fa, fb, sa, sb);
}
}
