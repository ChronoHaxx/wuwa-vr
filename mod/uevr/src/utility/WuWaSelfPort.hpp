#pragma once
// Game-build self-port. Every build-pinned address in the plugin is written for one reference
// build (WuWaSelfPortTable.hpp, generated from a capture of it). On that build the addresses are
// used as written. On any other build the first caller scans the loaded game once (about half a
// second) for the table's fingerprints, and a route uses only addresses found with exact
// evidence and code ranges unchanged apart from moved call/jump targets and RIP displacements.
// A route that needs anything not found refuses, exactly as on an unknown build before.
//
// Routes opt in by taking every address from at()/find() and verifying through
// wuwa_code_check::verify_portable(); a route still on base + RVA keeps its strict build check.
//
// Test switch: an empty file wuwa-self-port-force.txt beside the loaded backend scans even on
// the reference build, where every found site must map to itself.

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include <windows.h>
#include <spdlog/spdlog.h>

#include "WuWaCodeCompatibility.hpp"
#include "WuWaLguiProbe.hpp"
#include "WuWaSelfPortCore.hpp"

namespace wuwa_self_port {
static_assert(table::reference_timestamp == wuwa_code_compatibility::timestamp,
    "WuWaSelfPortTable.hpp is not generated from the build the source is pinned to");
namespace memory = wuwa_lgui_probe::detail;

struct LiveModule final : Memory {
    uintptr_t base{};
    bool copy(uint32_t rva, void* out, size_t size) const override {
        return uint64_t(rva) + size <= image_size &&
            memory::guarded_copy(out, reinterpret_cast<const void*>(base + rva), size);
    }
};

enum class State { reference, moved, unsupported };

struct Live {
    State state{State::unsupported};
    uintptr_t base{};
    uint32_t timestamp{}, image_size{};
    LiveModule module;
    Result result;      // moved builds
    std::string reason; // unsupported builds
};

namespace detail {
inline std::filesystem::path backend_directory() {
    HMODULE self{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&backend_directory), &self)) return {};
    return memory::module_path(self).parent_path();
}

// Readable pages of each section, split into code and data spans.
inline void read_spans(LiveModule& m, const IMAGE_NT_HEADERS64& nt, uint32_t headers) {
    const auto count = nt.FileHeader.NumberOfSections;
    for (WORD i = 0; i < count; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!memory::read(m.base + headers + i * sizeof(section), section)) continue;
        const uint32_t begin = section.VirtualAddress, end = begin + section.Misc.VirtualSize;
        if (!section.Misc.VirtualSize || end > m.image_size) continue;
        auto& spans = (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) ? m.code : m.data;
        for (uint32_t cursor = begin; cursor < end;) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<void*>(m.base + cursor), &region, sizeof(region)) != sizeof(region)) break;
            const auto region_end = uint32_t((std::min<uint64_t>)(end,
                reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize - m.base));
            if (region_end <= cursor) break;
            if (memory::readable(region)) {
                if (!spans.empty() && spans.back().end == cursor) spans.back().end = region_end;
                else spans.push_back({cursor, region_end});
            }
            cursor = region_end;
        }
    }
}

inline Live inspect() {
    Live live;
    const auto module = GetModuleHandleW(nullptr);
    live.base = reinterpret_cast<uintptr_t>(module);
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!module || _wcsicmp(memory::module_path(module).filename().c_str(), L"Client-Win64-Shipping.exe") != 0) {
        live.reason = "not the game executable"; return live;
    }
    if (!memory::read(live.base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < int(sizeof(dos)) ||
        dos.e_lfanew > 0x100000 || !memory::read(live.base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        live.reason = "invalid executable headers"; return live;
    }
    live.timestamp = nt.FileHeader.TimeDateStamp;
    live.image_size = nt.OptionalHeader.SizeOfImage;
    std::error_code ignored;
    const bool forced = std::filesystem::exists(backend_directory() / L"wuwa-self-port-force.txt", ignored);
    if (live.timestamp == table::reference_timestamp && !forced) {
        live.state = State::reference;
        spdlog::info("[WuWaSelfPort] game build {:x} is the reference build; pinned addresses used as written", live.timestamp);
        return live;
    }
    live.module.base = live.base;
    live.module.va_base = live.base;
    live.module.image_size = live.image_size;
    read_spans(live.module, nt, uint32_t(dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
        nt.FileHeader.SizeOfOptionalHeader));
    const auto started = std::chrono::steady_clock::now();
    try {
        live.result = locate(live.module);
    } catch (const std::exception& e) {
        live.reason = std::string{"scan failed: "} + e.what(); return live;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    size_t moved_sites{};
    for (const auto& [ref, m] : live.result.sites) moved_sites += m.live != ref;
    std::string refused;
    for (size_t i = 0; i < live.result.refused.size() && i < 12; ++i) refused += fmt::format("{}{:x}", i ? "," : "", live.result.refused[i]);
    live.state = live.result.sites.empty() ? State::unsupported : State::moved;
    if (live.state == State::unsupported) live.reason = "no pinned site found";
    spdlog::info("[WuWaSelfPort] game build {:x} (reference {:x}{}): found {}/{} sites ({} moved) in {} ms; ranges {} unchanged, "
        "{} changed, {} not found; refused [{}]", live.timestamp, table::reference_timestamp, forced ? ", forced scan" : "",
        live.result.sites.size(), std::size(table::sites), moved_sites, ms, live.result.ranges_ok, live.result.ranges_changed,
        live.result.ranges_unmapped, refused);
    return live;
}
} // namespace detail

// The running game build, inspected once (thread-safe).
inline const Live& live() {
    static const Live value = [] {
        try {
            return detail::inspect();
        } catch (const std::exception& e) {
            Live failed; failed.reason = e.what();
            spdlog::error("[WuWaSelfPort] game build inspection failed: {}", e.what());
            return failed;
        }
    }();
    return value;
}

// The live address of a reference RVA, or nothing on a build without it.
inline std::optional<uintptr_t> find(uint32_t reference_rva) {
    const auto& l = live();
    if (l.state == State::reference) return l.base + reference_rva;
    if (l.state != State::moved) return std::nullopt;
    const auto it = l.result.sites.find(reference_rva);
    if (it == l.result.sites.end()) return std::nullopt;
    return l.base + it->second.live;
}

inline uintptr_t at(uint32_t reference_rva) {
    if (const auto address = find(reference_rva)) return *address;
    throw std::runtime_error(fmt::format("game build {:x} has no match for pinned site {:x}", live().timestamp, reference_rva));
}

// Live RVA, for code that compares return addresses or vtables as RVAs.
inline std::optional<uint32_t> find_rva(uint32_t reference_rva) {
    const auto address = find(reference_rva);
    if (!address) return std::nullopt;
    return uint32_t(*address - live().base);
}

// A source range ({rva, size, hash}) at its live place: raw hash on the reference build, masked
// compare on a moved build.
inline bool range_matches(const wuwa_code_compatibility::Range& range) {
    const auto& l = live();
    if (l.state == State::moved) return range_unchanged(l.module, l.result, range.rva, range.size, range.hash);
    if (l.state != State::reference || !range.size || range.rva >= l.image_size || range.size > l.image_size - range.rva) return false;
    std::vector<uint8_t> bytes(range.size);
    return memory::executable_site(l.base + range.rva, range.size, GetModuleHandleW(nullptr)) &&
        memory::guarded_copy(bytes.data(), reinterpret_cast<const void*>(l.base + range.rva), bytes.size()) &&
        fnv(bytes) == range.hash;
}

// Code that must equal a byte list from the source: raw on the reference build, masked when moved.
inline bool code_matches(uint32_t reference_rva, std::span<const uint8_t> expected) {
    const auto& l = live();
    if (l.state == State::moved) return bytes_unchanged(l.module, l.result, reference_rva, expected);
    if (l.state != State::reference) return false;
    std::vector<uint8_t> bytes(expected.size());
    const auto site = l.base + reference_rva;
    return memory::executable_site(site, bytes.size(), GetModuleHandleW(nullptr)) &&
        memory::guarded_copy(bytes.data(), reinterpret_cast<const void*>(site), bytes.size()) &&
        std::equal(bytes.begin(), bytes.end(), expected.begin());
}

inline const char* state_name() {
    switch (live().state) {
    case State::reference: return "reference";
    case State::moved: return "self-ported";
    default: return "unsupported";
    }
}
} // namespace wuwa_self_port
