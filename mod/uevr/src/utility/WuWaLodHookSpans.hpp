#pragma once
#include <array>
#include <cstdint>
#include <span>

// Complete single-instruction spans proved against the saved native image.
// Check SafetyHook's actual displaced bytes while StartDisabled, before enable.
// A longer fallback can cover an incoming branch target despite valid hashes.
namespace wuwa_lod_hook_spans {
struct Site { uint32_t rva; uint8_t size; std::array<uint8_t,7> bytes; };
inline constexpr std::array<Site,5> sites{{
    {0x51c6e43,5,{0x41,0x8b,0x44,0x24,0x12}},
    {0x54e775d,7,{0x48,0x89,0xbd,0xf0,0x00,0x00,0x00}},
    {0x3f97251,7,{0x49,0x39,0x9e,0xf0,0x00,0x00,0x00}},
    {0x3f972bf,7,{0x49,0x89,0x9e,0xf0,0x00,0x00,0x00}},
    {0x3faf856,6,{0x8b,0x05,0x2c,0x81,0x2d,0x14}},
}};
inline bool valid(uint32_t rva,std::span<const uint8_t> displaced) noexcept {
    for(const auto& site:sites) {
        if(site.rva!=rva) continue;
        if(displaced.size()!=site.size) return false;
        for(size_t i=0;i<displaced.size();++i)
            if(displaced[i]!=site.bytes[i]) return false;
        return true;
    }
    return false;
}
} // namespace wuwa_lod_hook_spans
