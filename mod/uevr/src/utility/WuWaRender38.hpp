#pragma once
// Reviewed 3.8 rendering routes, PE timestamp 0x6ae17e03 (12 Nov 2026). The reference stays 3.7 (0x6ace6377).
// The runtime self-port refuses these ranges on 3.8 because their code changed, not just moved. Each was
// aligned instruction by instruction against the 3.7 capture; every difference that is not a moved call/jump
// target or RIP displacement is listed below and reviewed. Only exact live bytes are accepted (live hash).
// Evidence and method: docs/RENDER-3.8-PORT.md. Same shape as WuWaLgui38.hpp (HUD/menu, by Codex).
// Another 3.8 build (WuWaBuild38.hpp) uses the same table through the 3.8 self-port: each range must have
// been found there and be unchanged apart from moved call/jump targets and RIP displacements.
//
// Mono authored camera (FFakeStereoRenderingHook.cpp ensure_mono_projection_hook):
//   GetProjectionData 0x526f1a0 -> 0x5014d50, same 0xc7a bytes and 728 instructions. Only the stack frame
//   grew by 0x30 above the low locals (lea rbp,[rsp-0xc20]->-0xc50, sub rsp 0xd20->0xd50, saved xmm slots,
//   rbp+0x790/0xaf8/0xb50/0xb60/0xc80 -> +0x10/+0x30). rbp-rsp stays 0x100, so the locals the +0x299 mid-hook
//   reads (rbp+0xd0 view info, +0xe8 FOV, +0xfc aspect, +0x100 constrain, +0x104 projection mode) are
//   unchanged, and the bytes at +0x299 are identical. FMinimalViewInfo builder 0x4eba980 -> 0x4bc37d0: only
//   [rbx+0xa40..0xa48] -> +0x30 (another object's fields, not read by the hook). The other three are moved.
// Cinematic native aspect metadata: CalcSceneView piece 0x52677f8 -> 0x500d368 moves its stack init struct
//   from rbp+0x950 to rbp+0x980; the init fields written (+0x289 constrain, +0x28c aspect) are unchanged.
//   The view constructor and LGUI ranges only moved.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "WuWaBuild38.hpp"

namespace wuwa_render_38 {
using wuwa_build_38::Build;
inline constexpr uint32_t timestamp = wuwa_build_38::timestamp;
struct Range { uint32_t reference, live, size; uint64_t reference_hash, hash; };
inline constexpr std::array<Range, 9> ranges{{
    // Mono authored camera
    {0x526f1a0, 0x5014d50, 0xc7a, 0xb4296fa03fdb95beULL, 0x80d172ef5c1fd3b9ULL},
    {0x4eba980, 0x4bc37d0, 0x782, 0x083cde4571bb87c1ULL, 0xe1de2725315bb0ceULL},
    {0x5669f50, 0x5410cc0, 0x44, 0xd2e1723937fd68dcULL, 0xd2e1723937fd68dcULL},
    {0x55d57a0, 0x53821d0, 0x22, 0x2f1e3aef861c6d36ULL, 0x1ff5f0e9b860b6c0ULL},
    {0x56354c0, 0x53f8530, 0x19a, 0x859f1b3dc2a1336bULL, 0x91a34038bcc967e4ULL},
    // Cinematic native aspect metadata
    {0x52677f8, 0x500d368, 0x49, 0x3e37bd2eb292a5e0ULL, 0x5a04c74c1961d898ULL},
    {0x54cc6a9, 0x5274e09, 0x5b, 0x79d0dbf2636663bcULL, 0x79d0dbf2636663bcULL},
    {0x54ccad7, 0x5275237, 0x4a, 0xa3c547b54c5d0cc1ULL, 0xa3c547b54c5d0cc1ULL},
    {0x41024ed, 0x3e59f3d, 0xfa, 0x4e2509c845b2f4c1ULL, 0x0db51be5c2eb7c28ULL},
}};

inline uint64_t fingerprint(std::span<const uint8_t> bytes) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto value : bytes) { hash ^= value; hash *= 0x100000001b3ULL; }
    return hash;
}

// nullopt: not a 3.8 build, or a range this table does not list (the caller's own rule applies).
// A listed range on 3.8 must match its source contract and its live code, or it is refused.
template<class Read> std::optional<bool> check(const Build& build, uint32_t reference, uint32_t size,
        uint64_t reference_hash, Read read) {
    if (!build.family()) return std::nullopt;
    for (const auto& r : ranges) {
        if (r.reference != reference) continue;
        if (r.size != size || r.reference_hash != reference_hash) return false;
        if (build.sibling()) return build.unchanged(r.live, size, r.hash);
        std::vector<uint8_t> bytes(size);
        return read(r.live, std::span<uint8_t>{bytes}) && fingerprint(bytes) == r.hash;
    }
    return std::nullopt;
}
template<class Read> std::optional<bool> check(uint32_t build, uint32_t reference, uint32_t size,
        uint64_t reference_hash, Read read) {
    return check(Build{build}, reference, size, reference_hash, read);
}

// A reference inside a listed range.
inline bool listed(uint32_t reference) {
    for (const auto& r : ranges)
        if (reference >= r.reference && reference - r.reference < r.size) return true;
    return false;
}

// Live RVA of a listed range start (or a place inside one, at the same distance) on 3.8. nullopt also
// when another 3.8 build lacks that range; live_address then refuses instead of using the 3.7 self-port.
inline std::optional<uint32_t> address(const Build& build, uint32_t reference) {
    if (!build.family()) return std::nullopt;
    for (const auto& r : ranges)
        if (reference >= r.reference && reference - r.reference < r.size) {
            const auto live = build.at(r.live);
            return live ? std::optional<uint32_t>{*live + (reference - r.reference)} : std::nullopt;
        }
    return std::nullopt;
}
inline std::optional<uint32_t> address(uint32_t build, uint32_t reference) { return address(Build{build}, reference); }

// Far lighting: the one-eye rebuild detector's code proof (WuWaClvMismatch.hpp). 3.8 forms the view
// state's CLV volume address with `mov rdi,[rdi+0x1ec8]; add rdi,0x27b8` at 0x3bf3f40 (3.7: rsi, 0x1e88,
// 0x27a8 at 0x3eaf36c). The volume object is 0x2c8 bytes in both builds and its destructor is unchanged.
inline constexpr uint32_t clv_site = 0x3bf3f40;
inline constexpr std::array<uint8_t, 14> clv_code{
    0x48, 0x8b, 0xbf, 0xc8, 0x1e, 0x00, 0x00, 0x48, 0x81, 0xc7, 0xb8, 0x27, 0x00, 0x00};
inline constexpr uint32_t clv_state_volume_offset = 0x27b8;
} // namespace wuwa_render_38
