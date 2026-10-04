#pragma once

#include <array>

namespace wuwa_screen_composite {

// A 2D scene panel is opaque, unlike the separate HUD layer. Both DirectXTK
// SpriteBatch paths use premultiplied ONE / INV_SRC_ALPHA blending for RGB
// and alpha. Starting at alpha 1 keeps every scene/HUD composite opaque:
// a + 1 * (1 - a) == 1, without changing RGB compared with transparent black.
// Keep this separate from the transparent clear for the projection and HUD.
inline constexpr std::array<float, 4> opaque_black{0.0f, 0.0f, 0.0f, 1.0f};

} // namespace wuwa_screen_composite
