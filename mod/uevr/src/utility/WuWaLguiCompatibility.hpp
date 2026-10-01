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
    {0x4116330, 0x41, 0x60efb6b02523bbedULL},
    {0x4116371, 0x12, 0x23a814fbbfee31d0ULL},
    {0x237a0223, 0x6d, 0x2d412140168d6a7dULL},
    {0x41163f0, 0x19b, 0x793908e70401fab8ULL},
    {0x237a042b, 0x181, 0x318843efbacc51d2ULL},
    {0x411670c, 0x135, 0x293c3cf0a6b46ba7ULL},
    {0x4116841, 0x10, 0x76ba249dc185aaa5ULL},
    {0x4116851, 0x8, 0x09f1193ca0db958cULL},
    {0x4116859, 0xf, 0x95ddad66c305213aULL},
    {0x2379fb80, 0x71, 0x9dd1fdbdce55559dULL},
    {0x4115d51, 0x5f, 0x28d08288d8551c78ULL},
    {0x4115db0, 0x5c, 0xe647638338ad0636ULL},
    {0x40f74a0, 0xa5, 0xc398a6ef709686d1ULL},
    {0x23782bb0, 0x30c, 0x8ce38a7f0b1c3385ULL},
    {0x22f16e70, 0x14d5, 0x0f9207a98cbb9c87ULL},
    {0x3869860, 0x50, 0x5a5ba29d414b5995ULL},
    {0x3882ed0, 0xb, 0x6829c5c640f125f5ULL},
    {0x23432050, 0x590e, 0x877ac2a9672ea611ULL},
    {0x2378f750, 0x115, 0x224e881f6b196429ULL},
    {0x237a8780, 0xe00, 0x1a4fd993b0803b96ULL},
}};
// World-space stage-0 constructor and execution body. Independent from the
// working HUD/menu routes; used only for a read-only recording probe.
inline constexpr std::array<CodeRange, 2> stage0_probe_code_ranges{{
    {0x40f74a0, 0xa5, 0xc398a6ef709686d1ULL},
    {0x23782bb0, 0x30c, 0x8ce38a7f0b1c3385ULL},
}};
inline constexpr std::array<CodeRange, 30> code_ranges{{
    {0xf095f0, 0x258, 0xb39f63d5b5f2e4a8ULL},
    {0x40f76b0, 0xc6, 0xd53d528e46608d43ULL},
    {0x4100850, 0x31, 0x0349aa374cdb0912ULL},
    {0x4100881, 0x176, 0x69f9007ec8072e26ULL},
    {0x41009f7, 0x1f8, 0xea112b748afed1c8ULL},
    {0x4100bef, 0xec9, 0xa18069ac25bbf34fULL},
    {0x4101ab8, 0x94, 0xed918a632eecdf58ULL},
    {0x4101b4c, 0x119, 0x8c59521a02d2c617ULL},
    {0x4117a20, 0x35, 0xbefbc3fb498984c6ULL},
    {0x4117a55, 0x12, 0x64fea25de494faefULL},
    {0x4117a67, 0xef, 0x3cc296d25af4890bULL},
    {0x4117b56, 0x4ae, 0xe1772578b569e6d5ULL},
    {0x4118004, 0x68, 0xa405d07bf5396f75ULL},
    {0x411806c, 0x8e, 0xceeb5912641bf353ULL},
    {0x41180fa, 0x8, 0x1e18aacede1f65daULL},
    {0x4118102, 0xd, 0x44706d1d968d816dULL},
    {0x411810f, 0x179, 0x6b27b5a43a696b14ULL},
    // RDG base constructor: name/type/flags and initially-null RHI fields.
    {0x4810760, 0x3f, 0x93cd07b16d964f4aULL},
    {0x4826e60, 0xcc, 0xd2fb5fdd25c69d95ULL},
    {0x4826f2c, 0x8, 0x03903611b3afc965ULL},
    {0x4826f34, 0xdc, 0xab70cda64d845a50ULL},
    {0x4827010, 0x192, 0x2656f0fde48bc3c8ULL},
    {0x48271a2, 0x21, 0x3008d516515abeb7ULL},
    {0x48406f0, 0x11e, 0xaebcbf244cfdb426ULL},
    {0x4848bc0, 0x20, 0xe6463afa95503bdaULL},
    {0x484d640, 0x265, 0xc43b18cff99b7860ULL},
    {0x485c140, 0x1d, 0x9928daf8b512d77bULL},
    {0x485c15d, 0x68, 0x20feca6d144657cfULL},
    {0x485c1c5, 0x6f, 0xe83d53547bd0d731ULL},
    {0x485c234, 0xb, 0xc018694955d28651ULL},
}};

// Esc/item-popup stage: independently checked before installing its hooks.
// A mismatch here leaves the established HUD route available.
inline constexpr std::array<CodeRange, 22> menu_code_ranges{{
    {0x48107a0, 0xdc, 0x4da4938c895beb34ULL},
    {0x410b880, 0xc, 0xeb3da8378563a66bULL},
    {0x40f7780, 0xc6, 0x9598bf53eb3f968aULL},
    {0x4101c70, 0x3c, 0xbcce65f21e6109e1ULL},
    {0x4101cac, 0x1fb, 0xc04618a4efcfdeb0ULL},
    {0x4101ea7, 0x8c, 0x7929b0e85419efd7ULL},
    {0x4101f33, 0x11a, 0x66fa78448ff02cc3ULL},
    {0x410de40, 0x9c, 0xc231b2fae85ccea1ULL},
    {0x410dedc, 0x10c, 0x98412b2080b153b0ULL},
    {0x410dfe8, 0x36, 0xb3225c06ea38a341ULL},
    {0x4111680, 0x2a, 0xbd98528a72c9f342ULL},
    {0x41116aa, 0xef, 0xf98c5f81c0ccee05ULL},
    {0x4111799, 0x1b, 0x54bab7cfd2604a9fULL},
    {0x4115e10, 0xb9, 0xc5a64a9997c2b687ULL},
    {0x4115ec9, 0x13, 0xc4914c8c1f3dd03eULL},
    {0x4115edc, 0xf, 0xd07e79efcc7a972bULL},
    {0x4115eeb, 0x30, 0x1e70a1eb5047cb00ULL},
    {0x4115f1b, 0x2c1, 0x777b1c17eee7cc9fULL},
    {0x41161dc, 0x6e, 0x51521ef9cbdde1cfULL},
    {0x411624a, 0x4b, 0x31fa935a4062ff89ULL},
    {0x4116295, 0x8e, 0x41ee189bd6f0325cULL},
    {0x4116323, 0xa, 0x64f168621e06bd1eULL},
}};
} // namespace wuwa_lgui_redirect::compatibility
