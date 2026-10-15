// The LGUI view-rect rewrite: 16:9 gives every reader the window size; the taller layout keeps the
// canvas (eye) rect for everything the game computes, the ray, the controller's UI point -> pixel
// conversions and the script's stick cursor, and converts the mouse from window pixels into that
// rect where LGUI reads it (test 17n). Bits 5 and 6 still give the taller layout 17m's window-sized
// ray and point conversions, for diagnostics. Offline; no game.
#include "../mod/uevr/src/utility/WuWaLguiViewport.hpp"
#include <cassert>
#include <cmath>
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

static bool near_to(float a, float b) { return std::fabs(a - b) < 0.01f; }

int main() {
    auto& s = state();
    Frame f;
    assert(s.sites.load() == 0x9f);
    const auto lgui = mouse_returns[2], other = uintptr_t{0xd5824c}; // ProcessInput; the world-space ray

    // 16:9: the scaler, the ray and the point conversions all take the window size; the mouse is
    // already in window pixels.
    update(true, false, 1920, 1080);
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    f.eye(0x30); rewrite_input(f.at(), 0x30, 4); assert(is(f, 1920, 1080, 0x30));
    assert(s.pointer_size.load() == 0 && s.rewrites.load() == 4 && s.pointer_rewrites.load() == 0 && s.input_rewrites.load() == 0);
    f.eye(); record(f.rect(0xd0));
    { float m[2]{960, 540}; convert_mouse(m, lgui); assert(m[0] == 960 && m[1] == 540); }

    // Taller layout: every reader keeps the eye rect, input conversions too.
    update(false, true, 1920, 1080);
    assert(!active());
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(); rewrite(f.at(), 0xd0, 2); assert(is(f, 2064, 2208));
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    f.eye(0x30); rewrite_input(f.at(), 0x30, 4); assert(is(f, 2064, 2208, 0x30));
    assert(s.pointer_rewrites.load() == 0 && s.input_rewrites.load() == 0);

    // ...and the mouse, read by LGUI's ProcessInput, becomes the same 0..1 point in the eye rect:
    // the window centre is the rect centre, the corners its corners.
    { float m[2]{960, 540}; convert_mouse(m, lgui); assert(near_to(m[0], 1032) && near_to(m[1], 1104)); }
    { float m[2]{1920, 0}; convert_mouse(m, mouse_returns[0]); assert(near_to(m[0], 2064) && near_to(m[1], 0)); }
    { float m[2]{0, 1080}; convert_mouse(m, mouse_returns[1]); assert(near_to(m[0], 0) && near_to(m[1], 2208)); }
    assert(s.mouse_rewrites.load() == 3);
    // Other callers (the world-space mouse ray divides by the window size itself) keep window pixels.
    { float m[2]{960, 540}; convert_mouse(m, other); assert(m[0] == 960 && m[1] == 540); }
    // A rect offset inside the viewport moves the point with it, as the ray subtracts the minimum.
    { int32_t r[4]{100, 50, 2164, 2258}; record(r); float m[2]{960, 540}; convert_mouse(m, lgui);
      assert(near_to(m[0], 1132) && near_to(m[1], 1154)); f.eye(); record(f.rect(0xd0)); }
    // An empty rect is not recorded.
    { int32_t r[4]{}; record(r); assert(s.eye_size.load() == pack(2064, 2208)); }
    // Bit 7 off: the mouse stays in window pixels (17j/17k).
    s.sites.store(0x1f);
    { float m[2]{960, 540}; convert_mouse(m, lgui); assert(m[0] == 960 && m[1] == 540); }

    // Bits 5 and 6 (17m): the ray and the point conversions take the window size.
    s.sites.store(0x3f);
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    s.sites.store(0x5f);
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    assert(s.pointer_rewrites.load() == 1 && s.input_rewrites.load() == 1);
    s.sites.store(0x9f);

    // In 16:9 each input conversion follows its own site bit like any reader.
    update(true, true, 1920, 1080);
    assert(s.pointer_size.load() == 0);
    s.sites.store(0x9d);
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    s.sites.store(0x9f);

    // Stereo screen or mono theatre: nothing is rewritten or converted.
    update(false, false, 1920, 1080);
    f.eye(); rewrite_input(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    { float m[2]{960, 540}; convert_mouse(m, lgui); assert(m[0] == 960 && m[1] == 540); }

    // No projection data (empty rect): the game's fallback stays, in either layout.
    update(true, false, 1920, 1080);
    { auto* r = f.rect(0xd0); r[0] = r[1] = r[2] = r[3] = 0; }
    rewrite_input(f.at(), 0xd0, 1); assert(is(f, 0, 0));

    // An insane window size disables both.
    update(true, true, 16, 9);
    assert(s.size.load() == 0 && s.pointer_size.load() == 0);
    update(false, true, 40000, 1080);
    assert(s.pointer_size.load() == 0);

    std::cout << "lgui viewport: 8 groups passed\n";
    return 0;
}
