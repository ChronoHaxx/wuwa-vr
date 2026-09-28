#pragma once
#include <array>
#include <cstdint>

namespace wuwa_lgui_redirect::compatibility {
// Generated from the validated September 8 mapped capture. These fingerprints
// check accidental binary incompatibility; they are not an authenticity check.
// Ranges are the inspected unwind fragments within 1 MiB of each function;
// leaf functions without unwind metadata use a 32-byte entry fingerprint.
struct CodeRange { uint32_t rva, size; uint64_t fingerprint; };
// Stage-0 setup/capture/execution plus the common callback and caller fragment
// containing the last-view registration condition. Kept separate from HUD.
inline constexpr std::array<CodeRange, 20> world_label_code_ranges{{
    {0x237a01d0, 0x41, 0x0b1e49996ee3b947ULL},
    {0x237a0211, 0x12, 0x23a814fbbfee31d0ULL},
    {0x237a0223, 0x6d, 0x2d412140168d6a7dULL},
    {0x237a0290, 0x19b, 0x6973550ae785e48aULL},
    {0x237a042b, 0x181, 0x318843efbacc51d2ULL},
    {0x237a05ac, 0x135, 0x761de1540d4f1622ULL},
    {0x237a06e1, 0x10, 0x76ba249dc185aaa5ULL},
    {0x237a06f1, 0x8, 0x09f1193ca0db958cULL},
    {0x237a06f9, 0xf, 0x95ddad66c305213aULL},
    {0x2379fb80, 0x71, 0x9dd1fdbdce55559dULL},
    {0x2379fbf1, 0x5f, 0x28d08288d8551c78ULL},
    {0x2379fc50, 0x5c, 0x900b1a60d5657d63ULL},
    {0x2377f3a0, 0xa5, 0xd61a1188fc644076ULL},
    {0x23782bb0, 0x30c, 0x8ce38a7f0b1c3385ULL},
    {0x22f16e70, 0x14d5, 0x0f9207a98cbb9c87ULL},
    {0x22eedef0, 0x50, 0x4a4e41626a5765dbULL},
    {0x22f06cf0, 0xb, 0x6829c5c640f125f5ULL},
    {0x23432050, 0x590e, 0x877ac2a9672ea611ULL},
    {0x2378f750, 0x115, 0x224e881f6b196429ULL},
    {0x237a8780, 0xe00, 0x1a4fd993b0803b96ULL},
}};
// World-space stage-0 constructor and execution body. Independent from the
// working HUD/menu routes; used only for a read-only recording probe.
inline constexpr std::array<CodeRange, 2> stage0_probe_code_ranges{{
    {0x2377f3a0, 0xa5, 0xd61a1188fc644076ULL},
    {0x23782bb0, 0x30c, 0x8ce38a7f0b1c3385ULL},
}};
inline constexpr std::array<CodeRange, 30> code_ranges{{
    {0x210b6870, 0x258, 0x0b8e11cdb13438deULL},
    {0x2377f7a0, 0xc6, 0xe82a45646fcee970ULL},
    {0x23786090, 0x31, 0x4fce761e70d8c976ULL},
    {0x237860c1, 0x176, 0x12b195f3bc483327ULL},
    {0x23786237, 0x1f8, 0x11369bcb560be6f5ULL},
    {0x2378642f, 0xec9, 0xbec7aa595a3c9ee7ULL},
    {0x237872f8, 0x94, 0x7a27acd0627ea4b5ULL},
    {0x2378738c, 0x119, 0xadc4b415b41b34e3ULL},
    {0x237a18c0, 0x35, 0x506e17c5993143f3ULL},
    {0x237a18f5, 0x12, 0x64fea25de494faefULL},
    {0x237a1907, 0xef, 0x9fd5ae5f7d37ea27ULL},
    {0x237a19f6, 0x4ae, 0x100fb95c0bd09b8aULL},
    {0x237a1ea4, 0x68, 0x43fd52bd67159962ULL},
    {0x237a1f0c, 0x8e, 0x44961f16a10fb4d9ULL},
    {0x237a1f9a, 0x8, 0x1e18aacede1f65daULL},
    {0x237a1fa2, 0xd, 0x44706d1d968d816dULL},
    {0x237a1faf, 0x179, 0xfb6a4e727d857685ULL},
    // RDG base constructor: name/type/flags and initially-null RHI fields.
    {0x23e1b900, 0x3f, 0xab50b9240543d632ULL},
    {0x23e31570, 0xcc, 0xb68714d9cae379f0ULL},
    {0x23e3163c, 0x8, 0x03903611b3afc965ULL},
    {0x23e31644, 0xdc, 0xa3848293af9ffc7bULL},
    {0x23e31720, 0x192, 0x424ca20ffa2d1704ULL},
    {0x23e318b2, 0x21, 0xb8f051d1128a216bULL},
    {0x23e4adc0, 0x11e, 0x649eaa8382949025ULL},
    {0x23e533e0, 0x20, 0xe6463afa95503bdaULL},
    {0x23e57e10, 0x265, 0x4a135ee146f44abeULL},
    {0x23e668d0, 0x1d, 0x9928daf8b512d77bULL},
    {0x23e668ed, 0x68, 0x4bed7c4dbb53c591ULL},
    {0x23e66955, 0x6f, 0x6f42bbc89590eafdULL},
    {0x23e669c4, 0xb, 0xc018694955d28651ULL},
}};

// Esc/item-popup stage: independently checked before installing its hooks.
// A mismatch here leaves the established HUD route available.
inline constexpr std::array<CodeRange, 22> menu_code_ranges{{
    {0x23e1b940, 0xdc, 0xcc3a83fa78c72c17ULL},
    {0x23791340, 0xc, 0x242ddbd206e4505eULL},
    {0x2377f2d0, 0xc6, 0x337ebee2a6bb6ef0ULL},
    {0x23782760, 0x3c, 0x358c7d24197df8d3ULL},
    {0x2378279c, 0x1fb, 0x1fef60e51f7150aaULL},
    {0x23782997, 0x8c, 0xfb155a0b2b29e618ULL},
    {0x23782a23, 0x11a, 0xfb56f0dd30830144ULL},
    {0x237956d0, 0x9c, 0x25b2c40b26abb62cULL},
    {0x2379576c, 0x10c, 0x7606028be7e03db8ULL},
    {0x23795878, 0x36, 0x66f474a86fb656acULL},
    {0x23798800, 0x2a, 0xbd98528a72c9f342ULL},
    {0x2379882a, 0xef, 0xf98c5f81c0ccee05ULL},
    {0x23798919, 0x1b, 0x54bab7cfd2604a9fULL},
    {0x2379fcb0, 0xb9, 0xffb9f422118e2009ULL},
    {0x2379fd69, 0x13, 0xc4914c8c1f3dd03eULL},
    {0x2379fd7c, 0xf, 0xd07e79efcc7a972bULL},
    {0x2379fd8b, 0x30, 0xd553b0f2c50f8278ULL},
    {0x2379fdbb, 0x2c1, 0x48e1c6bc70747f13ULL},
    {0x237a007c, 0x6e, 0x9106d0aa5178c3d8ULL},
    {0x237a00ea, 0x4b, 0x3b35ce7b39166481ULL},
    {0x237a0135, 0x8e, 0xac6881fa1da98fe0ULL},
    {0x237a01c3, 0xa, 0x64f168621e06bd1eULL},
}};
} // namespace wuwa_lgui_redirect::compatibility
