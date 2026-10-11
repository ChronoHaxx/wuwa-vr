#pragma once

// Camera effects for both eyes (test 3f/3g). The game builds the second stereo
// view from a default camera (90 degree FOV, 4:3; see WuWaShadowPass.hpp), so
// that eye gets none of the camera's post-process settings: no depth of field
// (test 3d/3f: blur in one eye only), and possibly none of its grading. Before
// the view family renders, copy the source view's final post-process settings
// into the other: the depth-of-field fields only, or every plain-data field.
//
// Field offsets inside FPostProcessSettings come from the engine's own
// reflection. Where that block sits inside an FSceneView is not known for this
// build, so it is found once by matching three engine defaults (sensor width
// 24.576 mm, minimum f-stop 1.2, 5 blades) at a single offset in the first
// view, and confirmed in the second. Story cameras change exactly those values
// (cine cameras set their own filmback and blades; test 3f skipped every story
// shot), so after the search each copy only checks that both blocks hold
// plausible camera values. Game thread (BeginRenderViewFamily).
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <sdk/UObjectArray.hpp>
#include <sdk/UClass.hpp>
#include <sdk/FProperty.hpp>

#include "Logging.hpp"
#include "WuWaShadowPass.hpp"

namespace wuwa_dof_sync {
struct Layout {
    bool attempted{}, ready{};
    uint32_t base{};                 // FinalPostProcessSettings inside FSceneView
    uint32_t size{};                 // sizeof(FPostProcessSettings)
    uint32_t sensor{}, min_fstop{}, blades{}, focal{}, fstop{};
    std::vector<uint32_t> fields;    // 4-byte depth-of-field fields, relative to the block
    std::vector<std::pair<uint32_t, uint32_t>> plain; // plain-data runs (offset, bytes): numbers, colours, flags
    uint32_t plain_bytes{};
};
inline Layout layout{};
inline std::atomic<uint64_t> copies{}, refused{};
inline int last_auto_source{0};
// Static strings only: the menu reads this from the UI thread.
inline std::atomic<const char*> status{"Off"};

inline bool read_u32(uintptr_t address, uint32_t& value) { return wuwa_shadow::checked::read(address, value); }
inline bool read_f32(uintptr_t address, float& value) {
    uint32_t bits{};
    if (!read_u32(address, bits)) return false;
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

// Every property the engine marks plain old data (CPF_IsPlainOldData): floats,
// ints, enums, bitfield flags, colour/vector structs, raw object pointers.
// Arrays (post-process materials) own memory and are never copied.
inline void resolve_plain(sdk::UScriptStruct* settings) {
    const auto struct_size = settings->get_struct_size();
    if (struct_size <= 0 || struct_size > 0x4000) return;
    std::vector<std::pair<int32_t, bool>> all;
    int count{};
    for (auto field = settings->get_child_properties(); field && count < 1024; field = field->get_next(), ++count) {
        const auto property = static_cast<sdk::FProperty*>(field);
        const auto offset = property->get_offset();
        if (offset < 0 || offset >= struct_size) { all.clear(); return; }
        all.emplace_back(offset, property->is_pod());
    }
    if (all.size() < 16) return;
    std::sort(all.begin(), all.end());
    // A byte range belongs to the properties starting there, up to the next
    // distinct offset; it is plain only if every property starting there is.
    for (size_t i = 0; i < all.size();) {
        const auto offset = all[i].first;
        bool pod = true;
        size_t j = i;
        for (; j < all.size() && all[j].first == offset; ++j) pod = pod && all[j].second;
        const auto end = j < all.size() ? all[j].first : struct_size;
        if (pod && end > offset) {
            if (!layout.plain.empty() && layout.plain.back().first + layout.plain.back().second == uint32_t(offset)) {
                layout.plain.back().second += uint32_t(end - offset);
            } else {
                layout.plain.emplace_back(uint32_t(offset), uint32_t(end - offset));
            }
            layout.plain_bytes += uint32_t(end - offset);
        }
        i = j;
    }
    layout.size = uint32_t(struct_size);
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
        fstop = offset_of(L"DepthOfFieldFstop");
    if (sensor < 0 || min_fstop < 0 || blades < 0 || focal < 0 || fstop < 0) { status = "Depth-of-field fields not found"; return false; }
    layout.sensor = uint32_t(sensor); layout.min_fstop = uint32_t(min_fstop); layout.blades = uint32_t(blades);
    layout.focal = uint32_t(focal); layout.fstop = uint32_t(fstop);
    for (const auto* name : {L"DepthOfFieldFocalDistance", L"DepthOfFieldFstop", L"DepthOfFieldMinFstop", L"DepthOfFieldBladeCount",
             L"DepthOfFieldSensorWidth", L"DepthOfFieldDepthBlurAmount", L"DepthOfFieldDepthBlurRadius", L"DepthOfFieldFocalRegion",
             L"DepthOfFieldNearTransitionRegion", L"DepthOfFieldFarTransitionRegion", L"DepthOfFieldScale", L"DepthOfFieldNearBlurSize",
             L"DepthOfFieldFarBlurSize", L"DepthOfFieldOcclusion", L"DepthOfFieldSkyFocusDistance", L"DepthOfFieldVignetteSize",
             L"DepthOfFieldColorThreshold", L"DepthOfFieldSizeThreshold"}) {
        if (const auto at = offset_of(name); at >= 0) layout.fields.push_back(uint32_t(at));
    }
    resolve_plain(settings);
    spdlog::info("[WuWaDoF] PostProcessSettings: {} bytes, {} depth-of-field fields, {} plain-data runs ({} bytes)",
        layout.size, layout.fields.size(), layout.plain.size(), layout.plain_bytes);
    return true;
}

inline bool anchors_match(uintptr_t block) {
    float sensor{}, min_fstop{};
    uint32_t blades{};
    return read_f32(block + layout.sensor, sensor) && read_f32(block + layout.min_fstop, min_fstop) &&
        read_u32(block + layout.blades, blades) && sensor == 24.576f && min_fstop == 1.2f && blades == 5;
}

struct Camera { float focal{}, fstop{}, sensor{}, min_fstop{}; uint32_t blades{}; bool read{}; };
inline Camera camera_at(uintptr_t block) {
    Camera c{};
    c.read = read_f32(block + layout.focal, c.focal) && read_f32(block + layout.fstop, c.fstop) &&
        read_f32(block + layout.sensor, c.sensor) && read_f32(block + layout.min_fstop, c.min_fstop) &&
        read_u32(block + layout.blades, c.blades);
    return c;
}
// Any real camera: a filmback between 1 and 200 mm, f-stops above zero, at most 32 blades.
inline bool plausible(const Camera& c) {
    return c.read && std::isfinite(c.sensor) && c.sensor >= 1.0f && c.sensor <= 200.0f &&
        std::isfinite(c.min_fstop) && c.min_fstop > 0.0f && c.min_fstop <= 64.0f && c.blades <= 32 &&
        std::isfinite(c.fstop) && c.fstop > 0.0f && c.fstop < 1000.0f && std::isfinite(c.focal) && c.focal >= 0.0f && c.focal < 1.0e7f;
}

inline bool find_block(uintptr_t first, uintptr_t second) {
    constexpr uint32_t search = 0x6000;
    uint32_t found{}, matches{};
    for (uint32_t base = 0; base < search; base += 4) {
        if (anchors_match(first + base)) { found = base; ++matches; }
    }
    if (matches != 1 || !anchors_match(second + found)) {
        status = matches == 0 ? "Post-process block not found in the view (try again outside a story scene)" :
            matches > 1 ? "Post-process block ambiguous" : "Second view does not match the first";
        spdlog::warn("[WuWaDoF] block search failed: {} matches, {}", matches, status.load());
        return false;
    }
    layout.base = found;
    layout.ready = true;
    spdlog::info("[WuWaDoF] FinalPostProcessSettings at view+0x{:x}", found);
    return true;
}

// source: 0 copies the first view into the second, 1 the second into the first,
// 2 picks the view carrying the camera's focus (the other has none).
// everything: copy every plain-data field, not only depth of field.
inline void sync(bool depth_of_field, bool everything, int source, const void* first_view, const void* second_view) {
    if ((!depth_of_field && !everything) || !first_view || !second_view) {
        status = depth_of_field || everything ? "Waiting for a stereo frame" : "Off";
        return;
    }
    const auto a = reinterpret_cast<uintptr_t>(first_view), b = reinterpret_cast<uintptr_t>(second_view);
    if (!layout.attempted && !resolve_fields()) return;
    if (layout.fields.empty()) return;
    // The search needs the engine defaults, so it waits for a frame without a
    // story camera; a failed search is retried at most every 2 seconds.
    if (!layout.ready) {
        static uint64_t last_search{};
        const auto now = GetTickCount64();
        if (now - last_search < 2000) return;
        last_search = now;
        if (!find_block(a, b)) return;
    }
    const auto ca = camera_at(a + layout.base), cb = camera_at(b + layout.base);
    if (!plausible(ca) || !plausible(cb)) {
        ++refused;
        status = "Views hold unexpected values; copy skipped";
        SPDLOG_INFO_EVERY_N_SEC(5, "[WuWaDoF] copy skipped: focus {:.0f} / {:.0f}, f-stop {:.2f} / {:.2f}, sensor {:.3f} / {:.3f}, "
            "min f-stop {:.2f} / {:.2f}, blades {} / {}", ca.focal, cb.focal, ca.fstop, cb.fstop, ca.sensor, cb.sensor,
            ca.min_fstop, cb.min_fstop, ca.blades, cb.blades);
        return;
    }
    int from_index = source;
    if (source != 0 && source != 1) {
        // Auto: the view with a focus distance is the one built from the camera.
        if ((ca.focal > 0.0f) != (cb.focal > 0.0f)) last_auto_source = ca.focal > 0.0f ? 0 : 1;
        from_index = last_auto_source;
    }
    const auto from = (from_index == 1 ? b : a) + layout.base, to = (from_index == 1 ? a : b) + layout.base;
    bool copied = false;
    if (everything && !layout.plain.empty()) {
        if (!wuwa_shadow::writable_span(to, layout.size)) { ++refused; status = "Second view not writable; copy skipped"; return; }
        copied = true;
        for (const auto& [offset, bytes] : layout.plain) {
            if (!wuwa_shadow::checked::guarded_copy(reinterpret_cast<void*>(to + offset), reinterpret_cast<const void*>(from + offset), bytes)) {
                ++refused; status = "Copy failed"; return;
            }
        }
    } else {
        for (const auto offset : layout.fields) {
            uint32_t value{};
            if (!read_u32(from + offset, value) || !wuwa_shadow::write_field(to + offset, value)) {
                ++refused; status = "Copy failed"; return;
            }
        }
        copied = true;
    }
    if (!copied) return;
    ++copies;
    status = everything ? (from_index == 1 ? "All camera effects copied into the first view" : "All camera effects copied into the second view")
                        : (from_index == 1 ? "Blur copied into the first view" : "Blur copied into the second view");
    SPDLOG_INFO_EVERY_N_SEC(5, "[WuWaDoF] copy {} {} -> {}{}: focus {:.0f} / {:.0f} cm, f-stop {:.2f} / {:.2f}, sensor {:.2f} / {:.2f}, "
        "blades {} / {}", everything ? "all" : "blur", from_index + 1, 2 - from_index, source == 2 ? " (auto)" : "",
        ca.focal, cb.focal, ca.fstop, cb.fstop, ca.sensor, cb.sensor, ca.blades, cb.blades);
}
}
