#pragma once
// Reviewed 3.8 UI port, PE timestamp 0x6ae17e03. The reference remains 3.7.
// Only HUD, menu and viewport ranges are accepted here; every byte is checked,
// including relative targets. No change to the generic self-port acceptance rules.
// Evidence and review: docs/UI-3.8-PORT.md; full captures stay outside the repo.
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wuwa_lgui_38 {
inline constexpr uint32_t timestamp = 0x6ae17e03;
struct Range { uint32_t reference, live, size; uint64_t reference_hash, hash; };
inline constexpr std::array<Range, 58> ranges{{
    {0xf09af0, 0xa17d20, 0x258, 0xedae9041b63c8f2eULL, 0x1c247812aaca4ed9ULL},
    {0x40f7df0, 0x3e529e0, 0xc6, 0x1bfe801927a29ec1ULL, 0xd0b66e21795fe6d5ULL},
    {0x4100f90, 0x3e5a870, 0x31, 0x9764b7e25b4dbdffULL, 0xf0beab7a8cc1adf0ULL},
    {0x4100fc1, 0x3e5a8a1, 0x176, 0x154c7e31f35be452ULL, 0xc3e78950c6c750c7ULL},
    {0x4101137, 0x3e5aa17, 0x1f8, 0x1104a41fddee0692ULL, 0x7bb65d5f0452ed68ULL},
    {0x410132f, 0x3e5ac0f, 0xec9, 0xb05c9c69401bdfdeULL, 0x156eed0b0d21cb44ULL},
    {0x41021f8, 0x3e5bad8, 0x94, 0x9db89b1c536c3f9fULL, 0x3b878aa130666924ULL},
    {0x410228c, 0x3e5bb6c, 0x119, 0xb9a080681e452e57ULL, 0x7d39d14dc22a0086ULL},
    {0x4118160, 0x3e70bc0, 0x35, 0xe2c9dadd3d2b8cdeULL, 0xefa9b292bda9917cULL},
    {0x4118195, 0x3e70bf5, 0x12, 0x64fea25de494faefULL, 0xd6d866891f5dbfb7ULL},
    {0x41181a7, 0x3e70c07, 0xef, 0x9b640b669c20c51dULL, 0x13b1c0b294195b84ULL},
    {0x4118296, 0x3e70cf6, 0x4ae, 0x8901ac272831fc20ULL, 0x1ba5cc09b240101eULL},
    {0x4118744, 0x3e711a4, 0x68, 0x0645cf45599474d9ULL, 0x6a5185997ba59575ULL},
    {0x41187ac, 0x3e7120c, 0x8e, 0xad954a13333f9b21ULL, 0x80cb43dbdf5eaddbULL},
    {0x411883a, 0x3e7129a, 0x8, 0x1e18aacede1f65daULL, 0x1e18aacede1f65daULL},
    {0x4118842, 0x3e712a2, 0xd, 0x44706d1d968d816dULL, 0x44706d1d968d816dULL},
    {0x411884f, 0x3e712af, 0x179, 0xdda68bd12ed08663ULL, 0xd4ca222aa4566c5fULL},
    {0x4810e70, 0x44faf60, 0x3f, 0x2eed435c49dc4674ULL, 0x7fd73b0db7eac616ULL},
    {0x4827570, 0x4511660, 0xcc, 0xcbe668816609e1d6ULL, 0xee0ed32a5f97e283ULL},
    {0x482763c, 0x451172c, 0x8, 0x03903611b3afc965ULL, 0x03903611b3afc965ULL},
    {0x4827644, 0x4511734, 0xdc, 0x39c9c699e2ee3639ULL, 0x648e0b1ebe597f52ULL},
    {0x4827720, 0x4511810, 0x192, 0x2656f0fde48bc3c8ULL, 0x692c0a34c7e831bfULL},
    {0x48278b2, 0x45119a2, 0x21, 0x3f6932ffdec63580ULL, 0xa08ddc8c82dee96dULL},
    {0x4840e00, 0x452aef0, 0x11e, 0x12a2d84700e9aca8ULL, 0xe40132d40710a5abULL},
    {0x48492d0, 0x4533520, 0x20, 0xe6463afa95503bdaULL, 0xe6463afa95503bdaULL},
    {0x484dd50, 0x4537fa0, 0x265, 0x98b492cdc37334b7ULL, 0xc911b68930d3d362ULL},
    {0x485c850, 0x4546e30, 0x1d, 0x9928daf8b512d77bULL, 0x9928daf8b512d77bULL},
    {0x485c86d, 0x4546e4d, 0x68, 0xced4e824f2b8e398ULL, 0x1a97c7c2d07ae86cULL},
    {0x485c8d5, 0x4546eb5, 0x6f, 0x06982f8cd21c0324ULL, 0xc232eb659c7ec81cULL},
    {0x485c944, 0x4546f24, 0xb, 0xc018694955d28651ULL, 0xc018694955d28651ULL},
    {0x4810eb0, 0x44fafa0, 0xdc, 0x154e16e9cd0a1fe2ULL, 0x77e0351f4bbdb61eULL},
    {0x410bfc0, 0x3e65620, 0xc, 0xeb3da8378563a66bULL, 0x8b2504bb00321cafULL},
    {0x40f7ec0, 0x3e52770, 0xc6, 0xcfdc233a2ea3d650ULL, 0xc3c25ff50cbdb5a9ULL},
    {0x41023b0, 0x3e59e00, 0x3c, 0xf877bce57ddb07daULL, 0x814272b72e51351bULL},
    {0x41023ec, 0x3e59e3c, 0x1fb, 0x5570b9b8f345c208ULL, 0xb1ed3c0103529edbULL},
    {0x41025e7, 0x3e5a037, 0x8c, 0x22cccbe63e95da28ULL, 0x00de8b88da7e9ba8ULL},
    {0x4102673, 0x3e5a0c3, 0x11a, 0x0301c777b8fbbb9aULL, 0x7d7ae6793bfe87c8ULL},
    {0x410e580, 0x3e678d0, 0x9c, 0x41722bf6178b8d15ULL, 0x9cc0df4311e95ae2ULL},
    {0x410e61c, 0x3e6796c, 0x10c, 0x1cecdb74beb3ad63ULL, 0x91cc976b6da4070fULL},
    {0x410e728, 0x3e67a78, 0x36, 0xf8a486ddea9d0d00ULL, 0x8ce17817eb0f6476ULL},
    {0x4111dc0, 0x3e6afb0, 0x2a, 0xbd98528a72c9f342ULL, 0x4b140f69feb8c09aULL},
    {0x4111dea, 0x3e6afda, 0xef, 0xf98c5f81c0ccee05ULL, 0x8b655610fccf8685ULL},
    {0x4111ed9, 0x3e6b0c9, 0x1b, 0x54bab7cfd2604a9fULL, 0x54bab7cfd2604a9fULL},
    {0x4116550, 0x3e6efb0, 0xb9, 0x60afcb5ce23e497dULL, 0x9e91b644eb6d4527ULL},
    {0x4116609, 0x3e6f069, 0x13, 0xc4914c8c1f3dd03eULL, 0xc4914c8c1f3dd03eULL},
    {0x411661c, 0x3e6f07c, 0xf, 0xd07e79efcc7a972bULL, 0xd07e79efcc7a972bULL},
    {0x411662b, 0x3e6f08b, 0x30, 0xdb0a9127fec2a53aULL, 0x9a45dea480765b66ULL},
    {0x411665b, 0x3e6f0bb, 0x2c1, 0x48bd8fd275b256d3ULL, 0xa808ef3f28884269ULL},
    {0x411691c, 0x3e6f37c, 0x6e, 0x1e6422872a78b88fULL, 0x1b42af703baa873fULL},
    {0x411698a, 0x3e6f3ea, 0x4b, 0x628d4583f007d8dbULL, 0x855da5da4a1d1879ULL},
    {0x41169d5, 0x3e6f435, 0x8e, 0x6061598424f7f731ULL, 0x03a495ec0e4f27c7ULL},
    {0x4116a63, 0x3e6f4c3, 0xa, 0x64f168621e06bd1eULL, 0x64f168621e06bd1eULL},
    {0xc63c50, 0xc7b500, 0x1b4, 0xe7bdb6a9a75d2513ULL, 0x078722ec74698d65ULL},
    {0xc66cc0, 0xc7e570, 0xfc, 0x732abdc7fb02bdf7ULL, 0x94688c7f32f6fffcULL},
    {0xc66dc0, 0xc7e670, 0x10e, 0x8c087ac36c51d260ULL, 0x6ba3fcbf3a0a1aa3ULL},
    {0xc946a0, 0xcab170, 0x4d6, 0x0037b05e2a814eeaULL, 0x702f9831678a38fcULL},
    {0xc94b80, 0xcab650, 0x432, 0x8175d613282af1e9ULL, 0xa82beaa7370bd1ddULL},
    {0xd58a50, 0xd6e480, 0xc5, 0x264c1229681aea07ULL, 0x6e31751ff19a2d67ULL},
}};
inline uint64_t fingerprint(std::span<const uint8_t> bytes) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto value : bytes) { hash ^= value; hash *= 0x100000001b3ULL; }
    return hash;
}
// nullopt means this is not the reviewed build; use the original compatibility gate.
// On 3.8, an unknown/modified source range is refused, not silently accepted.
template<class Read> std::optional<bool> check(uint32_t build, uint32_t reference, uint32_t size,
        uint64_t reference_hash, Read read) {
    if (build != timestamp) return std::nullopt;
    for (const auto& r : ranges) {
        if (r.reference != reference || r.size != size || r.reference_hash != reference_hash) continue;
        std::vector<uint8_t> bytes(size);
        return read(r.live, std::span<uint8_t>{bytes}) && fingerprint(bytes) == r.hash;
    }
    return false;
}
inline std::optional<uint32_t> address(uint32_t build, uint32_t reference) {
    if (build != timestamp) return std::nullopt;
    // The execute thunk is found through its jump to 0x3e59e00. A similar thunk
    // at 0x3e64da0 calls another function and must never be used here.
    if (reference == 0x7feada8) return 0x7d3a3b8; // vtable: execute at +8
    for (const auto& r : ranges) if (r.reference == reference) return r.live;
    return std::nullopt;
}
struct Layout {
    uint32_t renderer_views, renderer_count, view_stride;
    uint32_t early_counts, dispatch_counts, depth_flag, render_rect;
};
inline constexpr Layout reference_layout{0x108, 0x110, 0xe940, 0x2164, 0x922c, 0x25ac, 0x1e70};
inline constexpr Layout reviewed_layout{0x110, 0x118, 0xe9b0, 0x21a4, 0x928c, 0x25f4, 0x1eb0};
inline const Layout& layout(uint32_t build) {
    return build == timestamp ? reviewed_layout : reference_layout;
}
} // namespace wuwa_lgui_38
