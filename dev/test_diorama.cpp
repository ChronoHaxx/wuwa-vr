#include "../mod/uevr/src/utility/WuWaDiorama.hpp"

#include <cassert>
#include <iostream>
#include <thread>

using wuwa_diorama::Scale;

int main() {
    for (float normal : {0.01f, 0.875f, 1.0f, 2.5f, 10.0f}) {
        Scale scale;
        assert(!scale.requested() && scale.effective(normal) == normal);
        // Off -> on in between eyes must not split their effective IPD.
        {
            Scale::Draw draw{scale, normal};
            assert(scale.effective(normal) == normal);
            std::thread ui([&] { scale.request(true); });
            ui.join();
            assert(scale.requested() && scale.effective(normal) == normal);
            Scale::Draw capture{scale, 9.0f};
            assert(scale.effective(9.0f) == normal);
        }
        {
            Scale::Draw draw{scale, normal};
            assert(scale.effective(normal) == 10.0f);
            scale.request(false); // also models config/runtime reset
            assert(scale.effective(normal) == 10.0f);
            {
                Scale::Draw nested{scale, normal};
                assert(scale.effective(normal) == 10.0f);
            }
            assert(scale.effective(normal) == 10.0f);
        }
        {
            Scale::Draw draw{scale, normal};
            assert(!scale.requested() && scale.effective(normal) == normal);
        }
        // Saving or editing the normal value deliberately during diorama must
        // not be overwritten by an old snapshot when the override is removed.
        scale.request(true);
        { Scale::Draw draw{scale, normal}; assert(scale.effective(normal) == 10.0f); }
        const float edited = 1.75f;
        scale.request(false);
        { Scale::Draw draw{scale, edited}; assert(scale.effective(edited) == edited); }
        assert(scale.effective(edited) == edited);
    }
    Scale scale;
    scale.request(true);
    {
        Scale::Draw outer{scale, 0.875f};
        try { Scale::Draw nested{scale, 3.0f}; throw 1; } catch (int) {}
        assert(scale.effective(0.875f) == 10.0f);
    }
    scale.request(false);
    { Scale::Draw next{scale, 0.875f}; assert(scale.effective(0.875f) == 0.875f); }
    Scale restarted;
    assert(!restarted.requested() && restarted.effective(0.875f) == 0.875f);
    std::cout << "PASS: five normal scales, mid-pair toggles, nested captures, reset, manual edit, exception unwind, restart\n";
}
