#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace wuwa_console_read {
enum class Access { Read, Write };

constexpr bool valid_name(std::string_view name, Access access) noexcept {
    if (name.size() > 96) return false;
    const bool renderer = name.size() > 2 && name.substr(0, 2) == "r.";
    const bool group = name.size() > 3 && name.substr(0, 3) == "sg.";
    if (!renderer && !(access == Access::Read && group)) return false;
    for (const auto c : name) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_')) return false;
    }
    return true;
}

// These are SDK-discovered indices, not game-layout offsets. Missing discovery
// must remain unavailable instead of allowing the SDK's synthetic zero fallback.
constexpr bool numeric_accessors_available(std::uint32_t as_command,
    std::uint32_t get_int, std::uint32_t get_float) noexcept {
    return as_command != 0 && get_int != 0 && get_float != 0;
}

enum class CachedAccess { Ready, Busy, NotCached, Incomplete };
constexpr CachedAccess cached_access(bool locked, bool present, std::uint32_t as_command,
    std::uint32_t get_int, std::uint32_t get_float) noexcept {
    if (!locked) return CachedAccess::Busy;
    if (!present) return CachedAccess::NotCached;
    return numeric_accessors_available(as_command, get_int, get_float) ?
        CachedAccess::Ready : CachedAccess::Incomplete;
}
constexpr const char* cached_access_error(CachedAccess access) noexcept {
    switch (access) {
    case CachedAccess::Busy: return "sdk_numeric_accessor_cache_busy";
    case CachedAccess::NotCached: return "sdk_numeric_accessors_not_cached";
    case CachedAccess::Incomplete: return "sdk_numeric_accessors_unavailable";
    case CachedAccess::Ready: return nullptr;
    }
    return "sdk_numeric_accessors_unavailable";
}

// Fixed bounded inventory: the inherited 14 startup names plus the current
// shadow/render diagnostic list. A caller cannot expand this into enumeration.
inline constexpr std::array<std::string_view, 41> snapshot_names{{
    "sg.ResolutionQuality", "sg.ViewDistanceQuality", "sg.AntiAliasingQuality",
    "sg.PostProcessQuality", "sg.ShadowQuality", "sg.TextureQuality",
    "sg.EffectsQuality", "sg.FoliageQuality", "sg.ShadingQuality",
    "sg.ReflectionQuality", "r.VSync", "r.VolumetricCloud",
    "sg.GlobalIlluminationQuality", "r.ReflectionMethod",
    "r.ShadowQuality", "r.Shadow.MaxResolution", "r.Shadow.CSM.MaxCascades",
    "r.Shadow.DistanceScale", "r.ContactShadows", "r.DistanceFieldShadowing",
    "r.ViewDistanceScale", "r.MaterialQualityLevel", "r.ScreenPercentage",
    "r.PostProcessAAQuality", "r.DefaultFeature.AntiAliasing", "r.MotionBlurQuality",
    "r.BloomQuality", "r.AmbientOcclusionLevels", "r.RefractionQuality", "r.DetailMode",
    "r.MaxAnisotropy", "r.MipMapLODBias", "r.SSR.Quality", "r.ReflectionEnvironment",
    "r.VolumetricFog", "r.OneFrameThreadLag", "r.NGX.DLSS.Enable",
    "r.RayTracing.Shadows", "r.RayTracing.Reflections", "r.ToonRimWidthFactor",
    "r.CLV.RefreshEveryFrame"
}};
static_assert(snapshot_names.size() <= 48);
} // namespace wuwa_console_read
