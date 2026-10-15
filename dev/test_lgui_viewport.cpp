// The LGUI view-rect rewrite: 16:9 gives every reader the window size; the taller layout gives it
// only to the input conversions, the mouse ray (test 17l) and the controller's UI point -> pixel
// conversions (test 17m), so mouse and controller presses meet the ray in one size. Offline; no game.
#include "../mod/uevr/src/utility/WuWaLguiViewport.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace wuwa_lgui_viewport;

struct Frame {
    uint8_t bytes[0x100]{};
    int32_t* rect(uint32_t offset) { return reinterpret_cast<int32_t*>(bytes + offset); }
    void eye(uint32_t offset = 0xd0) { auto* r = rect(offset); r[0] = 0; r[1] = 0; r[2] = 2064; r[3] = 2208; }
    uintptr_t at() { return reinterpret_cast<uintptr_t>(bytes); }
};

static bool is(Frame& f, int32_t w, int32_t h, uint32_t offset = 0xd0) {
    auto* r = f.rect(offset); return r[0] == 0 && r[1] == 0 && r[2] == w && r[3] == h;
}

int main() {
    auto& s = state();
    Frame f;
    assert(s.sites.load() == 0x7f);

    // 16:9: the scaler, the mouse ray and the point conversions all take the window size.
    update(true, false, 1920, 1080);
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    f.eye(0x30); rewrite_input(f.at(), 0x30, 4); assert(is(f, 1920, 1080, 0x30));
    assert(s.pointer_size.load() == 0 && s.rewrites.load() == 4 && s.pointer_rewrites.load() == 0 && s.input_rewrites.load() == 0);

    // Taller layout: the layout readers keep the eye rect; the mouse ray and both point conversions
    // (a controller press's button position) get the window size.
    update(false, true, 1920, 1080);
    assert(!active());
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(); rewrite(f.at(), 0xd0, 2); assert(is(f, 2064, 2208));
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    f.eye(0x30); rewrite_input(f.at(), 0x30, 4); assert(is(f, 1920, 1080, 0x30));
    assert(s.pointer_rewrites.load() == 1 && s.input_rewrites.load() == 2);

    // Bit 5 off: the mouse ray keeps the eye rect; bit 6 off: the point conversions keep it (17j).
    s.sites.store(0x5f);
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    s.sites.store(0x3f);
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    s.sites.store(0x7f);

    // In 16:9 each input conversion follows its own site bit like any reader.
    update(true, true, 1920, 1080);
    assert(s.pointer_size.load() == 0);
    s.sites.store(0x7d);
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    s.sites.store(0x7f);

    // Stereo screen or mono theatre: nothing is rewritten.
    update(false, false, 1920, 1080);
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));

    // No projection data (empty rect): the game's fallback stays, in either layout.
    update(false, true, 1920, 1080);
    { auto* r = f.rect(0xd0); r[0] = r[1] = r[2] = r[3] = 0; }
    rewrite_input(f.at(), 0xd0, 1); assert(is(f, 0, 0));

    // An insane window size disables both.
    update(true, true, 16, 9);
    assert(s.size.load() == 0 && s.pointer_size.load() == 0);
    update(false, true, 40000, 1080);
    assert(s.pointer_size.load() == 0);

    std::cout << "lgui viewport: 7 groups passed\n";
    return 0;
}
