#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wuwa_stereo {
enum class EarlyPrimaryResult { skipped, applied, write_failed, restore_failed };

// Called only on a completed second main view, before the caller adds it to
// the family and runs view setup. Build verification and the NSF/Same Pass
// policy live in WuWaShadowPass.hpp. No constructor input, matrix, family
// count, exposure pointer or temporal state is changed here.
template<class Read, class Writable, class Write>
EarlyPrimaryResult early_primary(uintptr_t view, uintptr_t init, Read read,
                                Writable writable, Write write) {
    auto field = [&](uintptr_t base, uintptr_t offset, auto& value) {
        return base && offset <= UINTPTR_MAX - base &&
            sizeof(value) <= UINTPTR_MAX - (base + offset) && read(base + offset, value);
    };
    uintptr_t family{}, init_family{}, state{}, init_state{}, data{}, first{}, first_family{}, first_state{};
    int32_t count{}, capacity{};
    uint32_t init_pass{};
    std::array<uint32_t, 2> a{}, b{};
    constexpr std::array<uintptr_t, 2> offsets{0x1a0, 0xc90};
    if (!field(init, 0x150, init_pass) || init_pass != 3 ||
        !field(init, 0xf8, init_family) || !field(init, 0x100, init_state) ||
        !field(view, 0, family) || !family || family != init_family ||
        !field(view, 8, state) || !state || state != init_state ||
        !field(family, 0, data) || !field(family, 8, count) || count != 1 ||
        !field(family, 12, capacity) || capacity < count || capacity > 8 ||
        !field(data, 0, first) || !first || first == view ||
        !field(first, 0, first_family) || first_family != family ||
        !field(first, 8, first_state) || !first_state || first_state == state)
        return EarlyPrimaryResult::skipped;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (!field(first, offsets[i], a[i]) || a[i] != 2 ||
            !field(view, offsets[i], b[i]) || b[i] != 3 || !writable(view + offsets[i]))
            return EarlyPrimaryResult::skipped;
    }
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        // Failed writes may still have changed the field.
        if (write(view + offsets[i], 2)) continue;
        bool restored = true;
        for (std::size_t j = 0; j <= i; ++j) {
            uint32_t current{};
            const bool ok = read(view + offsets[j], current) &&
                (current == 3 || (current == 2 && write(view + offsets[j], 3)));
            restored = ok && restored;
        }
        return restored ? EarlyPrimaryResult::write_failed : EarlyPrimaryResult::restore_failed;
    }
    // Both fields intentionally remain primary for the rest of this view's
    // lifetime, like upstream NSF Same Pass. Own/first state pointers remain
    // separate. No object address is retained after this function returns.
    return EarlyPrimaryResult::applied;
}
} // namespace wuwa_stereo
