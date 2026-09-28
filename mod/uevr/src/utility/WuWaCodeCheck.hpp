#pragma once
#include "WuWaCodeCompatibility.hpp"
#include "WuWaLguiProbe.hpp"

namespace wuwa_code_check {
namespace memory = wuwa_lgui_probe::detail;
namespace contract = wuwa_code_compatibility;
inline uintptr_t verify(const char* route, std::span<const contract::Range> ranges) {
    const auto module = GetModuleHandleW(nullptr);
    const auto base = reinterpret_cast<uintptr_t>(module);
    memory::require(module && !_wcsicmp(memory::module_path(module).filename().c_str(), L"Client-Win64-Shipping.exe"),
        "Stereo code check refused: unexpected executable name");
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    memory::require(memory::read(base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew >= sizeof(dos) && dos.e_lfanew <= 0x100000 &&
        memory::read_field(base, dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE,
        "Stereo code check refused: invalid executable PE headers");
    const contract::Identity identity{nt.FileHeader.Machine, nt.OptionalHeader.Magic,
        nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage};
    const auto result = contract::verify(identity, ranges, [=](uint32_t rva, std::span<uint8_t> bytes) {
        return base <= UINTPTR_MAX - identity.size &&
            memory::executable_site(base + rva, bytes.size(), module) &&
            memory::guarded_copy(bytes.data(), reinterpret_cast<const void*>(base + rva), bytes.size());
    });
    if (!result) {
        constexpr std::array<const char*, 8> reasons{
            "none", "machine", "PE format", "timestamp", "image size", "out-of-image range", "unreadable code", "code hash"};
        throw std::runtime_error(fmt::format(
            "{} refused: {} rva={:x} expected={:x} actual={:x}; image_size={:x} timestamp={:x}",
            route, reasons.at(size_t(result.failure)), result.rva, result.expected, result.actual, identity.size, identity.timestamp));
    }
    spdlog::info("[WuWaStereoCode] route={} verified_ranges={} image_size={:x} timestamp={:x}",
        route, ranges.size(), identity.size, identity.timestamp);
    return base;
}
} // namespace wuwa_code_check
