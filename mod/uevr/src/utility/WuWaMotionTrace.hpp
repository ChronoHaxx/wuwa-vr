#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include "WuWaInputTrace.hpp"

// Explicit local recordings only. No input synthesis, process inspection,
// network, or game configuration changes. A bounded sidecar keeps clean video
// independent of its diagnostic annotations.
namespace wuwa_motion {
using Json=nlohmann::json;
inline std::mutex mutex;
inline std::ofstream file;
inline std::string path, id, error;
inline std::atomic_uint64_t until{};
inline uint64_t next_sample{}, next_flush{};
inline uint32_t rows{}, render_rows{};
inline constexpr uint32_t render_row_limit=2048;
inline int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline bool active() { return GetTickCount64()<until.load(); }
inline Json status() {
    std::scoped_lock lock{mutex};
    if (!active() && file.is_open()) file.close();
    return {{"active",active()},{"path",path},{"id",id},{"rows",rows},{"render_rows",render_rows},{"error",error}};
}
inline void stop(const std::string& expected={}) {
    std::scoped_lock lock{mutex};
    if (!expected.empty() && expected!=id) throw std::runtime_error("Recording identity changed");
    until=0; if (file.is_open()) file.close();
}
inline Json start(const std::filesystem::path& directory, unsigned seconds) {
    std::scoped_lock lock{mutex};
    if (active()) throw std::runtime_error("A motion recording is already active");
    if (seconds==0 || seconds>300) throw std::runtime_error("Motion recording duration must be 1..300 seconds");
    if (file.is_open()) file.close();
    id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(unix_ms());
    const auto folder=directory/L"recordings";
    std::filesystem::create_directories(folder);
    const auto target=folder/("motion-"+id+".jsonl");
    if (std::filesystem::exists(target)) throw std::runtime_error("Recording already exists");
    const auto utf=target.u8string(); path.assign(utf.begin(),utf.end());
    file.open(target,std::ios::binary|std::ios::out); error.clear();
    if (!file) throw std::runtime_error("Cannot create motion recording");
    file << Json{{"type","header"},{"version",2},{"pid",GetCurrentProcessId()},{"id",id},
        {"unix_ms",unix_ms()},{"clock_ms",GetTickCount64()},
        {"scope","game and post-animation camera, tracking-space headset pose, origin, recent rendered-eye views and controller aim when available; optional render_stage0 rows describe CPU draw setup, not GPU completion; timestamps and ages are not exact GPU-frame synchronization; no video/audio"}}.dump()<<'\n';
    file.flush(); rows=0; render_rows=0; next_sample=0; next_flush=0;
    until=GetTickCount64()+seconds*1000ull;
    return {{"active",true},{"path",path},{"id",id}};
}
inline Json pad(const XINPUT_GAMEPAD& p) {
    return {{"buttons",p.wButtons},{"lt",p.bLeftTrigger},{"rt",p.bRightTrigger},
        {"lx",p.sThumbLX},{"ly",p.sThumbLY},{"rx",p.sThumbRX},{"ry",p.sThumbRY}};
}
inline bool append_render_stage0(Json observation) {
    if (!active()) return false;
    // The render hook must never wait for the camera/recording controller.
    // Sampling is already limited per eye and phase by the producer. A hard
    // independent cap prevents diagnostics from starving the camera trace.
    std::unique_lock lock{mutex,std::try_to_lock};
    if (!lock.owns_lock() || !active() || !file.is_open() || render_rows>=render_row_limit) return false;
    file<<Json{{"type","render_stage0"},{"seq",render_rows++},{"observation",std::move(observation)}}.dump()<<'\n';
    if (!file) { error="write failed"; until=0; file.close(); return false; }
    return true;
}
inline void append(Json camera) {
    if (!active()) return;
    std::scoped_lock lock{mutex};
    const auto now=GetTickCount64();
    if (!active() || !file.is_open() || now<next_sample) return;
    next_sample=now+33;
    std::array<wuwa_test::InputSample,8> samples{};
    Json inputs=Json::array();
    const bool available=wuwa_test::read_input_samples(samples);
    if (available) for (const auto& s:samples) {
        if (!s.at || now<s.at || now-s.at>1000) continue;
        inputs.push_back({{"age_ms",now-s.at},{"api",s.api},{"slot",s.index},{"raw_result",s.raw_result},
            {"result",s.result},{"raw",pad(s.raw.Gamepad)},{"delivered",pad(s.delivered.Gamepad)},
            {"changed_by",s.changed_by},{"focused",s.game_foreground},{"uevr_menu",s.menu}});
    }
    file<<Json{{"type","frame"},{"seq",rows++},{"clock_ms",now},{"unix_ms",unix_ms()},
        {"camera",std::move(camera)},{"inputs_available",available},{"inputs",std::move(inputs)}}.dump()<<'\n';
    if (now>=next_flush) { file.flush(); next_flush=now+500; }
    if (!file || rows>=10000) { error=!file ? "write failed" : "row limit reached"; until=0; file.close(); }
}
}
