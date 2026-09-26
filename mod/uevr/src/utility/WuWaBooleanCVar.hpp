#pragma once
#include <array>
#include <cstdint>
#include <windows.h>

namespace wuwa_boolean_cvar {
// Kuro's bool CVar data has adjacent one-byte game/render copies. It must
// never pass through TConsoleVariableData<int>, which would write eight bytes.
struct Sample { uintptr_t address{}; std::array<uint8_t, 2> values{}; };

inline bool read(uintptr_t slot, Sample& output) noexcept {
    __try {
        const auto address = *reinterpret_cast<const uintptr_t*>(slot);
        MEMORY_BASIC_INFORMATION info{};
        if (!address || address % alignof(short) != 0 ||
            VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
            (info.Protect & 0xff) != PAGE_READWRITE ||
            address - reinterpret_cast<uintptr_t>(info.BaseAddress) + 2 > info.RegionSize) return false;
        const auto pair = *reinterpret_cast<volatile const unsigned short*>(address);
        const std::array<uint8_t, 2> values{uint8_t(pair), uint8_t(pair >> 8)};
        if (values[0] > 1 || values[1] > 1) return false;
        output = {address, values};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

inline bool write(uintptr_t slot, const Sample& expected, uint8_t value) noexcept {
    if (value > 1 || expected.values[0] > 1 || expected.values[1] > 1) return false;
    Sample current{};
    if (!read(slot, current) || current.address != expected.address || current.values != expected.values) return false;
    __try {
        // One aligned compare/exchange touches exactly the two verified bytes;
        // refuse an intervening game change instead of overwriting it.
        const auto before = short(expected.values[0] | (expected.values[1] << 8));
        const auto after = short(value | (value << 8));
        return InterlockedCompareExchange16(reinterpret_cast<volatile short*>(expected.address), after, before) == before;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A single supported bool variable. The registration fingerprint binds the
// name, bool registration overload, GetBoolData call and destination slot.
// No registry enumeration or speculative console virtual call is used.
inline uintptr_t full_resolution_slot(uint64_t registration_fingerprint) noexcept {
    __try {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return 0;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != 0x6a74963e || nt->OptionalHeader.SizeOfImage < 0x37f89c60) return 0;
        const auto code = reinterpret_cast<const uint8_t*>(base + 0x20169320);
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < 0x70; ++i) { hash ^= code[i]; hash *= 1099511628211ULL; }
        if (hash != registration_fingerprint ||
            *reinterpret_cast<const uintptr_t*>(base + 0x37f89c48) != base + 0x2759e2f0) return 0;
        return base + 0x37f89c58;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
} // namespace wuwa_boolean_cvar
