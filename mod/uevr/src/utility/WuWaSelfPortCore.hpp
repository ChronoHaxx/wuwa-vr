#pragma once
// Finds the build-pinned sites of WuWaSelfPortTable.hpp in another game build. No Windows calls:
// the plugin reads the loaded module through Memory (WuWaSelfPort.hpp), dev/test_self_port.cpp
// reads a module capture. Mirrors simulate() in launcher/dev/wuwa_self_port_table.py.
//
// Only exact evidence is accepted: a site's masked window (every byte equal except rel32 call/jump
// targets and RIP-relative displacements) must match at exactly one place, a twin window only at
// the spot a nearby found site's shift predicts, a data address only where every located
// reference agrees, and a pointer slot only when one aligned slot holds the found pointee.
// Anything else stays unmapped, and the routes that need it refuse.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "WuWaSelfPortTable.hpp"

namespace wuwa_self_port {
namespace table = wuwa_self_port_table;

struct Span { uint32_t begin, end; }; // readable RVA range [begin, end)

// What the matcher needs from an image: its readable spans and a bounded copy.
struct Memory {
    uintptr_t va_base{};          // address of RVA 0 (pointer slots hold va_base + rva)
    uint32_t image_size{};
    std::vector<Span> code, data; // readable executable / non-executable spans
    virtual bool copy(uint32_t rva, void* out, size_t size) const = 0;
    virtual ~Memory() = default;
};

inline uint64_t fnv(std::span<const uint8_t> bytes) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto value : bytes) { hash ^= value; hash *= 0x100000001b3ULL; }
    return hash;
}

struct Mapping {
    uint32_t live{};
    const char* by{};   // window | twin_shift | refs | pointer
};

struct Result {
    std::unordered_map<uint32_t, Mapping> sites; // reference RVA -> live RVA
    std::vector<uint32_t> refused;               // reference RVAs left unmapped
    uint32_t ranges_ok{}, ranges_changed{}, ranges_unmapped{};
    std::vector<uint32_t> changed;               // ranges whose masked bytes differ
};

namespace detail {
inline const uint8_t* window_bytes(const table::Window& w) noexcept { return table::pool + w.pool; }
inline const uint8_t* window_mask(const table::Window& w) noexcept { return table::pool + w.pool + w.size; }

inline bool masked_equal(const Memory& m, int64_t at, const table::Window& w) {
    if (at < 0 || uint64_t(at) + w.size > m.image_size) return false;
    std::array<uint8_t, 512> live{};
    if (w.size > live.size() || !m.copy(uint32_t(at), live.data(), w.size)) return false;
    const auto* ref = window_bytes(w);
    const auto* mask = window_mask(w);
    for (uint32_t i = 0; i < w.size; ++i)
        if (mask[i] && live[i] != ref[i]) return false;
    return true;
}

inline bool in_spans(const std::vector<Span>& spans, uint32_t rva, uint32_t size) {
    return std::any_of(spans.begin(), spans.end(), [&](const Span& s) { return rva >= s.begin && rva + uint64_t(size) <= s.end; });
}

inline uint64_t load64(const uint8_t* p) noexcept { uint64_t v; std::memcpy(&v, p, 8); return v; }

// Every place each listed window matches (up to cap), in one pass over the code spans.
inline std::unordered_map<uint16_t, std::vector<uint32_t>> find_windows(const Memory& m,
    const std::vector<uint16_t>& wanted, size_t cap) {
    std::vector<std::pair<uint64_t, uint16_t>> keys;
    for (auto index : wanted) {
        const auto& w = table::windows[index];
        keys.emplace_back(load64(window_bytes(w) + w.anchor), index);
    }
    std::sort(keys.begin(), keys.end());
    std::vector<uint64_t> filter(1 << 10); // 64K-bit prefilter on a multiplicative hash
    const auto slot = [](uint64_t k) { return uint32_t((k * 0x9e3779b97f4a7c15ULL) >> 48); };
    for (const auto& [k, _] : keys) filter[slot(k) >> 6] |= 1ULL << (slot(k) & 63);
    std::unordered_map<uint16_t, std::vector<uint32_t>> hits;
    constexpr uint32_t chunk = 1u << 20;
    std::vector<uint8_t> buffer(chunk + 8);
    for (const auto& span : m.code) {
        for (uint32_t begin = span.begin; begin + 8 <= span.end; begin += chunk) {
            const uint32_t size = (std::min)(chunk + 7, span.end - begin);
            if (!m.copy(begin, buffer.data(), size)) continue;
            for (uint32_t i = 0; i + 8 <= size && i < chunk; ++i) {
                const auto k = load64(buffer.data() + i);
                const auto s = slot(k);
                if (!(filter[s >> 6] & (1ULL << (s & 63)))) continue;
                auto it = std::lower_bound(keys.begin(), keys.end(), std::pair<uint64_t, uint16_t>{k, 0});
                for (; it != keys.end() && it->first == k; ++it) {
                    auto& list = hits[it->second];
                    if (list.size() >= cap) continue;
                    const auto& w = table::windows[it->second];
                    const int64_t at = int64_t(begin) + i - w.anchor;
                    if (masked_equal(m, at, w)) list.push_back(uint32_t(at));
                }
            }
        }
    }
    return hits;
}

inline uint32_t find_pointer_slot(const Memory& m, uint64_t value, uint32_t& count) {
    count = 0;
    uint32_t found{};
    constexpr uint32_t chunk = 1u << 20;
    std::vector<uint8_t> buffer(chunk);
    for (const auto& span : m.data) {
        const uint32_t first = (span.begin + 7) & ~7u;
        for (uint32_t begin = first; begin + 8 <= span.end; begin += chunk) {
            const uint32_t size = (std::min)(chunk, span.end - begin) & ~7u;
            if (!size || !m.copy(begin, buffer.data(), size)) continue;
            for (uint32_t i = 0; i < size; i += 8) {
                if (load64(buffer.data() + i) == value) {
                    found = begin + i;
                    if (++count > 1) return 0;
                }
            }
        }
    }
    return count == 1 ? found : 0;
}
} // namespace detail

inline Result locate(const Memory& m) {
    using namespace detail;
    Result r;
    std::vector<uint16_t> wanted;
    for (const auto& s : table::sites) {
        if (s.kind == table::kind_code && s.window != table::no_window) wanted.push_back(s.window);
        for (uint16_t i = 0; i < s.refs; ++i) wanted.push_back(table::refs[s.first_ref + i].window);
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    const auto hits = find_windows(m, wanted, 2);
    const auto hits_of = [&](uint16_t w) -> const std::vector<uint32_t>* {
        auto it = hits.find(w); return it == hits.end() ? nullptr : &it->second;
    };
    // 1. Unique windows.
    for (const auto& s : table::sites) {
        if (s.kind != table::kind_code || s.window == table::no_window || table::windows[s.window].twins != 1) continue;
        const auto* h = hits_of(s.window);
        if (h && h->size() == 1) r.sites[s.rva] = {(*h)[0], "window"};
    }
    // 2. Twins and windows found more than once: the spot a nearby found site's shift predicts.
    std::vector<std::pair<uint32_t, uint32_t>> unique;
    for (const auto& [ref, map] : r.sites) unique.emplace_back(ref, map.live);
    for (const auto& s : table::sites) {
        if (s.kind != table::kind_code || s.window == table::no_window || r.sites.contains(s.rva)) continue;
        std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> nearby;
        for (const auto& u : unique) {
            const auto distance = u.first > s.rva ? u.first - s.rva : s.rva - u.first;
            if (distance <= table::twin_span) nearby.push_back({distance, u});
        }
        std::sort(nearby.begin(), nearby.end());
        for (size_t i = 0; i < nearby.size() && i < 3; ++i) {
            const int64_t spot = int64_t(s.rva) + int64_t(nearby[i].second.second) - int64_t(nearby[i].second.first);
            if (masked_equal(m, spot, table::windows[s.window])) { r.sites[s.rva] = {uint32_t(spot), "twin_shift"}; break; }
        }
    }
    // 3. Through rel32 references: data, and code the steps above left open.
    for (const auto& s : table::sites) {
        if (!s.refs || r.sites.contains(s.rva)) continue;
        std::vector<uint32_t> targets;
        for (uint16_t i = 0; i < s.refs; ++i) {
            const auto& ref = table::refs[s.first_ref + i];
            const auto* h = hits_of(ref.window);
            if (!h || h->size() != 1) continue;
            int32_t disp{};
            if (!m.copy((*h)[0] + ref.field, &disp, sizeof(disp))) continue;
            targets.push_back(uint32_t(int64_t((*h)[0]) + ref.end + disp));
        }
        if (!targets.empty() && std::all_of(targets.begin(), targets.end(), [&](uint32_t t) { return t == targets[0]; }) &&
            targets[0] < m.image_size)
            r.sites[s.rva] = {targets[0], "refs"};
    }
    // 4. Pointer slots: the one aligned slot holding the found pointee.
    for (const auto& s : table::sites) {
        if (s.kind != table::kind_pointer || r.sites.contains(s.rva)) continue;
        auto pointee = r.sites.find(s.pointee);
        if (pointee == r.sites.end()) continue;
        uint32_t count{};
        if (const auto slot = find_pointer_slot(m, m.va_base + pointee->second.live, count)) r.sites[s.rva] = {slot, "pointer"};
    }
    for (const auto& s : table::sites)
        if (!r.sites.contains(s.rva)) r.refused.push_back(s.rva);
    // Ranges: moved code is accepted only when every byte outside the moved fields is unchanged.
    for (const auto& range : table::ranges) {
        auto it = r.sites.find(range.rva);
        if (it == r.sites.end()) { ++r.ranges_unmapped; continue; }
        std::vector<uint8_t> live(range.size);
        if (!in_spans(m.code, it->second.live, range.size) || !m.copy(it->second.live, live.data(), live.size())) {
            ++r.ranges_unmapped; continue;
        }
        for (uint16_t i = 0; i < range.fields; ++i) {
            const auto& f = table::fields[range.first_field + i];
            std::memset(live.data() + f.offset, 0, (std::min)(uint32_t(f.length), range.size - f.offset));
        }
        if (fnv(live) == range.masked) ++r.ranges_ok;
        else { ++r.ranges_changed; r.changed.push_back(range.rva); }
    }
    return r;
}

// The masked check of one source range at its live address. The range must be in the table with
// the source's own hash, so a table generated from different source refuses rather than guesses.
inline bool range_unchanged(const Memory& m, const Result& r, uint32_t rva, uint32_t size, uint64_t hash) {
    const auto* range = std::find_if(std::begin(table::ranges), std::end(table::ranges),
        [&](const table::Range& x) { return x.rva == rva && x.size == size; });
    if (range == std::end(table::ranges) || range->hash != hash) return false;
    auto it = r.sites.find(rva);
    if (it == r.sites.end()) return false;
    std::vector<uint8_t> live(size);
    if (!detail::in_spans(m.code, it->second.live, size) || !m.copy(it->second.live, live.data(), size)) return false;
    for (uint16_t i = 0; i < range->fields; ++i) {
        const auto& f = table::fields[range->first_field + i];
        std::memset(live.data() + f.offset, 0, (std::min)(uint32_t(f.length), size - f.offset));
    }
    return fnv(live) == range->masked;
}

// Moved code that must equal a byte list (signatures, hook spans), masked like its range.
inline bool bytes_unchanged(const Memory& m, const Result& r, uint32_t rva, std::span<const uint8_t> expected) {
    const auto* range = std::find_if(std::begin(table::ranges), std::end(table::ranges),
        [&](const table::Range& x) { return x.rva == rva && x.size == expected.size(); });
    auto it = r.sites.find(rva);
    if (range == std::end(table::ranges) || it == r.sites.end() || range->hash != fnv(expected)) return false;
    std::vector<uint8_t> live(expected.size()), ref(expected.begin(), expected.end());
    if (!m.copy(it->second.live, live.data(), live.size())) return false;
    for (uint16_t i = 0; i < range->fields; ++i) {
        const auto& f = table::fields[range->first_field + i];
        const auto n = (std::min)(size_t(f.length), expected.size() - f.offset);
        std::memset(live.data() + f.offset, 0, n);
        std::memset(ref.data() + f.offset, 0, n);
    }
    return live == ref;
}
} // namespace wuwa_self_port
