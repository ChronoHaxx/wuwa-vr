// Treadmill run rules (mod/uevr/src/utility/WuWaRunMarker.hpp): automatic start and end,
// walking time, and route distance with teleports left out. Pure values; no files or devices.
#include "../mod/uevr/src/utility/WuWaRunMarker.hpp"

#include <cstdlib>
#include <iostream>

using namespace wuwa_run;

namespace {
int checks{};
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}
// Walking input every 100 ms from `from` to `to`.
void walk(Policy& p, int64_t from, int64_t to) {
    for (int64_t t = from; t <= to; t += 100) p.walk(t);
}
}

int main() {
    {
        Policy p;
        check(p.decide(0, true) == Decision::None, "idle start");
        walk(p, 1000, 10900);
        check(p.decide(10900, true) == Decision::None, "started before 10 s of walking");
        walk(p, 11000, 11000);
        check(p.decide(11000, true) == Decision::Start, "10 s of walking did not start a run");
        check(p.decide(11000, false) == Decision::None, "started with automatic runs off");
    }
    {
        Policy p;
        walk(p, 0, 6000);
        walk(p, 9000, 15000); // a 3 s pause restarts the 10 s count
        check(p.decide(15000, true) == Decision::None, "a pause did not restart the count");
        walk(p, 15100, 19000);
        check(p.decide(19000, true) == Decision::Start, "walking after a pause did not start a run");
    }
    {
        Policy p;
        walk(p, 0, 10000);
        p.start(10000, true);
        walk(p, 10100, 70000);
        check(p.current(70000).walking_ms >= 59000 && p.current(70000).walking_ms <= 60000, "walking time was not counted");
        check(p.decide(70000 + auto_end_ms - 1, true) == Decision::None, "ended before the idle time");
        check(p.decide(70000 + auto_end_ms, true) == Decision::End, "idle did not end an automatic run");
        p.end(70000 + auto_end_ms, true);
        check(!p.active && p.totals.duration_ms == 60000, "the automatic end kept the idle wait in the time");
        check(p.decide(70000 + auto_end_ms + 1, true) == Decision::None, "an ended run asked to end again");
    }
    {
        Policy p;
        p.start(0, false);
        p.walk(1000);
        check(p.decide(1000 + auto_end_ms * 2, true) == Decision::None, "a run started by hand ended by itself");
        p.end(1000 + auto_end_ms * 2, false);
        check(p.totals.duration_ms == 1000 + auto_end_ms * 2, "a manual end trimmed the time");
    }
    {
        Policy p;
        p.start(0, false);
        check(p.position(0, 0, 0), "first position");
        check(p.position(300, 400, 50), "a walking step was not counted");
        check(p.totals.distance_cm == 500.0, "height counted in distance");
        check(!p.position(300 + max_step_cm + 1, 400, 0), "a teleport was counted");
        check(p.totals.distance_cm == 500.0, "a teleport added distance");
        check(p.position(300 + max_step_cm + 101, 400, 0), "walking after a teleport");
        check(p.totals.distance_cm == 600.0, "distance after a teleport");
        check(!p.position(std::nan(""), 0, 0) && p.totals.distance_cm == 600.0, "a bad position changed the distance");
        p.start(5000, false);
        check(p.totals.distance_cm == 0.0 && !p.has_position, "a new run kept the old distance");
    }
    check(clock_text(0) == "0:00" && clock_text(61000) == "1:01" && clock_text(3723000) == "1:02:03", "clock text");
    std::cout << "PASS: " << checks << " treadmill run checks (pure values; no files or devices)\n";
    return 0;
}
