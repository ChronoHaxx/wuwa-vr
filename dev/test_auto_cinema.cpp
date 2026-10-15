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
    // clear() (automatic cinema switched) ends the episode and a manual override
    // but keeps the producer: the story-scene detector shares its generation.
    generation = lease.generation();
    assert(lease.sample(generation, "300", dialogue, 0, sources, Presentation::stereo_screen, 6000));
    assert(lease.sample(generation, "300", dialogue, 0, sources, Presentation::stereo_screen, 6100));
    lease.manual(); lease.clear();
    assert(lease.generation() == generation && !lease.active(6100));
    assert(lease.sample(generation, "300", dialogue, 0, sources, Presentation::stereo_screen, 6200));
    assert(lease.sample(generation, "300", dialogue, 0, sources, Presentation::stereo_screen, 6300));
    assert(lease.presentation(6300) == Presentation::stereo_screen); // Not suppressed after clear().
    // lease-v2: the plot bit drives the story layout in place of the widget whenever it is
    // known, and never reaches automatic cinema.
    const auto c = cinema_bits({uint8_t(plot | movie), 0, uint8_t(plot | movie | dialogue)});
    assert(c.active == movie && c.known == (movie | dialogue));
    auto s = story_bits({plot, 0, uint8_t(plot | dialogue)}); // plot on, UI hidden: widget inactive
    assert(s.active == dialogue && s.known == dialogue);
    s = story_bits({dialogue, 0, uint8_t(plot | dialogue)}); // widget on, plot over: the plot wins
    assert(s.active == 0 && s.known == dialogue);
    s = story_bits({dialogue, 0, dialogue}); // plot unknown (lease-v1 or no library): the widget
    assert(s.active == dialogue && s.known == dialogue);
    // lease-v3: a story picture reaches automatic cinema only and picks its own presentation,
    // also just after one (the crossfade between pictures); a story video still wins.
    const auto p = cinema_bits({uint8_t(picture | dialogue | plot), 0, uint8_t(picture | dialogue | plot)});
    assert(p.active == (picture | dialogue) && p.known == (picture | dialogue));
    s = story_bits({uint8_t(picture | plot), 0, uint8_t(picture | plot)});
    assert(s.active == dialogue && s.known == dialogue);
    s = story_bits({picture, 0, picture}); // plot unknown: a picture alone is no story scene
    assert(s.active == 0 && s.known == 0);
    const auto keep = Presentation::none, screen = Presentation::stereo_screen, mono = Presentation::mono_theatre;
    assert(choose({dialogue, 0, sources}, 0, false, keep, screen) == keep);
    assert(choose({uint8_t(dialogue | picture), 0, sources}, 0, false, keep, screen) == screen);
    assert(choose({dialogue, 0, sources}, 0, true, keep, screen) == screen);
    assert(choose({uint8_t(dialogue | picture), 0, sources}, 0, false, screen, keep) == keep); // pictures in full VR
    assert(choose({uint8_t(movie | picture), 0, sources}, 0, true, screen, screen) == mono);
    assert(choose({0, movie, sources}, movie, true, keep, screen) == mono); // a paused video it saw playing
    assert(choose({0, movie, sources}, 0, false, keep, screen) == keep);
    // Story scenes in full VR: a picture enters the screen, and its end hands the view back.
    Lease pictures; const auto pg = pictures.start();
    assert(pictures.sample(pg, "400", dialogue | picture, 0, sources, screen, 7000));
    assert(pictures.sample(pg, "400", dialogue | picture, 0, sources, screen, 7100));
    assert(pictures.presentation(7100) == screen);
    assert(pictures.sample(pg, "400", dialogue, 0, sources, keep, 8400));
    assert(!pictures.active(8400));
    assert(!pictures.sample(pg, "400", plot, 0, plot, screen, 8500)); // the plot bit never reaches a lease
    // lease-v4: the character screen reaches its own lease only, as the dialogue bit.
    auto w = showroom_bits({uint8_t(showroom | dialogue), 0, uint8_t(showroom | dialogue | movie)});
    assert(w.active == dialogue && w.hold == 0 && w.known == dialogue);
    w = showroom_bits({dialogue, 0, uint8_t(dialogue | movie)}); // not known: no observation
    assert(w.active == 0 && w.known == 0);
    const auto sc = cinema_bits({showroom, 0, showroom});
    assert(sc.active == 0 && sc.known == 0);
    s = story_bits({showroom, 0, uint8_t(showroom | plot)});
    assert(s.active == 0 && s.known == dialogue);
    assert(!pictures.sample(pg, "400", showroom, 0, showroom, screen, 8600)); // nor does the showroom bit itself
    assert(resolve(true, false, Presentation::none).screen);
    assert(resolve(false, true, Presentation::none).mono);
    std::cout << "PASS automatic cinematic lease, expiry, pause, manual precedence, world and producer fencing\n";
}
