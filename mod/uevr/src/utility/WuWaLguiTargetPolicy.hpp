#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wuwa_lgui_redirect {

enum class GraphStatus : size_t { valid, unreadable_object, other_vtable, unreadable_rhi, null_rhi, count };

struct RejectedDescription {
    GraphStatus status{};
    uintptr_t vtable_rva{};
    std::array<char, 48> name{'?'};
    std::array<int32_t, 2> size{};
    uint8_t format{}, type{}, flags{};
    bool metadata_valid{};
    bool operator==(const RejectedDescription&) const = default;
};

// The September 24 run observed this exact graph class/format/name with no RHI
// allocated at setup. A null RHI alone never identifies a UI draw. This policy
// was observed at the fingerprinted stage-2 0x237a18c0 hook. It is a live,
// default-off comparison, not a claim that every Tonemap pass contains UI.
inline constexpr bool is_transient_menu_candidate(const RejectedDescription& value,
        const std::array<int32_t, 2>& game_size, bool native_stereo_fix) noexcept {
    constexpr std::array<char, 48> expected_name{'T', 'o', 'n', 'e', 'm', 'a', 'p'};
    if (native_stereo_fix || value.status != GraphStatus::null_rhi || !value.metadata_valid ||
        value.vtable_rva != 0x2759ec60 || value.name != expected_name || value.type != 0 || value.format != 37) return false;
    if (value.flags != 0x30 && value.flags != 0x34 && value.flags != 0xb0 && value.flags != 0xb4) return false;
    for (size_t i = 0; i < 2; ++i) {
        if (game_size[i] < 64 || game_size[i] > 16384 || value.size[i] < 64 || value.size[i] > game_size[i]) return false;
    }
    return true;
}

// Stage 3's September 24 Esc capture uses the current game ViewFamilyTexture,
// not stage 2's transient Tonemap. Only the four measured descriptors qualify.
// The caller also verifies positive RHI identity, mesh counts and no bound depth.
inline constexpr bool is_stage3_view_family_target(const RejectedDescription& v,
        const std::array<int32_t, 2>& game_size, bool native_stereo_fix) noexcept {
    constexpr std::array<char, 48> name{'V','i','e','w','F','a','m','i','l','y','T','e','x','t','u','r','e'};
    return !native_stereo_fix && v.status == GraphStatus::valid && v.metadata_valid &&
        v.vtable_rva == 0x2759ec60 && v.name == name && v.type == 0 && v.format == 2 &&
        (v.flags == 0x31 || v.flags == 0x35 || v.flags == 0xb1 || v.flags == 0xb5) &&
        game_size[0] >= 64 && game_size[0] <= 16384 &&
        game_size[1] >= 64 && game_size[1] <= 16384 && v.size == game_size;
}

} // namespace wuwa_lgui_redirect
