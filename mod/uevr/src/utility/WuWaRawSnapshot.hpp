#pragma once
#include "WuWaEyeDiff.hpp"
#include <array>
#include <type_traits>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Opt-in, bounded raw copies of both main-eye views and view states, taken at a
// few of the eye-diff pairs. Unlike eye_pair_diff, every dword is kept, equal
// ones too, with a per-dword read-validity bit, so absolute values can be
// compared between eye slots and between captures. Read-only guarded reads; no
// hook, no write to game memory, no allocation and no file I/O on the game or
// render thread (the control thread drains the ring). Windows-free so the policy
// can be tested off-target; the caller supplies the guarded reader.
//
// The windows are fixed spans from each object's start. A readable dword does not
// show that it belongs to the object, and a slot index is not a physical eye.
namespace wuwa_raw_snapshot {

inline constexpr uint32_t view_bytes = wuwa_eye_diff::view_end;   // same windows as the eye diff
inline constexpr uint32_t state_bytes = wuwa_eye_diff::state_end;
inline constexpr uint32_t pairs_capacity = 3;   // raw before/after pairs per trace
inline constexpr uint32_t every_scheduled = 5;  // eye-diff pairs between raw pairs (300 validated pairs)
inline constexpr size_t capacity = 2 * pairs_capacity;

template<uint32_t Bytes> struct Block {
    static_assert(Bytes % 4 == 0 && Bytes > 0);
    static constexpr uint32_t dwords = Bytes / 4;
    uintptr_t address{};
    uint32_t readable{};                                // dwords read successfully
    std::array<uint32_t, dwords> words{};               // 0 where unreadable
    std::array<uint64_t, (dwords + 63) / 64> valid{};   // bit i: dword i was read
    bool read_ok(uint32_t i) const noexcept { return i < dwords && ((valid[i / 64] >> (i % 64)) & 1); }
};

struct Snapshot {
    wuwa_eye_diff::Identity id{};
    uint32_t ordinal{}; // raw pair index in this trace, 0..pairs_capacity-1
    std::array<Block<view_bytes>, 2> views{};
    std::array<Block<state_bytes>, 2> states{};
};

// Same read discipline as wuwa_eye_diff::compare: whole chunks where possible, a
// failed chunk retried dword by dword, the tail read singly, nothing past Bytes.
// Read is `bool(uintptr_t, T&)` for T = std::array<uint32_t,64> and uint32_t.
template<uint32_t Bytes, class Read> void read_block(Block<Bytes>& block, uintptr_t address, Read&& read) {
    constexpr uint32_t chunk = wuwa_eye_diff::chunk_dwords;
    block.address = address;
    block.readable = 0;
    block.words.fill(0);
    block.valid.fill(0);
    if (!address || address > UINTPTR_MAX - Bytes) return;
    const auto keep = [&](uint32_t i, uint32_t value) noexcept {
        block.words[i] = value;
        block.valid[i / 64] |= uint64_t{1} << (i % 64);
        ++block.readable;
    };
    const auto single = [&](uint32_t i) {
        uint32_t value{};
        if (read(address + 4 * uintptr_t(i), value)) keep(i, value);
    };
    uint32_t i = 0;
    while (i < Block<Bytes>::dwords) {
        if (Block<Bytes>::dwords - i >= chunk) {
            std::array<uint32_t, chunk> words{};
            if (read(address + 4 * uintptr_t(i), words)) {
                for (uint32_t k = 0; k < chunk; ++k) keep(i + k, words[k]);
            } else {
                for (uint32_t k = 0; k < chunk; ++k) single(i + k);
            }
            i += chunk;
        } else {
            single(i++);
        }
    }
}

template<class Read>
void capture(Snapshot& out, const wuwa_eye_diff::Identity& id, uint32_t ordinal,
             uintptr_t first_view, uintptr_t second_view, uintptr_t first_state, uintptr_t second_state,
             Read&& read) {
    out.id = id;
    out.ordinal = ordinal;
    read_block(out.views[0], first_view, read);
    read_block(out.views[1], second_view, read);
    read_block(out.states[0], first_state, read);
    read_block(out.states[1], second_state, read);
}

// Consulted only for snapshots the eye-diff sampler has already taken, so a raw
// pair is always a before/after pair of one sequence. Every `every_scheduled`-th
// eye-diff pair is copied, up to `pairs_capacity` pairs, and a pair starts only
// when both of its snapshots fit. Single writer (the pair() guard owner);
// counters are relaxed atomics for the status reader.
struct Counts {
    std::atomic<uint32_t> taken{}, refused{}, orphaned{};
};
class Schedule {
public:
    // `scheduled_ordinal`: 0-based index of the eye-diff pair this snapshot belongs to.
    bool take(bool requested, uint32_t phase, uint64_t sequence, uint32_t scheduled_ordinal,
              size_t free_slots) noexcept {
        if (!requested || !sequence) return false;
        if (phase == 1) {
            if (pending) { bump(counts_.orphaned); pending = 0; }
            if (scheduled_ordinal % every_scheduled != 0 || pairs >= pairs_capacity) return false;
            if (free_slots < 2) { bump(counts_.refused); return false; }
            pending = sequence;
            ordinal_ = pairs++;
            bump(counts_.taken);
            return true;
        }
        if (phase != 2 || !pending) return false;
        if (pending != sequence) { bump(counts_.orphaned); pending = 0; return false; }
        pending = 0;
        if (!free_slots) { bump(counts_.refused); return false; }
        bump(counts_.taken);
        return true;
    }
    uint32_t ordinal() const noexcept { return ordinal_; }
    const Counts& counts() const noexcept { return counts_; }
    void reset() noexcept {
        pending = 0; pairs = 0; ordinal_ = 0;
        for (auto* c : {&counts_.taken, &counts_.refused, &counts_.orphaned}) c->store(0, std::memory_order_relaxed);
    }
private:
    static void bump(std::atomic<uint32_t>& c) noexcept {
        c.store(c.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
    uint64_t pending{};
    uint32_t pairs{}, ordinal_{};
    Counts counts_{};
};

// Snapshots are about 50 KB, so they are filled in place in a reserved slot and
// visited in place by the consumer; nothing is copied through the stack. One
// producer (pair() under its guard), one control-thread consumer. Never overwrites.
template<size_t Capacity = capacity> class Ring {
    static_assert(Capacity > 0);
    struct Slot { Snapshot value{}; std::atomic<bool> ready{}; };
public:
    size_t free_slots() const noexcept {
        const auto used = next.load(std::memory_order_relaxed);
        return used >= Capacity ? 0 : Capacity - used;
    }
    template<class Fill> bool emplace(Fill&& fill) {
        auto index = next.load(std::memory_order_relaxed);
        do {
            if (index >= Capacity) { overflow.store(true, std::memory_order_relaxed); return false; }
        } while (!next.compare_exchange_weak(index, index + 1, std::memory_order_relaxed));
        fill(slots[index].value);
        slots[index].ready.store(true, std::memory_order_release);
        return true;
    }
    template<class Visit> bool consume(Visit&& visit) {
        if (drained >= Capacity || !slots[drained].ready.load(std::memory_order_acquire)) return false;
        visit(static_cast<const Snapshot&>(slots[drained].value));
        ++drained;
        return true;
    }
    // Control path only, under the owner's exclusive callback lock.
    void reset() noexcept {
        for (auto& slot : slots) slot.ready.store(false, std::memory_order_relaxed);
        next.store(0, std::memory_order_relaxed);
        drained = 0;
        overflow.store(false, std::memory_order_relaxed);
    }
    bool truncated() const noexcept { return overflow.load(std::memory_order_relaxed); }
private:
    // Slot\'s member initializers initialize every field. Avoid MSVC expanding
    // thousands of nested aggregate initializers at compile time.
    static_assert(!std::is_trivially_default_constructible_v<Slot>);
    std::array<Slot, Capacity> slots;
    std::atomic<size_t> next{};
    size_t drained{};
    std::atomic<bool> overflow{};
};

} // namespace wuwa_raw_snapshot
