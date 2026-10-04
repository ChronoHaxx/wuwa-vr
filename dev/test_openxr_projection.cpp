#include "../mod/uevr/src/utility/OpenXRProjectionPolicy.hpp"

#include <cassert>
#include <iostream>
#include <limits>

using openxr_projection::Cache;
using openxr_projection::Inputs;

int main() {
    const Inputs raw{0, 0, false, .1f,
        {{{-.8f, .6f, .7f, -.5f}, {-.6f, .8f, .7f, -.5f}}}};
    Cache cache;
    assert(cache.needs_update(raw)); // Initial raw/default matrices still build.
    cache.commit(raw);
    assert(!cache.needs_update(raw));

    // API/config changes have no UI dirty flag. Each requested input must
    // invalidate on its own, and returning to raw must also regenerate.
    unsigned changes = 0;
    auto changed = [&](Inputs input) {
        assert(cache.needs_update(input));
        assert(cache.needs_update(input)); // Merely checking cannot consume it.
        cache.commit(input);
        assert(!cache.needs_update(input));
        assert(cache.needs_update(raw));
        cache.commit(raw);
        assert(!cache.needs_update(raw));
        ++changes;
    };
    for (int mode : {1, 2}) {
        auto input = raw; input.horizontal = mode; changed(input);
        input = raw; input.vertical = mode; changed(input);
    }
    auto input = raw; input.grow = true; changed(input);
    input = raw; input.near_z = .2f; changed(input);
    for (size_t eye = 0; eye < 2; ++eye) for (size_t side = 0; side < 4; ++side) {
        input = raw; input.fov[eye][side] += .01f; changed(input);
    }
    assert(changes == 14);

    // The caller freezes one local input for the whole pair. New requested
    // values cannot mutate that value or the last completed cache entry.
    auto requested = raw;
    requested.horizontal = 1;
    const auto pair = requested;
    assert(cache.needs_update(pair));
    requested.horizontal = 2;
    requested.fov[1][0] -= .02f;
    cache.commit(pair);
    assert(!cache.needs_update(pair));
    assert(cache.needs_update(requested));
    assert(pair.horizontal == 1 && pair.fov == raw.fov);

    // A failed/interrupted calculation leaves the last completed pair active;
    // the same pending request is retried, rather than mistaken for cached.
    assert(cache.needs_update(requested));
    assert(!cache.needs_update(pair));
    cache.commit(requested);
    assert(!cache.needs_update(requested));
    cache.reset();
    assert(cache.needs_update(requested));

    // NaN runtime data must not compare equal and hide later refreshes.
    // This cache does not add FOV validation or alter legacy projection math.
    input = raw; input.fov[0][0] = std::numeric_limits<float>::quiet_NaN();
    cache.commit(input);
    assert(cache.needs_update(input));
    assert(cache.needs_update(raw));
    cache.commit(raw);
    assert(!cache.needs_update(raw));
    std::cout << "OpenXR projection cache: 14 input changes, snapshot/retry/reset checks passed\n";
}
