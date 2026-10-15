// Keeping the mouse in the game window: clip while wanted and in front, win the clip back when
// something resets it, and release only a clip that is still ours. Offline; no system calls.
#include "../mod/uevr/src/utility/WuWaCursorClip.hpp"
#include <cassert>
#include <iostream>
using namespace wuwa_cursor_clip;

int main() {
    const RECT window{0, 0, 2560, 1440}, moved{100, 50, 2660, 1490}, desktop{-1080, -392, 4720, 2096}, empty{};
    State s;

    // Not wanted and never clipped: nothing to do, whatever the system clip is.
    assert(decide(s, false, window, desktop) == Action::none && !s.ours);

    // Wanted: clip to the window once; then it stays.
    assert(decide(s, true, window, desktop) == Action::clip && s.ours && same(s.clipped, window));
    assert(decide(s, true, window, window) == Action::none && s.retaken == 0);

    // The game or another program reset it while in front: clipped again and counted.
    assert(decide(s, true, window, desktop) == Action::clip && s.retaken == 1);
    // The window moved: clipped to the new place, not counted as a reset.
    assert(decide(s, true, moved, window) == Action::clip && same(s.clipped, moved) && s.retaken == 1);

    // Focus left (or the setting went off): released, once.
    assert(decide(s, false, empty, moved) == Action::release && !s.ours);
    assert(decide(s, false, empty, desktop) == Action::none);

    // A clip already set by the game (its gameplay lock) is left as it is, and not taken as ours...
    State g;
    assert(decide(g, true, window, window) == Action::none && !g.ours);
    // ...so losing focus does not release it.
    assert(decide(g, false, empty, window) == Action::none);

    // Another program clipped somewhere else after ours: not released, and no longer ours.
    State o;
    assert(decide(o, true, window, desktop) == Action::clip);
    const RECT other{3000, 0, 3500, 500};
    assert(decide(o, false, empty, other) == Action::none && !o.ours);
    assert(decide(o, false, empty, window) == Action::none);

    // Minimised or unknown window: not clipped, and ours is released.
    State m;
    assert(decide(m, true, empty, desktop) == Action::none && !m.ours);
    assert(decide(m, true, window, desktop) == Action::clip);
    assert(decide(m, true, empty, window) == Action::release && !m.ours);

    std::cout << "cursor clip: 6 groups passed\n";
    return 0;
}
