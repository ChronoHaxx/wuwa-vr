#pragma once

#include <chrono>
#include <array>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include "Mod.hpp"
#include "utility/WuWaMouseRearm.hpp"
#include "utility/WuWaMotionTrace.hpp"
#include "utility/WuWaControlRecovery.hpp"
#include "utility/WuWaPrivacy.hpp"

namespace vrmod {

// Native presentation/input bridge for the frame-driven Polar control script.
// It never obtains Unreal objects from an XInput callback.
class WuWaControlsComponent : public ModComponent {
public:
    WuWaControlsComponent();
    std::string_view get_name() const override { return "WuWaControls"; }
    void on_draw_ui() override;
    void on_config_load(const utility::Config& cfg, bool set_defaults) override;
    void on_draw_first_person();
    void on_draw_language();
    void on_draw_recovery();
    void on_draw_shortcuts();
    void on_draw_experiments();
    void on_frame() override;
    void receive(std::string_view data);
    void record_rendered_view(int32_t index, const Rotator<float>* rotation, const Vector3f* position, bool doubles);
    nlohmann::json diagnostic_status();
    std::array<wuwa_privacy::Rect, 2> privacy_rectangles(int32_t width, int32_t height) const;
    bool floor_visible() const;
    bool menu_warning_active() const;
    bool fresh_menu_cursor() const;
    bool menu_warning_visible() const;
    bool mode_warning_active() const;
    bool status_warning_visible() const;
    bool passive_overlay_visible() const { return status_warning_visible() || floor_visible(); }
    void draw_passive_overlay();
    Matrix4x4f floor_transform() const;
    float floor_width() const { return m_sheet_width->value(); }
    static bool game_focused();

private:
    struct FocusValue : ModToggle {
        FocusValue() : ModToggle{"WuWaControls_Focused", false} {}
        std::string get() const override { return game_focused() ? "true" : "false"; }
        void set(const std::string&) override {}
        void config_load(const utility::Config&, bool) override {}
        void config_save(utility::Config&) override {}
    } m_focus;

    struct ClockValue : ModFloat {
        ClockValue() : ModFloat{"WuWaControls_Clock", 0.0f} {}
        std::string get() const override { return std::to_string((double)GetTickCount64() / 1000.0); }
        void set(const std::string&) override {}
        void config_load(const utility::Config&, bool) override {}
        void config_save(utility::Config&) override {}
    } m_clock;
    struct RecordingValue : ModToggle {
        RecordingValue() : ModToggle{"WuWaControls_Recording",false} {}
        std::string get() const override { return wuwa_motion::active() ? "true" : "false"; }
        void set(const std::string&) override {}
        void config_load(const utility::Config&,bool) override {}
        void config_save(utility::Config&) override {}
    } m_recording;

    // Adjustment is a temporary input mode, never restored on the next launch.
    struct AdjustValue : ModToggle {
        AdjustValue() : ModToggle{"WuWaControls_AdjustMode", false} {}
        void config_load(const utility::Config&, bool) override { value()=false; }
        void config_save(utility::Config&) override {}
    } m_adjust;

    // Outside control recovery: resetting camera/input does not reset language.
    const ModString::Ptr m_language{ModString::create("WuWaLocale", "en")};
    const ModToggle::Ptr m_enabled{ModToggle::create("WuWaControls_Enabled", true)};
    const ModToggle::Ptr m_keep_camera{ModToggle::create("WuWaControls_KeepCameraOnFocusLoss", true)};
    const ModToggle::Ptr m_recenter_position{ModToggle::create("WuWaControls_RecenterPosition", true)};
    const ModCombo::Ptr m_camera{ModCombo::create("WuWaControls_CameraMode", {"Game camera", "Fixed third person", "Freecam", "First person"}, 0)};
    const ModCombo::Ptr m_mesh{ModCombo::create("WuWaControls_MeshMode", {"Keep entire character visible", "Hide body in first person", "Hide head bones; keep body", "Hide body; keep original shadows", "Hide head bones; full shadow copy"}, 4)};
    const ModToggle::Ptr m_mouse{ModToggle::create("WuWaControls_MouseAssist", true)};
    const ModToggle::Ptr m_auto_mouse{ModToggle::create("WuWaControls_AutoMouseMenus", false)};
    const ModToggle::Ptr m_warn_hidden_ui{ModToggle::create("WuWaControls_WarnHiddenUI", true)};
    const ModToggle::Ptr m_walk{ModToggle::create("WuWaControls_PolarWalk", false)};
    const ModSlider::Ptr m_fixed_distance{ModSlider::create("WuWaControls_FixedDistance", -100.0f, 1500.0f, 150.0f)};
    const ModSlider::Ptr m_fixed_height{ModSlider::create("WuWaControls_FixedHeight", -100.0f, 400.0f, 30.0f)};
    const ModSlider::Ptr m_free_speed{ModSlider::create("WuWaControls_FreeSpeed", 10.0f, 3000.0f, 600.0f)};
    const ModSlider::Ptr m_free_turn{ModSlider::create("WuWaControls_FreeTurn", 10.0f, 300.0f, 90.0f)};
    const ModCombo::Ptr m_free_style{ModCombo::create("WuWaControls_FreeStyle", {"Polar fly", "Hover drone (inertia)", "Plane FPV", "Acro drone (manual)"}, 0)};
    const ModSlider::Ptr m_drone_response{ModSlider::create("WuWaControls_DroneResponse",0.5f,20.0f,6.0f)};
    const ModSlider::Ptr m_plane_speed{ModSlider::create("WuWaControls_PlaneSpeed",0.0f,3000.0f,300.0f)};
    const ModToggle::Ptr m_flight_roll{ModToggle::create("WuWaControls_FlightRoll",false)};
    const ModCombo::Ptr m_acro_throttle{ModCombo::create("WuWaControls_AcroThrottle", {"Right trigger (Xbox)", "Left stick Y (Mode 2)"}, 0)};
    const ModSlider::Ptr m_acro_rate{ModSlider::create("WuWaControls_AcroRate",30.0f,1000.0f,360.0f)};
    const ModSlider::Ptr m_acro_yaw_rate{ModSlider::create("WuWaControls_AcroYawRate",30.0f,720.0f,180.0f)};
    const ModSlider::Ptr m_acro_expo{ModSlider::create("WuWaControls_AcroExpo",0.0f,0.9f,0.35f)};
    const ModSlider::Ptr m_acro_thrust{ModSlider::create("WuWaControls_AcroThrust",1.1f,10.0f,4.0f)};
    const ModSlider::Ptr m_acro_drag{ModSlider::create("WuWaControls_AcroDrag",0.0f,3.0f,0.2f)};
    const ModSlider::Ptr m_acro_tilt{ModSlider::create("WuWaControls_AcroTilt",0.0f,60.0f,15.0f)};
    const ModToggle::Ptr m_acro_invert_pitch{ModToggle::create("WuWaControls_AcroInvertPitch",false)};
    const ModToggle::Ptr m_free_collision{ModToggle::create("WuWaControls_FreeCollision",false)};
    const ModToggle::Ptr m_collision_complex{ModToggle::create("WuWaControls_CollisionComplex",true)};
    const ModSlider::Ptr m_collision_radius{ModSlider::create("WuWaControls_CollisionRadius",1.0f,50.0f,10.0f)};
    const ModSlider::Ptr m_fp_forward{ModSlider::create("WuWaControls_FirstForward", -100.0f, 100.0f, 5.0f)};
    const ModSlider::Ptr m_fp_right{ModSlider::create("WuWaControls_FirstRight", -100.0f, 100.0f, 0.0f)};
    const ModSlider::Ptr m_fp_up{ModSlider::create("WuWaControls_FirstUp", -100.0f, 100.0f, 0.0f)};
    const ModToggle::Ptr m_fp_animation{ModToggle::create("WuWaControls_FollowHeadAnimation", false)};
    const ModCombo::Ptr m_fp_motion{ModCombo::create("WuWaControls_FirstMotion", {"Custom / saved behavior", "Comfort: stable height, level horizon", "Animated position + stick pitch", "Full animation rotation (experimental)"}, 0)};
    const ModCombo::Ptr m_fp_look{ModCombo::create("WuWaControls_FullFollowLook", {"Exact animation (stick does not steer view)", "Right-stick pitch; animated yaw and roll", "Use game view while either stick moves"}, 2)};
    const ModToggle::Ptr m_fp_smooth{ModToggle::create("WuWaControls_SmoothFullFollow",true)};
    const ModSlider::Ptr m_fp_blend_time{ModSlider::create("WuWaControls_FullFollowBlendTime",0.05f,0.75f,0.2f)};
    const ModToggle::Ptr m_fp_late{ModToggle::create("WuWaControls_LateHeadUpdate", false)};
    const ModToggle::Ptr m_fp_horizon{ModToggle::create("WuWaControls_LevelFirstPerson", true)};
    const ModToggle::Ptr m_sheet{ModToggle::create("WuWaControls_ShowShortcutSheet", true)};
    const ModCombo::Ptr m_sheet_page{ModCombo::create("WuWaControls_SheetPage", {"Automatic for current mode", "Everyday shortcuts", "Camera shortcuts", "Menus and HUD", "UEVR settings reference"}, 0)};
    const ModCombo::Ptr m_sheet_position{ModCombo::create("WuWaControls_SheetPosition", {"Below recentered headset origin", "In front of recentered headset origin"}, 0)};
    const ModSlider::Ptr m_sheet_width{ModSlider::create("WuWaControls_SheetWidth", 0.5f, 3.0f, 1.5f)};
    const ModSlider::Ptr m_sheet_drop{ModSlider::create("WuWaControls_SheetDrop", 0.2f, 2.0f, 1.2f)};
    const ModSlider::Ptr m_sheet_forward{ModSlider::create("WuWaControls_SheetForward", 0.1f, 2.0f, 0.65f)};
    const ModSlider::Ptr m_sheet_tilt{ModSlider::create("WuWaControls_SheetTilt", 0.0f, 90.0f, 60.0f)};
    // Privacy is deliberately outside the control-reset allowlist. Resetting
    // camera/input settings must not accidentally reveal a creator's ID.
    const ModToggle::Ptr m_privacy{ModToggle::create("WuWaPrivacy_Enabled",false)};
    const ModToggle::Ptr m_privacy_profile{ModToggle::create("WuWaPrivacy_ProfileID",true)};
    const ModCombo::Ptr m_privacy_profile_scope{ModCombo::create("WuWaPrivacy_ProfileScope", {"ESC / overlay menus (detected)", "All detected menus", "Always (manual mask)"},0)};
    const ModSlider::Ptr m_uid_left{ModSlider::create("WuWaPrivacy_UIDLeft",0.0f,1.0f,0.78f)};
    const ModSlider::Ptr m_uid_top{ModSlider::create("WuWaPrivacy_UIDTop",0.0f,1.0f,0.945f)};
    const ModSlider::Ptr m_uid_right{ModSlider::create("WuWaPrivacy_UIDRight",0.0f,1.0f,1.0f)};
    const ModSlider::Ptr m_uid_bottom{ModSlider::create("WuWaPrivacy_UIDBottom",0.0f,1.0f,1.0f)};
    const ModSlider::Ptr m_id_left{ModSlider::create("WuWaPrivacy_ProfileLeft",0.0f,1.0f,0.17f)};
    const ModSlider::Ptr m_id_top{ModSlider::create("WuWaPrivacy_ProfileTop",0.0f,1.0f,0.17f)};
    const ModSlider::Ptr m_id_right{ModSlider::create("WuWaPrivacy_ProfileRight",0.0f,1.0f,0.39f)};
    const ModSlider::Ptr m_id_bottom{ModSlider::create("WuWaPrivacy_ProfileBottom",0.0f,1.0f,0.215f)};

    struct MouseState { bool active{}, utility{}; int buttons{}, x{}, y{}; };
    mutable std::mutex m_bridge_mutex;
    MouseState m_mouse_state{};
    bool m_game_menu{};
    bool m_recenter_pending{}, m_screenshot_pending{};
    std::string m_script_status{"Camera script has not reported yet."};
    std::chrono::steady_clock::time_point m_received{}, m_last_frame{}, m_utility_begin{};
    bool m_left_down{}, m_utility_held{}, m_tab_down{}, m_input_armed{};
    wuwa_controls::MouseRearm m_mouse_rearm;
    std::array<bool, 4> m_keys{};
    int m_previous_buttons{};
    float m_mouse_x{}, m_mouse_y{}, m_scroll{};
    wuwa_controls::Recovery m_recovery;
    struct RecordedView {
        int32_t index{-1};
        std::array<double,3> position{}, rotation{};
        uint64_t clock_ms{};
    };
    std::array<RecordedView,3> m_recorded_views{};
    std::string m_recovery_status;
    wuwa_controls::Settings control_settings() const;
    void apply_control_settings(const wuwa_controls::Settings& settings);
    void release_input();
    void recenter(bool reset_position);
};

} // namespace vrmod
