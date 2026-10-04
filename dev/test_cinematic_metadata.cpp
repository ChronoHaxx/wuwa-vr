#include "../mod/uevr/src/utility/WuWaCinematicMetadata.hpp"
#include <cassert>
#include <limits>

int main() {
    using namespace wuwa_cinematic_metadata;
    const Aspect full{0, 0}, cinema{1, 2.39f}, retained{0, 1.7777778f};
    assert(shared(cinema, full, true) == cinema);
    assert(shared(full, cinema, true) == full); // constrained -> unrestricted shot
    assert(shared(retained, cinema, true) == retained); // no invented aspect when disabled
    assert(shared(cinema, cinema, true) == cinema);
    assert(!shared(cinema, full, false)); // unrelated family cannot consume the primary
    assert(!shared({1, 0}, full, true));
    assert(!shared({2, 1.77f}, full, true));
    assert(!shared({0, -1}, full, true));
    assert(!shared({1, std::numeric_limits<float>::quiet_NaN()}, full, true));
    assert(!shared({1, std::numeric_limits<float>::infinity()}, full, true));
    assert(!shared(cinema, {1, 0}, true)); // invalid destination is not silently overwritten
    assert(!shared(cinema, {0, 101}, true));
}
