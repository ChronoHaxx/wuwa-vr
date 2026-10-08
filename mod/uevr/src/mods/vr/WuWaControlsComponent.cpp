#define NOMINMAX
#include "utility/WuWaLocalizedUI.hpp"
#include "utility/WuWaMenuSignals.hpp"
#include "WuWaControlsComponent.hpp"
#include "../VR.hpp"
#include "../FrameworkConfig.hpp"
#include "../WindowMode.hpp"
#include "utility/WuWaTestControl.hpp"
#include "utility/WuWaClvRefresh.hpp"
#include "utility/WuWaRunMarker.hpp"
#include "utility/WuWaShortcutSheet.hpp"
#include "utility/WuWaStereoBasePose.hpp"
#include "utility/WuWaPlaytestControl.hpp"
#include "utility/WuWaLguiProbe.hpp"
#include <nlohmann/json.hpp>
#include <glm/gtx/transform.hpp>
#include <algorithm>
#include <cmath>

void wuwa_playtest::draw_controls(const std::filesystem::path& profile) {
    // This panel is independent of rendering/settings. Opening it only polls
    // the helper; results are written only by an explicit acknowledged action.
    static Client client;
    static std::string displayed_session,submitted_note,submitted_note_id;
    static std::array<char,2049> draft{};
    static int selected{};
    static bool confirm_finish{},review_draft{};
    client.poll(profile);
    ImGui::PushID("WuWaPlaytestPanel");
    wuwa_ui::TextWrapped("Start a developer playtest in the launcher, then save your observations here. Nothing passes automatically.");
    const auto reply=client.acknowledgement();
    if (!submitted_note_id.empty() && reply.value("id",std::string{})==submitted_note_id) {
        if (reply.value("ok",false) && std::string{draft.data()}==submitted_note) {
            draft.fill('\0');review_draft=false;
        }
        submitted_note_id.clear();
    }
    const auto message=client.message();
    if (!message.empty()) {
        if (client.failed()) wuwa_ui::TextColored(ImVec4{1.0f,0.7f,0.55f,1.0f},"%s",message.c_str());
        else wuwa_ui::TextWrapped("%s",message.c_str());
    }
    if (!client.available()) {
        wuwa_ui::TextWrapped("No active playtest connection. Keep the launcher running and open Developer playtests to start or resume a session.");
        const auto error=client.connection_error();
        if (!error.empty()) wuwa_ui::TextWrapped("%s",error.c_str());
        if (draft[0]) wuwa_ui::TextWrapped("Your unsent note is retained in this menu until the game closes.");
        ImGui::PopID();return;
    }
    const auto session=client.session();
    const auto session_id=session.at("id").get<std::string>();
    if (displayed_session!=session_id) {
        review_draft=draft[0]!='\0';displayed_session=session_id;selected=0;confirm_finish=false;
    }
    const auto& checks=session.at("checks");
    selected=std::clamp(selected,0,static_cast<int>(checks.size())-1);
    const auto localized=[](const nlohmann::json& text) {
        const auto language=wuwa_l10n::language();
        if (text.contains(language) && text.at(language).is_string()) return text.at(language).get<std::string>();
        if (language=="zh-Hans" && text.contains("zh") && text.at("zh").is_string()) return text.at("zh").get<std::string>();
        return text.at("en").get<std::string>();
    };
    wuwa_ui::TextWrapped("Selected build: %s",session.at("build_name").get<std::string>().c_str());
    wuwa_ui::TextWrapped("The selected package is recorded; this is not proof of the DLL loaded by the game.");
    ImGui::BeginDisabled(client.pending());
    const auto preview=localized(checks.at(selected).at("title"));
    if (ImGui::BeginCombo(wuwa_l10n::label("Checklist item").c_str(),preview.c_str())) {
        for (size_t index=0;index<checks.size();++index) {
            ImGui::PushID(static_cast<int>(index));
            const auto label=localized(checks.at(index).at("title"));
            if (ImGui::Selectable(label.c_str(),selected==static_cast<int>(index))) selected=static_cast<int>(index);
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const auto& check=checks.at(selected);
    const auto item_id=check.at("id").get<std::string>();
    wuwa_ui::TextWrapped("%s",localized(check.at("instructions")).c_str());
    const auto status=check.at("status").get<std::string>();
    const char* status_label=status=="pass" ? "Pass" : status=="fail" ? "Fail" : status=="blocked" ? "Blocked" : "Not tested";
    wuwa_ui::TextWrapped("Current result: %s",wuwa_l10n::text(status_label).c_str());
    for (const auto& choice:std::array<std::array<const char*,2>,4>{{
            {{"Pass","pass"}},{{"Fail","fail"}},{{"Blocked","blocked"}},{{"Not tested","not_tested"}}}}) {
        if (wuwa_ui::Button(choice[0])) client.submit(profile,session_id,"result",item_id,choice[1]);
        if (std::string_view{choice[1]}!="not_tested") ImGui::SameLine();
    }
    if (review_draft) wuwa_ui::TextWrapped("The session changed. Review your unsent note before adding it to this session.");
    ImGui::InputTextMultiline(wuwa_l10n::label("Observation").c_str(),draft.data(),draft.size(),ImVec2(-1,90));
    wuwa_ui::TextWrapped("Use a keyboard for text. Voice notes require explicit microphone consent in the developer web panel.");
    if (wuwa_ui::Button("Save observation")) {
        if (client.submit(profile,session_id,"note",item_id,draft.data())) {
            submitted_note=draft.data();submitted_note_id=client.pending_id();
        }
    }
    ImGui::SameLine();
    if (wuwa_ui::Button("Discard unsent note")) { draft.fill('\0');review_draft=false; }
    if (wuwa_ui::Button("Link latest recording")) client.submit(profile,session_id,"link-recording");
    wuwa_ui::TextWrapped("Linking saves the recording ID only. Review the video and add precise timestamps in the launcher.");
    ImGui::Separator();
    wuwa_ui::TextWrapped("Finish only when you are done. Untested checks remain Not tested; no result is inferred from notes.");
    ImGui::Checkbox(wuwa_l10n::label("I understand that untested checks remain Not tested").c_str(),&confirm_finish);
    ImGui::BeginDisabled(!confirm_finish || client.voice_busy() || draft[0]!='\0');
    if (wuwa_ui::Button("Finish playtest")) {
        if (client.submit(profile,session_id,"finish","","",true)) confirm_finish=false;
    }
    ImGui::EndDisabled();
    if (client.voice_busy()) wuwa_ui::TextWrapped("Stop the voice note or transcription in the launcher before finishing.");
    if (draft[0]) wuwa_ui::TextWrapped("Save or discard your unsent note before finishing.");
    ImGui::EndDisabled();
    ImGui::PopID();
}

namespace vrmod {
namespace {
constexpr const char* sheet[][2] = {
    {"GENERAL", "CAMERA"},
    {"L3 + R3    UEVR menu", "L3 + RB    Game / fixed camera"},
    {"L3 + A     Recenter view / portal", "L3 + Y/X   Fixed / first-person height"},
    {"L3 + LT / F7   Portal on / off", "L3 + RT    Diorama on / off (10x)"},
    {"LT + RT, then click L3: stereo screen", "LT + RT, then click R3: mono theatre"},
    {"Double L3  Windows screenshot", "LB + LT/RT Fixed camera farther / closer"},
    {"Double R3  Freecam on / off", "Freecam: left stick moves, right looks"},
    {"L3 + B     Show / hide game UI", "Freecam: LT rises, RT boosts speed"},
    {"L3 + View  First person on / off", "Freecam: LB descends; double RT = turbo"},
    {"L3 + Menu  Show / hide this sheet", "L3 + D-pad Down: animation follow on / off (first person)"},
    {"Sheet open: L3 + D-pad Left/Right = pages", "L3 + D-pad Up = automatic page"},
    {"Hold RB: full speed if Polar walk is on", "LB + R3: V once; hold 0.8 s for Tab wheel"},
    {"L3 + LB    Toggle HUD / mouse adjustment", "Release all controls after entering/leaving"},
    {"WHILE ADJUSTMENT IS ON", "HUD POSITION"},
    {"Left stick Cursor    A Click    B Back", "LT/RT      HUD nearer / farther"},
    {"X + stick  Scroll    D-pad Move map", "LB/RB      HUD down / up"},
    {"L3 + LB    Return to normal game controls", "LB + R3 utility shortcut remains available outside adjustment"},
};
void key(WORD code, bool down) {
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = code;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP; SendInput(1, &input, sizeof(input));
}
void mouse(DWORD flags, LONG x = 0, LONG y = 0, DWORD data = 0) {
    INPUT input{}; input.type = INPUT_MOUSE; input.mi.dwFlags = flags;
    input.mi.dx = x; input.mi.dy = y; input.mi.mouseData = data; SendInput(1, &input, sizeof(input));
}
nlohmann::json pose_frames_json(const wuwa_pose_pair::Frames& frames) {
    return {{"runtime_frame",frames.runtime_frame},{"g_frame_count",frames.game_frame},{"thread",frames.thread}};
}
nlohmann::json recorded_pose_json(const wuwa_pose_pair::Pose& pose) {
    const bool valid=wuwa_pose_pair::finite(pose);
    return {{"valid",valid},{"position",valid?nlohmann::json(pose.position):nlohmann::json(nullptr)},
        {"rotation",valid?nlohmann::json(pose.rotation):nlohmann::json(nullptr)}};
}
template<class T> nlohmann::json pose_field_json(const wuwa_pose_pair::Field<T>& field) {
    return field.valid?nlohmann::json(field.value):nlohmann::json(nullptr);
}
nlohmann::json constructor_pose_json(const wuwa_pose_pair::Constructor& c) {
    const auto& a=c.input;const auto& b=c.output;
    nlohmann::json input{{"address",a.address},{"origin_0x0",pose_field_json(a.origin)},
        {"rotation_0x10",pose_field_json(a.rotation)},{"projection_0x50",pose_field_json(a.projection)},
        {"alternate_projection_0xb0",pose_field_json(a.alternate_projection)},
        {"alternate_flag_0xf0",pose_field_json(a.alternate_flag)},
        {"family_0xf8",pose_field_json(a.family)},{"state_0x100",pose_field_json(a.state)},
        {"pass_0x150",pose_field_json(a.pass)}};
    auto first=nlohmann::json::array(),second=nlohmann::json::array();
    for(size_t i=0;i<wuwa_pose_pair::matrix_offsets.size();++i) {
        first.push_back(pose_field_json(b.object_320[i]));second.push_back(pose_field_json(b.object_7e0[i]));
    }
    nlohmann::json output{{"view",b.address},{"family_0x0",pose_field_json(b.family)},
        {"state_0x8",pose_field_json(b.state)},{"pass_0xc90",pose_field_json(b.pass)},
        {"matrix_offsets",wuwa_pose_pair::matrix_offsets},
        {"matrix_object_0x320",std::move(first)},{"matrix_object_0x7e0",std::move(second)}};
    return {{"constructor_ordinal",c.ordinal},{"preceding_offset_call_ordinal",c.preceding_call_ordinal},
        {"offset_call_association_proven",false},{"destination",c.destination},
        {"begin_ms",c.begin_ms},{"end_ms",c.end_ms},{"input_frames",pose_frames_json(c.input_frames)},
        {"output_frames",pose_frames_json(c.output_frames)},{"layout_verified",c.layout_verified},
        {"finished",c.finished},{"result_matches_destination",c.result_matches_destination},{"valid",c.valid},
        {"input",std::move(input)},{"after_native_before_early_correction",std::move(output)}};
}
nlohmann::json pose_pair_json(const wuwa_pose_pair::Pair& pair, uint64_t now, bool emit_constructors) {
    nlohmann::json result{{"version",1},{"available",pair.available},{"valid",pair.valid},
        {"association","latest retained completed viewport draw; explicit CPU scope identity, not the current motion sample or GPU frame"},
        {"valid_scope","two completed stereo-offset calls in one CPU viewport draw; constructor completeness is separate"}};
    if (!pair.available) return result;
    result["recording_session"]=pair.scope.recording_session;
    result["viewport_draw_sequence"]=pair.scope.draw_sequence;
    result["parent_viewport_draw_sequence"]=pair.scope.parent_draw_sequence;
    result["draw_frames"]=pose_frames_json(pair.scope.frames);
    result["begin_ms"]=pair.scope.begin_ms;result["end_ms"]=pair.end_ms;
    result["age_ms"]=now>=pair.end_ms?nlohmann::json(now-pair.end_ms):nlohmann::json(nullptr);
    result["fresh"]=now>=pair.end_ms && now-pair.end_ms<=250;
    result["closed"]=pair.closed;result["recording_continued"]=pair.recording_continued;
    result["call_count"]=pair.call_count;result["stored_calls"]=pair.stored_calls;
    result["capacity"]=wuwa_pose_pair::call_capacity;result["eye_calls"]=pair.eye_calls;
    result["overflow"]=pair.overflow;result["missing_eye"]=pair.missing_eye;
    result["duplicate_eye"]=pair.duplicate_eye;result["invalid_eye"]=pair.invalid_eye;
    result["frame_mismatch"]=pair.frame_mismatch;result["invalid_pose"]=pair.invalid_pose;
    result["incomplete_call"]=pair.incomplete_call;
    result["call_ordinal_scope"]="non-full stereo-offset calls in this viewport draw";
    auto calls=nlohmann::json::array();
    for (size_t i=0;i<pair.stored_calls;++i) {
        const auto& c=pair.calls[i];
        calls.push_back({{"call_ordinal",c.ordinal},{"raw_index",c.raw_index},{"logical_eye",c.logical_eye},
            {"begin_ms",c.begin_ms},{"end_ms",c.end_ms},{"input_frames",pose_frames_json(c.input_frames)},
            {"after_pre_frames",pose_frames_json(c.after_pre_frames)},{"output_frames",pose_frames_json(c.output_frames)},
            {"input_game_pose",recorded_pose_json(c.input_game)},
            {"stereo_base",{{"observed",c.stereo_base_observed},{"callsite_rva",c.callsite_rva},
                {"action",wuwa_stereo_base_pose::action_name(static_cast<wuwa_stereo_base_pose::Action>(c.stereo_base_action))},
                {"reason",wuwa_stereo_base_pose::reason_name(static_cast<wuwa_stereo_base_pose::Reason>(c.stereo_base_reason))},
                {"source_call_ordinal",c.stereo_base_source_ordinal}}},
            {"after_pre_callbacks_pose",recorded_pose_json(c.after_pre_callbacks)},
            {"output_pose",recorded_pose_json(c.output)},
            {"has_after_pre",c.has_after_pre},{"finished",c.finished}});
    }
    result["calls"]=std::move(calls);
    auto constructors=nlohmann::json::array();
    if (emit_constructors) for(size_t i=0;i<pair.stored_constructors;++i)
        constructors.push_back(constructor_pose_json(pair.constructors[i]));
    result["constructor_count"]=pair.constructor_count;result["constructor_capacity"]=wuwa_pose_pair::constructor_capacity;
    result["stored_constructors"]=pair.stored_constructors;
    result["constructors_sampled"]=emit_constructors;
    result["constructor_sample_interval_ms"]=200;
    if (!emit_constructors) result["constructor_omission_reason"]="bounded serialization cadence";
    result["constructor_overflow"]=pair.constructor_overflow;result["constructors"]=std::move(constructors);
    return result;
}
}

bool WuWaControlsComponent::auto_cinema_active() const {
    return auto_cinema_presentation() != wuwa_auto_cinema::Presentation::none;
}
wuwa_auto_cinema::Presentation WuWaControlsComponent::auto_cinema_presentation() const {
    return m_cinema_presentation.load();
}
void WuWaControlsComponent::advance_auto_cinema(bool runtime_ready) {
    std::scoped_lock lock{m_cinema_mutex};
    // Apply expiry on a game-frame boundary, never independently between eye
    // getter calls. A fully stalled game thread holds its last presentation
    // until the next tick; that tick releases a stale producer before rendering.
    if (!runtime_ready) m_cinema_lease.invalidate();
    m_cinema_presentation.store(m_cinema_lease.presentation(GetTickCount64()));
}
std::string WuWaControlsComponent::auto_cinema_status() const {
    std::scoped_lock lock{m_cinema_mutex};
    const auto now = GetTickCount64();
    const auto state = m_cinema_presentation.load() != wuwa_auto_cinema::Presentation::none && !m_cinema_lease.active(now) ?
        "Automatic lease expired; restoration waits for the next game tick" : m_cinema_lease.state(now);
    return std::string{state} + ". " + m_cinema_signal +
        " [detection " + std::to_string(static_cast<int>(std::round(m_cinema_detection_ms))) + " ms]";
}
void WuWaControlsComponent::reset_auto_cinema() {
    std::scoped_lock lock{m_cinema_mutex};
    m_cinema_lease.reset();
    m_cinema_presentation.store(wuwa_auto_cinema::Presentation::none);
    m_cinema_signal = "Detector has not reported";
    m_cinema_detection_ms = 0;
}
void WuWaControlsComponent::override_auto_cinema() {
    std::scoped_lock lock{m_cinema_mutex};
    m_cinema_lease.manual();
    m_cinema_presentation.store(wuwa_auto_cinema::Presentation::none);
}
std::string WuWaControlsComponent::CinemaValue::get() const {
    if (kind == 1) return "lease-v1";
    if (kind == 2) return owner.auto_cinema_status();
    if (kind == 3) return VR::get()->is_using_mono_theatre() ? "true" : "false";
    if (kind == 4 || kind == 5) return "effective-v1";
    if (kind == 6) return VR::get()->is_using_2d_screen() ? "true" : "false";
    std::scoped_lock lock{owner.m_cinema_mutex};
    return std::to_string(owner.m_cinema_lease.generation());
}
void WuWaControlsComponent::CinemaValue::set(const std::string& text) {
    if (kind == 4 || kind == 5) {
        if (text == "toggle") {
            if (kind == 4) VR::get()->set_mono_theatre_manually(!VR::get()->is_using_mono_theatre());
            else VR::get()->set_stereo_screen_manually(!VR::get()->is_using_2d_screen());
        }
        return;
    }
    if (kind > 1 || text.size() > 512) return;
    try {
        if (kind == 0) {
            std::scoped_lock lock{owner.m_cinema_mutex};
            if (text == "start") owner.m_cinema_lease.start();
            else if (text.starts_with("stop:")) {
                size_t consumed{};
                const auto generation = std::stoull(text.substr(5), &consumed);
                if (consumed == text.size() - 5) owner.m_cinema_lease.stop(generation);
            }
            return;
        }
        const auto sample = nlohmann::json::parse(text);
        if (!sample.is_object() || sample.value("version", 0) != 1) return;
        const auto token = sample.value("generation", std::string{});
        if (token.empty() || token.size() > 20 || token.find_first_not_of("0123456789") != std::string::npos) return;
        const auto generation = std::stoull(token);
        const auto world = sample.value("world", std::string{});
        const auto active = sample.value("active", -1), hold = sample.value("hold", -1), known = sample.value("known", -1);
        const auto detail = sample.value("detail", std::string{});
        const auto elapsed = sample.value("elapsed_ms", 0.0);
        if (world.empty() || world.size() > 32 || world.find_first_not_of("0123456789") != std::string::npos ||
            active < 0 || active > 3 || hold < 0 || hold > 3 || known < 0 || known > 3 ||
            detail.size() > 240 || !std::isfinite(elapsed) || elapsed < 0 || elapsed > 50) return;
        std::scoped_lock lock{owner.m_cinema_mutex};
        if (!VR::get()->is_auto_cinema_enabled()) return;
        const auto presentation = ((active & wuwa_auto_cinema::movie) ||
                (hold & owner.m_cinema_lease.seen_sources() & wuwa_auto_cinema::movie)) ?
            wuwa_auto_cinema::Presentation::mono_theatre : VR::get()->auto_story_presentation();
        if (owner.m_cinema_lease.sample(generation, world, static_cast<uint8_t>(active),
                static_cast<uint8_t>(hold), static_cast<uint8_t>(known), presentation, GetTickCount64())) {
            owner.m_cinema_signal = detail;
            owner.m_cinema_detection_ms = elapsed;
        }
    } catch (const std::exception&) { /* malformed or obsolete producer: expire naturally */ }
}

WuWaControlsComponent::WuWaControlsComponent() {
    m_options = {*m_language, *m_enabled, *m_keep_camera, *m_sync_eye_lod, *m_refill_far_lighting, m_suppress_npc_rim, *m_recenter_position, *m_camera, *m_mesh, *m_mouse, *m_auto_mouse, *m_warn_hidden_ui, m_adjust, *m_walk, *m_fixed_distance,
        *m_fixed_height, *m_free_speed, *m_free_turn, *m_free_style, *m_drone_response, *m_plane_speed,
        *m_flight_roll, *m_acro_throttle, *m_acro_rate, *m_acro_yaw_rate, *m_acro_expo,
        *m_acro_thrust, *m_acro_drag, *m_acro_tilt, *m_acro_invert_pitch,
        *m_free_collision, *m_collision_complex, *m_collision_radius, *m_fp_forward, *m_fp_right, *m_fp_up,
        *m_fp_animation, *m_fp_motion, *m_fp_look, *m_fp_smooth, *m_fp_blend_time, *m_fp_late, *m_fp_horizon, *m_sheet, *m_sheet_page, *m_sheet_position, *m_sheet_width, *m_sheet_drop,
        *m_sheet_forward, *m_sheet_tilt, m_focus, m_clock, m_recording, m_native_menu, m_hud_aspect_request, m_hud_aspect_status,
        m_playstation_state, m_playstation_status,
        m_cinema_producer, m_cinema_sample, m_cinema_status, m_effective_mono, m_toggle_mono, m_toggle_screen, m_effective_screen,
        *m_video_fps, *m_video_width, *m_video_telemetry, *m_steady_desktop, *m_steady_desktop_seconds,
        *m_privacy, *m_privacy_profile, *m_privacy_profile_scope, *m_uid_left, *m_uid_top, *m_uid_right, *m_uid_bottom,
        *m_id_left, *m_id_top, *m_id_right, *m_id_bottom};
}

WuWaControlsComponent::~WuWaControlsComponent() {
    wuwa_ps_hid::release_on_process_exit(m_playstation,
        wuwa_lgui_probe::detail::process_exiting.load(std::memory_order_relaxed));
}

void WuWaControlsComponent::on_config_load(const utility::Config& cfg, bool set_defaults) {
    reset_auto_cinema();
    ModComponent::on_config_load(cfg,set_defaults);
    if (wuwa_test::is_wuwa())
        wuwa_l10n::request(m_language->value(), Framework::get_persistent_dir("wuwa-languages"));
    if (!wuwa_test::is_wuwa() || set_defaults || !m_recovery.launch().empty()) return;
    auto launch=cfg.get_key_values();
    for (const IModValue& value : m_options) launch[value.get_config_name()]=value.get();
    m_recovery.remember_launch(launch);
    try {
        utility::Config supplied;
        if (supplied.load(Framework::get_persistent_dir("wuwa-profile-defaults.txt").string()) &&
            supplied.get("WuWaProfileDefaultsVersion")==std::optional<std::string>{"1"})
            m_recovery.supplied(supplied.get_key_values());
    } catch (const std::exception& e) { spdlog::warn("[WuWaControls] Profile reset unavailable: {}",e.what()); }
}

wuwa_controls::Settings WuWaControlsComponent::control_settings() const {
    wuwa_controls::Settings result;
    for (const auto& [key,unused] : m_recovery.launch())
        if (const auto value=VR::get()->get_value(key)) result[key]=value->get();
    for (const auto& [key,unused] : m_recovery.supplied())
        if (const auto value=VR::get()->get_value(key)) result[key]=value->get();
    return result;
}

void WuWaControlsComponent::apply_control_settings(const wuwa_controls::Settings& settings) {
    // Validate the entire bounded, scoped set before changing any live option.
    for (const auto& [key,text] : settings) {
        if (!wuwa_controls::recoverable(key)) continue;
        const auto value=VR::get()->get_value(key);
        if (!value) continue;
        if (!wuwa_controls::valid_value_like(value->get(),text))
            throw std::runtime_error("Invalid profile value: "+key);
    }
    for (const auto& [key,text] : settings)
        if (wuwa_controls::recoverable(key))
            if (const auto value=VR::get()->get_value(key)) value->set(text);
    m_adjust.value()=false;
    release_input(); m_input_armed=false;
    std::scoped_lock lock{m_bridge_mutex};
    m_mouse_state={}; m_recenter_pending=m_screenshot_pending=false;
}

void WuWaControlsComponent::on_draw_language() {
    const auto options=wuwa_l10n::languages();
    const auto selected=wuwa_l10n::language();
    const auto found=std::find_if(options.begin(),options.end(),[&](const auto& x){return x.id==selected;});
    const std::string preview=found==options.end() ? "English" : found->name+" ("+found->id+")";
    // Always-readable label provides a recovery path after an accidental choice.
    if(ImGui::BeginCombo("Language / WuWa",preview.c_str())) {
        for(const auto& option:options) {
            const std::string name=option.name+" ("+option.id+")";
            if(ImGui::Selectable(name.c_str(),option.id==selected)) {
                m_language->value()=option.id;
                wuwa_l10n::request(option.id,Framework::get_persistent_dir("wuwa-languages"));
            }
        }
        ImGui::EndCombo();
    }
    wuwa_ui::TextWrapped("WuWa controls and shortcut pages only. Base UEVR, game text and some technical help remain English. Translations are community-editable drafts.");
    if(wuwa_ui::TreeNode("Edit translations")) {
        wuwa_ui::TextWrapped("Edit the UTF-8 JSON files in your game profile's wuwa-languages folder. Missing entries fall back to English. Reload applies changes without restarting the game.");
        if(wuwa_ui::Button("Reload language files"))
            wuwa_l10n::request(m_language->value(),Framework::get_persistent_dir("wuwa-languages"),true);
        const auto state=wuwa_l10n::status();
        if(!state.empty()) wuwa_ui::TextWrapped("%s",state.c_str());
        ImGui::TreePop();
    }
}

void WuWaControlsComponent::on_draw_recovery() {
    if (m_adjust.value() || mode_warning_active()) {
        wuwa_ui::TextColored(ImVec4{1.0f,0.77f,0.24f,1.0f},"HUD / mouse mode is ON: gameplay buttons are redirected");
        if (wuwa_ui::Button("Exit mouse mode now")) {
            m_adjust.value()=false; m_auto_mouse->value()=false;
            release_input(); m_input_armed=false;
        }
        wuwa_ui::TextWrapped("L3 + LB exits manual adjustment. Release all controls afterwards.");
    }
    if (!VR::get()->is_gui_enabled()) {
        wuwa_ui::TextColored(ImVec4{1.0f,0.25f,0.25f,1.0f},"GAME UI IS HIDDEN - menus are hidden too");
        if (wuwa_ui::Button("Show game UI now"))
            if (const auto value=VR::get()->get_value("VR_EnableGUI")) value->set("true");
    }
    if (wuwa_ui::Button("Reset HUD aspect")) {
        m_hud_aspect_status.set("Refresh queued; requires the supplied Comfort script.");
        m_hud_aspect_request.set("true");
    }
    wuwa_ui::TextWrapped("Requests a HUD layout refresh without opening ESC. If this game version is unsupported, the status explains why; saved HUD size and position stay unchanged.");
    wuwa_ui::TextWrapped("%s", m_hud_aspect_status.get().c_str());
    if (wuwa_ui::TreeNode("Restore profile settings")) {
        wuwa_ui::TextWrapped("Restore this build's supplied controls, first-person/freecam settings, camera scale, aiming and HUD layout. Rendering and runtime selection are preserved. Temporary HUD/mouse mode is always turned off. You can undo the reset below.");
        const auto reset=[&](const wuwa_controls::Settings& settings,const char* message) {
            const auto before=control_settings();
            try {
                apply_control_settings(settings); m_recovery.remember_before_reset(before); m_recovery_status=message;
            } catch (const std::exception& e) { m_recovery_status=e.what(); }
        };
        ImGui::BeginDisabled(m_recovery.supplied().empty());
        if (wuwa_ui::Button("Restore supplied profile controls")) reset(m_recovery.supplied(),"Supplied profile restored. Release all controls before continuing.");
        ImGui::EndDisabled();
        if (m_recovery.supplied().empty()) wuwa_ui::TextWrapped("This older profile has no supplied-defaults file. Reset this build in the launcher while the game is closed, or use this launch's settings below.");
        ImGui::BeginDisabled(m_recovery.launch().empty());
        if (wuwa_ui::Button("Restore controls from this launch")) reset(m_recovery.launch(),"Launch settings restored. Release all controls before continuing.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!m_recovery.can_undo());
        if (wuwa_ui::Button("Undo last controls reset")) {
            try { apply_control_settings(m_recovery.undo()); m_recovery.finish_undo(); m_recovery_status="Settings from before the reset restored; mouse mode remains off."; }
            catch (const std::exception& e) { m_recovery_status=e.what(); }
        }
        ImGui::EndDisabled();
        wuwa_ui::TextWrapped("Launch settings include any custom settings saved before starting this game. Supplied profile controls are the build's original choices, not UEVR factory defaults.");
        if (!m_recovery_status.empty()) wuwa_ui::TextWrapped("%s",m_recovery_status.c_str());
        ImGui::TreePop();
    }
}

bool WuWaControlsComponent::game_focused() {
    return wuwa_test::is_wuwa() && g_framework->get_window() != nullptr &&
        GetForegroundWindow() == g_framework->get_window() && !g_framework->is_drawing_ui();
}

void WuWaControlsComponent::on_draw_shortcuts() {
    wuwa_ui::draw(*m_enabled,"Enable WuWa controller shortcuts");
    wuwa_ui::draw(*m_mouse,"Enable controller mouse shortcuts");
    wuwa_ui::TextWrapped("Close UEVR before using these shortcuts. L3/R3 mean clicking the sticks; View is the two-squares button and Menu is the three-lines button.");
    wuwa_ui::TextWrapped("PlayStation: L1/R1 = LB/RB, L2/R2 = LT/RT, Cross/Circle/Square/Triangle = A/B/X/Y, Share/Create = View, Options = Menu.");
    wuwa_ui::TextWrapped("%s", m_playstation_status.get().c_str());
    wuwa_ui::TextWrapped("Direct PlayStation shortcuts are experimental. The game still receives their buttons. Steam Input is preferred when available; release all controls after changing input sources.");
    if (ImGui::BeginTable("Everyday controller shortcuts", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        wuwa_ui::TableSetupColumn("Buttons", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        wuwa_ui::TableSetupColumn("Action");
        for (const auto& row : std::array<std::array<const char*,2>,12>{{
            {{"L3 + R3", "Open / close UEVR settings"}},
            {{"L3 + B", "Show / hide game HUD and menus"}},
            {{"L3 + A", "Recenter headset / portal"}},
            {{"L3 + LT / F7", "Portal on / off"}},
            {{"L3 + RT", "Diorama on / off (10x)"}},
            {{"LT + RT, then click L3", "Stereo screen on / off"}},
            {{"LT + RT, then click R3", "Mono theatre on / off (no stereo depth)"}},
            {{"L3 + Menu", "Show / hide shortcut sheet"}},
            {{"L3 + LB, release", "Toggle HUD / mouse adjustment"}},
            {{"L3 + View", "First person on / off"}},
            {{"Double R3", "Freecam on / off"}},
            {{"L3 + RB", "Game / fixed camera"}},
        }}) {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); wuwa_ui::TextUnformatted(row[0]);
            ImGui::TableNextColumn(); wuwa_ui::TextWrapped("%s", row[1]);
        }
        ImGui::EndTable();
    }
    if (wuwa_ui::TreeNode("How to use view toggles")) {
        wuwa_ui::TextWrapped("Hold L3, then fully squeeze LT for the portal or RT for diorama. Keep the other trigger released; release all controls before repeating. Close UEVR and game menus and leave HUD/mouse adjustment first. Physical gamepad passthrough bypasses these shortcuts.");
        wuwa_ui::TextWrapped("Fully hold both triggers first, then click L3 for the stereo screen or R3 for mono theatre with no stereo depth. Release all controls and center the sticks before repeating. Available during dialogue; close UEVR and leave HUD/mouse adjustment first.");
        wuwa_ui::TextWrapped("Mono preserves your saved stereo-screen choice; screen off exits both screen modes. Neither changes portal or normal world scale. Manual toggles take priority over automatic cinema for the current scene.");
        wuwa_ui::TextWrapped("Diorama uses a temporary 10x scale with Native Stereo, with the portal on or off. Turning it off returns to your normal saved scale, including deliberate scale edits. It starts off each launch, settings reload and runtime reinitialization. Head movement is magnified; L3 + A recenters.");
        ImGui::TreePop();
    }
    wuwa_ui::draw(*m_sheet,"Show shortcut sheet");
    wuwa_ui::draw(*m_sheet_page,"Shortcut sheet page");
    wuwa_ui::TextWrapped("With the sheet visible, L3 + D-pad left/right changes page; L3 + D-pad up returns to automatic. L3 + Menu hides the sheet.");
    if (wuwa_ui::Button("Bring sheet in front")) { m_sheet->value() = true; m_sheet_position->value() = 1; }
    ImGui::SameLine();
    if (wuwa_ui::Button("Place sheet at feet")) { m_sheet->value() = true; m_sheet_position->value() = 0; }
    wuwa_ui::TextWrapped("Close UEVR settings to see the sheet in the headset or simulator. It is absent from the game's flat spectator view. At-feet placement uses your recentered headset origin, not the character's feet.");
    if (FrameworkConfig::get()->is_always_show_cursor())
        wuwa_ui::TextWrapped("Sheet is blocked by FrameworkConfig > Always Show Cursor. Turn that off to display it.");
    if (wuwa_ui::TreeNode("Shortcut sheet placement")) {
        wuwa_ui::draw(*m_sheet_position,"Sheet position");
        wuwa_ui::draw(*m_sheet_width,"Sheet width (meters)");
        if (m_sheet_position->value() == 0) {
            wuwa_ui::draw(*m_sheet_drop,"Below standing origin (meters)");
            wuwa_ui::draw(*m_sheet_forward,"Forward of standing origin (meters)");
            wuwa_ui::draw(*m_sheet_tilt,"Tilt toward feet (degrees)");
        } else {
            wuwa_ui::TextWrapped("Front placement is two meters ahead of the recentered headset origin. Width still adjusts its size.");
        }
        wuwa_ui::TextWrapped("The sheet stays at your recentered origin, including in HUD/mouse mode. UEVR settings and the hidden-UI recovery warning take priority. Only its L3 + D-pad page shortcuts consume input while visible.");
        ImGui::TreePop();
    }
    if (wuwa_ui::TreeNode("All shortcut contexts")) {
        for (const auto& row : sheet) { wuwa_ui::TextUnformatted(row[0]); wuwa_ui::TextUnformatted(row[1]); }
        ImGui::TreePop();
    }
}

void WuWaControlsComponent::on_draw_experiments() {
        if (wuwa_ui::TreeNode("Record camera and Xbox diagnostics")) {
            if (wuwa_ui::Button("Start motion sidecar (5 minutes max)")) {
                try { wuwa_motion::start(Framework::get_persistent_dir(),300); }
                catch (const std::exception& e) { spdlog::warn("[WuWaMotion] {}",e.what()); }
            }
            ImGui::SameLine();
            if (wuwa_ui::Button("Stop motion sidecar")) wuwa_motion::stop();
            const auto state=wuwa_motion::status();
            wuwa_ui::Text("%s: %u samples",state["active"].get<bool>()?"Recording":"Stopped",state["rows"].get<uint32_t>());
            wuwa_ui::TextWrapped("%s",state["path"].get<std::string>().c_str());
            wuwa_ui::TextWrapped("Local controller and camera data only, about 30 samples/sec. The clean-video recorder starts this automatically. It never presses buttons. Video/sidecar alignment uses timestamps, not a claim of GPU-frame synchronization.");
            ImGui::TreePop();
        }
        wuwa_ui::draw(*m_free_collision,"Freecam collision (experimental)");
        if (m_free_collision->value()) {
            wuwa_ui::draw(*m_collision_radius,"Camera collision radius (game units)");
            wuwa_ui::draw(*m_collision_complex,"Trace mesh triangles when supported");
            wuwa_ui::TextWrapped("Sweeps the camera against surfaces that block the game's Visibility trace. Slides along contact surfaces. Unsupported queries hold movement and report a reason below. Physical headset leaning is not constrained. Starts off; world geometry still needs live verification.");
        }
        if (wuwa_ui::TreeNode("Legacy stereo comparison bench (60-second tests)")) {
            wuwa_ui::TextWrapped("These old comparison tests are separate from the normal far-object repair. Some deliberately invert or duplicate eye data; they are not recommended player settings.");
            // Diagnostic windows for the far-foliage freeze in one eye. Each button opens a
            // 60 s window that expires by itself; nothing is saved to the profile. Verdicts
            // go to the log with the active mode so the session can be read back afterwards.
            static constexpr std::array<const char*, 3> labels{
                "Swap the two eye states",
                "Both eyes use the right eye's state (views[0])",
                "Both eyes use the left eye's state (views[1])"};
            const bool busy = wuwa_shadow::test_active() || wuwa_stereo_order::active(GetTickCount64());
            const bool can_start = wuwa_shadow::ready() && !wuwa_shadow::faulted.load() && !busy &&
                !wuwa_shadow::state_swap_active() && !wuwa_shadow::target_swap_active() && !wuwa_shadow::construct_active();
            ImGui::BeginDisabled(!can_start);
            for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
                if (wuwa_ui::Button(labels[i])) {
                    wuwa_shadow::set_state_swap(60, i);
                    spdlog::info("[WuWaBench] start mode={} (menu)", wuwa_shadow::swap_mode_names[i]);
                }
            }
            static constexpr std::array<const char*, 4> construct_labels{
                "Construct left view as primary (pass 2)",
                "Construct left view as primary, family hidden",
                "Construct left view with family hidden",
                "Construct left view with eye index 0"};
            for (int i = 0; i < 4; ++i) {
                if (wuwa_ui::Button(construct_labels[i])) {
                    wuwa_shadow::set_construct(60, i + 1);
                    spdlog::info("[WuWaBench] start construct mode={} (menu)", wuwa_shadow::construct_mode_names[i + 1]);
                }
            }
            if (wuwa_ui::Button("Sync left eye LOD to right eye (fix candidate)")) {
                wuwa_shadow::set_lod_sync(60);
                spdlog::info("[WuWaBench] start lod_sync (menu)");
            }
            if (wuwa_ui::Button("Build left view like the first eye (K5)")) {
                if (wuwa_second_eye::set_window(60)) spdlog::info("[WuWaBench] start second_eye (menu)");
            }
            if (wuwa_ui::Button("Swap the eyes' poses and projections (stereo looks inverted)")) {
                wuwa_shadow::set_eye_swap(60);
                spdlog::info("[WuWaBench] start eye_swap (menu)");
            }
            if (wuwa_ui::Button("Swap the eyes' render targets (stereo looks inverted)")) {
                wuwa_shadow::set_target_swap(60);
                spdlog::info("[WuWaBench] start target_swap (menu)");
            }
            ImGui::EndDisabled();
            const bool target_active = wuwa_shadow::target_swap_active() || wuwa_shadow::construct_active() != 0 ||
                wuwa_shadow::eye_swap_active();
            const bool active = wuwa_shadow::state_swap_active() || target_active;
            if (active && wuwa_ui::Button("Stop now")) {
                wuwa_shadow::set_state_swap(0);
                wuwa_shadow::set_target_swap(0);
                wuwa_shadow::set_construct(0, 0);
                wuwa_shadow::set_eye_swap(0);
                wuwa_second_eye::set_window(0);
                wuwa_shadow::set_lod_sync(0);
                spdlog::info("[WuWaBench] stopped (menu)");
            }
            const auto mode = wuwa_shadow::construct_active() ? wuwa_shadow::construct_mode_names[static_cast<size_t>(wuwa_shadow::construct_active())] :
                wuwa_shadow::eye_swap_active() ? "eye_swap" :
                wuwa_shadow::target_swap_active() ? "target_swap" :
                wuwa_shadow::swap_mode_names[static_cast<size_t>(wuwa_shadow::swap_mode.load())];
            if (target_active) wuwa_ui::Text("Active: %s (targets swapped %llu, constructed %llu)", mode,
                static_cast<unsigned long long>(wuwa_shadow::target_swap_applied.load()),
                static_cast<unsigned long long>(wuwa_shadow::construct_applied.load()));
            else if (active) wuwa_ui::Text("Active: %s, %llu s left, applied %llu, restored %llu", mode,
                static_cast<unsigned long long>(wuwa_shadow::state_swap_remaining_ms() / 1000),
                static_cast<unsigned long long>(wuwa_shadow::swap_applied.load()),
                static_cast<unsigned long long>(wuwa_shadow::swap_restored.load()));
            else if (wuwa_shadow::faulted.load()) wuwa_ui::TextWrapped("Stopped: a write or restore failed. Restart the game before trying again.");
            else if (!wuwa_shadow::ready()) wuwa_ui::TextWrapped("Unavailable: the eye pair is not verified yet (or this game build is not supported).");
            else if (busy) wuwa_ui::TextWrapped("Unavailable while another graphics test runs.");
            else wuwa_ui::Text("Idle");
            static constexpr std::array<const char*, 4> verdicts{
                "Far tree: both eyes sway", "Far tree: left frozen", "Far tree: right frozen", "Visual problem (note it)"};
            for (size_t i = 0; i < verdicts.size(); ++i) {
                if (i) ImGui::SameLine();
                if (wuwa_ui::Button(verdicts[i])) {
                    spdlog::info("[WuWaBench] verdict=\"{}\" mode={} active={} remaining_ms={} applied={} restored={} skipped={} target_swaps={}",
                        verdicts[i], mode, active, wuwa_shadow::state_swap_remaining_ms(), wuwa_shadow::swap_applied.load(),
                        wuwa_shadow::swap_restored.load(), wuwa_shadow::swap_skipped.load(), wuwa_shadow::target_swap_applied.load());
                }
            }
            wuwa_ui::TextWrapped("Stand where the far tree freezes, start a window, close the menu and look, then press what you saw. Each start or stop may cause a one-frame blur.");
            ImGui::TreePop();
        }
    wuwa_ui::draw(*m_auto_mouse,"Automatically use mouse in game menus (legacy)");
    wuwa_ui::TextWrapped("Legacy automatic mouse can intercept game menu buttons. Manual L3 + LB adjustment remains the normal choice.");
}

void WuWaControlsComponent::on_draw_recording() {
    const auto profile=Framework::get_persistent_dir();
    m_video.poll(profile);
    const auto state=m_video.state();
    if (!m_video.connected())
        wuwa_ui::TextWrapped("Keep WuWa VR Launcher running to record from here. Its browser tab can be closed.");
    else if (!m_video.available())
        wuwa_ui::TextWrapped("The recorder is missing from this launcher package.");
    else if (state=="starting") wuwa_ui::TextWrapped("Starting the recorder. Close the UEVR menu for a clear view.");
    else if (state=="recording") wuwa_ui::TextWrapped("Recording video. Close the UEVR menu for a clear view.");
    else if (state=="finishing") wuwa_ui::TextWrapped("Finishing the video. Please wait.");
    else if (state=="saved") wuwa_ui::TextWrapped("Recording saved. Use Open recordings in the launcher.");
    else if (state=="busy") wuwa_ui::TextWrapped("Wait for the launcher operation to finish.");
    else if (state=="error") wuwa_ui::TextWrapped("Recording failed. See the message below.");
    const bool busy=state=="starting" || state=="recording" || state=="finishing" || state=="busy";
    ImGui::BeginDisabled(busy || m_video.pending());
    wuwa_ui::draw(*m_video_fps,"Target recording rate");
    wuwa_ui::draw(*m_video_width,"Maximum pixels per eye");
    wuwa_ui::draw(*m_video_telemetry,"Include camera and controller data");
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!m_video.available() || busy || m_video.pending());
    if (wuwa_ui::Button("Start video recording")) {
        constexpr int rates[]{30,45,60},widths[]{720,1024,1280};
        m_video.submit(profile,true,rates[std::clamp(m_video_fps->value(),0,2)],
                      widths[std::clamp(m_video_width->value(),0,2)],m_video_telemetry->value());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_video.available() || (state!="starting" && state!="recording") || m_video.pending());
    if (wuwa_ui::Button("Stop video recording")) m_video.submit(profile,false);
    ImGui::EndDisabled();
    if (m_video.pending()) wuwa_ui::TextWrapped("Waiting for the launcher...");
    const auto message=m_video.message();
    if (!message.empty()) wuwa_ui::TextWrapped("%s",message.c_str());
    wuwa_ui::TextWrapped("SteamVR and the updated OpenXR Simulator are supported. VDXR capture is not available yet. Video has no audio and stops after five minutes. Actual frame rate depends on the game.");
    wuwa_ui::TextWrapped("Simulator: keep its preview open, choose Both eyes / side-by-side, and turn Full render off. Recording uses the preview size.");
    ImGui::Separator();
    wuwa_ui::draw(*m_steady_desktop,"Steady desktop view (for OBS and streaming)");
    ImGui::BeginDisabled(!m_steady_desktop->value());
    wuwa_ui::draw(*m_steady_desktop_seconds,"Steadiness (seconds of smoothing)");
    ImGui::EndDisabled();
    wuwa_ui::TextWrapped("Smooths head shake out of the game window, so OBS records a calm, full-resolution view with game audio. The headset image is not changed. The window shows a slightly narrower view so the picture can move inside it.");
    ImGui::Separator();
    const bool run_active = wuwa_run::active.load();
    if (wuwa_ui::Button(run_active ? "End run" : "Start run")) {
        if (!wuwa_run::mark(profile, !run_active)) wuwa_ui::TextWrapped("Could not write the run marker file.");
    }
    const auto since_press = wuwa_run::unix_ms() - wuwa_run::last_ms.load();
    if (wuwa_run::last_ms.load() != 0 && since_press < 20000) {
        wuwa_ui::TextWrapped("%s", run_active ? "Run started. Now open the in-game map for 3 seconds."
                                              : "Run ended. Now open the in-game map, zoomed out, for 3 seconds.");
    } else if (run_active) {
        wuwa_ui::TextWrapped("Run in progress: %d min.", static_cast<int>((wuwa_run::unix_ms() - wuwa_run::started_ms.load()) / 60000));
    }
    wuwa_ui::TextWrapped("Exercise recordings: start OBS (F9), press Start run, open the in-game map for 3 seconds. At the end: End run, open the map zoomed out, then stop OBS. Each press flashes a magenta square in the desktop view so the video lines up with your route log.");
    ImGui::Separator();
    wuwa_ui::draw(*m_privacy,"Streamer privacy: cover player IDs");
    if (m_privacy->value()) {
        wuwa_ui::TextWrapped("Black boxes cover the bottom-right UID and the ESC profile ID row in the extracted game UI, including its VR/portal and spectator copies. Check a short recording before sharing: other layouts, names, chat and diagnostics are not anonymized.");
        if (wuwa_ui::TreeNode("Privacy box placement")) {
            wuwa_ui::draw(*m_privacy_profile,"Also cover the ESC profile ID row");
            wuwa_ui::draw(*m_privacy_profile_scope,"When to show the profile ID box");
            wuwa_ui::TextWrapped("The top-left box normally follows the ESC/overlay menu render path, including gamepad menus. Other popups using that same path may also show it. The bottom-right UID box stays on. Check your recording; unknown menu layouts are not guaranteed to be covered. Always is a manual fallback.");
            wuwa_ui::draw(*m_uid_left,"UID left"); wuwa_ui::draw(*m_uid_top,"UID top");
            wuwa_ui::draw(*m_uid_right,"UID right"); wuwa_ui::draw(*m_uid_bottom,"UID bottom");
            wuwa_ui::draw(*m_id_left,"Profile ID left"); wuwa_ui::draw(*m_id_top,"Profile ID top");
            wuwa_ui::draw(*m_id_right,"Profile ID right"); wuwa_ui::draw(*m_id_bottom,"Profile ID bottom");
            ImGui::TreePop();
        }
    }
}

void WuWaControlsComponent::on_draw_ui() {
    wuwa_ui::draw(*m_camera,"Camera view");
    if (m_camera->value()==2 && wuwa_ui::Button("Leave freecam")) m_camera->value()=0;
    wuwa_ui::draw(*m_walk,"Walk by default; hold RB for normal speed");
    wuwa_ui::draw(*m_recenter_position,"L3 + A also resets headset position (seated)");
    if (wuwa_ui::Button("Reset headset position and direction now")) recenter(true);
    wuwa_ui::TextWrapped("Recenter keeps world scale and camera offsets. Simulator Home resets only the simulator, not the game's VR origin.");
    ImGui::BeginDisabled(!m_mouse->value());
    wuwa_ui::draw(m_adjust,"HUD / mouse adjustment ON (L3 + LB toggles)");
    ImGui::EndDisabled();
    if (wuwa_ui::TreeNode("HUD and focus comfort")) {
        wuwa_ui::draw(*m_keep_camera,"Keep camera and head hiding during Alt-Tab / UEVR settings");
        wuwa_ui::TextWrapped("Input still pauses when WuWa loses focus. Real game menus temporarily restore the game camera and character visibility.");
        wuwa_ui::draw(*m_warn_hidden_ui,"Warn when a menu opens with game UI hidden");
        wuwa_ui::TextWrapped("Hidden UI stays quiet during normal gameplay. A recovery notice appears only when the game cursor or a known menu-rendering path indicates a menu. L3 + B restores UI. An unknown menu may not be detected; UEVR settings always retain the Show game UI button.");
        wuwa_ui::TextWrapped("Close UEVR, press L3 + LB once, then release the controller. Left stick moves the cursor; A clicks, B goes back, X + stick scrolls. LT/RT moves the HUD nearer/farther; LB/RB lowers/raises it. L3 + LB again returns to normal controls. Adjustment pauses freecam flight and starts off each launch.");
        wuwa_ui::TextWrapped("Settings: D-pad selects, A activates, left stick scrolls the focused pane. Mouse wheel also scrolls. Release RT first: holding it adjusts the camera instead.");
        ImGui::TreePop();
    }
    if (wuwa_ui::TreeNode("Camera customization")) {
        wuwa_ui::draw(*m_fixed_distance,"Fixed camera distance (game units)");
        wuwa_ui::draw(*m_fixed_height,"Fixed camera height above pawn (game units)");
        wuwa_ui::draw(*m_free_speed,"Freecam movement speed");
        wuwa_ui::draw(*m_free_turn,"Freecam turning speed");
        wuwa_ui::draw(*m_free_style,"Freecam movement style");
        if (m_free_style->value()==1) {
            wuwa_ui::draw(*m_drone_response,"Drone response (higher = less drift)");
            wuwa_ui::TextWrapped("Hover drone: left stick moves horizontally, right stick looks, LT rises, LB descends, RT boosts. Release the stick to settle into a hover.");
        } else if (m_free_style->value()==2) {
            wuwa_ui::draw(*m_plane_speed,"Plane cruise speed (game units / second)");
            wuwa_ui::TextWrapped("Plane: right stick pitches/banks, left stick X steers yaw. RT increases cruise speed, LT decreases it. Hold LB to brake, RB to boost. Double R3 exits. Cruise pauses when focus or controller samples are lost; release controls then move a stick to rearm after Alt-Tab.");
        } else if (m_free_style->value()==3) {
            wuwa_ui::TextWrapped("Acro: right stick controls roll/pitch rates; left stick X controls yaw. Stick forward pitches down. Centered sticks stop rotation without levelling. Gravity, thrust and momentum keep acting while armed. Full camera roll is part of this mode.");
            wuwa_ui::draw(*m_acro_throttle,"Acro throttle control");
            wuwa_ui::TextWrapped("Release RT (Xbox) or pull left stick fully down (Mode 2), then tap RB to arm. Tap RB again or hold LB to pause/disarm. LB + RB levels the drone and stops it. Double R3 exits. Focus loss, menus and shortcut chords pause/disarm it; arm again to resume.");
            if (m_acro_throttle->value()==1)
                wuwa_ui::TextWrapped("Mode 2: centered left stick means 50%% throttle. An Xbox stick springs back to center; hold it down for zero throttle.");
            wuwa_ui::draw(*m_acro_rate,"Pitch / roll rate (degrees / second)");
            wuwa_ui::draw(*m_acro_yaw_rate,"Yaw rate (degrees / second)");
            wuwa_ui::draw(*m_acro_expo,"Stick expo (softens center)");
            wuwa_ui::draw(*m_acro_thrust,"Maximum thrust / weight");
            wuwa_ui::draw(*m_acro_drag,"Air drag (per second)");
            wuwa_ui::draw(*m_acro_tilt,"FPV camera upward tilt (degrees)");
            wuwa_ui::draw(*m_acro_invert_pitch,"Invert acro pitch stick");
        } else wuwa_ui::TextWrapped("Polar fly: left stick moves along view, right stick looks. LT rises, LB descends; RT boosts, double RT gives turbo.");
        if (m_free_style->value()!=3) wuwa_ui::draw(*m_flight_roll,"Show hover-drone / plane camera roll");
        wuwa_ui::TextWrapped("These modes move the viewpoint; the character stays put. Acro is a camera flight model, without motor, propeller or battery simulation.");
        ImGui::TreePop();
    }
    if (wuwa_ui::TreeNode("Advanced rendering and script diagnostics")) {
        wuwa_ui::draw(*m_sync_eye_lod,"Keep matched far-object detail (recommended)");
        wuwa_ui::TextWrapped("Keeps distant trees and props on the same level of detail in both eyes. This is the normal foliage repair; legacy comparison tests are separate.");
        wuwa_ui::draw(*m_refill_far_lighting,"Refresh far-lighting cache (limited workaround)");
        if (wuwa_ui::Button("Refill far lighting now")) wuwa_clv::request();
        wuwa_ui::TextWrapped("Requests a bounded cascade-lighting-volume refresh after stereo starts or resumes, after detected teleports, or on request. It helped the tested ship/wheel lighting case; it is not a general fix for dark objects or character rims. Each refill can cause a short hitch.");
        wuwa_ui::draw(m_suppress_npc_rim,"Hide character rim lighting (optional workaround)");
        wuwa_ui::TextWrapped("Optional workaround, off by default. Removes toon-depth rim lighting from all characters using it, including nearby characters. This hides the observed extra eye contour; it does not repair the underlying stereo cause. Restores the prior value when disabled, in 2D screen mode, or when native VR is inactive.");
        const auto rim_status = wuwa_rim::status();
        wuwa_ui::TextWrapped("%s",wuwa_rim::message(rim_status.code));
        std::scoped_lock lock{m_bridge_mutex};
        wuwa_ui::TextWrapped("%s", m_script_status.c_str());
        ImGui::TreePop();
    }
}

void WuWaControlsComponent::on_draw_first_person() {
        if (wuwa_ui::Button(m_camera->value()==3 ? "Leave first person" : "Enter first person"))
            m_camera->value()=m_camera->value()==3 ? 0 : 3;
        wuwa_ui::TextWrapped("L3 + View toggles first person. These settings apply when first person is active.");
        wuwa_ui::draw(*m_fp_motion,"First person motion");
        wuwa_ui::draw(*m_mesh,"First person character visibility");
        if (wuwa_ui::TreeNode("First-person position")) {
            wuwa_ui::draw(*m_fp_forward,"First person forward offset");
            wuwa_ui::draw(*m_fp_right,"First person right offset");
            wuwa_ui::draw(*m_fp_up,"First person height offset");
            wuwa_ui::TextWrapped("While playing in first person, hold L3 + Y to raise eye level or L3 + X to lower it. This lets you see the result with this menu closed.");
            if (wuwa_ui::Button("Lower eye level by 5")) m_fp_up->value() = (std::max)(-100.0f, m_fp_up->value() - 5.0f);
            ImGui::SameLine();
            if (wuwa_ui::Button("Raise eye level by 5")) m_fp_up->value() = (std::min)(100.0f, m_fp_up->value() + 5.0f);
            ImGui::TreePop();
        }
        if (m_fp_motion->value()==3 && (VR::get()->get_aim_method()!=VR::AimMethod::GAME || VR::get()->is_decoupled_pitch_enabled())) {
            wuwa_ui::TextWrapped("Full animation is paused: headset/controller aim or Decoupled Pitch conflicts with animated head turning. Game-view rotation is being used; head position still follows the character.");
            if (wuwa_ui::Button("Use game aim for full animation")) {
                VR::get()->set_aim_method(VR::AimMethod::GAME);
                if (const auto value=VR::get()->get_value("VR_AimModifyPlayerControlRotation")) value->set("false");
                VR::get()->set_decoupled_pitch(false);
            }
        }
        if (m_fp_motion->value()==3)
            wuwa_ui::TextWrapped("Full animation follows character turns and can be intense. L3 + D-pad Down returns to your previous motion choice.");
        if (wuwa_ui::TreeNode("Animation comfort")) {
            wuwa_ui::TextWrapped("L3 + D-pad Down toggles full animation follow and your previous first-person motion. Custom keeps your saved settings. The other choices sample the head after animation. Animated position preserves normal stick aiming. Full animation follows character turns and bone rotation; it can be intense.");
            int stick_choice=m_fp_look->value()==2 ? 1 : 0;
            const char* choices[]{"Exact animation", "Use game view while either stick moves"};
            if (wuwa_ui::Combo("Full animation: stick override",&stick_choice,choices,2)) m_fp_look->value()=stick_choice==1 ? 2 : 0;
            if (m_fp_look->value()==1) wuwa_ui::TextWrapped("Legacy pitch-only override is active; choose either option above to replace it.");
            ImGui::BeginDisabled(m_fp_look->value()!=2);
            wuwa_ui::draw(*m_fp_smooth,"Smooth transition between animation and game view");
            if (m_fp_smooth->value()) wuwa_ui::draw(*m_fp_blend_time,"Transition duration (seconds)");
            ImGui::EndDisabled();
            wuwa_ui::TextWrapped("Smoothing blends stick handovers and reported movement-state changes, such as takeoff/landing. It starts enabled on the supplied profile. Head position stays current; ordinary aiming and animation are immediate after the handover. Off restores the instant switch.");
            wuwa_ui::TextWrapped("Game-view override shows the game's camera direction while either stick is used, then returns to animation 0.4 seconds after release. Full animation and headset view can still disagree with movement or target selection; aim alignment remains under investigation. These options never rotate or move the character for you.");
            if (wuwa_ui::TreeNode("Custom motion settings")) {
                ImGui::BeginDisabled(m_fp_motion->value()!=0);
                wuwa_ui::draw(*m_fp_animation,"Follow animated head / neck position (Custom)");
                wuwa_ui::draw(*m_fp_horizon,"Keep first person horizon level (Custom)");
                wuwa_ui::draw(*m_fp_late,"Refresh position before drawing (Custom)");
                ImGui::EndDisabled();
                wuwa_ui::TextWrapped("Horizon level removes the game's camera pitch from the VR view. Turn it off for right-stick up/down look, and disable UEVR's Decoupled Pitch below. Head aiming is an alternative; grapple selection still needs game testing.");
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        if (wuwa_ui::TreeNode("Aiming and right-stick pitch")) {
            wuwa_ui::draw(*m_fp_look,"All stick overrides (includes legacy pitch-only)");
            wuwa_ui::TextWrapped("These are UEVR's existing global aim settings, shared with VR > Input. They are not limited to first person.");
            for (const auto& [key,label] : {std::pair{"VR_AimMethod","Aim direction"},
                    std::pair{"VR_AimModifyPlayerControlRotation","Send aim to player control rotation"},
                    std::pair{"VR_DecoupledPitch","Decouple game camera pitch"}}) {
                if (const auto value=VR::get()->get_value(key)) wuwa_ui::draw(*value,label);
            }
            if (wuwa_ui::Button("Try headset aim")) {
                VR::get()->set_aim_method(VR::AimMethod::HEAD);
                if (const auto value=VR::get()->get_value("VR_AimModifyPlayerControlRotation")) value->set("true");
            }
            ImGui::SameLine();
            if (wuwa_ui::Button("Use right-stick pitch")) {
                VR::get()->set_aim_method(VR::AimMethod::GAME);
                if (const auto value=VR::get()->get_value("VR_AimModifyPlayerControlRotation")) value->set("false");
                VR::get()->set_decoupled_pitch(false);
                m_fp_horizon->value()=false;
                m_fp_motion->value()=2;
            }
            wuwa_ui::TextWrapped("Headset aim selects Head and sends that direction to the game; the stick still turns. Right-stick pitch selects Game, disables control-rotation override and both pitch locks. Neither changes LB + Y or the selected utility.");
            ImGui::TreePop();
        }
        if (wuwa_ui::TreeNode("Character visibility details")) {
            if (m_mesh->value()==4) wuwa_ui::TextWrapped("Default: keeps the body visible and head bones hidden, with a full shadow copy. Original non-casting/hidden equipment stays excluded. Copies are removed on exit, menus, rig changes or script reset. Unsupported rigs report a fallback below.");
            wuwa_ui::TextWrapped("Animation off: stable height relative to the character root. On: follow the head, or reconstruct it from a visible parent bone when the head is hidden. This follows leaning and sprinting but can add bobbing. Missing rig support falls back to the stable anchor; see the status below.");
            wuwa_ui::TextWrapped("Hide head bones keeps the visible body, but also removes the head from its shadow. Hide body; keep full character shadow requests hidden shadows only from the game's original visible shadow casters; it does not enable unused wings or effect rigs. Shadow support depends on the character/material.");
            wuwa_ui::TextWrapped("A hidden head bone has no usable animated pose: the neck supplies an approximation. For actual head-bone tracking choose Keep entire character visible or Hide body; keep full character shadow. Separate accessories without a head bone may remain visible in head-only mode. Exit and re-enter first person in a neutral pose to recalibrate. Offsets use game units. L3 + A recenters headset/simulator displacement.");
            ImGui::TreePop();
        }
    if (wuwa_ui::TreeNode("First-person script status")) {
        std::scoped_lock lock{m_bridge_mutex};
        wuwa_ui::TextWrapped("%s", m_script_status.c_str());
        ImGui::TreePop();
    }

}

nlohmann::json WuWaControlsComponent::diagnostic_status() {
    // Copy the script's report under the bridge lock; never inspect UObject
    // pointers, change camera state or generate input from this heartbeat.
    nlohmann::json result;
    {
        std::scoped_lock lock{m_bridge_mutex};
        const auto age = m_received == std::chrono::steady_clock::time_point{} ? int64_t{-1}
            : std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_received).count();
        result = {{"script_status", m_script_status}, {"script_age_ms", age},
            {"script_fresh", age >= 0 && age <= 1000}, {"game_menu", m_game_menu},
            {"hud_mouse",m_adjust.value() || m_mouse_state.active}};
    }
    // Developer input leases consume only this bounded native/script status;
    // the XInput callback never locks this component or follows camera objects.
    const auto game_window=g_framework->get_window();
    result["strict_game_foreground"]=game_window && GetForegroundWindow()==game_window;
    result["native_menu"]=wuwa_menu::detected(GetTickCount64(),false);
    result["uevr_menu"]=g_framework->is_drawing_ui();
    result["motion_active"]=VR::get()->is_using_controllers();
    result["passthrough"]=VR::get()->physical_gamepad_passthrough();
    result["slot_filter"]=VR::get()->gamepad_slot_filter();
    result["playstation_shortcuts"]=m_playstation_status.get();
    // "Eligible" is deliberately not a claim that the user can see/read it.
    result["sheet_state"] = !m_sheet->value() ? "off"
        : g_framework->is_drawing_ui() ? "hidden_by_uevr_menu"
        : !VR::get()->is_hmd_active() ? "headset_inactive" : "eligible";
    result["hidden_ui_warning"] = menu_warning_active();
    result["mouse_mode_warning"] = mode_warning_active();
    result["profile_defaults_available"] = !m_recovery.supplied().empty();
    result["streamer_privacy"] = m_privacy->value();
    return result;
}

bool WuWaControlsComponent::menu_warning_active() const {
    if (!wuwa_test::is_wuwa() || !m_warn_hidden_ui->value() || VR::get()->is_gui_enabled()) return false;
    return wuwa_menu::detected(GetTickCount64(),fresh_menu_cursor());
}

bool WuWaControlsComponent::fresh_menu_cursor() const {
    std::scoped_lock lock{m_bridge_mutex};
    return m_game_menu && m_received!=std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now()-m_received<=std::chrono::milliseconds(1000);
}

bool WuWaControlsComponent::menu_warning_visible() const {
    return !g_framework->is_drawing_ui() && VR::get()->is_hmd_active() && menu_warning_active();
}

bool WuWaControlsComponent::mode_warning_active() const {
    if (!wuwa_test::is_wuwa() || !m_enabled->value() || !m_mouse->value() || VR::get()->physical_gamepad_passthrough()) return false;
    if (m_adjust.get()=="true") return true;
    std::scoped_lock lock{m_bridge_mutex};
    return m_auto_mouse->value() && m_mouse_state.active && !m_mouse_state.utility &&
        m_received!=std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now()-m_received<=std::chrono::milliseconds(1000);
}

bool WuWaControlsComponent::status_warning_visible() const {
    // A visible sheet includes both recovery hints. Keep its controls and
    // placement available even when mouse mode and hidden UI are combined.
    return !g_framework->is_drawing_ui() && VR::get()->is_hmd_active() &&
        !floor_visible() && (menu_warning_active() || mode_warning_active());
}

bool WuWaControlsComponent::floor_visible() const {
    return wuwa_test::is_wuwa() && m_sheet->value() && !g_framework->is_drawing_ui() && VR::get()->is_hmd_active();
}

Matrix4x4f WuWaControlsComponent::floor_transform() const {
    auto basis = Matrix4x4f{glm::inverse(VR::get()->get_rotation_offset())};
    if (m_sheet_position->value() == 1) {
        basis[3] = glm::vec4{Vector3f{VR::get()->get_standing_origin()} - Vector3f{basis[2]} * 2.0f, 1.0f};
        return basis;
    }
    const auto center = Vector3f{VR::get()->get_standing_origin()} - Vector3f{basis[2]} * m_sheet_forward->value()
        - Vector3f{0.0f, m_sheet_drop->value(), 0.0f};
    auto transform = basis * glm::rotate(glm::radians(-m_sheet_tilt->value()), Vector3f{1.0f, 0.0f, 0.0f});
    transform[3] = glm::vec4{center, 1.0f};
    return transform;
}

void WuWaControlsComponent::draw_passive_overlay() {
    if (status_warning_visible()) {
        const bool hidden=menu_warning_active();
        const bool shortcut=m_enabled->value() && !VR::get()->physical_gamepad_passthrough();
        if (hidden) wuwa_sheet::draw_hidden_ui_warning(ImGui::GetBackgroundDrawList(),ImGui::GetIO().DisplaySize,ImGui::GetFont(),shortcut,true);
        if (mode_warning_active()) wuwa_sheet::draw_mouse_mode_warning(ImGui::GetBackgroundDrawList(),ImGui::GetIO().DisplaySize,ImGui::GetFont(),m_adjust.value(),hidden);
        return;
    }
    if (!floor_visible()) return;
    int page=m_sheet_page->value()-1;
    if (page<0) {
        std::scoped_lock lock{m_bridge_mutex};
        page=m_adjust.value() || m_mouse_state.active || m_script_status.starts_with("Game menu:") ? 2 : m_camera->value()!=0 ? 1 : 0;
    }
    wuwa_sheet::draw(ImGui::GetBackgroundDrawList(),ImGui::GetIO().DisplaySize,page,ImGui::GetFont(),m_free_style->value(),m_adjust.value(),mode_warning_active(),menu_warning_active());
}

std::array<wuwa_privacy::Rect, 2> WuWaControlsComponent::privacy_rectangles(int32_t width,int32_t height) const {
    if (!wuwa_test::is_wuwa() || !m_privacy->value()) return {};
    return {wuwa_privacy::pixels(m_uid_left->value(),m_uid_top->value(),m_uid_right->value(),m_uid_bottom->value(),width,height),
        m_privacy_profile->value() && wuwa_menu::profile_mask(m_privacy_profile_scope->value(),GetTickCount64(),fresh_menu_cursor()) ?
            wuwa_privacy::pixels(m_id_left->value(),m_id_top->value(),m_id_right->value(),m_id_bottom->value(),width,height) : wuwa_privacy::Rect{}};
}

uint64_t WuWaControlsComponent::pose_recording_session(std::string* recording_id) noexcept {
    if (!wuwa_motion::active() || !wuwa_test::is_wuwa()) return 0;
    try {
        std::unique_lock motion_lock{wuwa_motion::mutex,std::defer_lock};
        std::unique_lock bridge_lock{m_bridge_mutex,std::defer_lock};
        if (std::try_lock(motion_lock,bridge_lock)!=-1) {++m_pose_capture_lock_misses;return 0;}
        if (!wuwa_motion::active()) return 0;
        if (m_pose_recording_id!=wuwa_motion::id) {
            m_pose_recording_id=wuwa_motion::id;
            if (++m_pose_recording_session==0) ++m_pose_recording_session;
            m_recorded_pose_pair={};
            m_next_pose_constructor_emit_ms=0;
        }
        if (recording_id) *recording_id=m_pose_recording_id;
        return m_pose_recording_session;
    } catch (...) { return 0; }
}

wuwa_pose_pair::Pose WuWaControlsComponent::sample_recorded_pose(
    const Rotator<float>* rotation, const Vector3f* position, bool doubles) noexcept {
    wuwa_pose_pair::Pose pose{};
    if (!rotation || !position) return pose;
    // Observation must not turn an unavailable pointer into a recording crash.
    __try {
        if (doubles) {
            const auto* r=reinterpret_cast<const Rotator<double>*>(rotation);
            const auto* p=reinterpret_cast<const Vector3d*>(position);
            pose.rotation={r->pitch,r->yaw,r->roll};pose.position={p->x,p->y,p->z};
        } else {
            pose.rotation={rotation->pitch,rotation->yaw,rotation->roll};
            pose.position={position->x,position->y,position->z};
        }
        pose.valid=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { pose.valid=false; }
    if (!wuwa_pose_pair::finite(pose)) pose.valid=false;
    return pose;
}

void WuWaControlsComponent::record_pose_pair(const wuwa_pose_pair::Pair& pair) noexcept {
    if (!pair.available || !wuwa_motion::active() || !wuwa_test::is_wuwa()) return;
    try {
        std::unique_lock motion_lock{wuwa_motion::mutex,std::defer_lock};
        std::unique_lock bridge_lock{m_bridge_mutex,std::defer_lock};
        if (std::try_lock(motion_lock,bridge_lock)!=-1) {++m_pose_capture_lock_misses;return;}
        if (wuwa_motion::active() && m_pose_recording_id==wuwa_motion::id &&
            pair.scope.recording_session==m_pose_recording_session) m_recorded_pose_pair=pair;
    } catch (...) {}
}

void WuWaControlsComponent::record_rendered_view(int32_t index, const Rotator<float>* rotation, const Vector3f* position, bool doubles) {
    if (!wuwa_motion::active() || !wuwa_test::is_wuwa() || !rotation || !position || index<0 || index>=3) return;
    RecordedView view; view.index=index; view.clock_ms=GetTickCount64();
    if (doubles) {
        const auto r=reinterpret_cast<const Rotator<double>*>(rotation);
        const auto p=reinterpret_cast<const Vector3d*>(position);
        view.rotation={r->pitch,r->yaw,r->roll}; view.position={p->x,p->y,p->z};
    } else {
        view.rotation={rotation->pitch,rotation->yaw,rotation->roll}; view.position={position->x,position->y,position->z};
    }
    for (auto v : view.rotation) if (!std::isfinite(v)) return;
    for (auto v : view.position) if (!std::isfinite(v)) return;
    std::scoped_lock lock{m_bridge_mutex};
    m_recorded_views[index]=view;
}

void WuWaControlsComponent::receive(std::string_view data) {
    if (data.size() > 16384 || !wuwa_test::is_wuwa()) return;
    try {
        const auto value = nlohmann::json::parse(data);
        if (!value.is_object()) return;
        if (value.value("motion_only",false)) {
            if (wuwa_motion::active() && value.contains("motion") && value["motion"].is_object()) {
                std::string recording_id;
                const auto pose_session=pose_recording_session(&recording_id);
                if (!pose_session || recording_id.empty()) return;
                wuwa_pose_pair::Pair pose_pair{};
                bool emit_constructors=false;
                const auto motion_now=GetTickCount64();
                auto motion=value["motion"];
                const auto vr=VR::get();
                motion["world_scale"]=vr->get_world_scale();
                const auto sample=vr->get_head_and_standing_origin();
                motion["head_m"]={sample[0].x,sample[0].y,sample[0].z};
                motion["origin_m"]={sample[1].x,sample[1].y,sample[1].z};
                const auto head_rotation=glm::quat{vr->get_rotation(vr->get_hmd_index())};
                const auto offset=vr->get_rotation_offset();
                motion["head_orientation_xyzw"]={head_rotation.x,head_rotation.y,head_rotation.z,head_rotation.w};
                motion["rotation_offset_xyzw"]={offset.x,offset.y,offset.z,offset.w};
                motion["aim_method"]=static_cast<int>(vr->get_aim_method());
                motion["aim_modifies_control_rotation"]=vr->is_aim_modify_player_control_rotation_enabled();
                motion["rendered_views"]=nlohmann::json::array();
                {
                    std::scoped_lock lock{m_bridge_mutex};
                    for (const auto& view : m_recorded_views) {
                        const auto age=GetTickCount64()-view.clock_ms;
                        if (view.index>=0 && age<=250)
                            motion["rendered_views"].push_back({{"index",view.index},{"position",view.position},
                                {"rotation",view.rotation},{"clock_ms",view.clock_ms},{"age_ms",age}});
                    }
                    if (pose_session && m_recorded_pose_pair.scope.recording_session==pose_session) {
                        pose_pair=m_recorded_pose_pair;
                        if (pose_pair.available && motion_now>=m_next_pose_constructor_emit_ms) {
                            emit_constructors=true;
                        }
                    }
                }
                motion["pose_pair"]=pose_pair_json(pose_pair,motion_now,emit_constructors);
                motion["pose_capture_lock_misses_total"]=m_pose_capture_lock_misses.load();
                const bool written=wuwa_motion::append(std::move(motion),recording_id);
                if (written && emit_constructors) {
                    // Commit the cadence only for an accepted motion row: the
                    // recorder's 33 ms gate may reject this Lua callback.
                    std::scoped_lock lock{m_bridge_mutex};
                    if (m_pose_recording_id==recording_id && m_pose_recording_session==pose_session)
                        m_next_pose_constructor_emit_ms=std::max(m_next_pose_constructor_emit_ms,motion_now+200);
                }
            }
            return;
        }
        MouseState next{};
        next.active = value.value("mouse", false);
        next.utility = value.value("utility", false);
        next.buttons = value.value("buttons", 0) & 0xffff;
        next.x = std::clamp(value.value("x", 0), -32768, 32767);
        next.y = std::clamp(value.value("y", 0), -32768, 32767);
        auto status = value.value("status", std::string{});
        if (status.size() > 512) status.resize(512);
        std::scoped_lock lock{m_bridge_mutex};
        m_mouse_state = next;
        m_game_menu = value.value("menu", false);
        m_recenter_pending |= value.value("recenter", false);
        m_screenshot_pending |= value.value("screenshot", false);
        m_received = std::chrono::steady_clock::now();
        if (!status.empty()) m_script_status = std::move(status);
    } catch (...) {
        std::scoped_lock lock{m_bridge_mutex};
        m_mouse_state = {}; m_recenter_pending = m_screenshot_pending = false;
        m_game_menu = false;
    }
}

void WuWaControlsComponent::release_input() {
    m_mouse_rearm.reset();
    if (m_left_down) mouse(MOUSEEVENTF_LEFTUP);
    constexpr WORD keys[]{'W', 'S', 'A', 'D'};
    for (int i = 0; i < 4; ++i) { if (m_keys[i]) key(keys[i], false); m_keys[i] = false; }
    if (m_tab_down) key(VK_TAB, false);
    m_left_down = m_tab_down = m_utility_held = false;
    m_previous_buttons = 0;
    m_mouse_x = m_mouse_y = m_scroll = 0.0f;
}

std::string WuWaControlsComponent::PlayStationValue::get() const {
    if (status) {
        if (!owner.m_enabled->value()) return "PlayStation shortcuts paused: WuWa shortcuts disabled";
        if (VR::get()->physical_gamepad_passthrough()) return "PlayStation shortcuts paused: controller passthrough enabled";
        if (VR::get()->gamepad_slot_filter() != 0) return "PlayStation shortcuts paused: an XInput slot is selected";
        return owner.m_playstation->status();
    }
    const auto s = owner.m_playstation->snapshot();
    if (!owner.m_enabled->value() || !game_focused() || VR::get()->physical_gamepad_passthrough() ||
        VR::get()->gamepad_slot_filter() != 0 || !owner.m_playstation->active(s, GetTickCount64())) return "unavailable";
    const auto& p = s.pad;
    return "ps-v1," + std::to_string(s.generation) + "," + std::to_string(s.stamp) + "," +
        std::to_string(p.buttons) + "," + std::to_string(p.lt) + "," + std::to_string(p.rt) + "," +
        std::to_string(p.lx) + "," + std::to_string(p.ly) + "," + std::to_string(p.rx) + "," + std::to_string(p.ry);
}

void WuWaControlsComponent::advance_playstation() {
    if (!wuwa_test::is_wuwa()) return;
    if (m_enabled->value()) m_playstation->start();
    const auto s = m_playstation->snapshot(); const auto now = GetTickCount64();
    // Menu closing must work while UEVR itself is open; game_focused() excludes
    // that state intentionally for ordinary gameplay shortcuts.
    const bool window_focused = g_framework->get_window() && GetForegroundWindow() == g_framework->get_window();
    const bool active = m_enabled->value() && window_focused && !VR::get()->physical_gamepad_passthrough() &&
        VR::get()->gamepad_slot_filter() == 0 && m_playstation->active(s, now);
    if (s.generation != m_playstation_generation) { m_playstation_generation = s.generation; m_playstation_menu.reset(); }
    if (!active) {
        m_playstation_menu.reset();
        // Keep the old menu gesture fenced until its physical release. A
        // neutral second Xbox pad must not rearm the newly mapped PS chord.
        if (!wuwa_ps::recent(s.stamp, now, 250) || (s.pad.buttons & 0xc0) != 0xc0)
            wuwa_ps_hid::hid_menu_held.store(false);
        return;
    }
    wuwa_ps_hid::hid_menu_held.store((s.pad.buttons & 0xc0) == 0xc0);
    if (m_playstation_menu.update(s.pad, now, FrameworkConfig::get()->is_enable_l3_r3_toggle(),
        FrameworkConfig::get()->is_l3_r3_long_press() && !g_framework->is_drawing_ui())) {
        g_framework->set_draw_ui(!g_framework->is_drawing_ui());
    }
}

void WuWaControlsComponent::on_frame() {
    advance_playstation();
    if (wuwa_motion::active()) wuwa_test::input_watch_until=GetTickCount64()+1000;
    const auto now = std::chrono::steady_clock::now();
    const float delta = std::clamp(std::chrono::duration<float>(now - m_last_frame).count(), 0.0f, 0.05f);
    m_last_frame = now;
    MouseState state{};
    bool fresh{}, recenter{}, screenshot{};
    {
        std::scoped_lock lock{m_bridge_mutex};
        fresh = now - m_received <= std::chrono::milliseconds(250);
        if (fresh) {
            state = m_mouse_state; recenter = m_recenter_pending; screenshot = m_screenshot_pending;
        }
        m_recenter_pending = m_screenshot_pending = false;
    }
    if (!fresh || !m_enabled->value() || !game_focused() || VR::get()->physical_gamepad_passthrough()) {
        m_input_armed = false;
        release_input(); return;
    }
    if (!m_input_armed) {
        m_input_armed = !state.buttons && !state.utility && std::abs(state.x)<8000 && std::abs(state.y)<8000;
        release_input(); return;
    }
    if (recenter) this->recenter(m_recenter_position->value());
    if (screenshot) {
        key(VK_LWIN,true); key(VK_SNAPSHOT,true); key(VK_SNAPSHOT,false); key(VK_LWIN,false);
    }
    if (!m_mouse->value()) { release_input(); return; }
    if (state.utility) {
        if (!m_utility_held) { m_utility_begin = now; key('V', true); key('V', false); }
        if (!m_tab_down && now - m_utility_begin >= std::chrono::milliseconds(800)) { key(VK_TAB, true); m_tab_down = true; }
    } else if (m_tab_down) { key(VK_TAB, false); m_tab_down = false; }
    m_utility_held = state.utility;
    if (!state.active) {
        m_mouse_rearm.reset();
        if (m_left_down) { mouse(MOUSEEVENTF_LEFTUP); m_left_down = false; }
        constexpr WORD keys[]{'W', 'S', 'A', 'D'};
        for (int i=0; i<4; ++i) { if (m_keys[i]) key(keys[i], false); m_keys[i]=false; }
        m_previous_buttons = 0; m_mouse_x = m_mouse_y = m_scroll = 0; return;
    }
    if (!m_mouse_rearm.accept(static_cast<uint16_t>(state.buttons), state.x, state.y)) return;
    const auto axis = [](int v) { return std::abs(v) < 8000 ? 0.0f : (float)v / 32768.0f; };
    if (state.buttons & XINPUT_GAMEPAD_X) {
        m_scroll += axis(state.y) * delta * 900.0f;
        const auto amount = (int)m_scroll;
        if (amount) { mouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)amount); m_scroll -= (float)amount; }
        m_mouse_x = m_mouse_y = 0;
    } else {
        m_scroll = 0;
        m_mouse_x += axis(state.x) * delta * 900.0f; m_mouse_y -= axis(state.y) * delta * 900.0f;
        const auto x=(int)m_mouse_x, y=(int)m_mouse_y;
        if (x || y) { mouse(MOUSEEVENTF_MOVE, x, y); m_mouse_x-=(float)x; m_mouse_y-=(float)y; }
    }
    const bool click = (state.buttons & XINPUT_GAMEPAD_A) != 0;
    if (click != m_left_down) { mouse(click ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP); m_left_down=click; }
    constexpr WORD buttons[]{XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT};
    constexpr WORD keys[]{'W','S','A','D'};
    for (int i=0; i<4; ++i) { const bool held=(state.buttons & buttons[i])!=0; if (held!=m_keys[i]) key(keys[i],held); m_keys[i]=held; }
    if ((state.buttons & XINPUT_GAMEPAD_B) && !(m_previous_buttons & XINPUT_GAMEPAD_B)) { key(VK_ESCAPE,true); key(VK_ESCAPE,false); }
    m_previous_buttons=state.buttons;
}
void WuWaControlsComponent::recenter(bool reset_position) {
    const auto& vr=VR::get();
    if (!vr->is_hmd_active()) return;
    const auto head=vr->get_position(0);
    if (!std::isfinite(head.x) || !std::isfinite(head.y) || !std::isfinite(head.z)) return;
    if (reset_position) vr->set_standing_origin(head);
    vr->recenter_view(); vr->recenter_horizon(); WindowMode::get()->request_recenter();
}
} // namespace vrmod
