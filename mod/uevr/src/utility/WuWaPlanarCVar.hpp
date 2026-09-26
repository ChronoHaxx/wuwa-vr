#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <windows.h>

namespace wuwa_planar_cvar {
// This game's EnablePlanarReflection is registered through the float overload,
// despite its name. Permit only its exact 0.0/1.0 values in the bounded test.
// Never interpret 1.0f's integer bit pattern as an integer console variable.
struct Sample { uintptr_t address{}; std::array<uint32_t, 2> bits{}; };
inline bool binary(uint32_t bits) { return bits == 0 || bits == 0x3f800000; }
inline bool read(uintptr_t slot, Sample& output) noexcept {
    __try {
        const auto address = *reinterpret_cast<const uintptr_t*>(slot);
        MEMORY_BASIC_INFORMATION info{};
        if (!address || address % 8 != 0 ||
            VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
            (info.Protect & 0xff) != PAGE_READWRITE ||
            address - reinterpret_cast<uintptr_t>(info.BaseAddress) + 8 > info.RegionSize) return false;
        const auto pair = *reinterpret_cast<volatile const uint64_t*>(address);
        const std::array<uint32_t, 2> bits{uint32_t(pair), uint32_t(pair >> 32)};
        if (!binary(bits[0]) || !binary(bits[1])) return false;
        output = {address, bits};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool write(uintptr_t slot, const Sample& expected, int value) noexcept {
    if ((value != 0 && value != 1) || !binary(expected.bits[0]) || !binary(expected.bits[1])) return false;
    Sample current{};
    if (!read(slot, current) || current.address != expected.address || current.bits != expected.bits) return false;
    __try {
        const auto before = uint64_t{expected.bits[0]} | (uint64_t{expected.bits[1]} << 32);
        const uint64_t bits = value == 1 ? 0x3f800000 : 0;
        const auto after = bits | (bits << 32);
        return InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(expected.address),
            std::bit_cast<LONG64>(after), std::bit_cast<LONG64>(before)) == std::bit_cast<LONG64>(before);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline uintptr_t verified_slot() noexcept {
    __try {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return 0;
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt->FileHeader.TimeDateStamp != 0x6a74963e || nt->OptionalHeader.SizeOfImage < 0x37f5fa60) return 0;
        // Name/default/float registration/GetFloatData/destination are all bound
        // by the verified registration function; reject a changed game build.
        const auto code = reinterpret_cast<const uint8_t*>(base + 0x2014a190);
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < 0x75; ++i) { hash ^= code[i]; hash *= 1099511628211ULL; }
        if (hash != 0x2efab16f2b447a85ULL ||
            *reinterpret_cast<const uintptr_t*>(base + 0x37f5fa48) != base + 0x2759e310) return 0;
        return base + 0x37f5fa58;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
} // namespace wuwa_planar_cvar
