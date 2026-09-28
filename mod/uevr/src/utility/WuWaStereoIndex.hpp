#pragma once

namespace wuwa_stereo {

// These are CalculateStereoProjectionMatrix callback arguments, not the
// game's FSceneView stereo enum (2/3). Keep UEVR's existing detection until
// callback 2 establishes the older 1/2 convention. A later full-view 0 must
// then leave WuWa's eye mapping consistent with CalculateStereoViewOffset.
struct EyeIndexConvention {
    bool starts_from_one{true};
    bool saw_two{};

    constexpr void observe(int index, bool preserve_after_two) {
        if (index == 2) {
            starts_from_one = true;
            saw_two = true;
        } else if (index == 0 && !(preserve_after_two && saw_two)) {
            starts_from_one = false;
        }
    }

    constexpr int eye(int index) const {
        // Preserve the old full-view call's output while retaining the
        // convention for the following stereo calls.
        if (index == 0) return 0;
        return starts_from_one ? ((index + 1) % 2) : (index % 2);
    }

    constexpr bool is_full_view(int index, bool preserve_after_two) const {
        return preserve_after_two && saw_two && index == 0;
    }
};

} // namespace wuwa_stereo
