// The recorder's slow-game stop (1.2.7): a recording ends when the game's own frame rate, averaged over the last
// 20 s of time it held SteamVR's scene, is under 5 fps. Loading freezes and scene changes must pass; a lag spiral
// must not. Offline; the patterns follow the owner's recordings (25 Oct 2026).
#include "../launcher/dev/steamvr-capture/slow_game.hpp"
#include <cassert>
#include <cstdio>
#include <functional>

namespace {
// Drives the guard as the recorder does: a sample every `step` seconds while the game holds the scene, with the
// game drawing `rate(t)` frames a second (0: frozen), and `scene(t)` false while SteamVR shows something else.
// Returns the time it first reported slow, or -1.
double run(SlowGame& guard, double seconds, double step, const std::function<double(double)>& rate,
           const std::function<bool(double)>& scene = [](double) { return true; }, uint32_t first_frame = 1000) {
    double frames = first_frame;
    for (double t = 0; t < seconds; t += step) {
        frames += rate(t) * step;
        if (!scene(t)) { guard.pause(t); continue; }
        guard.sample(t, static_cast<uint32_t>(static_cast<uint64_t>(frames)));
        if (guard.slow()) return t;
    }
    return -1;
}
}

int main() {
    // A healthy game, sampled at the side-by-side recorder's 30 a second: never slow.
    { SlowGame g(5, 20); assert(run(g, 120, 1.0 / 30, [](double) { return 30.0; }) < 0); }
    // A 10 s loading freeze (no new frames) between normal play: the 20 s average stays near 15 fps.
    { SlowGame g(5, 20); assert(run(g, 90, 1.0 / 30, [](double t) { return t >= 40 && t < 50 ? 0.0 : 30.0; }) < 0); }
    // Two freezes of 8 s, 6 s apart (a teleport, then a cutscene load): 14 frozen seconds in 20, still passes.
    { SlowGame g(5, 20);
      assert(run(g, 90, 1.0 / 30, [](double t) { return (t >= 40 && t < 48) || (t >= 54 && t < 62) ? 0.0 : 30.0; }) < 0); }
    // A lag spiral: 30 fps, then 3 fps from 30 s. The average crosses 5 fps after about 18.5 s of it.
    { SlowGame g(5, 20); const double at = run(g, 120, 1.0 / 30, [](double t) { return t < 30 ? 30.0 : 3.0; });
      assert(at > 30 + 15 && at < 30 + 21); }
    // The same spiral while captures crawl (one sample every 0.4 s, as when the GPU is full): still caught in time.
    { SlowGame g(5, 20); const double at = run(g, 120, 0.4, [](double t) { return t < 30 ? 30.0 : 3.0; });
      assert(at > 30 + 15 && at < 30 + 22); }
    // Slow from the very start: nothing is decided before 20 s of the game's time.
    { SlowGame g(5, 20); const double at = run(g, 60, 1.0 / 30, [](double) { return 2.0; });
      assert(at >= 20 && at < 21); }
    // SteamVR shows something else for 30 s (the game lost the scene): that time does not count, and normal play
    // around it passes.
    { SlowGame g(5, 20);
      assert(run(g, 120, 1.0 / 30, [](double t) { return t >= 25 && t < 55 ? 0.0 : 30.0; },
                 [](double t) { return !(t >= 25 && t < 55); }) < 0); }
    // A spiral where the game keeps losing the scene (1 s held, 2 s lost): only held time counts, so 20 s of it
    // takes about a minute of wall time, but it is caught.
    { SlowGame g(5, 20);
      const double at = run(g, 200, 1.0 / 30, [](double) { return 3.0; }, [](double t) { return static_cast<int>(t) % 3 == 0; });
      assert(at > 55 && at < 70); }
    // Off (0 fps) never stops; a stricter rule (10 fps for 10 s) catches 8 fps quickly.
    { SlowGame g(0, 20); assert(!g.enabled()); assert(run(g, 60, 1.0 / 30, [](double) { return 1.0; }) < 0); }
    { SlowGame g(10, 10); const double at = run(g, 60, 1.0 / 30, [](double) { return 8.0; }); assert(at >= 10 && at < 11); }
    // The frame index wrapping past 2^32 does not read as a huge or negative rate.
    { SlowGame g(5, 20);
      assert(run(g, 60, 1.0 / 30, [](double) { return 30.0; }, [](double) { return true; }, 0xFFFFF000u) < 0); }
    { SlowGame g(5, 20);
      assert(run(g, 60, 1.0 / 30, [](double) { return 2.0; }, [](double) { return true; }, 0xFFFFFF00u) > 0); }
    // The showcase loop polls every 2 ms: the stored samples stay bounded (about ten a second of window).
    { SlowGame g(5, 20); run(g, 60, 0.002, [](double) { return 30.0; }); assert(g.stored() < 260); }
    std::puts("slow game stop: all checks passed");
    return 0;
}
