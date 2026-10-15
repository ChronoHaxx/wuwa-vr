// The LGUI view-rect rewrite: 16:9 gives every caller the window size; the taller layout gives it
// only to the mouse ray (test 17l). Offline; no game.
#include "../mod/uevr/src/utility/WuWaLguiViewport.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace wuwa_lgui_viewport;

struct Frame {
    uint8_t bytes[0x100]{};
    int32_t* rect() { return reinterpret_cast<int32_t*>(bytes + 0xd0); }
    void eye() { auto* r = rect(); r[0] = 0; r[1] = 0; r[2] = 2064; r[3] = 2208; }
    uintptr_t at() { return reinterpret_cast<uintptr_t>(bytes); }
};

static bool is(Frame& f, int32_t w, int32_t h) { auto* r = f.rect(); return r[0] == 0 && r[1] == 0 && r[2] == w && r[3] == h; }

int main() {
    auto& s = state();
    Frame f;

    // 16:9: the scaler and the mouse ray both take the window size.
    update(true, false, 1920, 1080);
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(); rewrite_pointer(f.at(), 0xd0); assert(is(f, 1920, 1080));
    assert(s.pointer_size.load() == 0 && s.rewrites.load() == 2 && s.pointer_rewrites.load() == 0);

    // Taller layout: the scaler keeps the eye rect, the mouse ray gets the window size.
    update(false, true, 1920, 1080);
    assert(!active());
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(); rewrite_pointer(f.at(), 0xd0); assert(is(f, 1920, 1080));
    assert(s.pointer_rewrites.load() == 1);

    // Bit 5 off (diagnostics): the mouse ray keeps the eye rect too.
    s.sites.store(0x1f);
    f.eye(); rewrite_pointer(f.at(), 0xd0); assert(is(f, 2064, 2208));
    s.sites.store(0x3f);

    // In 16:9 the mouse ray follows site 1's bit like any caller.
    update(true, true, 1920, 1080);
    assert(s.pointer_size.load() == 0);
    s.sites.store(0x3d);
    f.eye(); rewrite_pointer(f.at(), 0xd0); assert(is(f, 2064, 2208));
    s.sites.store(0x3f);

    // Stereo screen or mono theatre: nothing is rewritten.
    update(false, false, 1920, 1080);
    f.eye(); rewrite_pointer(f.at(), 0xd0); assert(is(f, 2064, 2208));
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));

    // No projection data (empty rect): the game's fallback stays, in either layout.
    update(false, true, 1920, 1080);
    { auto* r = f.rect(); r[0] = r[1] = r[2] = r[3] = 0; }
    rewrite_pointer(f.at(), 0xd0); assert(is(f, 0, 0));

    // An insane window size disables both.
    update(true, true, 16, 9);
    assert(s.size.load() == 0 && s.pointer_size.load() == 0);
    update(false, true, 40000, 1080);
    assert(s.pointer_size.load() == 0);

    std::cout << "lgui viewport: 7 groups passed\n";
    return 0;
}
