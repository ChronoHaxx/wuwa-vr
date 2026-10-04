#include "../mod/uevr/src/utility/WuWaCopyBounds.hpp"
#include <cassert>
#include <iostream>

using namespace wuwa_copy_bounds;
int main() {
    const Texture old_world{3, 5120, 2560, 1, 1, 1, 0, 87};
    const Texture new_world{3, 5120, 1440, 1, 1, 1, 0, 91};
    const Texture old_eye{3, 2560, 2560, 1, 1, 1, 0, 87};
    const Box old_left{0, 0, 0, 2560, 2560, 1};
    const Box new_left{0, 0, 0, 2560, 1440, 1};
    // Actual failure: old 2560-high sources into new 1440-high XR target.
    assert(!region(old_world, new_world, old_left));
    assert(!region(old_eye, new_world, old_left, {2560, 0, 0}));
    assert(!whole(old_world, new_world));
    assert(!whole(new_world, old_world)); // grow isn't a whole-copy match either
    assert(region(new_world, new_world, new_left));
    assert(region(new_world, new_world, new_left, {2560, 0, 0}));
    assert(!region(new_world, new_world, new_left, {2561, 0, 0}));
    assert(!region(new_world, new_world, new_left, {UINT32_MAX, 0, 0}));
    assert(!region(new_world, new_world, {0, 0, 0, 5121, 1440, 1}));
    assert(!region(new_world, new_world, {3, 0, 0, 2, 1440, 1}));
    assert(!region(new_world, new_world, {0, 0, 0, 0, 1440, 1}));
    assert(!region(new_world, new_world, {0, 0, 0, 2560, 1440, 2}));
    assert(!region(new_world, new_world, new_left, {0, 1, 0}));
    assert(!region(new_world, new_world, new_left, {0, 0, 1}));

    auto color = new_world; color.format = 87;
    assert(whole(color, new_world)); // preserve UNORM -> sRGB encoded RGB copy
    color.format = 28; assert(!whole(color, new_world)); // RGBA vs BGRA
    color = new_world; color.mips = 2; assert(!whole(color, new_world));
    color = new_world; color.depth_or_array = 2; assert(!whole(color, new_world));
    color = new_world; color.samples = 4; assert(!whole(color, new_world));
    color = new_world; color.quality = 1; assert(!whole(color, new_world));
    color = new_world; color.dimension = 1; assert(!whole(color, color));
    color = new_world; color.format = 39;
    auto depth = color; depth.format = 40;
    assert(whole(color, depth)); // R32_TYPELESS -> D32_FLOAT
    assert(region(depth, depth, {0, 0, 0, 5120, 1440, 1}));
    auto msaa = new_world; msaa.samples = 4;
    assert(region(msaa, msaa, {0, 0, 0, 5120, 1440, 1}));
    assert(!region(msaa, msaa, new_left));
    assert(!fits(UINT64_MAX, 2, UINT64_MAX));
    assert(fits(UINT64_MAX - 2, 2, UINT64_MAX));
    std::cout << "Copy bounds and resize transition cases passed\n";
}
