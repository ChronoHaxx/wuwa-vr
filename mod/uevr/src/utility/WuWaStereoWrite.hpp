#pragma once
#include "WuWaStereoParameters.hpp"
#include <windows.h>

namespace wuwa_stereo_parameters {
// Only the caller-owned parameter block may be changed. A stale observation,
// protected page or a different block leaves all bytes untouched.
inline bool apply(uintptr_t output, const Plan& p) noexcept {
    if (!output || output > UINTPTR_MAX - 0x90 - sizeof(Block)) return false;
    const auto address = output + 0x90;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
        (region.Protect & 0xff) != PAGE_READWRITE ||
        address < reinterpret_cast<uintptr_t>(region.BaseAddress) ||
        address - reinterpret_cast<uintptr_t>(region.BaseAddress) > region.RegionSize ||
        sizeof(Block) > region.RegionSize - (address - reinterpret_cast<uintptr_t>(region.BaseAddress))) return false;
    __try {
        if (std::memcmp(reinterpret_cast<const void*>(address), &p.before, sizeof(Block)) != 0) return false;
        std::memcpy(reinterpret_cast<void*>(address), &p.after, sizeof(Block));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace wuwa_stereo_parameters
