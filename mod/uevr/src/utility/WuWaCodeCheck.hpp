#pragma once
#include "WuWaCodeCompatibility.hpp"
#include "WuWaLguiProbe.hpp"
#include "WuWaSelfPort.hpp"
#include "WuWaRender38.hpp"

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

// The live address of a reference-build RVA: the reviewed 3.8 table first (WuWaRender38.hpp), then
// the self-port. Routes verified with verify_portable take their hook addresses from here.
inline uintptr_t live_address(uint32_t reference) {
    const auto& live = wuwa_self_port::live();
    if (const auto rva = wuwa_render_38::address(live.timestamp, reference)) return live.base + *rva;
    return wuwa_self_port::at(reference);
}

// verify() for routes that take every address from wuwa_self_port::at()/find(): on the reference
// build the raw hashes must match as before; on a self-ported build each range must have been
// found and be unchanged apart from moved call/jump targets and RIP displacements. On 3.8 a range
// listed in WuWaRender38.hpp must match its reviewed live bytes exactly instead.
inline void verify_portable(const char* route, std::span<const contract::Range> ranges) {
    const auto& live = wuwa_self_port::live();
    if (live.state == wuwa_self_port::State::reference) { (void)verify(route, ranges); return; }
    if (live.state != wuwa_self_port::State::moved)
        throw std::runtime_error(fmt::format("{} refused: game build {:x} is not supported ({})", route, live.timestamp, live.reason));
    const auto module = GetModuleHandleW(nullptr);
    size_t reviewed{};
    for (const auto& range : ranges) {
        const auto approved = wuwa_render_38::check(live.timestamp, range.rva, range.size, range.hash,
            [&](uint32_t rva, std::span<uint8_t> bytes) {
                return uint64_t(rva) + bytes.size() <= live.image_size &&
                    memory::executable_site(live.base + rva, bytes.size(), module) &&
                    memory::guarded_copy(bytes.data(), reinterpret_cast<const void*>(live.base + rva), bytes.size());
            });
        if (approved) {
            if (!*approved)
                throw std::runtime_error(fmt::format("{} refused: reviewed 3.8 range {:x}+{:x} does not match game build {:x}",
                    route, range.rva, range.size, live.timestamp));
            ++reviewed;
            continue;
        }
        if (!wuwa_self_port::range_matches(range)) {
            const auto found = wuwa_self_port::find_rva(range.rva);
            throw std::runtime_error(fmt::format("{} refused: code range {:x}+{:x} {} in game build {:x}", route, range.rva,
                range.size, found ? fmt::format("changed at {:x}", *found) : std::string{"not found"}, live.timestamp));
        }
    }
    spdlog::info("[WuWaStereoCode] route={} verified_ranges={} image_size={:x} timestamp={:x} self_ported_from={:x} reviewed_3_8={}",
        route, ranges.size(), live.image_size, live.timestamp, wuwa_self_port::table::reference_timestamp, reviewed);
}
} // namespace wuwa_code_check
