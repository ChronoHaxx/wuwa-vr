// Off-target lifecycle harness for WuWaLodProbe.hpp's pair(). Not built on its own:
// check-eye-diff-json.py extracts the eye-diff serializers, `struct Record` and pair()
// VERBATIM into probe-record.inc and probe-pair.inc and compiles this file around them.
// Everything Windows-specific is an instrumented stand-in, so a scenario can replay the
// call order FFakeStereoRenderingHook.cpp uses and the render thread's hold on the
// callback lock. Nothing here proves game behaviour; it pins the probe's own logic to
// the lifecycle the 28 Sep far capture showed.
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <vector>
#include "WuWaLodSnapshot.hpp"
#include "WuWaEyeDiff.hpp"

namespace wuwa_lod_probe {
using Json = nlohmann::json;

// ---- instrumented stand-ins -------------------------------------------------------------
struct SRWLOCK { int shared{}; bool exclusive{}; };
inline bool TryAcquireSRWLockExclusive(SRWLOCK* l) {
    if (l->shared || l->exclusive) return false;
    l->exclusive = true;
    return true;
}
inline void ReleaseSRWLockExclusive(SRWLOCK* l) { if (!l->exclusive) std::abort(); l->exclusive = false; }
inline bool TryAcquireSRWLockShared(SRWLOCK* l) { if (l->exclusive) return false; ++l->shared; return true; }
inline void ReleaseSRWLockShared(SRWLOCK* l) { if (l->shared <= 0) std::abort(); --l->shared; }
inline uint32_t last_error{};
inline uint32_t GetLastError() { return last_error; }
inline void SetLastError(uint32_t e) { last_error = e; }
inline uint64_t clock_ms{};
inline uint64_t GetTickCount64() { return clock_ms; }
inline uint32_t GetCurrentThreadId() { return 0x1234; }

// Fake game memory: the family, two main views and their two view states.
struct Area { uintptr_t base; std::vector<uint32_t> words; };
inline std::vector<Area> areas;
namespace memory {
template<class T> bool read(uintptr_t at, T& out) {
    out = {};
    for (const auto& area : areas) {
        const size_t bytes = area.words.size() * 4;
        if (at >= area.base && at - area.base <= bytes - sizeof(T)) {
            std::memcpy(&out, reinterpret_cast<const char*>(area.words.data()) + (at - area.base), sizeof(T));
            return true;
        }
    }
    return false;
}
} // namespace memory

// ---- verbatim from WuWaLodProbe.hpp: eye-diff serializers and struct Record ----------------
#include "probe-record.inc"

// Superset of the Probe members pair() touches (current and merged PR #1 versions).
struct Probe {
    std::atomic<uint64_t> until{UINT64_MAX}, sequence{};
    std::array<uintptr_t, 2> states{};
    uint32_t frame_offset{};
    std::atomic<uint32_t> reads_failed{}, lock_misses{};
    std::vector<Record> rows; // unbounded here; the real ring is bounded and counted separately
    void append(const Record& r) noexcept { rows.push_back(r); }
    wuwa_eye_diff::Ring<> eye_diff_ring;
    wuwa_eye_diff::Sampler eye_diff_sampler;
    wuwa_eye_diff::Sample eye_diff_scratch;
    std::atomic<uint64_t> eye_diff_dropped{};
    struct PairCounts { std::atomic<uint32_t> calls{}, lock_misses{}, invalid{}, rows{}; };
    std::array<PairCounts, 3> pair_counts{};
    std::atomic<bool> pair_guard{};
    std::atomic<uint32_t> pair_guard_misses{};
    Record before_row{};
};
inline std::atomic<bool> armed{true};
inline SRWLOCK callbacks{};
inline Probe* owner{};

// ---- verbatim from WuWaLodProbe.hpp: pair() -----------------------------------------------
#include "probe-pair.inc"
} // namespace wuwa_lod_probe

using namespace wuwa_lod_probe;

namespace {
// Stock UE constructs FSceneViewFamily with FrameNumber = UINT_MAX and assigns it inside
// BeginRenderingViewFamily. WuWa's actual pre-submission value is unobserved; the capture
// only shows it never passed a filter the post-submission value passed.
constexpr uint32_t unassigned = 0xffffffffu;
constexpr uintptr_t family_at = 0x80000, view_a = 0x100000, view_b = 0x900000,
                    state_a = 0x1100000, state_b = 0x1900000;

void put64(std::vector<uint32_t>& w, size_t offset, uint64_t v) {
    w[offset / 4] = uint32_t(v); w[offset / 4 + 1] = uint32_t(v >> 32);
}
uint32_t& family_frame() { return areas[0].words[0x64 / 4]; }

void build_memory() {
    areas.clear();
    areas.push_back({family_at, std::vector<uint32_t>(0x40)});
    for (auto base : {view_a, view_b, state_a, state_b}) areas.push_back({base, std::vector<uint32_t>(0x1000)});
    auto& va = areas[1].words; auto& vb = areas[2].words; auto& sa = areas[3].words; auto& sb = areas[4].words;
    for (size_t i = 0; i < 0x1000; ++i) { va[i] = vb[i] = uint32_t(i * 3 + 1); sa[i] = sb[i] = uint32_t(i * 5 + 7); }
    put64(va, 0, family_at); put64(vb, 0, family_at); // same family
    put64(va, 8, state_a); put64(vb, 8, state_b);     // per-eye view state
    // View: eye-specific projection words, a rect x that differs.
    vb[0x320 / 4] = 0x3f0a0000; vb[0x324 / 4] = 0xbf000000; vb[0x2f8 / 4] = 1;
    // State: a float pair 3.25 apart, a pointer-like qword (low half differs), a flag.
    const float t0 = 1234.5f, t1 = 1231.25f;
    std::memcpy(&sa[0x1c0 / 4], &t0, 4); std::memcpy(&sb[0x1c0 / 4], &t1, 4);
    sa[0x40 / 4] = 0x8bfe9970; sb[0x40 / 4] = 0x8bfeb2d0; sa[0x44 / 4] = sb[0x44 / 4] = 0x1a;
    sa[0x300 / 4] = 0; sb[0x300 / 4] = 1;
}

struct Scenario {
    const char* name;
    uint32_t pairs{};
    uint32_t first_frame{};
    uint32_t stride{1};          // assigned-frame advance per pair
    uint32_t second_advance{};   // advance by the second submission (0: rewound, shared frame)
    uint32_t invalid_first{};    // leading pairs whose first view has no state (validation fails)
    bool unassigned_before{true};// family frame not yet assigned before the first submission
    bool render_busy_after{true};// render callbacks hold the shared lock after the submissions
    uint32_t skip_after_every{}; // phase 2 not reached for pair indices divisible by this
    uint32_t busy_before_every{};// a render callback holds the shared lock at phase 1 on every Nth pair
    uint32_t restate_every{};    // first view reports a different state after phase 1 for indices divisible by this
};

template<class S> Json sampler_counts(const S& sampler) {
    if constexpr (requires { sampler.counts().orphaned.load(); }) {
        const auto& c = sampler.counts();
        return {{"before_seen", c.before_seen.load()}, {"before_taken", c.before_taken.load()},
                {"after_taken", c.after_taken.load()}, {"ring_full", c.ring_full.load()},
                {"orphaned", c.orphaned.load()}};
    } else {
        return nullptr; // merged PR #1 sampler: no counters
    }
}

// Replays FFakeStereoRenderingHook.cpp's non-reversed NSF path for `s.pairs` game frames.
Json run(const Scenario& s, std::ostream* fixture) {
    build_memory();
    callbacks = {};
    clock_ms = 100000;
    auto probe = std::make_unique<Probe>();
    owner = probe.get();
    const auto family = reinterpret_cast<const void*>(family_at);
    const auto first = reinterpret_cast<const void*>(view_a), second = reinterpret_cast<const void*>(view_b);
    std::vector<uint32_t> valid_frames, after_frames;
    uint32_t frame = s.first_frame;
    for (uint32_t i = 0; i < s.pairs; ++i, frame += s.stride) {
        put64(areas[1].words, 8, i < s.invalid_first ? 0 : state_a);
        family_frame() = s.unassigned_before ? unassigned : frame;
        const bool busy_before = s.busy_before_every && i % s.busy_before_every == s.busy_before_every - 1;
        if (busy_before) ++callbacks.shared;
        const uint64_t token = pair(family, first, second, 0x64);              // phase 1
        if (busy_before) --callbacks.shared;
        family_frame() = frame;                                                 // first submission
        if (s.restate_every && i % s.restate_every == 0) put64(areas[1].words, 8, state_a + 0x10);
        if (token) pair(family, first, second, 0x64, token, 3);                 // phase 3
        family_frame() = frame + s.second_advance;                              // second submission
        const bool skip_after = s.skip_after_every && i % s.skip_after_every == 0;
        if (s.render_busy_after) ++callbacks.shared;
        if (token && !skip_after) pair(family, first, second, 0x64, token);     // phase 2
        if (s.render_busy_after) --callbacks.shared;
        if (token) { valid_frames.push_back(frame); after_frames.push_back(frame + s.second_advance); }
        if (callbacks.shared || callbacks.exclusive) std::abort(); // pair() released what it took
        clock_ms += 16;
    }
    std::array<uint32_t, 4> rows{};
    for (const auto& r : probe->rows) if (r.phase < rows.size()) ++rows[r.phase];
    uint32_t before = 0, after = 0, paired = 0, invalid_regions = 0;
    std::set<uint64_t> before_set;
    std::vector<uint64_t> before_sequences;
    std::set<uint32_t> before_frames;
    auto popped = std::make_unique<wuwa_eye_diff::Sample>();
    while (probe->eye_diff_ring.pop(*popped)) {
        if (fixture) *fixture << eye_diff_json(*popped).dump() << '\n';
        if (!popped->view.valid || !popped->state.valid) ++invalid_regions;
        if (popped->id.phase == 1) {
            ++before; before_set.insert(popped->id.sequence);
            before_sequences.push_back(popped->id.sequence);
            before_frames.insert(popped->id.frames[0]); before_frames.insert(popped->id.frames[1]);
        } else {
            ++after; if (before_set.count(popped->id.sequence)) ++paired;
        }
    }
    const auto phase_counts = [&](size_t i) {
        const auto& c = probe->pair_counts[i];
        return Json{{"calls", c.calls.load()}, {"lock_misses", c.lock_misses.load()},
                    {"invalid", c.invalid.load()}, {"rows", c.rows.load()}};
    };
    owner = nullptr;
    return {{"scenario", s.name}, {"sequences", probe->sequence.load()},
            {"valid_frames", valid_frames}, {"after_frames", after_frames},
            {"rows", {{"before", rows[1]}, {"after_first", rows[3]}, {"after", rows[2]}}},
            {"eye_diff", {{"before", before}, {"after", after}, {"after_paired", paired},
                          {"before_sequences", before_sequences}, {"before_frames", before_frames},
                          {"invalid_regions", invalid_regions}, {"dropped", probe->eye_diff_dropped.load()},
                          {"truncated", probe->eye_diff_ring.truncated()}}},
            {"lock_misses", probe->lock_misses.load()}, {"reads_failed", probe->reads_failed.load()},
            {"pairs", {{"before", phase_counts(0)}, {"after_first", phase_counts(1)}, {"after", phase_counts(2)}}},
            {"guard_misses", probe->pair_guard_misses.load()},
            {"sampler", sampler_counts(probe->eye_diff_sampler)}};
}
} // namespace

int main(int argc, char** argv) {
    const Scenario scenarios[] = {
        // The 28 Sep far capture: family frame unassigned before submission, render callbacks
        // holding the shared lock after the submissions, a few invalid snapshots at the start.
        {.name = "far_capture_lifecycle", .pairs = 480, .first_frame = 6865, .invalid_first = 5},
        // Frame assigned before submission but advancing by two per pair (second submission
        // not rewound) from an odd frame: a frame-modulo gate can never open.
        {.name = "stride_two_odd_frames", .pairs = 240, .first_frame = 6871, .stride = 2,
         .second_advance = 1, .unassigned_before = false, .render_busy_after = false},
        // Phase 2 never reached for the scheduled pairs: orphans are counted, never paired.
        {.name = "after_snapshot_lost", .pairs = 300, .first_frame = 100, .skip_after_every = 60},
        // Render callback holds the shared lock at phase 1 on every 7th pair.
        {.name = "render_holds_lock_before", .pairs = 420, .first_frame = 30, .busy_before_every = 7},
        // A view's state changes after phase 1 published it: phases 3 and 2 must refuse the
        // pair rather than record (or re-publish) a state the callbacks do not match.
        {.name = "state_changed_mid_pair", .pairs = 300, .first_frame = 90, .restate_every = 60},
        // Long enough to fill the 128-sample ring: pairs are never split, nothing overwritten.
        {.name = "ring_capacity", .pairs = 60 * 70, .first_frame = 1},
        // Two sampled pairs written as the committed native-schema fixture.
        {.name = "fixture", .pairs = 61, .first_frame = 60},
    };
    std::ofstream fixture;
    if (argc > 1) {
        fixture.open(argv[1], std::ios::binary);
        fixture << Json{{"type", "header"}, {"version", 1}, {"seconds", 30}}.dump() << '\n';
    }
    for (const auto& s : scenarios) {
        const bool writes = argc > 1 && std::string(s.name) == "fixture";
        std::cout << run(s, writes ? &fixture : nullptr).dump() << '\n';
    }
    return fixture.is_open() && !fixture ? 1 : 0;
}
