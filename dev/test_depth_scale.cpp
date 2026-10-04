#include "../mod/uevr/src/utility/WuWaDepthScale.hpp"

#include <array>
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>

using wuwa_depth_scale::Draw;
using wuwa_depth_scale::Snapshot;

int main() {
    assert(!Draw::snapshot().required);
    Draw::record(100.0f); // Outside our draw must not invent a sample.
    assert(!Draw::snapshot().valid());

    std::array<Snapshot, 2> queued{};
    {
        Draw draw{true};
        assert(Draw::snapshot().required && !Draw::snapshot().valid());
        Draw::record(100.0f);
        Draw::record(100.0f);
        queued[0] = Draw::snapshot();
        assert(queued[0].valid() && 10.0f / queued[0].world_units_per_metre == 0.1f);
        {
            Draw capture{true};
            assert(!Draw::snapshot().valid());
            Draw::record(250.0f);
            assert(Draw::snapshot().world_units_per_metre == 250.0f);
        }
        assert(Draw::snapshot().world_units_per_metre == 100.0f);
    }
    {
        Draw draw{true};
        Draw::record(1000.0f); // A newer draw enables diorama.
        queued[1] = Draw::snapshot();
    }
    // Submitting the old image after the toggle must retain its old depth.
    assert(queued[0].world_units_per_metre == 100.0f);
    assert(queued[1].world_units_per_metre == 1000.0f);
    auto nsf_clone = queued[1];
    queued[1] = {};
    assert(nsf_clone.required && nsf_clone.world_units_per_metre == 1000.0f);
    assert(nsf_clone.for_submission(42, 42, true).valid());
    const auto aliased = nsf_clone.for_submission(42, 45, true);
    assert(aliased.required && !aliased.valid()); // Same ring slot, wrong image.
    const auto missing = Snapshot{}.for_submission(42, 42, true);
    assert(missing.required && !missing.valid()); // Missing hook cannot select legacy.
    assert(!Snapshot{}.for_submission(42, 45, false).required); // Unrelated path unchanged.

    for (const auto bad : {0.0f, -1.0f, std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::quiet_NaN()}) {
        Draw draw{true};
        Draw::record(bad);
        Draw::record(100.0f); // A later good sample cannot erase the failure.
        assert(Draw::snapshot().required && !Draw::snapshot().valid());
    }
    {
        Draw draw{true};
        Draw::record(100.0f);
        Draw::record(1000.0f); // Split-scale eyes must not get guessed depth.
        Draw::record(1000.0f);
        assert(!Draw::snapshot().valid());
    }
    {
        Draw unrelated{false};
        Draw::record(1000.0f);
        assert(!Draw::snapshot().required); // Legacy game/AFR path preserved.
    }
    {
        Draw draw{true};
        Draw::record(100.0f);
        std::thread worker([] {
            assert(!Draw::snapshot().required); // A render thread cannot read GT TLS.
            Draw child{true};
            Draw::record(1000.0f);
            assert(Draw::snapshot().world_units_per_metre == 1000.0f);
        });
        worker.join();
        assert(Draw::snapshot().world_units_per_metre == 100.0f);
        try {
            Draw nested{true};
            Draw::record(1000.0f);
            throw 1;
        } catch (int) {}
        assert(Draw::snapshot().world_units_per_metre == 100.0f);
    }
    assert(!Draw::snapshot().required);
    std::cout << "depth scale metadata tests passed\n";
}
