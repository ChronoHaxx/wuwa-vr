#include "../mod/uevr/src/utility/WuWaScreenComposite.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using Pixel = std::array<float, 4>;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

float unorm8(float value) {
    return std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f) / 255.0f;
}

// Models the pinned DirectXTK11 AlphaBlend / DirectXTK12 SpriteBatch default:
// ONE source, INV_SRC_ALPHA destination, ADD, for both RGB and alpha.
// Quantize after each draw, like the actual B8G8R8A8_UNORM intermediates.
Pixel over(const Pixel& source, const Pixel& destination) {
    Pixel output{};
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = unorm8(source[i] + destination[i] * (1.0f - source[3]));
    }
    return output;
}

float srgb_decode(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f
        : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}
}

int main() {
    try {
        const Pixel old_clear{0.0f, 0.0f, 0.0f, 0.0f};
        const auto opaque = wuwa_screen_composite::opaque_black;
        const std::array<Pixel, 5> colors{{
            {0.0f, 0.0f, 0.0f, 0.0f}, {0.02f, 0.05f, 0.1f, 0.0f},
            {0.25f, 0.5f, 0.75f, 0.0f}, {1.0f, 0.1f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f, 0.0f}
        }};
        std::size_t cases{};
        for (auto scene : colors) {
            // Scene alpha is not coverage: exercise all possible 8-bit values,
            // including nonzero color with zero alpha from opaque game buffers.
            for (unsigned a = 0; a != 256; ++a) {
                scene[3] = static_cast<float>(a) / 255.0f;
                const auto old_scene = over(scene, old_clear);
                const auto new_scene = over(scene, opaque);
                require(new_scene[3] == 1.0f, "scene panel alpha must remain opaque");
                for (unsigned h = 0; h != 256; ++h) {
                    const float alpha = static_cast<float>(h) / 255.0f;
                    const Pixel hud{alpha * 0.8f, alpha * 0.4f, alpha * 0.2f, alpha};
                    const auto old_composite = over(hud, old_scene);
                    const auto new_composite = over(hud, new_scene);
                    require(new_composite[3] == 1.0f, "HUD must not make scene panel translucent");
                    for (std::size_t channel = 0; channel != 3; ++channel) {
                        require(old_composite[channel] == new_composite[channel], "encoded RGB changed");
                        require(srgb_decode(old_composite[channel]) == srgb_decode(new_composite[channel]),
                            "runtime-decoded RGB changed");
                    }
                    ++cases;
                }
            }
        }
        const Pixel game{0.5f, 0.25f, 0.1f, 0.0f};
        require(over(game, old_clear)[3] == 0.0f, "old transparent-scene reproducer changed");
        require(over(game, opaque)[3] == 1.0f, "opaque scene correction failed");
        require(over(Pixel{0, 0, 0, 0}, old_clear)[3] == 0.0f,
            "separate transparent HUD model must remain transparent");
        std::cout << "2D screen composite: " << cases
            << " scene/HUD alpha combinations preserve RGB and opaque coverage\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
