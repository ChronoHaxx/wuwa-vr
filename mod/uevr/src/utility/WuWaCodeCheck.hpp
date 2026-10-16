#pragma once
#include "WuWaCodeCompatibility.hpp"
#include "WuWaLguiProbe.hpp"
#include "WuWaSelfPort.hpp"

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

// verify() for routes that take every address from wuwa_self_port::at()/find(): on the reference
// build the raw hashes must match as before; on a self-ported build each range must have been
// found and be unchanged apart from moved call/jump targets and RIP displacements.
inline void verify_portable(const char* route, std::span<const contract::Range> ranges) {
    const auto& live = wuwa_self_port::live();
    if (live.state == wuwa_self_port::State::reference) { (void)verify(route, ranges); return; }
    if (live.state != wuwa_self_port::State::moved)
        throw std::runtime_error(fmt::format("{} refused: game build {:x} is not supported ({})", route, live.timestamp, live.reason));
    for (const auto& range : ranges) {
        if (!wuwa_self_port::range_matches(range)) {
            const auto found = wuwa_self_port::find_rva(range.rva);
            throw std::runtime_error(fmt::format("{} refused: code range {:x}+{:x} {} in game build {:x}", route, range.rva,
                range.size, found ? fmt::format("changed at {:x}", *found) : std::string{"not found"}, live.timestamp));
        }
    }
    spdlog::info("[WuWaStereoCode] route={} verified_ranges={} image_size={:x} timestamp={:x} self_ported_from={:x}",
        route, ranges.size(), live.image_size, live.timestamp, wuwa_self_port::table::reference_timestamp);
}
} // namespace wuwa_code_check
