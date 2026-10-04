#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>

namespace wuwa_controls {
using Settings = std::map<std::string, std::string>;
// ModValue::get emits booleans, decimal floats or integral enum values. Check
// the corresponding conversion before applying any of the profile settings.
inline bool valid_value_like(const std::string& current, const std::string& text) {
    if (current=="true" || current=="false") return text=="true" || text=="false";
    try {
        size_t used{};
        if (current.find_first_of(".eE")!=std::string::npos) {
            const auto value=std::stof(text,&used);
            return used==text.size() && std::isfinite(value);
        }
        const auto value=std::stoll(text,&used);
        return used==text.size() && value>=(std::numeric_limits<int32_t>::min)() &&
            value<=(std::numeric_limits<int32_t>::max)();
    } catch (...) { return false; }
}
inline bool recoverable(std::string_view key) {
    if (key.starts_with("WuWaControls_")) {
        return key!="WuWaControls_Focused" && key!="WuWaControls_Clock" &&
            key!="WuWaControls_Recording" && key!="WuWaControls_AdjustMode" &&
            key!="WuWaControls_NativeMenu" && key!="WuWaControls_ResetHudAspect" &&
            key!="WuWaControls_HudAspectStatus";
    }
    if (key.starts_with("UI_")) return true;
    constexpr std::array keys{"VR_EnableGUI", "VR_CameraForwardOffset", "VR_CameraRightOffset",
        "VR_CameraUpOffset", "VR_WorldScale", "VR_DepthScale", "VR_AimMethod", "VR_AimInterp",
        "VR_AimSpeed", "VR_AimModifyPlayerControlRotation", "VR_AimUsePawnControlRotation",
        "VR_DecoupledPitch", "VR_DecoupledPitchUIAdjust"};
    for (auto name : keys) if (key==name) return true;
    return false;
}
inline Settings recovery_settings(const Settings& values) {
    Settings result;
    for (const auto& [key,value] : values) if (recoverable(key)) result.emplace(key,value);
    return result;
}
class Recovery {
public:
    void remember_launch(const Settings& values) {
        if (m_launch.empty()) m_launch=recovery_settings(values);
    }
    void supplied(const Settings& values) { m_supplied=recovery_settings(values); }
    const Settings& launch() const { return m_launch; }
    const Settings& supplied() const { return m_supplied; }
    bool can_undo() const { return !m_undo.empty(); }
    void remember_before_reset(const Settings& values) { m_undo=recovery_settings(values); }
    const Settings& undo() const { return m_undo; }
    void finish_undo() { m_undo.clear(); }
private:
    Settings m_launch,m_supplied,m_undo;
};
}
