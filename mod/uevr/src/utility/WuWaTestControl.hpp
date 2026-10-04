#pragma once

#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <sdk/CVar.hpp>
#include <sdk/ConsoleManager.hpp>
#include "WuWaInputTrace.hpp"
#include "WuWaInputSequenceBridge.hpp"
#include "WuWaShadowPass.hpp"
#include "WuWaSecondEyeBuild.hpp"
#include "WuWaClvRefresh.hpp"
#include "WuWaSteadyView.hpp"
#include "WuWaPerf.hpp"
#include "WuWaMotionTrace.hpp"
#include "WuWaBooleanCVar.hpp"
#include "WuWaPlanarCVar.hpp"
#include "WuWaPlanarProbe.hpp"
#include "WuWaTranslucencyStereo.hpp"
#include "WuWaKuroReflection.hpp"
#include "WuWaReflectionCapture.hpp"
#include "WuWaWaterObservation.hpp"
#include "WuWaStereoLease.hpp"
#include "WuWaLodProbe.hpp"
#include "WuWaSceneFrame.hpp"
#include "WuWaStereoOrder.hpp"
#include "WuWaProjectionTest.hpp"
#include "WuWaRimSuppression.hpp"

namespace wuwa_test {
// Explicit, expiring requests for one-variable graphics comparisons. This is
// not a console command endpoint. It never enumerates the console registry,
// changes saved/frozen CVars, or falls back to speculative virtual calls.
using Json = nlohmann::json;
inline wuwa_stereo::CandidateLease stereo_candidate_lease;
inline bool stereo_candidate_value(size_t i,bool configured) {
    return stereo_candidate_lease.value(GetTickCount64(),i,configured);
}
inline Json stereo_candidate_status() {
    const auto now=GetTickCount64();
    return {{"active",stereo_candidate_lease.active(now)}, {"id",stereo_candidate_lease.id()},
        {"values",stereo_candidate_lease.values()}, {"remaining_ms",stereo_candidate_lease.remaining(now)}};
}

inline Json projection_base_json(const wuwa_projection_test::Base& b) {
    return {{"horizontal",b.horizontal},{"vertical",b.vertical},{"grow",b.grow},{"screen",b.screen},
        {"openxr",b.openxr},{"native",b.native},{"ready",b.ready},{"native_fix",b.native_fix}};
}
inline Json projection_test_status(uint64_t now) {
    const auto s = wuwa_projection_test::lease.status(now);
    Json result{{"active",s.active},{"id",s.id},{"reason",s.reason},{"remaining_ms",s.remaining_ms},
        {"base",s.base ? projection_base_json(*s.base) : Json(nullptr)},
        {"effective_horizontal",s.effective_horizontal},{"applied",nullptr},
        {"applied_matches_effective",false},
        {"scope","Transient horizontal symmetry; runtime CPU matrices, not a submitted frame or NPC fix"}};
    if (s.applied) {
        const auto& a = *s.applied;
        result["applied"] = {{"sequence",a.sequence},{"at_tick_ms",a.at_ms},
            {"age_ms",now >= a.at_ms ? Json(now-a.at_ms) : Json(nullptr)},
            {"base",projection_base_json(a.selection.base)},
            {"lease_id",a.selection.lease_id},{"epoch",a.selection.epoch},
            {"key",{{"horizontal",a.key.horizontal},{"vertical",a.key.vertical},
                {"grow",a.key.grow},{"near_z",a.key.near_z},{"raw_fov",a.key.fov}}},
            {"matrices_column_major",a.matrices},{"crop_bounds",a.bounds}};
        result["applied_matches_effective"] = s.base && a.selection.base == *s.base &&
            a.key.horizontal == s.effective_horizontal &&
            a.selection.lease_id == (s.active ? s.id : std::string{});
    }
    return result;
}
enum class CVarStorage { integer, boolean, planar_float, impostor_integer, mesh_cache_integer };
struct CVarSpec { const char* name; const wchar_t* wide_name; int minimum, maximum; CVarStorage storage{}; };
inline constexpr std::array<CVarSpec, 43> test_cvars{{
    {"r.AmbientOcclusionLevels", L"r.AmbientOcclusionLevels", -1, 4},
    {"r.ShadowQuality", L"r.ShadowQuality", 0, 5},
    {"r.ContactShadows", L"r.ContactShadows", 0, 1},
    {"r.EyeAdaptationQuality", L"r.EyeAdaptationQuality", 0, 3},
    {"r.DistanceFieldAO", L"r.DistanceFieldAO", 0, 1},
    {"r.SSGI.Enable", L"r.SSGI.Enable", 0, 1},
    {"r.TranslucentLightingVolume", L"r.TranslucentLightingVolume", 0, 1},
    {"r.Kuro.KuroCustomShadowDecal", L"r.Kuro.KuroCustomShadowDecal", 0, 1},
    {"r.Kuro.TranslucentToonShadowMode", L"r.Kuro.TranslucentToonShadowMode", 0, 1},
    {"r.Kuro.EnablePlanarReflection", L"r.Kuro.EnablePlanarReflection", 0, 1, CVarStorage::planar_float},
    {"r.SSR.Quality", L"r.SSR.Quality", 0, 10},
    {"r.ReflectionEnvironment", L"r.ReflectionEnvironment", 0, 2},
    {"r.Kuro.SeparateTranslucencyBlur", L"r.Kuro.SeparateTranslucencyBlur", 0, 1},
    {"r.KuroDownsampleTranslucencyFullRes", L"r.KuroDownsampleTranslucencyFullRes", 0, 1, CVarStorage::boolean},
    // Registration in the inspected WuWa build uses GetIntData (+0x58) for
    // these four. Custom Kuro foliage controls also include float/bool data;
    // those must not be sent through the integer-pair writer.
    {"r.AllowOcclusionQueries", L"r.AllowOcclusionQueries", 0, 1},
    {"vr.RoundRobinOcclusion", L"vr.RoundRobinOcclusion", 0, 1},
    {"foliage.ForceLOD", L"foliage.ForceLOD", -1, 8},
    {"foliage.DisableCull", L"foliage.DisableCull", 0, 1},
    // ForceMode alone has verified int32-pair storage. Enabled/NearestSlice
    // are integer references and cannot use this writer.
    {"r.ImposterVer2.ForceMode", L"r.ImposterVer2.ForceMode", 0, 2, CVarStorage::impostor_integer},
    // The verified registration/accessor below use paired int32 data. This
    // expiring comparison regenerates draw commands; it never hides geometry
    // or changes the shipped rendering defaults.
    {"r.MeshDrawCommands.UseCachedCommands", L"r.MeshDrawCommands.UseCachedCommands", 0, 1, CVarStorage::mesh_cache_integer},
    // Lighting diagnostics (1 Oct, 3.7): far lighting follows the view state. Integer pairs only;
    // a float CVar fails the range check and is refused before any write.
    {"r.Shadow.CacheDirectLightShadow", L"r.Shadow.CacheDirectLightShadow", 0, 1},
    {"r.Shadow.CacheWholeSceneShadows", L"r.Shadow.CacheWholeSceneShadows", 0, 1},
    {"r.Shadow.EnableCSMStable", L"r.Shadow.EnableCSMStable", 0, 1},
    {"r.Shadow.UseStaticCSM0Cache", L"r.Shadow.UseStaticCSM0Cache", 0, 1},
    {"r.Shadow.DirectLightCacheIncludeDFShadow", L"r.Shadow.DirectLightCacheIncludeDFShadow", 0, 1},
    {"r.Shadow.MaxNumFarShadowCascades", L"r.Shadow.MaxNumFarShadowCascades", 0, 16},
    {"r.Shadow.ForceCastFarShadow", L"r.Shadow.ForceCastFarShadow", 0, 1},
    {"r.Shadow.MaxNumDirectLightCSMCacheUpdatesPerLightPerFrame", L"r.Shadow.MaxNumDirectLightCSMCacheUpdatesPerLightPerFrame", -1, 64},
    {"r.Shadow.CSMMode3EnableUpdateIntervalOverride", L"r.Shadow.CSMMode3EnableUpdateIntervalOverride", 0, 1},
    {"r.DistanceFieldShadowing", L"r.DistanceFieldShadowing", 0, 1},
    {"r.DistanceFieldAO.MultiView", L"r.DistanceFieldAO.MultiView", 0, 1},
    {"r.AOGlobalDistanceField", L"r.AOGlobalDistanceField", 0, 1},
    {"r.VolumetricFog", L"r.VolumetricFog", 0, 1},
    {"r.VolumetricFog.TemporalReprojection", L"r.VolumetricFog.TemporalReprojection", 0, 1},
    {"r.KuroFogRendering", L"r.KuroFogRendering", 0, 4},
    {"r.Kuro.MobileFog", L"r.Kuro.MobileFog", 0, 1},
    {"r.SkyAtmosphere", L"r.SkyAtmosphere", 0, 1},
    {"r.Kuro.LandscapeCapture", L"r.Kuro.LandscapeCapture", 0, 1},
    {"r.Kuro.DisableGlobalGITransition", L"r.Kuro.DisableGlobalGITransition", 0, 1},
    {"r.Kuro.GlobalGIRenderQuality", L"r.Kuro.GlobalGIRenderQuality", 0, 8},
    {"r.KuroVolumetricLight.Enable", L"r.KuroVolumetricLight.Enable", 0, 1},
    {"r.Kuro.EnableCacheWeatherData", L"r.Kuro.EnableCacheWeatherData", 0, 1},
    {"r.Kuro.SuperFarFogTickIntervalMax", L"r.Kuro.SuperFarFogTickIntervalMax", 0, 10000},
}};

struct DataSample { sdk::TConsoleVariableData<int>* data{}; int game{}, render{}; uintptr_t boolean_slot{}, float_slot{}; };

inline bool read_data(uintptr_t slot, DataSample& sample, CVarStorage storage = CVarStorage::integer) {
    if (storage == CVarStorage::planar_float) {
        wuwa_planar_cvar::Sample floats{};
        if (!wuwa_planar_cvar::read(slot, floats)) return false;
        sample = {reinterpret_cast<sdk::TConsoleVariableData<int>*>(floats.address),
            floats.bits[0] == 0x3f800000 ? 1 : 0, floats.bits[1] == 0x3f800000 ? 1 : 0, 0, slot};
        return true;
    }
    if (storage == CVarStorage::boolean) {
        wuwa_boolean_cvar::Sample bytes{};
        if (!wuwa_boolean_cvar::read(slot, bytes)) return false;
        sample = {reinterpret_cast<sdk::TConsoleVariableData<int>*>(bytes.address), bytes.values[0], bytes.values[1], slot};
        return true;
    }
    __try {
        auto data = *(sdk::TConsoleVariableData<int>**)slot;
        MEMORY_BASIC_INFORMATION info{};
        if (data == nullptr || VirtualQuery(data, &info, sizeof(info)) != sizeof(info)
            || info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) != 0
            || (info.Protect & 0xff) != PAGE_READWRITE
            || (uintptr_t)data - (uintptr_t)info.BaseAddress + sizeof(*data) > info.RegionSize) {
            return false;
        }
        sample = {data, data->get(0), data->get(1)};
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline bool write_data(const DataSample& sample, int value) {
    if (sample.float_slot) {
        if ((sample.game != 0 && sample.game != 1) || (sample.render != 0 && sample.render != 1)) return false;
        return wuwa_planar_cvar::write(sample.float_slot,
            {reinterpret_cast<uintptr_t>(sample.data),
                {sample.game == 1 ? 0x3f800000u : 0u, sample.render == 1 ? 0x3f800000u : 0u}}, value);
    }
    if (sample.boolean_slot) {
        if (value < 0 || value > 1 || sample.game < 0 || sample.game > 1 || sample.render < 0 || sample.render > 1) return false;
        return wuwa_boolean_cvar::write(sample.boolean_slot,
            {reinterpret_cast<uintptr_t>(sample.data), {uint8_t(sample.game), uint8_t(sample.render)}}, uint8_t(value));
    }
    __try {
        // Reuse UEVR's data-layout guard. Unlike Wrapper::set(), no unverified
        // IConsoleVariable fallback is allowed for this test endpoint.
        return sample.data->set(value);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

inline void write_json(const std::filesystem::path& path, const Json& value) {
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out{temporary, std::ios::trunc | std::ios::binary};
        out << value.dump(2);
        out.flush();
        if (!out) {
            throw std::runtime_error("test response write failed");
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        throw std::runtime_error("test response replace failed");
    }
}

inline int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

struct GraphicsLease {
    const CVarSpec* spec{};
    uintptr_t slot{};
    DataSample before{};
    int value{};
    uint64_t until{};
    std::string id{};
};

inline GraphicsLease graphics_lease{}; // accessed only by the game thread
inline std::string last_restore{};
// Menu actions cross from the render/UI thread to the existing game-thread
// endpoint. A menu click must never race a CVar lease or touch game pointers.
inline std::atomic_int menu_shadow_request{}; // 1 = off for 20s, 2 = restore
inline std::atomic<const char*> menu_shadow_result{"No menu shadow test requested."};

inline void draw_controls() {
    const auto now = GetTickCount64();
    input_watch_until.store(now + 2000);
    ImGui::Separator();
    ImGui::TextUnformatted("Input diagnostics");
    if (ImGui::Button("Start input recording (until stopped)")) {
        start_input_trace(0, true);
        spdlog::info("[WuWaInput] menu trace started; modifier bits: 1 FrameworkConfig, 2 VR, 4 UObjectHook, 8 PluginLoader, 16 LuaLoader, 32 other, 64 selected-slot filter");
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop input trace")) stop_input_trace();
    ImGui::Text("Trace: %s (%u / 1000 transitions)", tracing_input() ? "recording" : "off", input_trace_rows.load());
    ImGui::Text("Incomplete observations: %u lock misses, %u reader overflows", input_trace_dropped.load(), input_trace_reader_overflow.load());
    ImGui::TextWrapped("Recording stops at 1000 changes or when the game exits. This records controller data and focus events, not whether the gameplay action succeeded.");
    if (ImGui::Button("Mute VR motion input for 60 seconds")) motion_input_muted_until = now + 60000;
    ImGui::SameLine();
    if (ImGui::Button("End motion mute")) motion_input_muted_until = 0;
    const auto mute_until = motion_input_muted_until.load();
    ImGui::Text("Temporary motion mute: %.1fs remaining", mute_until > now ? (mute_until - now) / 1000.0 : 0.0);
    ImGui::Text("Focus-loss messages: %llu received, %llu blocked", (unsigned long long)focus_losses.load(),
        (unsigned long long)focus_losses_blocked.load());
    const auto focus_test = active_focus_test();
    ImGui::Text("Temporary focus test: %s", focus_test == 1 ? "Windows forwarding" : focus_test == 2 ? "focused XR input" : "none");
    if (focus_test != 0 && ImGui::Button("End temporary focus test")) focus_test_until = 0;
    if (ImGui::TreeNode("Raw / delivered controller states")) {
        ImGui::TextWrapped("Raw is Windows XInput. Delivered is after UEVR, native plugins and Lua. The menu may intentionally consume input; close it for the gameplay test.");
        std::array<InputSample, 8> samples{};
        if (read_input_samples(samples)) {
            bool any = false;
            for (const auto& sample : samples) {
                if (sample.at == 0 || (sample.raw_result != ERROR_SUCCESS && sample.result != ERROR_SUCCESS)) continue;
                any = true;
                ImGui::Text("XInput 1_%u / pad %u (%.1fs ago): result %u -> %u", sample.api == 14 ? 4u : 3u,
                    sample.index, (now >= sample.at ? now - sample.at : 0) / 1000.0, sample.raw_result, sample.result);
                ImGui::Text("Buttons %04X -> %04X; packet %u -> %u; modified by mask %02X",
                    sample.raw.Gamepad.wButtons, sample.delivered.Gamepad.wButtons,
                    sample.raw.dwPacketNumber, sample.delivered.dwPacketNumber, sample.changed_by);
                ImGui::Text("Triggers %u/%u -> %u/%u; foreground %s; XR state %d; motion %s",
                    sample.raw.Gamepad.bLeftTrigger, sample.raw.Gamepad.bRightTrigger,
                    sample.delivered.Gamepad.bLeftTrigger, sample.delivered.Gamepad.bRightTrigger,
                    sample.game_foreground ? "game" : "other window", sample.xr_state, sample.motion ? "active" : "inactive");
            }
            if (!any) ImGui::TextUnformatted("Waiting for a connected gamepad sample.");
        }
        ImGui::TextWrapped("Modifier mask: 01 FrameworkConfig, 02 VR, 04 UObjectHook, 08 native plugins, 10 Lua, 20 other, 40 selected-slot filter. XR: 4 visible, 5 focused.");
        ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::Text("Shadow correction: %s; applied/restored %llu/%llu", wuwa_shadow::enabled() ? "on" : "off",
        (unsigned long long)wuwa_shadow::applied.load(), (unsigned long long)wuwa_shadow::restored.load());
    ImGui::Text("Invalid pairs: %llu; fault: %s", (unsigned long long)wuwa_shadow::invalid_pairs.load(),
        wuwa_shadow::faulted.load() ? "yes" : "no");
    if (wuwa_shadow::ready() && ImGui::Button("Compare shadows: off for 20 seconds")) menu_shadow_request = 1;
    if (ImGui::Button("End temporary shadow test")) menu_shadow_request = 2;
    ImGui::TextWrapped("%s", menu_shadow_result.load());
    ImGui::TextWrapped("Shadow tests restore the current Same Pass setting automatically. No quality or profile setting is changed.");
}

// Diagnostic console lease (1 Oct, 3.7): set one r.* console variable through the engine's
// console manager for a bounded window, then put the previous value back. For variables the
// static data lookup cannot find. Runs on the game thread (pre-engine tick), like the console.
struct ConsoleLease { sdk::IConsoleVariable* var{}; std::string name; std::wstring previous; float applied{}; uint64_t until{}; };
inline ConsoleLease console_lease{};
inline sdk::IConsoleVariable* find_console_variable(const std::string& name) {
    if (name.rfind("r.", 0) != 0 || name.size() > 96) throw std::runtime_error("console tests accept r.* names only");
    for (const auto c : name) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_') throw std::runtime_error("invalid name");
    const auto manager = sdk::FConsoleManager::get();
    if (manager == nullptr) throw std::runtime_error("console manager unavailable");
    auto* object = manager->find(std::wstring(name.begin(), name.end()));
    if (object == nullptr || object->AsCommand() != nullptr) throw std::runtime_error("no such console variable");
    return static_cast<sdk::IConsoleVariable*>(object);
}
inline std::wstring console_value_text(sdk::IConsoleVariable* var) {
    const auto f = var->GetFloat();
    const auto i = var->GetInt();
    if (static_cast<float>(i) == f) return std::to_wstring(i);
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.9g", f);
    return buffer;
}
inline std::string restore_console() {
    auto& lease = console_lease;
    if (lease.var == nullptr) return "inactive";
    std::string result = "restored";
    try {
        if (std::abs(lease.var->GetFloat() - lease.applied) > 1e-6f) result = "restore_conflict_value_changed";
        else lease.var->Set(lease.previous.c_str());
    } catch (...) { result = "restore_failed"; }
    spdlog::info("[WuWaTest] console lease {} restored: {}", lease.name, result);
    lease = {};
    return result;
}

inline std::string restore_graphics() {
    auto& lease = graphics_lease;
    if (lease.spec == nullptr) {
        return "inactive";
    }
    DataSample current{};
    std::string result = "restore_refused_pointer_changed";
    if (read_data(lease.slot, current, lease.spec->storage) && current.data == lease.before.data) {
        // Respect an intervening game/menu change; don't overwrite someone
        // else's setting while restoring this temporary test.
        if (current.game != lease.value || current.render != lease.value) {
            result = "restore_conflict_value_changed";
        } else if (write_data(current, lease.before.game)) {
            DataSample after{};
            result = read_data(lease.slot, after, lease.spec->storage) && after.data == lease.before.data
                && after.game == lease.before.game && after.render == lease.before.render
                ? "restored" : "restore_readback_failed";
        } else {
            result = "restore_write_failed";
        }
    }
    spdlog::info("[WuWaTest] {} {} id={}", result, lease.spec->name, lease.id);
    last_restore = result;
    lease = {};
    return result;
}

inline Json test_status(const Json& camera, const Json& live_options) {
    Json status{{"version", 1}, {"pid", GetCurrentProcessId()}, {"unix_ms", unix_ms()},
        {"trace", tracing_input()}, {"trace_rows", input_trace_rows.load()},
        {"trace_epoch", input_trace_epoch.load()}, {"trace_until_stopped", input_trace_session.load()},
        {"trace_dropped", input_trace_dropped.load()}, {"trace_reader_overflow", input_trace_reader_overflow.load()},
        {"trace_focus_rows", input_focus_rows.load()},
        {"motion_muted", motion_input_muted()}, {"last_restore", last_restore},
        {"active", graphics_lease.spec != nullptr}};
    status["shadow"] = wuwa_shadow::status();
    status["camera"] = camera;
    status["input_sequence"]=wuwa_input_sequence_bridge::status();
    status["motion_recording"] = wuwa_motion::status();
    status["planar_probe"] = wuwa_planar_probe::status();
    status["planar_eye_correction"] = wuwa_planar_probe::correction_status();
    status["stereo_translucency"] = wuwa_translucency::status();
    status["kuro_reflection"] = wuwa_kuro_reflection::status();
    status["reflection_capture_projection"] = wuwa_reflection_capture::status();
    status["scene_frame_pair"] = wuwa_scene_frame::status();
    status["native_submission_order_test"] = wuwa_stereo_order::status();
    status["kuro_water_observation"] = wuwa_water_observation::status();
    status["stereo_candidate_test"] = stereo_candidate_status();
    status["projection_test"] = projection_test_status(GetTickCount64());
    status["rim_suppression"] = wuwa_rim::diagnostic_status();
    status["lod_probe"] = wuwa_lod_probe::status();
    status["live_options"] = live_options;
    status["graphics_test_cvars"] = Json::array();
    for (const auto& spec : test_cvars) status["graphics_test_cvars"].push_back(spec.name);
    status["focus_test"] = active_focus_test();
    status["focus_losses"] = focus_losses.load();
    status["focus_losses_blocked"] = focus_losses_blocked.load();
    const auto& lease = graphics_lease;
    if (lease.spec != nullptr) {
        status["id"] = lease.id;
        status["name"] = lease.spec->name;
        status["before"] = lease.before.game;
        status["requested"] = lease.value;
        status["remaining_ms"] = lease.until > GetTickCount64() ? lease.until - GetTickCount64() : 0;
        DataSample current{};
        if (read_data(lease.slot, current, lease.spec->storage) && current.data == lease.before.data) {
            status["actual"] = current.game;
            status["render"] = current.render;
        }
    }
    return status;
}

template<class IsFrozen, class CameraStatus, class OptionsStatus, class ProjectionBase>
void process_test_request(const std::filesystem::path& directory, IsFrozen is_frozen, CameraStatus camera_status, OptionsStatus options_status, ProjectionBase projection_base) {
    if (!is_wuwa()) {
        return;
    }
    const auto now = GetTickCount64();
    // Observe every game tick, not just the file-poll interval. A later render
    // boundary also checks independently, so expiration needs no helper client.
    wuwa_projection_test::lease.select(now, projection_base());
    if(wuwa_input_sequence_bridge::needs_guard_refresh(now)) {
        try { wuwa_input_sequence_bridge::refresh_guards(camera_status()); }
        catch(...) { wuwa_input_sequence_bridge::refresh_guards(Json::object()); }
    }
    if (graphics_lease.spec != nullptr && now >= graphics_lease.until) {
        restore_graphics();
    }
    if (console_lease.var != nullptr && now >= console_lease.until) {
        restore_console();
    }
    const auto menu_request = menu_shadow_request.exchange(0);
    if (menu_request == 2) {
        wuwa_shadow::set_test(0, true);
        menu_shadow_result = "Temporary override ended; using current Same Pass setting.";
        spdlog::info("[WuWaTest] menu ended shadow comparison");
    } else if (menu_request == 1) {
        if (graphics_lease.spec == nullptr && !wuwa_projection_test::lease.status(now).active && !stereo_candidate_lease.active(now) && !wuwa_stereo_order::active(now) && !wuwa_shadow::test_active() && !wuwa_shadow::state_swap_active() && wuwa_shadow::ready()) {
            wuwa_shadow::set_test(20, false);
            menu_shadow_result = "20-second comparison started; restores automatically.";
            spdlog::info("[WuWaTest] menu shadow correction off for 20 seconds");
        } else {
            menu_shadow_result = "Test refused: another test is active, or shadow checks are not ready.";
        }
    }
    static uint64_t next_poll{};
    if (now < next_poll) {
        return;
    }
    next_poll = now + 500;
    const auto request_path = directory / L"wuwa-test.request.json";
    const auto response_path = directory / L"wuwa-test.response.json";
    const auto status_path = directory / L"wuwa-test.status.json";
    std::error_code ec;
    const bool has_request = std::filesystem::exists(request_path, ec);
    if (!has_request) {
        // Keep a small heartbeat for the capture helper; no per-frame I/O.
        static uint64_t next_status{};
        if (now >= next_status) {
            next_status = now + 2000;
            try { write_json(status_path, test_status(camera_status(), options_status())); } catch (...) {}
        }
        return;
    }
    Json reply{{"version", 1}, {"pid", GetCurrentProcessId()}, {"status", "error"}};
    try {
        const auto size = std::filesystem::file_size(request_path);
        if (size == 0 || size > 4096) {
            std::filesystem::remove(request_path);
            throw std::runtime_error("request must contain 1..4096 bytes");
        }
        Json request;
        {
            std::ifstream in{request_path, std::ios::binary};
            request = Json::parse(in);
        }
        std::filesystem::remove(request_path);
        const auto id = request.at("id").get<std::string>();
        if (id.empty() || id.size() > 64) {
            throw std::runtime_error("invalid request id");
        }
        reply["id"] = id;
        const auto expires = request.at("expires_unix_ms").get<int64_t>();
        if (request.at("version") != 1 || request.at("pid") != GetCurrentProcessId()
            || expires < unix_ms() || expires > unix_ms() + 60000) {
            throw std::runtime_error("wrong version, process, or expired request");
        }
        const auto op = request.at("op").get<std::string>();
        reply["op"] = op;
        // Keep comparison isolation in both directions. Queries, raw observers
        // and end/restore operations remain available during the lease.
        const bool starts_timed_graphics =
            (op == "console_set" || op == "native_submission_order" || op == "stereo_candidates" ||
             op == "shadow_pass" || op == "construct_mode" || op == "lod_sync" || op == "second_eye" ||
             op == "eye_swap" || op == "target_swap" || op == "state_swap") && request.value("seconds",0) != 0;
        if (wuwa_projection_test::lease.status(now).active &&
            (op == "begin" || starts_timed_graphics ||
             (op == "clv_refresh" && request.value("frames",1) > 0) ||
             (op == "steady_view" && request.value("bypass_seconds",0) > 0)))
            throw std::runtime_error("Projection comparison is active");

        if (op == "projection_test") {
            for (auto it = request.begin(); it != request.end(); ++it) {
                const auto& k = it.key();
                if (k != "version" && k != "pid" && k != "id" && k != "op" && k != "expires_unix_ms" &&
                    k != "action" && k != "seconds" && k != "expected" && k != "lease_id")
                    throw std::runtime_error("Unknown projection test field");
            }
            const auto action = request.at("action").get<std::string>();
            const auto current = projection_base();
            wuwa_projection_test::lease.select(now,current);
            if (action == "begin") {
                if (!request.at("seconds").is_number_integer()) throw std::runtime_error("Projection duration must be an integer");
                const auto& e = request.at("expected");
                if (!e.is_object() || e.size()!=4 || !e.at("horizontal").is_number_integer() ||
                    !e.at("vertical").is_number_integer() || !e.at("grow").is_boolean() || !e.at("screen").is_boolean())
                    throw std::runtime_error("Projection expected baseline needs horizontal, vertical, grow, screen");
                auto expected = current;
                expected.horizontal=e.at("horizontal").get<int>(); expected.vertical=e.at("vertical").get<int>();
                expected.grow=e.at("grow").get<bool>(); expected.screen=e.at("screen").get<bool>();
                const auto clv = wuwa_clv::status();
                const bool conflict = wuwa_rim::busy() || clv.value("manual_pending",false) || clv.value("owns_refresh",false) ||
                    graphics_lease.spec || console_lease.var || stereo_candidate_lease.active(now) ||
                    wuwa_stereo_order::active(now) || wuwa_shadow::test_active() || wuwa_shadow::state_swap_active() ||
                    wuwa_shadow::construct_active() || wuwa_shadow::eye_swap_active() || wuwa_shadow::target_swap_active() ||
                    wuwa_shadow::lod_sync_active() || wuwa_shadow::lod_sync_bypassed() || wuwa_second_eye::active() ||
                    wuwa_steady_view::bypassed();
                wuwa_projection_test::lease.begin(now,request.at("seconds").get<int>(),id,current,expected,conflict);
            } else if (action == "end") {
                wuwa_projection_test::lease.end(now,request.at("lease_id").get<std::string>());
            } else if (action != "query") throw std::runtime_error("Projection action must be query, begin or end");
            reply["projection_test"] = projection_test_status(now);
        } else if (op == "input_sequence") {
            reply["input_sequence"]=wuwa_input_sequence_bridge::request(request,camera_status());
        } else if (op == "native_submission_order") {
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 30)
                throw std::runtime_error("Submission order test duration must be 0..30 seconds");
            if (seconds && (graphics_lease.spec || stereo_candidate_lease.active(now) ||
                wuwa_shadow::test_active() || wuwa_shadow::state_swap_active() || !wuwa_shadow::ready() ||
                wuwa_stereo_order::active(now)))
                throw std::runtime_error("Submission pair is not verified or another graphics test is active");
            wuwa_stereo_order::until = seconds ? now + seconds * 1000 : 0;
            reply["native_submission_order_test"] = wuwa_stereo_order::status();
        } else if (op == "stereo_candidates") {
            const auto seconds=request.value("seconds",0);
            if (seconds==0) stereo_candidate_lease.end(now,request.value("lease_id",std::string{}));
            else {
                if (graphics_lease.spec || wuwa_shadow::test_active() || wuwa_shadow::state_swap_active() ||
                    wuwa_stereo_order::active(now))
                    throw std::runtime_error("Another graphics comparison is active");
                const auto& values=request.at("values");
                if (!values.is_array() || values.size()!=3 || !values[0].is_boolean() ||
                    !values[1].is_boolean() || !values[2].is_boolean())
                    throw std::runtime_error("Stereo comparison needs exactly three Boolean values");
                stereo_candidate_lease.begin(now,seconds,id,values.get<std::array<bool,3>>());
            }
            reply["stereo_candidate_test"]=stereo_candidate_status();
        } else if (op == "planar_probe") {
            reply["planar_probe"] = wuwa_planar_probe::request(directory, request.value("seconds", 0));
        } else if (op == "lod_probe") {
            reply["lod_probe"] = wuwa_lod_probe::request(directory, request.value("seconds", 0),
                request.value("view_uniforms",false), request.value("mesh_bindings",false),
                request.value("raw_snapshots",false));
        } else if (op == "record_motion") {
            const auto seconds=request.value("seconds",0);
            if (seconds<0 || seconds>300) throw std::runtime_error("Motion recording duration must be 0..300 seconds");
            if (seconds==0) { wuwa_motion::stop(request.value("recording_id",std::string{})); reply["recording"]=wuwa_motion::status(); }
            else reply["recording"]=wuwa_motion::start(directory,seconds);
        } else if (op == "trace") {
            const auto seconds = request.value("seconds", 90);
            const auto until_stopped = request.value("until_stopped", false);
            if (seconds < 0 || seconds > 120) throw std::runtime_error("trace duration must be 0..120 seconds");
            if (seconds == 0 && !until_stopped) stop_input_trace();
            else start_input_trace(seconds, until_stopped);
        } else if (op == "motion_input") {
            const auto seconds = request.value("mute_seconds", 0);
            if (seconds < 0 || seconds > 120) throw std::runtime_error("mute duration must be 0..120 seconds");
            motion_input_muted_until.store(seconds == 0 ? 0 : GetTickCount64() + seconds * 1000);
        } else if (op == "input_focus") {
            const auto seconds = request.value("seconds", 0);
            const auto policy = request.value("policy", std::string{"none"});
            if (seconds < 0 || seconds > 120 || (policy != "windows" && policy != "xr" && policy != "none")) {
                throw std::runtime_error("focus test requires windows/xr/none and 0..120 seconds");
            }
            if (seconds == 0 || policy == "none") {
                focus_test_until = 0;
            } else {
                if (active_focus_test() != 0) throw std::runtime_error("another focus test is active");
                focus_test_policy = policy == "windows" ? 1 : 2;
                focus_test_until = GetTickCount64() + seconds * 1000;
            }
            reply["focus_test"] = active_focus_test();
        } else if (op == "shadow_query") {
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "shadow_pass") {
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 60) throw std::runtime_error("shadow test duration must be 0..60 seconds");
            if (seconds != 0 && (graphics_lease.spec != nullptr || stereo_candidate_lease.active(now) || wuwa_shadow::test_active() || wuwa_shadow::state_swap_active() || wuwa_stereo_order::active(now) || !wuwa_shadow::ready())) {
                throw std::runtime_error("shadow pair unverified, test faulted, or another graphics test is active");
            }
            wuwa_shadow::set_test(seconds, request.value("enabled", true), request.value("full_view", false));
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "construct_mode") {
            // Fix bench: construct WuWa's secondary view differently for 0..60 s (0 ends it).
            const auto seconds = request.value("seconds", 0);
            const auto mode = request.value("mode", 0);
            if (seconds < 0 || seconds > 60 || mode < 0 || mode > 4) throw std::runtime_error("construct_mode needs seconds 0..60 and mode 0..4");
            wuwa_shadow::set_construct(seconds, mode);
            spdlog::info("[WuWaBench] construct mode {} for {} s (control)", wuwa_shadow::construct_mode_names[static_cast<size_t>(wuwa_shadow::construct_active())], seconds);
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "lod_sync") {
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 60) throw std::runtime_error("lod_sync needs seconds 0..60");
            const auto enabled = request.value("enabled", true);
            wuwa_shadow::set_lod_sync(seconds, enabled);
            spdlog::info("[WuWaBench] LOD sync forced {} for {} s (control)", enabled ? "on" : "off", seconds);
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "second_eye") {
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 60) throw std::runtime_error("second_eye needs seconds 0..60");
            if (!wuwa_second_eye::set_window(seconds)) throw std::runtime_error("K5 unavailable: CalcSceneView did not match this build");
            spdlog::info("[WuWaBench] second eye built as first for {} s (control)", seconds);
            reply["second_eye"] = {{"active", wuwa_second_eye::active()}, {"applied", wuwa_second_eye::applied.load()},
                {"state_restored", wuwa_second_eye::state_restored.load()}, {"install", wuwa_second_eye::install_result.load()}};
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "eye_swap") {
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 60) throw std::runtime_error("eye_swap needs seconds 0..60");
            wuwa_shadow::set_eye_swap(seconds);
            spdlog::info("[WuWaBench] eye pose swap for {} s (control)", seconds);
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "target_swap") {
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 60) throw std::runtime_error("target_swap needs seconds 0..60");
            wuwa_shadow::set_target_swap(seconds);
            spdlog::info("[WuWaBench] target swap for {} s (control)", seconds);
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "state_swap") {
            // Diagnostic view-state exchange between the two main views, per pair,
            // for a bounded window; 0 ends it. Nothing is saved to the profile.
            const auto seconds = request.value("seconds", 0);
            if (seconds < 0 || seconds > 60) throw std::runtime_error("State swap duration must be 0..60 seconds");
            if (seconds != 0 && (graphics_lease.spec != nullptr || stereo_candidate_lease.active(now) ||
                wuwa_shadow::test_active() || wuwa_shadow::state_swap_active() || wuwa_stereo_order::active(now) ||
                !wuwa_shadow::ready() || wuwa_shadow::faulted.load())) {
                throw std::runtime_error("Eye pair unverified, writes faulted, or another graphics test is active");
            }
            const auto mode_name = request.value("mode", std::string{"exchange"});
            int mode = -1;
            for (size_t i = 0; i < wuwa_shadow::swap_mode_names.size(); ++i)
                if (mode_name == wuwa_shadow::swap_mode_names[i]) mode = static_cast<int>(i);
            if (mode < 0) throw std::runtime_error("State swap mode must be exchange, first_for_both or second_for_both");
            wuwa_shadow::set_state_swap(seconds, mode);
            spdlog::info("[WuWaTest] view-state swap window {} s mode {}", seconds, mode_name);
            reply["shadow"] = wuwa_shadow::status();
        } else if (op == "clv_refresh") {
            const auto frames = request.value("frames", 1);
            if (frames > 0) wuwa_clv::request(static_cast<uint32_t>(frames));
            reply["clv"] = wuwa_clv::status();
        } else if (op == "perf") {
            // Frame-time window: action start opens it, stop closes it and returns the summary.
            const auto action = request.value("action", std::string{"stop"});
            if (action == "start") {
                wuwa_perf::start();
            } else if (action == "stop") {
                const auto s = wuwa_perf::stop();
                reply["perf"] = {{"frames", s.frames}, {"mean_ms", s.mean_ms}, {"p50_ms", s.p50_ms},
                    {"p90_ms", s.p90_ms}, {"p99_ms", s.p99_ms}, {"max_ms", s.max_ms}};
            } else {
                throw std::runtime_error("perf action must be start or stop");
            }
        } else if (op == "steady_view") {
            const auto seconds = request.value("bypass_seconds", 0);
            if (seconds < 0 || seconds > 120) throw std::runtime_error("bypass_seconds must be 0..120");
            wuwa_steady_view::bypass(seconds);
            reply["steady"] = {{"bypassed", wuwa_steady_view::bypassed()}, {"frames", wuwa_steady_view::frames_steadied.load()},
                {"dx", wuwa_steady_view::last_dx.load()}, {"dy", wuwa_steady_view::last_dy.load()},
                {"roll", wuwa_steady_view::last_roll.load()}};
        } else if (op == "console_get") {
            auto* var = find_console_variable(request.at("name").get<std::string>());
            reply["int"] = var->GetInt();
            reply["float"] = var->GetFloat();
        } else if (op == "console_set") {
            const auto name = request.at("name").get<std::string>();
            const auto seconds = request.value("seconds", 0);
            if (seconds != 0 && _stricmp(name.c_str(), wuwa_rim::variable_name) == 0 && wuwa_rim::busy())
                throw std::runtime_error("Disable the character rim workaround before testing its console variable");
            if (seconds == 0) {
                reply["restore"] = restore_console();
            } else {
                if (seconds < 5 || seconds > 60) throw std::runtime_error("seconds must be 0 or 5..60");
                if (console_lease.var != nullptr || graphics_lease.spec != nullptr) throw std::runtime_error("another graphics test is active");
                const auto value = request.at("value").get<double>();
                if (!std::isfinite(value) || std::abs(value) > 100000.0) throw std::runtime_error("value out of range");
                auto* var = find_console_variable(name);
                const auto previous = console_value_text(var);
                wchar_t text[64]{};
                swprintf_s(text, L"%.9g", value);
                var->Set(text);
                console_lease = {var, name, previous, var->GetFloat(), now + uint64_t(seconds) * 1000};
                reply["before"] = std::string(previous.begin(), previous.end());
                reply["after_float"] = console_lease.applied;
                reply["after_int"] = var->GetInt();
                spdlog::info("[WuWaTest] console lease {} = {} for {} s (was {})", name, value, seconds, reply["before"].get<std::string>());
            }
        } else if (op == "restore") {
            if (graphics_lease.spec != nullptr && request.value("lease_id", "") != graphics_lease.id) {
                throw std::runtime_error("lease id does not match active test");
            }
            reply["restore"] = restore_graphics();
        } else if (op == "query" || op == "begin") {
            if (graphics_lease.spec != nullptr || wuwa_shadow::test_active() || wuwa_shadow::state_swap_active() || stereo_candidate_lease.active(now) || wuwa_stereo_order::active(now)) throw std::runtime_error("another graphics test is active");
            const auto name = request.at("name").get<std::string>();
            const CVarSpec* spec = nullptr;
            for (const auto& candidate : test_cvars) {
                if (name == candidate.name) { spec = &candidate; break; }
            }
            if (spec == nullptr) throw std::runtime_error("CVar is not in the graphics test allowlist");
            if (is_frozen(spec->wide_name)) throw std::runtime_error("CVar is frozen; test refused without changing the saved override");
            const auto started = GetTickCount64();
            uintptr_t slot{};
            if (spec->storage == CVarStorage::planar_float) {
                slot = wuwa_planar_cvar::verified_slot();
            } else if (spec->storage == CVarStorage::boolean) {
                slot = wuwa_boolean_cvar::full_resolution_slot(0x07109ce2d6076611ULL);
            } else if (spec->storage == CVarStorage::impostor_integer) {
                // Registration at 0x1fef1a10 stores GetIntData (+0x58) in this
                // slot. Verify the complete registration before interpreting it.
                static const uintptr_t verified = [] {
                    constexpr std::array<wuwa_code_compatibility::Range, 1> ranges{{
                        {0x1fef1a10, 112, 0xfba316eaf9087354ULL}}};
                    return wuwa_code_check::verify("Impostor ForceMode integer data", ranges) + 0x37c9b558;
                }();
                slot = verified;
            } else if (spec->storage == CVarStorage::mesh_cache_integer) {
                static const uintptr_t verified = [] {
                    constexpr std::array<wuwa_code_compatibility::Range, 2> ranges{{
                        {0x20147d80, 115, 0x6c945d338d1019e1ULL},
                        {0x23622d10, 66, 0xbef84d43c4fde750ULL}}};
                    return wuwa_code_check::verify("Cached draw command integer data", ranges) + 0x37f5fa28;
                }();
                slot = verified;
            } else if (const auto wrapper = sdk::find_cvar_data_cached(L"Engine", spec->wide_name)) {
                slot = wrapper->address();
            }
            DataSample sample{};
            if (!slot || !read_data(slot, sample, spec->storage)) throw std::runtime_error("CVar data unavailable or type/code verification failed");
            if (sample.game < spec->minimum || sample.game > spec->maximum || sample.game != sample.render) {
                throw std::runtime_error("CVar data out of range or game/render copies disagree");
            }
            reply["name"] = name;
            reply["before"] = sample.game;
            reply["actual"] = sample.game;
            reply["render"] = sample.render;
            reply["storage"] = spec->storage == CVarStorage::planar_float ? "verified_float_pair" :
                spec->storage == CVarStorage::boolean ? "verified_bool_pair" : "int32_pair";
            reply["resolve_ms"] = GetTickCount64() - started;
            if (op == "begin") {
                if (expires < unix_ms()) throw std::runtime_error("request expired during resolution");
                const auto value = request.at("value").get<int>();
                const auto seconds = request.value("seconds", 30);
                if (value < spec->minimum || value > spec->maximum || seconds < 5 || seconds > 60) {
                    throw std::runtime_error("value or lease duration out of range");
                }
                if (request.at("expected") != sample.game) throw std::runtime_error("baseline changed; test refused");
                if (!write_data(sample, value)) throw std::runtime_error("CVar data write refused");
                graphics_lease = {spec, slot, sample, value, GetTickCount64() + seconds * 1000, id};
                DataSample after{};
                if (!read_data(slot, after, spec->storage) || after.data != sample.data
                    || after.game != value || after.render != value) {
                    restore_graphics();
                    throw std::runtime_error("CVar write readback failed");
                }
                reply["actual"] = after.game;
                reply["render"] = after.render;
            }
        } else {
            throw std::runtime_error("unsupported operation");
        }
        reply["status"] = "ok";
    } catch (const std::exception& error) {
        reply["error"] = error.what();
        // Malformed requests must not be parsed again on every poll.
        std::filesystem::remove(request_path, ec);
    } catch (...) {
        reply["error"] = "test request failed";
        std::filesystem::remove(request_path, ec);
    }
    reply["unix_ms"] = unix_ms();
    try {
        write_json(response_path, reply);
        write_json(status_path, test_status(camera_status(), options_status()));
    } catch (const std::exception& error) {
        spdlog::warn("[WuWaTest] {}", error.what());
    }
    spdlog::info("[WuWaTest] {}", reply.dump());
}
}
