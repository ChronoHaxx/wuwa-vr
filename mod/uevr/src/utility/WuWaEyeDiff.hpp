#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Read-only, bounded comparison of the two main-eye scene views and of their
// view-state objects, taken at the two points where the existing LOD trace
// already snapshots the NSF pair. It answers ONE question for a local capture:
// which raw dwords differ between the eyes at the moment the game hands the
// views to the renderer, and do those dwords change between a near and a far
// (failing) position? No hook, no game-memory write, no allocation, no
// logging, no file I/O, no address is dereferenced later. Windows-free so the
// policy can be tested off-target; the caller supplies the guarded reader.
namespace wuwa_eye_diff {

struct Delta { uint32_t offset{}, first{}, second{}; };

inline constexpr uint32_t chunk_dwords = 64;
inline constexpr uint32_t max_span = 0x4000; // bytes; bounds one comparison

template<size_t Capacity> struct Region {
    uint32_t begin{}, end{};
    uint32_t compared{};   // dwords read from BOTH objects and compared
    uint32_t unreadable{}; // dwords skipped because either object was unreadable
    uint32_t differing{};  // every differing dword, including those past Capacity
    uint32_t count{};      // retained deltas, ascending offset
    bool valid{};          // arguments accepted; false means nothing was read
    std::array<Delta, Capacity> deltas{};
    constexpr bool truncated() const noexcept { return differing > count; }
};

// The region is an out-parameter: samples are ~20 KB and must not be copied
// through stack temporaries on the game thread. Read is
// `bool(uintptr_t address, T& value)` for T = std::array<uint32_t,64>
// and T = uint32_t. A failed chunk is retried dword by dword so one unmapped
// page never hides the readable dwords before it. The final partial chunk is
// read dword by dword, so nothing beyond `end` is ever touched.
template<size_t Capacity, class Read>
void compare(Region<Capacity>& region, uintptr_t first, uintptr_t second,
             uint32_t begin, uint32_t end, Read&& read) {
    // Reset in place: `region = {}` would build an 18 KB temporary. Stale deltas past
    // `count` are never read, so the array itself is left alone.
    region.begin = begin;
    region.end = end;
    region.compared = region.unreadable = region.differing = region.count = 0;
    region.valid = false;
    if (!first || !second || first == second || begin >= end || ((begin | end) & 3) != 0 ||
        end - begin > max_span || first > UINTPTR_MAX - end || second > UINTPTR_MAX - end)
        return;
    region.valid = true;
    const auto note = [&](uint32_t offset, uint32_t a, uint32_t b) noexcept {
        ++region.compared;
        if (a == b) return;
        ++region.differing;
        if (region.count < Capacity) region.deltas[region.count++] = {offset, a, b};
    };
    const auto single = [&](uint32_t offset) noexcept {
        uint32_t a{}, b{};
        if (read(first + offset, a) && read(second + offset, b)) note(offset, a, b);
        else ++region.unreadable;
    };
    uint32_t at = begin;
    while (at < end) {
        if ((end - at) / 4 >= chunk_dwords) {
            std::array<uint32_t, chunk_dwords> a{}, b{};
            if (read(first + at, a) && read(second + at, b)) {
                for (uint32_t i = 0; i < chunk_dwords; ++i) note(at + 4 * i, a[i], b[i]);
            } else {
                for (uint32_t i = 0; i < chunk_dwords; ++i) single(at + 4 * i);
            }
            at += 4 * chunk_dwords;
        } else {
            single(at);
            at += 4;
        }
    }
}

// Extents. The game-thread FSceneView is proven readable through +0x1002 (the
// mode bytes the pair snapshot already reads); +0x1e40 is where the next
// verified structure begins, so deltas above +0x1004 may be neighbouring heap
// if the view is shorter. Ascending order keeps verified-extent deltas first.
// The view-state extent is UNKNOWN: the window is deliberately wide because the
// engine's per-eye LOD, fade and HLOD history lives in this object at an offset
// this build has not established. Expect pointer noise; interpret only dwords
// that stay non-pointer-like across many samples.
inline constexpr uint32_t view_end = 0x1e40, state_end = 0x4000;
inline constexpr uint32_t view_verified_end = 0x1004;
inline constexpr size_t view_capacity = 384, state_capacity = 1536, ring_capacity = 128;

struct Identity {
    uint64_t tick_ms{}, sequence{};
    uint32_t thread{}, phase{};
    std::array<uint32_t, 2> frames{};
};
struct Sample {
    Identity id{};
    std::array<uintptr_t, 2> views{}, states{}; // historical identifiers only
    Region<view_capacity> view{};
    Region<state_capacity> state{};
};

// One "before submissions" (phase 1) snapshot every `interval_frames` family
// frames, then the "after submissions" (phase 2) snapshot of THAT SAME pair
// (same sequence token), so a 30 second far-to-near walk is not exhausted in
// the first seconds. The mid-pair snapshot (phase 3) is never sampled. Used
// only under the probe's exclusive callback lock; reset with the ring.
inline constexpr uint32_t interval_frames = 60;
class Sampler {
public:
    bool take(uint32_t phase, uint64_t sequence, uint32_t first_frame, uint32_t second_frame,
              bool ring_full) noexcept {
        if (ring_full || !sequence) return false;
        if (phase == 1) {
            if (first_frame % interval_frames != 0 && second_frame % interval_frames != 0) return false;
            pending = sequence;
            return true;
        }
        if (phase == 2 && pending == sequence) {
            pending = 0;
            return true;
        }
        return false;
    }
    void reset() noexcept { pending = 0; }
private:
    uint64_t pending{};
};

template<class Read>
void sample(Sample& out, const Identity& id, uintptr_t first_view, uintptr_t second_view,
            uintptr_t first_state, uintptr_t second_state, Read&& read) {
    out.id = id;
    out.views = {first_view, second_view};
    out.states = {first_state, second_state};
    compare<view_capacity>(out.view, first_view, second_view, 0, view_end, read);
    compare<state_capacity>(out.state, first_state, second_state, 0, state_end, read);
}

// One producer path (the pair snapshot, already serialized by the probe's
// exclusive callback lock) and one control-thread consumer. Never overwrites.
template<size_t Capacity = ring_capacity> class Ring {
    static_assert(Capacity > 0);
    struct Slot { Sample sample{}; std::atomic<bool> ready{}; };
public:
    bool full() const noexcept { return next.load(std::memory_order_relaxed) >= Capacity; }
    bool append(const Sample& s) noexcept {
        auto index = next.load(std::memory_order_relaxed);
        do {
            if (index >= Capacity) { overflow.store(true, std::memory_order_relaxed); return false; }
        } while (!next.compare_exchange_weak(index, index + 1, std::memory_order_relaxed));
        slots[index].sample = s;
        slots[index].ready.store(true, std::memory_order_release);
        return true;
    }
    bool pop(Sample& s) noexcept {
        if (drained >= Capacity || !slots[drained].ready.load(std::memory_order_acquire)) return false;
        s = slots[drained++].sample;
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
    std::array<Slot, Capacity> slots{};
    std::atomic<size_t> next{};
    size_t drained{};
    std::atomic<bool> overflow{};
};

} // namespace wuwa_eye_diff
