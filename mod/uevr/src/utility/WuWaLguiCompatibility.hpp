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
    {0x4116a70, 0x41, 0xad721e0c2d602a20ULL},
    {0x4116ab1, 0x12, 0x23a814fbbfee31d0ULL},
    {0x237a0223, 0x6d, 0x2d412140168d6a7dULL},
    {0x4116b30, 0x19b, 0xec8ce6339eaf674bULL},
    {0x237a042b, 0x181, 0x318843efbacc51d2ULL},
    {0x4116e4c, 0x135, 0x79cdb27d73c35d82ULL},
    {0x4116f81, 0x10, 0x76ba249dc185aaa5ULL},
    {0x4116f91, 0x8, 0x09f1193ca0db958cULL},
    {0x4116f99, 0xf, 0x95ddad66c305213aULL},
    {0x2379fb80, 0x71, 0x9dd1fdbdce55559dULL},
    {0x4116491, 0x5f, 0x28d08288d8551c78ULL},
    {0x41164f0, 0x5c, 0xea8169aff99c0522ULL},
    {0x40f7be0, 0xa5, 0xc85c78e144ffd263ULL},
    {0x23782bb0, 0x30c, 0x8ce38a7f0b1c3385ULL},
    {0x22f16e70, 0x14d5, 0x0f9207a98cbb9c87ULL},
    {0x3869fa0, 0x50, 0x2c64c2256c44fef0ULL},
    {0x3883610, 0xb, 0x6829c5c640f125f5ULL},
    {0x23432050, 0x590e, 0x877ac2a9672ea611ULL},
    {0x2378f750, 0x115, 0x224e881f6b196429ULL},
    {0x237a8780, 0xe00, 0x1a4fd993b0803b96ULL},
}};
// World-space stage-0 constructor and execution body. Independent from the
// working HUD/menu routes; used only for a read-only recording probe.
inline constexpr std::array<CodeRange, 2> stage0_probe_code_ranges{{
    {0x40f7be0, 0xa5, 0xc85c78e144ffd263ULL},
    {0x23782bb0, 0x30c, 0x8ce38a7f0b1c3385ULL},
}};
inline constexpr std::array<CodeRange, 30> code_ranges{{
    {0xf09af0, 0x258, 0xedae9041b63c8f2eULL},
    {0x40f7df0, 0xc6, 0x1bfe801927a29ec1ULL},
    {0x4100f90, 0x31, 0x9764b7e25b4dbdffULL},
    {0x4100fc1, 0x176, 0x154c7e31f35be452ULL},
    {0x4101137, 0x1f8, 0x1104a41fddee0692ULL},
    {0x410132f, 0xec9, 0xb05c9c69401bdfdeULL},
    {0x41021f8, 0x94, 0x9db89b1c536c3f9fULL},
    {0x410228c, 0x119, 0xb9a080681e452e57ULL},
    {0x4118160, 0x35, 0xe2c9dadd3d2b8cdeULL},
    {0x4118195, 0x12, 0x64fea25de494faefULL},
    {0x41181a7, 0xef, 0x9b640b669c20c51dULL},
    {0x4118296, 0x4ae, 0x8901ac272831fc20ULL},
    {0x4118744, 0x68, 0x0645cf45599474d9ULL},
    {0x41187ac, 0x8e, 0xad954a13333f9b21ULL},
    {0x411883a, 0x8, 0x1e18aacede1f65daULL},
    {0x4118842, 0xd, 0x44706d1d968d816dULL},
    {0x411884f, 0x179, 0xdda68bd12ed08663ULL},
    // RDG base constructor: name/type/flags and initially-null RHI fields.
    {0x4810e70, 0x3f, 0x2eed435c49dc4674ULL},
    {0x4827570, 0xcc, 0xcbe668816609e1d6ULL},
    {0x482763c, 0x8, 0x03903611b3afc965ULL},
    {0x4827644, 0xdc, 0x39c9c699e2ee3639ULL},
    {0x4827720, 0x192, 0x2656f0fde48bc3c8ULL},
    {0x48278b2, 0x21, 0x3f6932ffdec63580ULL},
    {0x4840e00, 0x11e, 0x12a2d84700e9aca8ULL},
    {0x48492d0, 0x20, 0xe6463afa95503bdaULL},
    {0x484dd50, 0x265, 0x98b492cdc37334b7ULL},
    {0x485c850, 0x1d, 0x9928daf8b512d77bULL},
    {0x485c86d, 0x68, 0xced4e824f2b8e398ULL},
    {0x485c8d5, 0x6f, 0x06982f8cd21c0324ULL},
    {0x485c944, 0xb, 0xc018694955d28651ULL},
}};

// Esc/item-popup stage: independently checked before installing its hooks.
// A mismatch here leaves the established HUD route available.
inline constexpr std::array<CodeRange, 22> menu_code_ranges{{
    {0x4810eb0, 0xdc, 0x154e16e9cd0a1fe2ULL},
    {0x410bfc0, 0xc, 0xeb3da8378563a66bULL},
    {0x40f7ec0, 0xc6, 0xcfdc233a2ea3d650ULL},
    {0x41023b0, 0x3c, 0xf877bce57ddb07daULL},
    {0x41023ec, 0x1fb, 0x5570b9b8f345c208ULL},
    {0x41025e7, 0x8c, 0x22cccbe63e95da28ULL},
    {0x4102673, 0x11a, 0x0301c777b8fbbb9aULL},
    {0x410e580, 0x9c, 0x41722bf6178b8d15ULL},
    {0x410e61c, 0x10c, 0x1cecdb74beb3ad63ULL},
    {0x410e728, 0x36, 0xf8a486ddea9d0d00ULL},
    {0x4111dc0, 0x2a, 0xbd98528a72c9f342ULL},
    {0x4111dea, 0xef, 0xf98c5f81c0ccee05ULL},
    {0x4111ed9, 0x1b, 0x54bab7cfd2604a9fULL},
    {0x4116550, 0xb9, 0x60afcb5ce23e497dULL},
    {0x4116609, 0x13, 0xc4914c8c1f3dd03eULL},
    {0x411661c, 0xf, 0xd07e79efcc7a972bULL},
    {0x411662b, 0x30, 0xdb0a9127fec2a53aULL},
    {0x411665b, 0x2c1, 0x48bd8fd275b256d3ULL},
    {0x411691c, 0x6e, 0x1e6422872a78b88fULL},
    {0x411698a, 0x4b, 0x628d4583f007d8dbULL},
    {0x41169d5, 0x8e, 0x6061598424f7f731ULL},
    {0x4116a63, 0xa, 0x64f168621e06bd1eULL},
}};
// LGUICanvas GetProjectionMatrix, view-rect getter, GetViewportSize and the two
// UI point conversions, whole bodies (read on 3.7 14 Oct 2026, unchanged in 0x6ace6377). Each
// asks the local player for the left-eye projection data; see WuWaLguiViewport.hpp.
// Then the pointer ray (GenerateRay) up to its view-rect call: the event data stays in
// rbp there, which the taller layout reads to tell the mouse apart (read 15 Oct 2026).
inline constexpr std::array<CodeRange, 6> viewport_code_ranges{{
    {0xc63c50, 0x1b4, 0xe7bdb6a9a75d2513ULL},
    {0xc66cc0, 0xfc, 0x732abdc7fb02bdf7ULL},
    {0xc66dc0, 0x10e, 0x8c087ac36c51d260ULL},
    {0xc946a0, 0x4d6, 0x0037b05e2a814eeaULL},
    {0xc94b80, 0x432, 0x8175d613282af1e9ULL},
    {0xd58a50, 0xc5, 0x264c1229681aea07ULL},
}};
} // namespace wuwa_lgui_redirect::compatibility
