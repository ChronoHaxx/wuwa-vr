// Off-target checks for the diagnostic view-state swap transaction. No game,
// headset or Windows API: the two view fields live in a fake memory map whose
// reads and writes can be made to fail or to be changed "by the engine".
#if __has_include("../../mod/uevr/src/utility/WuWaStateSwap.hpp")
#include "../../mod/uevr/src/utility/WuWaStateSwap.hpp"
#else
#include "../upstream/UEVR/src/utility/WuWaStateSwap.hpp"
#endif
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

using wuwa_stereo::StateSwapTransaction;
using Result = StateSwapTransaction::Result;

namespace {
constexpr uintptr_t first_field = 0x100008, second_field = 0x900008;   // view+0x8 of two views
constexpr uintptr_t state_a = 0x383a0aac0, state_b = 0x38972d560;       // as in the 29 Sep traces

struct Memory {
    std::map<uintptr_t, uintptr_t> cells;
    std::set<uintptr_t> unreadable, unwritable, ignores_write;  // ignores_write: write "succeeds", value unchanged
    int writes = 0;
} memory;

bool read(uintptr_t at, uintptr_t& out) {
    if (memory.unreadable.count(at) || !memory.cells.count(at)) return false;
    out = memory.cells[at];
    return true;
}
bool write(uintptr_t at, uintptr_t value) {
    ++memory.writes;
    if (memory.unwritable.count(at) || !memory.cells.count(at)) return false;
    if (!memory.ignores_write.count(at)) memory.cells[at] = value;
    return true;
}
void reset(uintptr_t a = state_a, uintptr_t b = state_b) {
    memory = {};
    memory.cells[first_field] = a;
    memory.cells[second_field] = b;
}
bool original() { return memory.cells[first_field] == state_a && memory.cells[second_field] == state_b; }

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}

void swaps_for_the_scope_and_restores() {
    reset();
    {
        StateSwapTransaction swap{first_field, second_field, &read, &write};
        check(swap.applied(), "applied");
        check(memory.cells[first_field] == state_b && memory.cells[second_field] == state_a, "states exchanged");
        check(swap.original(0) == state_a && swap.original(1) == state_b, "originals kept");
    }
    check(original(), "both originals back after the scope");
    check(memory.writes == 4, "two writes to swap, two to restore");
}

void refuses_without_writing() {
    struct Case { const char* name; uintptr_t a, b, fa, fb; bool unreadable_b; };
    const Case cases[] = {
        {"null first field", state_a, state_b, 0, second_field, false},
        {"same field twice", state_a, state_b, first_field, first_field, false},
        {"overlapping fields", state_a, state_b, first_field, first_field + 4, false},
        {"null state", 0, state_b, first_field, second_field, false},
        {"shared state", state_a, state_a, first_field, second_field, false},
        {"unreadable field", state_a, state_b, first_field, second_field, true},
    };
    for (const auto& c : cases) {
        reset(c.a, c.b);
        memory.cells[first_field + 4] = 0;
        if (c.unreadable_b) memory.unreadable.insert(second_field);
        {
            StateSwapTransaction swap{c.fa, c.fb, &read, &write};
            check(swap.result() == Result::skipped, c.name);
        }
        check(memory.writes == 0, c.name);
    }
}

void failed_second_write_rolls_back_the_first() {
    reset();
    memory.unwritable.insert(second_field);
    {
        StateSwapTransaction swap{first_field, second_field, &read, &write};
        check(swap.result() == Result::write_failed, "write failure reported");
        check(original(), "first field restored at once");
        check(swap.restore(), "rollback verified");
    }
    check(original(), "still original after the scope");
}

void write_that_does_not_stick_is_a_failure() {
    reset();
    memory.ignores_write.insert(first_field);
    {
        StateSwapTransaction swap{first_field, second_field, &read, &write};
        check(swap.result() == Result::write_failed, "read-back mismatch reported");
    }
    check(original(), "nothing left exchanged");
}

void engine_change_is_never_overwritten() {
    reset();
    constexpr uintptr_t engine_value = 0x3faaa2200;
    bool restored = true;
    {
        StateSwapTransaction swap{first_field, second_field, &read, &write};
        check(swap.applied(), "applied before the change");
        memory.cells[first_field] = engine_value;       // someone else rewrote the field mid-pair
        restored = swap.restore();
        check(!restored, "restore reports the conflict");
        check(memory.cells[first_field] == engine_value, "foreign value left alone");
        check(memory.cells[second_field] == state_b, "untouched field still restored");
        const int writes = memory.writes;
        check(!swap.restore() && memory.writes == writes, "second restore writes nothing");
    }
    check(!restored, "conflict stays reported");
}

void field_already_restored_needs_no_write() {
    reset();
    {
        StateSwapTransaction swap{first_field, second_field, &read, &write};
        memory.cells[first_field] = state_a;             // already back
        const int writes = memory.writes;
        check(swap.restore(), "accepted");
        check(memory.writes == writes + 1, "only the other field written");
    }
    check(original(), "original");
}

void share_modes_write_one_field_and_restore() {
    using Mode = StateSwapTransaction::Mode;
    reset();
    {
        StateSwapTransaction share{first_field, second_field, &read, &write, Mode::first_for_both};
        check(share.applied(), "first_for_both applied");
        check(memory.cells[first_field] == state_a && memory.cells[second_field] == state_a,
            "second view uses the first view's state");
        check(memory.writes == 1, "one write to share");
    }
    check(original(), "second view's own state back");
    check(memory.writes == 2, "one write to restore");
    reset();
    {
        StateSwapTransaction share{first_field, second_field, &read, &write, Mode::second_for_both};
        check(share.applied(), "second_for_both applied");
        check(memory.cells[first_field] == state_b && memory.cells[second_field] == state_b,
            "first view uses the second view's state");
    }
    check(original(), "first view's own state back");
}

void share_mode_failure_rolls_back() {
    using Mode = StateSwapTransaction::Mode;
    reset();
    memory.ignores_write.insert(second_field);
    {
        StateSwapTransaction share{first_field, second_field, &read, &write, Mode::first_for_both};
        check(share.result() == Result::write_failed, "share write that does not stick is a failure");
    }
    check(original(), "nothing left shared");
    reset(state_a, state_a);
    {
        StateSwapTransaction share{first_field, second_field, &read, &write, Mode::first_for_both};
        check(share.result() == Result::skipped, "already shared: skipped");
    }
    check(memory.writes == 0, "no write when both views already share a state");
}
} // namespace

int main() {
    swaps_for_the_scope_and_restores();
    refuses_without_writing();
    failed_second_write_rolls_back_the_first();
    write_that_does_not_stick_is_a_failure();
    engine_change_is_never_overwritten();
    field_already_restored_needs_no_write();
    share_modes_write_one_field_and_restore();
    share_mode_failure_rolls_back();
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "state swap: 8 cases passed\n";
    return 0;
}
