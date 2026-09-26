#pragma once
#include <optional>

namespace wuwa_stereo {
// These are existing UEVR options. The comparison changes no camera, HUD,
// resolution, controller, engine CVar, or Windows runtime setting.
struct Settings {
    int method{};
    int synchronized_method{};
    bool native_fix{};
    bool extreme_compatibility{};
    bool operator==(const Settings&) const = default;
};

class Comparison {
public:
    enum class Choice { native, separate_eyes };

    Settings select(Settings current, Choice choice) {
        if (!saved) saved = current;
        current.method = choice == Choice::native ? 0 : 1;
        // Skip Draw reuses one game tick for the eye pair. Native Stereo Fix
        // and Extreme Compatibility would obscure which renderer is selected.
        if (choice == Choice::separate_eyes) current.synchronized_method = 1;
        current.native_fix = false;
        current.extreme_compatibility = false;
        return current;
    }

    bool pending() const { return saved.has_value(); }
    bool can_restore(bool additional_menus) const {
        return saved && !(additional_menus && saved->native_fix);
    }
    std::optional<Settings> restore(bool additional_menus) {
        // Do not reintroduce the combination rejected by VR::on_frame.
        if (!can_restore(additional_menus)) return std::nullopt;
        const auto result = saved;
        saved.reset();
        return result;
    }

private:
    std::optional<Settings> saved;
};
} // namespace wuwa_stereo
