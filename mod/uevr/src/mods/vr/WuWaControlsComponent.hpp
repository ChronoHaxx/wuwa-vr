#pragma once

#include <chrono>
#include <atomic>
#include <array>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include "Mod.hpp"
#include "utility/WuWaMouseRearm.hpp"
#include "utility/WuWaMotionTrace.hpp"
#include "utility/WuWaControlRecovery.hpp"
#include "utility/WuWaPrivacy.hpp"
#include "utility/WuWaVideoBridge.hpp"
#include "utility/WuWaMenuSignals.hpp"
#include "utility/WuWaPosePair.hpp"
#include "utility/WuWaAutoCinema.hpp"
#include "utility/WuWaNearClip.hpp"
#include "utility/WuWaPlayStationHid.hpp"
#include "utility/WuWaShortcutSheet.hpp"

namespace vrmod {

// Native presentation/input bridge for the frame-driven Polar control script.
// It never obtains Unreal objects from an XInput callback.
class WuWaControlsComponent : public ModComponent {
public:
    bool sync_eye_lod() const { return m_sync_eye_lod->value(); }
    bool stereo_dof() const { return m_stereo_dof->value(); }
    int stereo_dof_source() const { return m_stereo_dof_source->value(); }
    bool stereo_all_effects() const { return m_stereo_effects->value() == 1; }
    bool refill_far_lighting() const { return m_refill_far_lighting->value(); }
    bool ambient_occlusion() const { return m_ambient_occlusion->value(); }
    // Whether AO actually renders (r.AmbientOcclusionLevels non-zero), however it was turned on:
    // the toggle, or a value set in UEVR's cvar list. Written by CVarManager on the game thread.
    bool ambient_occlusion_rendering() const { return m_ao_rendering.load(std::memory_order_relaxed); }
    void set_ambient_occlusion_rendering(bool on) { m_ao_rendering.store(on, std::memory_order_relaxed); }
    bool automatic_runs() const { return m_run_auto->value(); }
    // One line about the treadmill run: on, last one, or how one starts (translated, logical order).
    std::string run_status() const;
    bool suppress_npc_rim() const { return m_suppress_npc_rim.requested.load(); }
    bool steady_desktop_view() const { return m_steady_desktop->value(); }
    float steady_desktop_seconds() const { return m_steady_desktop_seconds->value(); }
    WuWaControlsComponent();
    ~WuWaControlsComponent() override;
    std::string_view get_name() const override { return "WuWaControls"; }
    void on_draw_ui() override;
    void on_config_load(const utility::Config& cfg, bool set_defaults) override;
    void on_draw_first_person();
    void on_draw_language();
    void on_draw_recovery();
    void on_draw_shortcuts();
    void on_draw_recording();
    void on_draw_experiments();
    void on_frame() override;
    void receive(std::string_view data);
    void record_rendered_view(int32_t index, const Rotator<float>* rotation, const Vector3f* position, bool doubles);
    uint64_t pose_recording_session(std::string* recording_id = nullptr) noexcept;
    static wuwa_pose_pair::Pose sample_recorded_pose(const Rotator<float>* rotation, const Vector3f* position, bool doubles) noexcept;
    void record_pose_pair(const wuwa_pose_pair::Pair& pair) noexcept;
    nlohmann::json diagnostic_status();
    std::array<wuwa_privacy::Rect, 2> privacy_rectangles(int32_t width, int32_t height) const;
    bool floor_visible() const;
    // For the VR menu's Test page (UI thread): the launcher recorder's state
    // ("offline", "unavailable", "waiting" or its own state), and start / stop
    // with the saved recording settings.
    std::string menu_recording_state();
    void menu_recording_toggle();
    // Privacy boxes: 0 off, 1 while the launcher records, 2 always.
    int privacy_mode() const { return m_privacy->value(); }
    bool recording_live() const { return m_recording_live.load(std::memory_order_relaxed); }
    bool menu_warning_active() const;
    bool fresh_menu_cursor() const;
    bool menu_warning_visible() const;
    bool mode_warning_active() const;
    bool status_warning_visible() const;
    // The game's mouse cursor drawn over its UI in the headset (the system cursor never reaches it).
    bool cursor_visible() const;
    bool mouse_in_use() const; // a game menu's cursor, moved in the last 10 s, over the game window in front
    bool passive_overlay_visible() const { return status_warning_visible() || floor_visible() || cursor_visible(); }
    void draw_passive_overlay();
    Matrix4x4f floor_transform() const;
    float floor_width() const;
    static bool game_focused();
    bool auto_cinema_active() const;
    wuwa_auto_cinema::Presentation auto_cinema_presentation() const;
    void advance_auto_cinema(bool runtime_ready, bool story_ready);
    // A story dialogue or video the detector sees (HUD page "Story scenes in 16:9").
    bool story_scene_active() const { return m_story_scene.load(std::memory_order_relaxed); }
    std::string auto_cinema_status() const;
    void reset_auto_cinema();
    void override_auto_cinema();

private:
    std::unique_ptr<wuwa_ps_hid::Reader> m_playstation{std::make_unique<wuwa_ps_hid::Reader>()};
    wuwa_ps::MenuGesture m_playstation_menu;
    uint64_t m_playstation_generation{};
    void advance_playstation();
    struct PlayStationValue : ModString {
        WuWaControlsComponent& owner;
        bool status;
        PlayStationValue(WuWaControlsComponent& o, const char* name, bool s)
            : ModString{name, ""}, owner{o}, status{s} {}
        std::string get() const override;
        void set(const std::string&) override {}
        void config_load(const utility::Config&, bool) override {}
        void config_save(utility::Config&) override {}
    } m_playstation_state{*this, "WuWaControls_PlayStationState", false},
      m_playstation_status{*this, "WuWaControls_PlayStationStatus", true};
    mutable std::mutex m_cinema_mutex;
    wuwa_auto_cinema::Lease m_cinema_lease;
    std::atomic<wuwa_auto_cinema::Presentation> m_cinema_presentation{wuwa_auto_cinema::Presentation::none};
    std::string m_cinema_signal{"Detector has not reported"};
    double m_cinema_detection_ms{};
    // The same detector samples, held with the cinema lease's timing, for the 16:9
    // layout during story scenes. Never changes the view.
    wuwa_auto_cinema::Lease m_story_lease;
    uint64_t m_picture_seen_ms{}; // the last sample with a story picture on screen (cinema mutex)
    std::atomic<bool> m_story_scene{false};
    int8_t m_layout_seen{-1}; // the last full VR layout (game thread), -1 before the first tick
    // Transient protocol: generation fences old Lua callbacks; native expiry
    // restores the base view even if Lua stops running without reset callbacks.
    struct CinemaValue : ModString {
        WuWaControlsComponent& owner;
        int kind;
        CinemaValue(WuWaControlsComponent& owner_, int kind_, const char* name)
            : ModString{name, ""}, owner{owner_}, kind{kind_} {}
        std::string get() const override;
        void set(const std::string&) override;
        void config_load(const utility::Config&, bool) override {}
        void config_save(utility::Config&) override {}
    } m_cinema_producer{*this, 0, "WuWaControls_AutoCinemaProducer"},
      m_cinema_sample{*this, 1, "WuWaControls_AutoCinemaSample"},
      m_cinema_status{*this, 2, "WuWaControls_AutoCinemaStatus"},
      m_effective_mono{*this, 3, "WuWaControls_EffectiveMonoTheatre"},
      m_toggle_mono{*this, 4, "WuWaControls_ToggleMonoTheatre"},
      m_toggle_screen{*this, 5, "WuWaControls_ToggleStereoScreen"},
      m_effective_screen{*this, 6, "WuWaControls_EffectiveScreen"},
      m_scene_detect{*this, 7, "WuWaControls_StoryLayoutDetect"};
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

    struct NativeMenuValue : ModToggle {
        NativeMenuValue() : ModToggle{"WuWaControls_NativeMenu", false} {}
        std::string get() const override {
            // Only render observations here. Feeding the Lua cursor report
            // back into this getter would keep a closed menu latched on.
            return wuwa_menu::detected(GetTickCount64(), false) ? "true" : "false";
        }
        void set(const std::string&) override {}
        void config_load(const utility::Config&, bool) override {}
        void config_save(utility::Config&) override {}
    } m_native_menu;

    // Adjustment is a temporary input mode, never restored on the next launch.
    struct AdjustValue : ModToggle {
        AdjustValue() : ModToggle{"WuWaControls_AdjustMode", false} {}
        void config_load(const utility::Config&, bool) override { value()=false; }
        void config_save(utility::Config&) override {}
    } m_adjust;

    // One-shot requests and results cross the UI/game-thread boundary; neither
    // is a player preference or part of profile restoration.
    struct HudAspectRequest : ModToggle {
        HudAspectRequest() : ModToggle{"WuWaControls_ResetHudAspect", false} {}
        std::atomic<bool> pending{false};
        std::string get() const override { return pending.load() ? "true" : "false"; }
        void set(const std::string& text) override { pending.store(text=="true" || text=="1"); }
        void config_load(const utility::Config&, bool) override { pending.store(false); }
        void config_save(utility::Config&) override {}
    } m_hud_aspect_request;
    struct HudAspectStatus : ModString {
        HudAspectStatus() : ModString{"WuWaControls_HudAspectStatus", "Ready"} {}
        mutable std::mutex mutex;
        std::string status{"Ready"};
        std::string get() const override { std::scoped_lock lock{mutex}; return status; }
        void set(const std::string& text) override { std::scoped_lock lock{mutex}; status=text.substr(0,512); }
        void config_load(const utility::Config&, bool) override { set("Ready"); }
        void config_save(utility::Config&) override {}
    } m_hud_aspect_status;
    // Close-up match (02_WuWaVR_PolarControls.lua): how much of a long-lens
    // shot's subject size full VR keeps, in percent; 0 is off. Its live report
    // is transient, like the HUD status above.
    const ModSlider::Ptr m_lens_match{ModSlider::create("WuWaControls_LensMatch", 0.0f, 150.0f, 75.0f)};
    // The closest the eye may come to the subject, in cm.
    const ModSlider::Ptr m_lens_nearest{ModSlider::create("WuWaControls_LensNearest", 5.0f, 150.0f, 10.0f)};
    // Experiment: depth of field in stereo views (the camera script sets the show flag).
    const ModToggle::Ptr m_stereo_dof{ModToggle::create("WuWaStereo_DepthOfField", true)};
    // Which eye's depth of field both eyes use (the second view lacks it), and a
    // blur multiplier the camera script applies to the f-stop in full VR.
    const ModCombo::Ptr m_stereo_dof_source{ModCombo::create("WuWaStereo_DepthOfFieldSource", {"First view", "Second view", "Auto"}, 2)};
    // The second eye is built from a default camera: copy only the blur, or every
    // plain post-process value (grading, vignette, exposure...) as well (test 3g).
    const ModCombo::Ptr m_stereo_effects{ModCombo::create("WuWaStereo_EyeEffects", {"Blur only", "All camera effects"}, 1)};
    // Close-up subject: characters inside the 2D frame (3g) or the game's focus distance (3f).
    const ModCombo::Ptr m_lens_subject{ModCombo::create("WuWaControls_LensSubject", {"Characters in the shot", "Game focus only"}, 0)};
    const ModSlider::Ptr m_stereo_dof_strength{ModSlider::create("WuWaStereo_DepthOfFieldStrength", 0.25f, 4.0f, 1.0f)};
    // Full VR has its own strength (test 3h: half looked right in full VR; since the
    // close-up camera changes, 3m: 1x looks best there too, as on the screens).
    const ModSlider::Ptr m_stereo_dof_strength_vr{ModSlider::create("WuWaStereo_DepthOfFieldStrengthFullVR", 0.25f, 4.0f, 1.0f)};
    // Close-up match within a shot: track the game camera's own moves from the cut (3i),
    // keep the cut's distance (3g), or follow the game's focus distance (3h).
    // The test plan's step card takes the controls card's place below you; off
    // shows the controls card as a public release does (no plan there).
    const ModToggle::Ptr m_show_step_card{ModToggle::create("WuWaSteps_ShowCard", true)};
    const ModCombo::Ptr m_lens_follow{ModCombo::create("WuWaControls_LensFollow", {"Track the camera", "Fixed per shot", "Follow the focus"}, 0)};
    struct LensStatus : ModString {
        LensStatus() : ModString{"WuWaControls_LensStatus", ""} {}
        mutable std::mutex mutex;
        std::string status;
        std::string get() const override { std::scoped_lock lock{mutex}; return status; }
        void set(const std::string& text) override { std::scoped_lock lock{mutex}; status=text.substr(0,240); }
        void config_load(const utility::Config&, bool) override { set(""); }
        void config_save(utility::Config&) override {}
    } m_lens_status;
    // Close-up near clip (utility/WuWaNearClip.hpp): in story close-ups nothing
    // nearer than this is drawn, in cm; 10 is the game's own value (off). The
    // camera script sends the per-shot value, already kept short of the person
    // in focus, as a transient request. Off by default since test 16: a plane
    // slices characters and props open (recorded 15 Oct).
    const ModSlider::Ptr m_closeup_near_clip{ModSlider::create("WuWaControls_CloseUpNearClip", 10.0f, 50.0f, 10.0f)};
    // Test 16: in close-ups, a character within reach of an eye and with no bone in
    // the 2D frame is hidden whole (camera script). Off by default; LT + RT, then Y.
    const ModToggle::Ptr m_hide_near{ModToggle::create("WuWaControls_HideNearCharacters", false)};
    // While hiding is off: a note naming the shortcut, once per scene, when the script
    // finds such a character (transient WuWaControls_NearSeen).
    const ModToggle::Ptr m_near_hint{ModToggle::create("WuWaControls_NearHint", true)};
    struct NearSeen : ModString {
        NearSeen() : ModString{"WuWaControls_NearSeen", ""} {}
        std::string get() const override { return {}; }
        void set(const std::string&) override { wuwa_near_clip::near_seen_ms.store(GetTickCount64()); }
        void config_load(const utility::Config&, bool) override {}
        void config_save(utility::Config&) override {}
    } m_near_seen;
    struct NearClipNow : ModString {
        NearClipNow() : ModString{"WuWaControls_NearClipNow", "0"} {}
        mutable std::mutex mutex;
        std::string text{"0"};
        std::string get() const override { std::scoped_lock lock{mutex}; return text; }
        void set(const std::string& value) override {
            { std::scoped_lock lock{mutex}; text = value.substr(0, 16); }
            wuwa_near_clip::request(value);
        }
        void config_load(const utility::Config&, bool) override { set("0"); }
        void config_save(utility::Config&) override {}
    } m_near_clip_now;

    // Outside control recovery: resetting camera/input does not reset language.
    const ModString::Ptr m_language{ModString::create("WuWaLocale", "en")};
    const ModCombo::Ptr m_video_fps{ModCombo::create("WuWaRecording_FPS", {"30 fps", "45 fps", "60 fps"}, 0)};
    const ModCombo::Ptr m_video_width{ModCombo::create("WuWaRecording_Width", {"720", "1024", "1280"}, 1)};
    const ModToggle::Ptr m_video_telemetry{ModToggle::create("WuWaRecording_Telemetry", true)};
    const ModToggle::Ptr m_steady_desktop{ModToggle::create("WuWaRecording_SteadyDesktop", true)};
    const ModSlider::Ptr m_steady_desktop_seconds{ModSlider::create("WuWaRecording_SteadySeconds", 0.05f, 1.0f, 0.35f)};
    wuwa_video::Client m_video;
    const ModToggle::Ptr m_enabled{ModToggle::create("WuWaControls_Enabled", true)};
    const ModToggle::Ptr m_keep_camera{ModToggle::create("WuWaControls_KeepCameraOnFocusLoss", true)};
    // Native stereo: give the second eye the first eye's LOD distance factor and FOV so
    // far foliage and props switch LOD together (the game builds the second eye with 90).
    const ModToggle::Ptr m_sync_eye_lod{ModToggle::create("WuWaStereo_SyncEyeLod", true)};
    // Bounded lighting-volume refresh; not a general material or NPC rim repair.
    const ModToggle::Ptr m_refill_far_lighting{ModToggle::create("WuWaStereo_RefillFarLighting", true)};
    // Screen-space ambient occlusion shades the two eyes differently with off-centre eye
    // frustums (rocks, hills, roofs; Lelouche, 9 Oct 2026). While it renders, OpenXR
    // renders symmetric frustums (VR::get_effective_horizontal_projection_override), so
    // it is on by default from 1.2.2; off sets r.AmbientOcclusionLevels 0 (CVarManager.cpp).
    // Cost measured 13 Oct 2026: about 2.2 ms a frame (7%), simulator, 3292 px per eye.
    const ModToggle::Ptr m_ambient_occlusion{ModToggle::create("WuWaStereo_AmbientOcclusion", true)};
    // Configs saved before 1.2.2 hold the old default (off). 1 = moved to the new default.
    const ModInt32::Ptr m_ambient_occlusion_default{ModInt32::create("WuWaStereo_AmbientOcclusionDefault", 0)};
    std::atomic<bool> m_ao_rendering{false};
    struct RimToggle : ModToggle {
        RimToggle() : ModToggle{"WuWaStereo_SuppressNpcRim", false} {}
        std::atomic<bool> requested{false};
        std::string get() const override { return requested.load() ? "true" : "false"; }
        void set(const std::string& text) override { requested.store(text == "true" || text == "1"); }
        void config_load(const utility::Config& cfg, bool defaults) override {
            requested.store(defaults ? false : cfg.get<bool>(get_config_name()).value_or(false));
        }
        void config_save(utility::Config& cfg) override { cfg.set<bool>(get_config_name(), requested.load()); }
        bool draw(std::string_view name) override {
            bool value = requested.load();
            const bool changed = ImGui::Checkbox(name.data(), &value);
            if (changed) requested.store(value);
            return changed;
        }
    } m_suppress_npc_rim;
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
    // Controls cards (1.2.0, replacing the four-page sheet): hold L3, or LT + RT,
    // for half a second and a card rises under your view, lighting each button
    // as it is used; L3 + Menu keeps it open. New keys, so a saved "sheet
    // always on" does not pin the new card open.
    const ModToggle::Ptr m_cards{ModToggle::create("WuWaControls_Cards", true)};
    // Pinned from the start, low enough to stay out of the HUD's way (test 3n).
    const ModToggle::Ptr m_pin_card{ModToggle::create("WuWaControls_PinCard", true)};
    const ModSlider::Ptr m_card_width{ModSlider::create("WuWaControls_CardWidth", 0.5f, 1.6f, 0.9f)};
    // Where the card rises: degrees below the line of sight, and how far away.
    const ModSlider::Ptr m_card_below{ModSlider::create("WuWaControls_CardBelow", 15.0f, 75.0f, 50.0f)};
    const ModSlider::Ptr m_card_distance{ModSlider::create("WuWaControls_CardDistance", 0.5f, 1.5f, 0.9f)};
    // The line after a view switch shows for five switches, then stays off
    // until turned back on.
    const ModToggle::Ptr m_view_hints{ModToggle::create("WuWaControls_ViewHints", true)};
    const ModInt32::Ptr m_view_hints_shown{ModInt32::create("WuWaControls_ViewHintsShown", 0)};
    // With Reality Runner open, walking starts a run and a long pause ends it (WuWaRunMarker.hpp).
    const ModToggle::Ptr m_run_auto{ModToggle::create("WuWaRun_Auto", true)};
    // A one-line tip shows on the first three launches.
    const ModInt32::Ptr m_tips_shown{ModInt32::create("WuWaControls_TipsShown", 0)};
    // What the camera script reports held: "l3:<buttons>:<lt>:<rt>", "views:..." or nothing.
    struct CardValue : ModString {
        CardValue() : ModString{"WuWaControls_Card", ""} {}
        mutable std::mutex mutex;
        std::string held;
        std::string get() const override { std::scoped_lock lock{mutex}; return held; }
        void set(const std::string& text) override { std::scoped_lock lock{mutex}; held = text.substr(0, 64); }
        void config_load(const utility::Config&, bool) override { set(""); }
        void config_save(utility::Config&) override {}
    } m_card;
    // Asked by the overlay and the UI frame (both on the present thread).
    mutable std::mutex m_card_mutex;
    mutable bool m_card_up{};
    mutable uint64_t m_card_since{}, m_card_released{};
    mutable Matrix4x4f m_card_anchor{1.0f};
    // The level direction from the card back to where the head was (its yaw).
    mutable glm::vec3 m_card_back{0.0f, 0.0f, 1.0f};
    // Which of the five view hints is showing.
    mutable int m_view_hint{};
    mutable int m_view_seen{-1};
    mutable uint64_t m_view_chip_until{};
    wuwa_sheet::CardState m_card_last{};
    std::atomic<uint64_t> m_tip_until{};
    uint64_t m_tip_eligible_since{};
    bool m_tip_done{};
    bool held_card(wuwa_sheet::CardState& state) const;
    bool card_visible() const;
    Matrix4x4f card_anchor(glm::vec3& back) const;
    // 0 nothing, 1 the first-launch tip, 2 the view-switch chip.
    int tip_kind() const;
    void update_tip();
    // Privacy is deliberately outside the control-reset allowlist. Resetting
    // camera/input settings must not accidentally reveal a creator's ID.
    // The boxes are approximate and can cover useful HUD, so by default they
    // show only while the launcher records (test 3d, Oct 2026). A profile that
    // had the old WuWaPrivacy_Enabled on keeps covering always (see on_config_load).
    const ModCombo::Ptr m_privacy{ModCombo::create("WuWaPrivacy_Mode",{"Off","While recording","Always"},1)};
    // Set on the UI thread from the launcher's status, read by the render thread.
    std::atomic<bool> m_recording_live{false};
    void refresh_recording_live();
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
    std::string m_pose_recording_id;
    uint64_t m_pose_recording_session{};
    wuwa_pose_pair::Pair m_recorded_pose_pair{};
    uint64_t m_next_pose_constructor_emit_ms{};
    std::atomic<uint64_t> m_pose_capture_lock_misses{};
    std::string m_recovery_status;
    wuwa_controls::Settings control_settings() const;
    void apply_control_settings(const wuwa_controls::Settings& settings);
    void release_input();
    void recenter(bool reset_position);
};

} // namespace vrmod
