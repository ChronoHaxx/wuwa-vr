#include "../mod/uevr/src/utility/WuWaMonoNativeProjection.hpp"
#include <cassert>
#include <iostream>

using namespace wuwa_mono_native;
int main() {
    const Rect full{0, 0, 2560, 1440};
    auto same = fit(full, full, 2560, 1440);
    assert(same && same->full == full && same->constrained == full);
    // Original 16:9 camera in a square source: bars, not vertical stretch.
    auto square = fit(full, full, 1600, 1600);
    assert(square && (square->constrained == Rect{0, 350, 1600, 1250}));
    // An authored 2.56:1 crop remains cropped after resolution scaling.
    auto cinematic = fit(full, {0, 220, 2560, 1220}, 1280, 720);
    assert(cinematic && (cinematic->constrained == Rect{0, 110, 1280, 610}));
    // Split origin/asymmetric bars must retain their relative positions.
    auto offset = fit({100, 200, 1100, 700}, {200, 250, 1000, 650}, 2000, 1000);
    assert(offset && (offset->constrained == Rect{200, 100, 1800, 900}));
    assert(!fit({0, 0, 0, 1440}, full, 1600, 1600));
    assert(!fit(full, {0, 220, 2561, 1220}, 1280, 720));
    assert(!fit(full, {0, 220, 2560, 1220}, 0, 720));
    assert(!fit(full, full, UINT32_MAX, 720));
    assert(!fit(full, {0, 0, 1, 1}, 1, 1)); // rounded-away source
    // Width arithmetic must not overflow when the source spans signed range.
    auto extreme = fit({INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX},
        {INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX}, 100, 100);
    assert(extreme && (extreme->constrained == Rect{0, 0, 100, 100}));
    const Rect old_view{30, 40, 2030, 1040}, new_view{230, 140, 1930, 940};
    const auto transform = crop_transform(old_view, new_view);
    assert(transform);
    // Test asymmetric principal points and arbitrary clip W values: the crop
    // must preserve screen-space rays rather than recenter either eye.
    for (double principal : {-0.18, 0.0, 0.21}) {
        for (double w : {0.25, 1.0, 10.0}) {
            const double x = (0.3 + principal) * w, y = -0.3 * w;
            const double xp = transform->x_scale * x + transform->x_offset * w;
            const double yp = transform->y_scale * y + transform->y_offset * w;
            const double old_x = 30 + (x / w + 1) * 1000;
            const double old_y = 40 + (1 - y / w) * 500;
            const double new_x = 230 + (xp / w + 1) * 850;
            const double new_y = 140 + (1 - yp / w) * 400;
            assert(std::abs(old_x - new_x) < 1e-9 && std::abs(old_y - new_y) < 1e-9);
        }
    }
    const auto relative = map_relative({100, 100, 1100, 1100}, {100, 300, 1100, 900}, {0, 0, 2000, 1000});
    assert(relative && (*relative == Rect{0, 200, 2000, 800}));
    std::cout << "Native mono viewport fitting passed\n";
}
