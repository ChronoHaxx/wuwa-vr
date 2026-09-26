#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "Logging.hpp"
#include "WuWaLguiProbe.hpp"

namespace wuwa_shadow {
namespace checked = wuwa_lgui_probe::detail;

// September 8 mapped-image constructor proves init+0x150 is copied to both
// view+0xc90 and view+0x1a0. The game's stereo predicate at RVA 0x24bbf2d0
// accepts pass 0/2 as primary. No other differing dwords are treated as enums.
inline constexpr std::array<uint32_t, 2> pass_offsets{0x1a0, 0xc90};
struct CodeCheck { uint32_t rva, size; std::array<uint8_t, 24> bytes; };
inline constexpr std::array<CodeCheck, 6> code_checks{{
    {0x24abff25, 12, {0x48,0x8b,0x82,0xf8,0x00,0x00,0x00,0x33,0xff,0x48,0x89,0x01}},
    {0x24abff34, 14, {0x48,0x8b,0x82,0x00,0x01,0x00,0x00,0x48,0x8b,0xd9,0x48,0x89,0x41,0x08}},
    {0x24ac01e2, 13, {0x41,0x8b,0x85,0x50,0x01,0x00,0x00,0x89,0x83,0x90,0x0c,0x00,0x00}},
    {0x24ac091a, 13, {0x41,0x8b,0x85,0x50,0x01,0x00,0x00,0x89,0x85,0x90,0x00,0x00,0x00}},
    {0x24ac0c39, 12, {0x8b,0x85,0x90,0x00,0x00,0x00,0x89,0x83,0xa0,0x01,0x00,0x00}},
    {0x24bbf2d0, 14, {0xf7,0x82,0x90,0x0c,0x00,0x00,0xfd,0xff,0xff,0xff,0x0f,0x94,0xc0,0xc3}},
}};

inline std::once_flag verification;
inline std::atomic<int> compatibility{}; // 0 unchecked, 1 matched, -1 rejected
inline std::atomic<uint64_t> enabled_until{}, valid_pairs{}, invalid_pairs{},
    applied{}, restored{}, last_valid_ms{};
inline std::atomic<bool> faulted{}, configured_enabled{}, test_enabled{true};

inline bool compatible() {
    std::call_once(verification, [] {
        wchar_t path[MAX_PATH]{};
        const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH ||
            _wcsicmp(std::filesystem::path{path}.filename().c_str(), L"Client-Win64-Shipping.exe") != 0) {
            compatibility = -1;
            return;
        }
        const auto module = GetModuleHandleW(nullptr);
        const auto base = reinterpret_cast<uintptr_t>(module);
        for (const auto& check : code_checks) {
            std::array<uint8_t, 24> bytes{};
            if (!checked::executable_site(base + check.rva, check.size, module) ||
                !checked::guarded_copy(bytes.data(), reinterpret_cast<const void*>(base + check.rva), check.size) ||
                std::memcmp(bytes.data(), check.bytes.data(), check.size) != 0) {
                compatibility = -1;
                spdlog::error("[WuWaShadow] code check failed at {:x}; observation and writes disabled", check.rva);
                return;
            }
        }
        compatibility = 1;
        spdlog::info("[WuWaShadow] six constructor/predicate code checks matched; observing two known pass fields");
    });
    return compatibility.load() == 1;
}

inline bool writable_field(uintptr_t address) {
    MEMORY_BASIC_INFORMATION region{};
    if (address == 0 || address > UINTPTR_MAX - sizeof(uint32_t) ||
        VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) != sizeof(region) ||
        !checked::readable(region)) return false;
    const auto protection = region.Protect & 0xff;
    const auto base = reinterpret_cast<uintptr_t>(region.BaseAddress);
    return (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY) &&
        base <= address && region.RegionSize <= UINTPTR_MAX - base &&
        address + sizeof(uint32_t) <= base + region.RegionSize;
}

inline bool write_field(uintptr_t address, uint32_t value) {
    if (!writable_field(address) ||
        !checked::guarded_copy(reinterpret_cast<void*>(address), &value, sizeof(value))) return false;
    uint32_t actual{};
    return checked::read(address, actual) && actual == value;
}

inline bool ready() {
    const auto last = last_valid_ms.load();
    return compatibility.load() == 1 && !faulted.load() && valid_pairs.load() >= 20 &&
        last != 0 && GetTickCount64() - last < 2000;
}

inline bool test_active() { return GetTickCount64() < enabled_until.load(); }
inline bool enabled() {
    return ready() && (test_active() ? test_enabled.load() : configured_enabled.load());
}

// A temporary comparison overrides the menu setting and expires back to it.
// Clearing a test never changes or saves the user's Same Pass setting.
inline void set_test(int seconds, bool value) {
    if (seconds == 0) {
        enabled_until = 0;
        return;
    }
    test_enabled = value;
    enabled_until = GetTickCount64() + static_cast<uint64_t>(seconds) * 1000;
}

inline nlohmann::json status() {
    return {{"compatible", compatibility.load()}, {"ready", ready()}, {"enabled", enabled()},
        {"configured_enabled", configured_enabled.load()}, {"test_active", test_active()},
        {"faulted", faulted.load()}, {"valid_pairs", valid_pairs.load()},
        {"invalid_pairs", invalid_pairs.load()}, {"applied", applied.load()}, {"restored", restored.load()}};
}

// Construct immediately before the second BeginRenderingViewFamily call and
// destroy immediately after it. That call copies views for the render thread,
// as required by the existing UEVR temporary view-count/target changes too.
// The existing Native Stereo Fix Same Pass setting enables the verified fix.
// A temporary comparison can override it; all code/view/write guards still apply.
class PassScope {
public:
    PassScope(const void* family, const void* first, void* second, int32_t count, bool same_pass) {
        configured_enabled = same_pass;
        if (!compatible() || faulted.load() || count != 2 || first == nullptr ||
            second == nullptr || first == second || family == nullptr) return;
        const auto a = reinterpret_cast<uintptr_t>(first);
        const auto b = reinterpret_cast<uintptr_t>(second);
        uintptr_t af{}, bf{};
        std::array<uint32_t, 2> av{}, bv{};
        bool valid = checked::read(a, af) && checked::read(b, bf) &&
            af == reinterpret_cast<uintptr_t>(family) && bf == af;
        for (size_t i = 0; i < pass_offsets.size(); ++i) {
            const bool fields_valid = checked::read_field(a, pass_offsets[i], av[i]) &&
                checked::read_field(b, pass_offsets[i], bv[i]) && av[i] == 2 && bv[i] == 3 &&
                b <= UINTPTR_MAX - pass_offsets[i] && writable_field(b + pass_offsets[i]);
            valid = fields_valid && valid;
        }
        if (!valid) {
            ++invalid_pairs;
            SPDLOG_INFO_EVERY_N_SEC(5, "[WuWaShadow] skipped pair: family_match={} pass1={}/{} pass2={}/{}",
                af == reinterpret_cast<uintptr_t>(family) && bf == af, av[0], av[1], bv[0], bv[1]);
            return;
        }
        ++valid_pairs;
        last_valid_ms = GetTickCount64();
        SPDLOG_INFO_EVERY_N_SEC(5, "[WuWaShadow] pair=2/2,3/3 enabled={} valid={} invalid={} applied={} restored={}",
            enabled(), valid_pairs.load(), invalid_pairs.load(), applied.load(), restored.load());
        if (!enabled()) return;
        view = b;
        for (size_t i = 0; i < pass_offsets.size(); ++i) {
            touched[i] = true; // also restore after a failed write/readback
            if (!write_field(view + pass_offsets[i], 2)) {
                faulted = true;
                spdlog::error("[WuWaShadow] write failed; restoring and disabling further tests");
                restore();
                return;
            }
        }
        complete = true;
        ++applied;
    }

    ~PassScope() { restore(); }
    PassScope(const PassScope&) = delete;
    PassScope& operator=(const PassScope&) = delete;

private:
    void restore() {
        bool ok = true;
        for (size_t i = 0; i < pass_offsets.size(); ++i) {
            if (!touched[i]) continue;
            touched[i] = false;
            uint32_t current{};
            const auto address = view + pass_offsets[i];
            // Do not overwrite an unexpected engine-side change.
            const bool restored_field = checked::read(address, current) &&
                (current == 3 || (current == 2 && write_field(address, 3)));
            ok = restored_field && ok;
        }
        if (!ok) {
            faulted = true;
            spdlog::error("[WuWaShadow] restoration failed or engine changed pass; further writes disabled");
        } else if (complete) {
            ++restored;
        }
        complete = false;
    }
    uintptr_t view{};
    std::array<bool, 2> touched{};
    bool complete{};
};
} // namespace wuwa_shadow
