#pragma once
#include <cmath>
#include <cstdint>
#include <optional>

namespace wuwa_cinematic_metadata {
// Independent native draw metadata, not an optical projection or eye pose.
struct Aspect {
    uint8_t constrained{};
    float ratio{};
    bool operator==(const Aspect&) const = default;
};
inline bool valid(Aspect value) noexcept {
    return value.constrained <= 1 && std::isfinite(value.ratio) && value.ratio >= 0 && value.ratio <= 100 &&
        (!value.constrained || value.ratio > 0.01f);
}
inline std::optional<Aspect> shared(Aspect primary, Aspect secondary, bool same_family_pair) noexcept {
    if (!same_family_pair || !valid(primary) || !valid(secondary)) return std::nullopt;
    // A false primary flag is an explicit decision too. Carry its ratio
    // unchanged so no stale constraint survives a shot or mode transition.
    return primary;
}
} // namespace wuwa_cinematic_metadata
