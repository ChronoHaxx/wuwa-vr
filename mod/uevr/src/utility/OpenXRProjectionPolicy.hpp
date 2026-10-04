#pragma once

#include <array>
#include <optional>

// Cache identity only: no projection math, settings, clocks or runtime APIs.
// Sample once at the existing matrix-update boundary and derive both eyes
// from that same value. Commit only after both matrices/bounds are published.
// The owner serializes access; this does not make legacy ModValue setters atomic.
namespace openxr_projection {
using Fov = std::array<float, 4>; // angleLeft, angleRight, angleUp, angleDown
struct Inputs {
    int horizontal{}, vertical{};
    bool grow{};
    float near_z{};
    std::array<Fov, 2> fov{};
    bool operator==(const Inputs& other) const noexcept {
        return horizontal == other.horizontal && vertical == other.vertical &&
            grow == other.grow && near_z == other.near_z && fov == other.fov;
    }
};

class Cache {
public:
    bool needs_update(const Inputs& input) const noexcept {
        return !last || !(*last == input);
    }
    void commit(const Inputs& input) noexcept { last = input; }
    void reset() noexcept { last.reset(); }
private:
    std::optional<Inputs> last;
};
} // namespace openxr_projection
