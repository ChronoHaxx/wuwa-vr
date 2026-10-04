#include "../mod/uevr/src/utility/WuWaScreenEyeOrder.hpp"

#include <array>
#include <cassert>
#include <iostream>

using wuwa_screen_eye_order::openxr_sources;

static_assert(openxr_sources(true, true, false, true)[0] == 1);
static_assert(openxr_sources(true, true, false, true)[1] == 0);
static_assert(openxr_sources(true, false, false, true)[0] == 0);
static_assert(openxr_sources(false, true, false, true)[0] == 0);
static_assert(openxr_sources(true, true, true, true)[0] == 0);
static_assert(openxr_sources(true, true, false, false)[0] == 0);

int main() {
    // Distinct source pixels model the existing NSF double-wide compositor.
    // Screen mode must preserve its eye assignment, with HUD pixels duplicated
    // in both eyes. Applying SwapEyes again to the completed pair would fail.
    constexpr std::array<std::array<int, 3>, 2> screens{{{11, 12, 99}, {21, 22, 99}}};
    for (bool swap : {false, true}) {
        const auto order = openxr_sources(true, swap, false, true);
        std::array<std::array<int, 3>, 2> normal{};
        normal[swap ? 1 : 0] = screens[0];
        normal[swap ? 0 : 1] = screens[1];
        assert(screens[order[0]] == normal[0]);
        assert(screens[order[1]] == normal[1]);
        assert(screens[order[0]][2] == 99 && screens[order[1]][2] == 99);
    }

    // Independent gates: an unrelated saved SwapEyes setting must not change
    // ordinary stereo, an unavailable-capture fallback, or AFR/duplicate AFR.
    for (bool nsf : {false, true}) {
        for (bool swap : {false, true}) {
            for (bool afr : {false, true}) {
                for (bool capture : {false, true}) {
                    const auto order = openxr_sources(nsf, swap, afr, capture);
                    assert(order[0] < 2 && order[1] < 2 && order[0] != order[1]);
                    if (!nsf || !swap || afr || !capture) {
                        assert(order[0] == 0 && order[1] == 1);
                    }
                }
            }
        }
    }
    std::cout << "2D screen eye order: compositor parity and fallback gates passed\n";
}
