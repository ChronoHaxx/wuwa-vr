#pragma once
#include <cstdint>
#include <optional>

namespace wuwa_scene_frame_policy {
// Verified against BeginRenderingViewFamily's actual dispatch and store, not
// the generic SDK's first INC instruction. Its WuWa fallback picks +0x64e4,
// an unrelated object add/remove counter; the real method is vtable slot 207.
constexpr uintptr_t scene_vtable_rva = 0x277a0d70;
constexpr uintptr_t counter_offset = 0x64e8;
constexpr uintptr_t family_scene_offset = 0x20;
constexpr uintptr_t family_frame_offset = 0x64;

template<class T, class Read>
bool field(uintptr_t base, uintptr_t offset, T& value, Read read) {
    return base && offset <= UINTPTR_MAX-base &&
        sizeof(T)-1 <= UINTPTR_MAX-(base+offset) && read(base+offset,value);
}

template<class Read>
bool scene_matches(uintptr_t base, uintptr_t scene, Read read) {
    if (!base || base > UINTPTR_MAX-scene_vtable_rva-208*sizeof(uintptr_t) ||
        !scene || (scene & (alignof(uintptr_t)-1))) return false;
    uintptr_t vtable{}, render_scene{}, get_frame{}, increment{};
    return field(scene,0,vtable,read) && vtable==base+scene_vtable_rva &&
        field(vtable,189*sizeof(uintptr_t),render_scene,read) && render_scene==base+0x203e8df0 &&
        field(vtable,206*sizeof(uintptr_t),get_frame,read) && get_frame==base+0x235cdf60 &&
        field(vtable,207*sizeof(uintptr_t),increment,read) && increment==base+0x235d1d80;
}

struct Pair {
    uintptr_t base{}, family{}, scene{};
    uint32_t before{};
    uint32_t expected() const { return before+uint32_t{1}; }
};

template<class Read>
std::optional<Pair> prepare(uintptr_t base, uintptr_t family, Read read) {
    uintptr_t scene{};
    uint32_t frame{};
    if (!field(family,family_scene_offset,scene,read) || !scene_matches(base,scene,read) ||
        !field(scene,counter_offset,frame,read)) return {};
    return Pair{base,family,scene,frame};
}

template<class Read>
bool same_scene(const Pair& p, Read read) {
    uintptr_t scene{};
    return field(p.family,family_scene_offset,scene,read) && scene==p.scene &&
        scene_matches(p.base,scene,read);
}

template<class Read>
bool advanced_once(const Pair& p, Read read) {
    uint32_t scene_frame{}, family_frame{};
    return same_scene(p,read) && field(p.scene,counter_offset,scene_frame,read) &&
        field(p.family,family_frame_offset,family_frame,read) &&
        scene_frame==p.expected() && family_frame==p.expected();
}

template<class Read, class CompareExchange>
bool rewind(const Pair& p, Read read, CompareExchange compare_exchange) {
    // Both engine outputs must confirm the first submission advanced exactly
    // once. An intervening capture, changed scene or stale family is a refusal.
    return advanced_once(p,read) &&
        compare_exchange(p.scene+counter_offset,p.expected(),p.before);
}

template<class Read, class CompareExchange>
bool restore_if_unadvanced(const Pair& p, Read read, CompareExchange compare_exchange) {
    // An aborted second submission must not leave the scene counter behind.
    // Never overwrite a later increment or touch a replacement scene.
    return same_scene(p,read) &&
        compare_exchange(p.scene+counter_offset,p.before,p.expected());
}
} // namespace wuwa_scene_frame_policy
