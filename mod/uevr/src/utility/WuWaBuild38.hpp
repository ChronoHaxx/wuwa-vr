#pragma once
// How the running game build relates to the reviewed 3.8 build (PE 0x6ae17e03), for the reviewed 3.8
// tables (WuWaLgui38.hpp, WuWaRender38.hpp):
//  * reviewed: that exact build. Live RVAs as written; code must match the exact live hashes.
//  * sibling: another 3.8 build, such as another client (a player on PE 0x6ae17bd7, 16 Nov 2026) or a
//    hotfix, in which the 3.8 self-port table (WuWaSelfPortTable38.hpp) found that code. RVAs come from
//    map(); code must be unchanged apart from moved call/jump targets and RIP displacements (unchanged()).
//  * anything else: the tables do not apply and the 3.7 self-port rules stay in charge.
// Pure (no Windows): WuWaSelfPort.hpp builds the live one; the tests build their own.
#include <cstdint>
#include <functional>
#include <optional>

namespace wuwa_build_38 {
inline constexpr uint32_t timestamp = 0x6ae17e03;

struct Build {
    uint32_t timestamp{};
    std::function<std::optional<uint32_t>(uint32_t rva38)> map;
    std::function<bool(uint32_t rva38, uint32_t size, uint64_t hash38)> unchanged;

    bool sibling() const { return map && unchanged; }
    bool reviewed() const { return !sibling() && timestamp == wuwa_build_38::timestamp; }
    bool family() const { return reviewed() || sibling(); }
    // The live RVA of a 3.8 RVA, or nothing (not a 3.8 build, or that code was not found).
    std::optional<uint32_t> at(uint32_t rva38) const {
        if (reviewed()) return rva38;
        return sibling() ? map(rva38) : std::nullopt;
    }
};
} // namespace wuwa_build_38
