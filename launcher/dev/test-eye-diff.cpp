// Off-target checks for the paired-eye raw diff policy. No game, headset or
// Windows API is used: memory is two fake buffers behind a guarded-reader stub.
#if __has_include("../../mod/uevr/src/utility/WuWaEyeDiff.hpp")
#include "../../mod/uevr/src/utility/WuWaEyeDiff.hpp"
#include "../../mod/uevr/src/utility/WuWaRawSnapshot.hpp"
#else
#include "../upstream/UEVR/src/utility/WuWaEyeDiff.hpp"
#include "../upstream/UEVR/src/utility/WuWaRawSnapshot.hpp"
#endif
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

using namespace wuwa_eye_diff;

namespace {
constexpr uintptr_t base_a = 0x100000, base_b = 0x900000;

// Two fake objects, a per-dword "unmapped" mask per object and the highest
// byte each object was read at, so over-reads are observable.
struct Memory {
    std::vector<uint32_t> a, b;
    std::vector<bool> bad_a, bad_b;
    uintptr_t max_a{}, max_b{};
    explicit Memory(size_t dwords) : a(dwords), b(dwords), bad_a(dwords), bad_b(dwords) {
        for (size_t i = 0; i < dwords; ++i) a[i] = b[i] = uint32_t(0x1000 + i);
    }
    template<class T> bool read(uintptr_t address, T& out) {
        const bool first = address >= base_a && address < base_a + a.size() * 4;
        const bool second = address >= base_b && address < base_b + b.size() * 4;
        if (!first && !second) return false;
        const auto& data = first ? a : b;
        const auto& bad = first ? bad_a : bad_b;
        const auto origin = first ? base_a : base_b;
        constexpr size_t words = sizeof(T) / 4;
        const size_t index = (address - origin) / 4;
        for (size_t i = 0; i < words; ++i) {
            if (index + i >= data.size() || bad[index + i]) return false;
        }
        auto& high = first ? max_a : max_b;
        high = (std::max)(high, address + sizeof(T));
        std::memcpy(&out, &data[index], sizeof(T));
        return true;
    }
    auto reader() { return [this](uintptr_t address, auto& out) { return read(address, out); }; }
};

void identical_objects_report_nothing() {
    Memory m(0x400);
    Region<8> r;
    compare(r, base_a, base_b, 0, 0x1000, m.reader());
    assert(r.valid && r.compared == 0x400 && r.differing == 0 && r.count == 0 &&
           r.unreadable == 0 && !r.truncated());
}

void differences_are_ascending_and_carry_both_values() {
    Memory m(0x400); // 16 whole chunks
    m.b[0] = 7;      // first dword
    m.a[63] = 9;     // last dword of the first chunk
    m.b[64] = 11;    // first dword of the second chunk
    m.b[0x3ff] = 13; // last dword
    Region<8> r;
    compare(r, base_a, base_b, 0, 0x1000, m.reader());
    assert(r.valid && r.differing == 4 && r.count == 4 && !r.truncated());
    assert(r.deltas[0].offset == 0 && r.deltas[0].first == 0x1000 && r.deltas[0].second == 7);
    assert(r.deltas[1].offset == 63 * 4 && r.deltas[1].first == 9 && r.deltas[1].second == 0x1000 + 63);
    assert(r.deltas[2].offset == 64 * 4 && r.deltas[2].second == 11);
    assert(r.deltas[3].offset == 0x3ff * 4 && r.deltas[3].second == 13);
}

void truncation_keeps_lowest_offsets_and_counts_every_difference() {
    Memory m(0x400);
    for (size_t i = 10; i < 20; ++i) m.b[i] = 0xdead0000u + uint32_t(i);
    Region<4> r;
    compare(r, base_a, base_b, 0, 0x1000, m.reader());
    assert(r.differing == 10 && r.count == 4 && r.truncated());
    for (uint32_t i = 0; i < 4; ++i) assert(r.deltas[i].offset == (10 + i) * 4);
}

void partial_range_never_reads_past_end() {
    // 100 dwords: one full chunk plus a 36-dword tail read dword by dword.
    Memory m(0x400);
    m.b[99] = 5;
    m.b[100] = 6; // outside the requested range: must not be reported or read
    Region<8> r;
    compare(r, base_a, base_b, 0, 400, m.reader());
    assert(r.valid && r.compared == 100 && r.differing == 1 && r.deltas[0].offset == 99 * 4);
    assert(m.max_a == base_a + 400 && m.max_b == base_b + 400);
}

void unreadable_dwords_are_skipped_not_compared() {
    Memory m(0x400);
    m.bad_a[70] = true;                 // mid-chunk hole in the first object
    m.bad_b[130] = m.bad_b[131] = true; // hole in the second object
    m.a[70] = 1; m.b[70] = 2;           // would differ, but is not readable
    m.b[5] = 3;                         // readable difference before the holes
    m.b[200] = 4;                       // readable difference after both holes
    Region<8> r;
    compare(r, base_a, base_b, 0, 0x1000, m.reader());
    assert(r.valid && r.unreadable == 3 && r.compared == 0x400 - 3);
    assert(r.differing == 2 && r.deltas[0].offset == 5 * 4 && r.deltas[1].offset == 200 * 4);
}

void unmapped_second_object_is_never_a_false_difference() {
    Memory m(0x100);
    for (size_t i = 0; i < m.bad_b.size(); ++i) m.bad_b[i] = true;
    Region<8> r;
    compare(r, base_a, base_b, 0, 0x400, m.reader());
    assert(r.valid && r.compared == 0 && r.unreadable == 0x100 && r.differing == 0);
}

void a_reused_region_is_fully_reset() {
    Memory m(0x400);
    m.b[1] = 99;
    Region<8> r;
    compare(r, base_a, base_b, 0, 0x1000, m.reader());
    assert(r.differing == 1);
    Memory clean(0x400);
    compare(r, base_a, base_b, 0, 0x1000, clean.reader()); // same scratch, no stale delta
    assert(r.valid && r.differing == 0 && r.count == 0 && r.compared == 0x400 && !r.truncated());
    compare(r, base_a, base_a, 0, 0x1000, clean.reader()); // now refused
    assert(!r.valid && r.compared == 0 && r.count == 0);
}

void invalid_arguments_read_nothing() {
    Memory m(0x400);
    auto reader = m.reader();
    const auto rejected = [&](uintptr_t a, uintptr_t b, uint32_t begin, uint32_t end) {
        Region<8> r;
        compare(r, a, b, begin, end, reader);
        return !r.valid && r.compared == 0 && r.differing == 0 && r.unreadable == 0;
    };
    assert(rejected(0, base_b, 0, 0x100));
    assert(rejected(base_a, 0, 0, 0x100));
    assert(rejected(base_a, base_a, 0, 0x100));        // one view compared to itself
    assert(rejected(base_a, base_b, 0x100, 0x100));    // empty
    assert(rejected(base_a, base_b, 0x100, 0x80));     // reversed
    assert(rejected(base_a, base_b, 2, 0x100));        // misaligned begin
    assert(rejected(base_a, base_b, 0, 0x102));        // misaligned end
    assert(rejected(base_a, base_b, 0, max_span + 4)); // oversized
    constexpr auto top = (std::numeric_limits<uintptr_t>::max)();
    assert(rejected(top - 0x80, base_b, 0, 0x100));    // address arithmetic would wrap
    assert(rejected(base_a, top - 0x80, 0, 0x100));
    assert(m.max_a == 0 && m.max_b == 0);
}

// Sampler policy. The schedule is keyed to the probe's pair sequence (issued only
// to validated before-submission snapshots), never to a family frame: see
// check-eye-diff-json.py for the same policy driven through the verbatim pair().
void sampler_pairs_before_and_after_of_one_sequence() {
    Sampler s;
    assert(s.take(1, 1, 128));   // first validated pair of a capture is on the schedule
    assert(!s.take(3, 1, 127));  // mid-pair snapshot never sampled
    assert(s.take(2, 1, 127));   // the matching after-snapshot
    assert(!s.take(2, 1, 126));  // ...exactly once
    assert(s.take(1, 61, 126));  // next scheduled pair, 60 sequences later
    assert(!s.take(2, 62, 125)); // a different pair is not this pair's partner: orphan
    assert(!s.take(2, 61, 125)); // ...and the orphaned partner is forgotten, not revived
    const auto& c = s.counts();
    assert(c.before_taken == 2 && c.after_taken == 1 && c.orphaned == 1);
}

void sampler_refuses_off_schedule_full_ring_and_unsequenced() {
    Sampler s;
    assert(!s.take(1, 2, 128));  // off the 60-pair schedule
    assert(!s.take(2, 2, 128));  // and so no partner is owed
    assert(!s.take(1, 0, 128));  // no sequence token (phase 1 failed validation), no sample
    assert(!s.take(0, 1, 128));
    assert(!s.take(4, 1, 128));
    assert(!s.take(1, 61, 1));   // room for the before but not its after: never start a pair
    assert(!s.take(2, 61, 1));
    assert(s.take(1, 121, 2));
    assert(!s.take(2, 121, 0));  // ring filled in between: partner refused, counted, no crash
    assert(s.counts().ring_full == 2);
    assert(s.take(1, 181, 128));
    s.reset();
    assert(!s.take(2, 181, 128)); // reset forgets the owed partner
    assert(s.counts().before_seen == 0 && s.counts().orphaned == 0); // ...and its counters
}

// One validated phase 1 per pair; phases 3 and 2 carry the same token. Frames
// are not an input: the real family-frame lifecycle (unassigned before the first
// submission, strides) is replayed through pair() in check-eye-diff-json.py.
void sampler_cadence_over_a_simulated_walk() {
    Sampler s;
    uint32_t before = 0, after = 0;
    for (uint64_t sequence = 1; sequence <= 600; ++sequence) {
        if (s.take(1, sequence, 128 - before - after)) ++before;
        if (s.take(3, sequence, 128 - before - after)) assert(false);
        if (s.take(2, sequence, 128 - before - after)) ++after;
    }
    assert(before == 10 && after == 10); // one pair per 60 pairs, none unpaired
    assert(s.counts().before_seen == 600 && s.counts().orphaned == 0);
}

// Invalid early snapshots never reach the sampler (no token), so they neither
// consume a sequence nor shift the schedule.
void invalid_early_snapshots_do_not_shift_the_schedule() {
    Sampler s;
    uint64_t sequence = 0;
    std::vector<uint64_t> scheduled;
    for (uint32_t i = 0; i < 200; ++i) {
        const bool valid = i >= 17;                       // first 17 pairs fail validation
        if (!valid) continue;
        ++sequence;
        if (s.take(1, sequence, 128)) scheduled.push_back(sequence);
        s.take(2, sequence, 128);
    }
    assert((scheduled == std::vector<uint64_t>{1, 61, 121, 181})); // 183 valid pairs
}

void ring_reports_free_slots() {
    auto ring = std::make_unique<Ring<3>>();
    auto s = std::make_unique<Sample>();
    assert(ring->free_slots() == 3);
    assert(ring->append(*s) && ring->free_slots() == 2);
    assert(ring->append(*s) && ring->append(*s) && ring->free_slots() == 0);
    assert(!ring->append(*s) && ring->free_slots() == 0);
    ring->reset();
    assert(ring->free_slots() == 3);
}

void sample_compares_views_and_states_independently() {
    // One backing buffer serves both windows, so a difference planted at +0x2f8
    // lies inside the view window AND the (larger) state window.
    Memory mem(0x1000);
    mem.b[0x2f8 / 4] = 1234;
    auto out = std::make_unique<Sample>();
    sample(*out, {10, 3, 99, 1, {30, 30}}, base_a, base_b, base_a, base_a, mem.reader());
    assert(out->id.sequence == 3 && out->id.phase == 1 && out->views[0] == base_a);
    assert(out->view.valid && out->view.differing == 1 && out->view.deltas[0].offset == 0x2f8);
    assert(!out->state.valid); // identical state pointers are refused, not reported as equal
    sample(*out, {10, 4, 99, 2, {31, 31}}, base_a, base_b, base_a, base_b, mem.reader());
    assert(out->view.differing == 1 && out->state.valid && out->state.differing == 1 &&
           out->state.deltas[0].offset == 0x2f8 && out->state.deltas[0].second == 1234);
    // Reusing the scratch sample leaves no stale delta from the previous call.
    Memory clean(0x1000);
    sample(*out, {11, 5, 99, 1, {60, 60}}, base_a, base_b, base_a, base_b, clean.reader());
    assert(out->view.differing == 0 && out->state.differing == 0 && out->id.sequence == 5);
}

void ring_never_overwrites_and_resets() {
    auto ring = std::make_unique<Ring<3>>();
    auto s = std::make_unique<Sample>();
    assert(!ring->full());
    for (uint64_t i = 1; i <= 3; ++i) { s->id.sequence = i; assert(ring->append(*s)); }
    assert(ring->full() && !ring->truncated());
    s->id.sequence = 4;
    assert(!ring->append(*s) && ring->truncated());
    auto out = std::make_unique<Sample>();
    for (uint64_t i = 1; i <= 3; ++i) { assert(ring->pop(*out) && out->id.sequence == i); }
    assert(!ring->pop(*out));
    ring->reset();
    assert(!ring->full() && !ring->truncated() && !ring->pop(*out));
    s->id.sequence = 9;
    assert(ring->append(*s) && ring->pop(*out) && out->id.sequence == 9);
}
// Raw snapshots: every dword of both objects, equal ones included, with validity.
void raw_block_keeps_equal_values_and_marks_unreadable_dwords() {
    Memory m(0x800);                 // 0x2000 bytes per object; the view window is 0x1e40
    m.b[3] = 99;                     // one difference; everything else equal
    m.bad_b[70] = true;              // one unmapped dword inside a chunk
    auto s = std::make_unique<wuwa_raw_snapshot::Snapshot>();
    wuwa_raw_snapshot::capture(*s, {5, 61, 7, 1, {1, 1}}, 2, base_a, base_b, 0, base_b, m.reader());
    const auto& a = s->views[0]; const auto& b = s->views[1];
    assert(s->id.sequence == 61 && s->id.phase == 1 && s->ordinal == 2);
    assert(a.readable == a.dwords && b.readable == b.dwords - 1);
    assert(a.words[0] == 0x1000 && b.words[0] == 0x1000);  // equal values are kept
    assert(b.words[3] == 99 && a.words[3] == 0x1000 + 3);
    assert(!b.read_ok(70) && b.words[70] == 0 && b.read_ok(69) && b.read_ok(71));
    assert(m.max_a <= base_a + wuwa_raw_snapshot::view_bytes && m.max_b <= base_b + 0x2000);
    assert(s->states[0].address == 0 && s->states[0].readable == 0);  // null object: nothing read
    assert(s->states[1].readable == 0x800 - 1 && !s->states[1].read_ok(70)); // ends at 0x2000, one hole
    assert(s->states[1].read_ok(0x7ff) && !s->states[1].read_ok(0x800));
}

void raw_block_is_fully_reset_on_reuse() {
    Memory m(0x1000);
    auto s = std::make_unique<wuwa_raw_snapshot::Snapshot>();
    wuwa_raw_snapshot::capture(*s, {}, 0, base_a, base_b, base_a, base_b, m.reader());
    Memory empty(1);
    empty.bad_a[0] = empty.bad_b[0] = true;
    wuwa_raw_snapshot::capture(*s, {}, 1, base_a, base_b, base_a, base_b, empty.reader());
    for (const auto& block : s->views) assert(block.readable == 0 && block.words[5] == 0 && !block.read_ok(5));
    for (const auto& block : s->states) assert(block.readable == 0 && block.words[0x100] == 0);
}

void raw_schedule_copies_every_fifth_eye_diff_pair_up_to_its_budget() {
    wuwa_raw_snapshot::Schedule s;
    std::vector<uint64_t> taken;
    for (uint32_t ordinal = 0; ordinal < 20; ++ordinal) {
        const uint64_t sequence = 1 + 60 * uint64_t(ordinal); // the eye-diff pair's sequence
        if (s.take(true, 1, sequence, ordinal, 6)) taken.push_back(sequence);
        if (s.take(true, 2, sequence, ordinal, 6)) taken.push_back(sequence);
    }
    assert((taken == std::vector<uint64_t>{1, 1, 301, 301, 601, 601}));
    assert(s.counts().taken == 6 && s.counts().orphaned == 0 && s.counts().refused == 0);
    wuwa_raw_snapshot::Schedule off;
    assert(!off.take(false, 1, 1, 0, 6) && !off.take(true, 1, 0, 0, 6)); // not requested / no sequence
    wuwa_raw_snapshot::Schedule tight;
    assert(!tight.take(true, 1, 1, 0, 1) && tight.counts().refused == 1); // never start half a pair
    assert(tight.take(true, 1, 301, 5, 2) && tight.ordinal() == 0);
    assert(!tight.take(true, 2, 302, 5, 1) && tight.counts().orphaned == 1); // wrong partner
    tight.reset();
    assert(tight.counts().taken == 0 && tight.take(true, 1, 1, 0, 2));
}

void raw_ring_fills_in_place_and_never_overwrites() {
    auto ring = std::make_unique<wuwa_raw_snapshot::Ring<2>>();
    assert(ring->free_slots() == 2);
    assert(ring->emplace([](wuwa_raw_snapshot::Snapshot& s) { s.id.sequence = 1; }));
    assert(ring->emplace([](wuwa_raw_snapshot::Snapshot& s) { s.id.sequence = 2; }));
    assert(!ring->emplace([](wuwa_raw_snapshot::Snapshot&) { assert(false); }) && ring->truncated());
    std::vector<uint64_t> seen;
    while (ring->consume([&](const wuwa_raw_snapshot::Snapshot& s) { seen.push_back(s.id.sequence); })) {}
    assert((seen == std::vector<uint64_t>{1, 2}) && ring->free_slots() == 0);
    ring->reset();
    assert(ring->free_slots() == 2 && !ring->truncated() && !ring->consume([](const auto&) { assert(false); }));
}
} // namespace

int main() {
    identical_objects_report_nothing();
    differences_are_ascending_and_carry_both_values();
    truncation_keeps_lowest_offsets_and_counts_every_difference();
    partial_range_never_reads_past_end();
    unreadable_dwords_are_skipped_not_compared();
    unmapped_second_object_is_never_a_false_difference();
    a_reused_region_is_fully_reset();
    invalid_arguments_read_nothing();
    sampler_pairs_before_and_after_of_one_sequence();
    sampler_refuses_off_schedule_full_ring_and_unsequenced();
    sampler_cadence_over_a_simulated_walk();
    invalid_early_snapshots_do_not_shift_the_schedule();
    ring_reports_free_slots();
    raw_block_keeps_equal_values_and_marks_unreadable_dwords();
    raw_block_is_fully_reset_on_reuse();
    raw_schedule_copies_every_fifth_eye_diff_pair_up_to_its_budget();
    raw_ring_fills_in_place_and_never_overwrites();
    sample_compares_views_and_states_independently();
    ring_never_overwrites_and_resets();
    std::cout << "eye diff checks passed\n";
    return 0;
}
