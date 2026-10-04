#include "../mod/uevr/src/utility/WuWaAutoCinema.hpp"
#include <cassert>
#include <iostream>
using namespace wuwa_auto_cinema;
int main() {
    Lease lease;
    auto generation = lease.start();
    auto sample = [&](uint64_t now, uint8_t active, uint8_t hold, uint8_t known, Presentation mode = Presentation::mono_theatre) {
        assert(lease.sample(generation, "100", active, hold, known, mode, now));
    };
    sample(1000, movie, 0, sources); assert(!lease.active(1000));
    sample(1100, movie, 0, sources); assert(lease.active(1100));
    assert(lease.presentation(1999) == Presentation::mono_theatre && !lease.active(2000));
    sample(2000, 0, movie, sources); assert(!lease.active(2000)); // Paused cannot revive expiry.
    sample(2100, movie, 0, sources); assert(!lease.active(2100));
    sample(2200, movie, 0, sources); assert(lease.active(2200));
    sample(2300, 0, movie, sources); assert(lease.active(3100)); // Pause holds an established movie.
    sample(2400, 0, 0, sources); assert(lease.active(2400));
    sample(2899, 0, 0, sources); assert(lease.active(2899));
    sample(2900, 0, 0, sources); assert(!lease.active(2900)); // Valid stop debounce.
    sample(3000, dialogue, 0, sources, Presentation::stereo_screen);
    sample(3100, dialogue, 0, sources, Presentation::stereo_screen);
    assert(lease.presentation(3100) == Presentation::stereo_screen);
    auto view = resolve(false, true, lease.presentation(3100));
    assert(view.screen && !view.mono); // Automatic stereo can override saved mono, without changing it.
    lease.manual(); // The UI/mono chord reads effective false, then selects manual mono true.
    bool saved_screen = false, saved_mono = !view.mono;
    sample(3200, dialogue, 0, sources); assert(!lease.active(3200));
    assert(lease.sample(generation, "0", 0, 0, 0, Presentation::none, 3210));
    sample(3220, dialogue, 0, sources); assert(!lease.active(3220)); // Temporary world loss is not episode end.
    view = resolve(saved_screen, saved_mono, lease.presentation(3200)); assert(view.mono);
    generation = lease.start(); // Script reload cannot erase the manual episode override.
    sample(3300, dialogue, 0, sources); sample(3400, dialogue, 0, sources);
    assert(!lease.active(3400));
    sample(3500, 0, 0, movie); sample(4200, 0, 0, movie);
    assert(!lease.active(4200)); // Unknown story is not a stop; still suppressed.
    sample(4300, dialogue, 0, sources); assert(!lease.active(4300));
    sample(4400, 0, 0, sources); sample(4900, 0, 0, sources);
    sample(5000, movie, 0, sources); sample(5100, movie, 0, sources); assert(lease.active(5100));
    view = resolve(saved_screen, saved_mono, lease.presentation(5100));
    lease.manual(); manual_screen(saved_screen, saved_mono, !view.screen);
    assert(!saved_screen && !saved_mono); // Auto mono -> explicit screen off actually exits.
    assert(!resolve(saved_screen, saved_mono, lease.presentation(5100)).screen);
    const auto old = generation;
    generation = lease.start();
    lease.stop(old); assert(lease.generation() == generation);
    assert(!lease.sample(old, "100", movie, 0, sources, Presentation::mono_theatre, 5200));
    assert(lease.sample(generation, "200", movie, 0, sources, Presentation::mono_theatre, 5300));
    assert(lease.sample(generation, "200", movie, 0, sources, Presentation::mono_theatre, 5400));
    assert(lease.active(5400)); // New world is a new episode.
    assert(!lease.sample(generation, "200", movie, 0, 0, Presentation::mono_theatre, 5500));
    assert(lease.sample(generation, "0", 0, 0, 0, Presentation::none, 5500));
    assert(!lease.active(5500)); // World loss immediately releases the view.
    lease.reset(); assert(!lease.active(5500));
    assert(resolve(true, false, Presentation::none).screen);
    assert(resolve(false, true, Presentation::none).mono);
    std::cout << "PASS automatic cinematic lease, expiry, pause, manual precedence, world and producer fencing\n";
}
