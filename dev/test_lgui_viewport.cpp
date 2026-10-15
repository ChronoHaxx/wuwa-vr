// The LGUI view-rect rewrite: 16:9 gives every reader the window size; the taller layout keeps the
// canvas (eye) rect for everything the game computes, the controller's UI point -> pixel conversions
// and the script's stick cursor, and gives the pointer ray the window size for the mouse alone
// (pointer 0 in pointer mode, test 17o). Bits 5 and 6 still give the taller layout 17m's
// window-sized ray and point conversions, for diagnostics. Offline; no game.
#include "../mod/uevr/src/utility/WuWaLguiViewport.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
using namespace wuwa_lgui_viewport;

struct Frame {
    uint8_t bytes[0x100]{};
    int32_t* rect(uint32_t offset) { return reinterpret_cast<int32_t*>(bytes + offset); }
    void eye(uint32_t offset = 0xd0) { auto* r = rect(offset); r[0] = 0; r[1] = 0; r[2] = 2064; r[3] = 2208; }
    uintptr_t at() { return reinterpret_cast<uintptr_t>(bytes); }
};

// A fake LGUIPointerEventData with the two fields the ray reads.
struct Event {
    uint8_t bytes[0x50]{};
    Event(uint8_t input_type, int32_t pointer_id) {
        bytes[event_input_type] = input_type;
        std::memcpy(bytes + event_pointer_id, &pointer_id, sizeof(pointer_id));
    }
    const uint8_t* at() const { return bytes; }
};

static bool is(Frame& f, int32_t w, int32_t h, uint32_t offset = 0xd0) {
    auto* r = f.rect(offset); return r[0] == 0 && r[1] == 0 && r[2] == w && r[3] == h;
}

int main() {
    auto& s = state();
    Frame f;
    assert(s.sites.load() == 0x9f);
    const Event mouse{0, 0}, stick{1, 0}, press{0, 999}, stick_press{1, 999};
    assert(is_mouse(mouse.at()) && !is_mouse(stick.at()) && !is_mouse(press.at()) && !is_mouse(stick_press.at()));
    assert(!is_mouse(nullptr));

    // 16:9: the scaler, the ray (every pointer) and the point conversions all take the window size.
    update(true, false, 1920, 1080);
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 1920, 1080));
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 1920, 1080));
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, stick.at()); assert(is(f, 1920, 1080));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    f.eye(0x30); rewrite_input(f.at(), 0x30, 4); assert(is(f, 1920, 1080, 0x30));
    assert(s.pointer_size.load() == 0 && s.rewrites.load() == 5 && s.pointer_rewrites.load() == 0 &&
           s.input_rewrites.load() == 0 && s.mouse_rewrites.load() == 0);

    // Taller layout: every reader keeps the eye rect, the point conversions too...
    update(false, true, 1920, 1080);
    assert(!active());
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));
    f.eye(); rewrite(f.at(), 0xd0, 2); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    f.eye(0x30); rewrite_input(f.at(), 0x30, 4); assert(is(f, 2064, 2208, 0x30));
    // ...and so does the ray for the stick cursor and the controller presses (canvas pixels)...
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, stick.at()); assert(is(f, 2064, 2208));
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, press.at()); assert(is(f, 2064, 2208));
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, stick_press.at()); assert(is(f, 2064, 2208));
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, nullptr); assert(is(f, 2064, 2208));
    assert(s.pointer_rewrites.load() == 0 && s.input_rewrites.load() == 0 && s.mouse_rewrites.load() == 0);
    // ...while the mouse's ray (window pixels) takes the window size.
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 1920, 1080));
    assert(s.mouse_rewrites.load() == 1 && s.pointer_rewrites.load() == 0);
    // Bit 7 off: the mouse's ray keeps the eye rect too (17k).
    s.sites.store(0x1f);
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 2064, 2208));
    assert(s.mouse_rewrites.load() == 1);

    // Bits 5 and 6 (17m): the ray for every pointer and the point conversions take the window size.
    s.sites.store(0x3f);
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, stick.at()); assert(is(f, 1920, 1080));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    s.sites.store(0x5f);
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, press.at()); assert(is(f, 2064, 2208));
    assert(s.pointer_rewrites.load() == 1 && s.input_rewrites.load() == 1 && s.mouse_rewrites.load() == 1);
    s.sites.store(0x9f);

    // In 16:9 each input conversion follows its own site bit like any reader.
    update(true, true, 1920, 1080);
    assert(s.pointer_size.load() == 0);
    s.sites.store(0x9d);
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 1920, 1080, 0x40));
    s.sites.store(0x9f);

    // Stereo screen or mono theatre: nothing is rewritten.
    update(false, false, 1920, 1080);
    f.eye(); rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 2064, 2208));
    f.eye(0x40); rewrite_input(f.at(), 0x40, 3); assert(is(f, 2064, 2208, 0x40));
    f.eye(); rewrite(f.at(), 0xd0, 1); assert(is(f, 2064, 2208));

    // No projection data (empty rect): the game's fallback stays, in either layout.
    update(true, false, 1920, 1080);
    { auto* r = f.rect(0xd0); r[0] = r[1] = r[2] = r[3] = 0; }
    rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 0, 0));
    update(false, true, 1920, 1080);
    rewrite_pointer_ray(f.at(), 0xd0, mouse.at()); assert(is(f, 0, 0));
    // The rect size is recorded for the log; an empty rect is not.
    f.eye(); record(f.rect(0xd0)); assert(s.eye_size.load() == (uint64_t{2064} << 32 | 2208));
    { int32_t r[4]{}; record(r); assert(s.eye_size.load() == (uint64_t{2064} << 32 | 2208)); }

    // An insane window size disables both.
    update(true, true, 16, 9);
    assert(s.size.load() == 0 && s.pointer_size.load() == 0);
    update(false, true, 40000, 1080);
    assert(s.pointer_size.load() == 0);

    std::cout << "lgui viewport: 8 groups passed\n";
    return 0;
}
