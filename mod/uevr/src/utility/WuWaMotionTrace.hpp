#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include "WuWaInputTrace.hpp"
#include "WuWaInputSequenceBridge.hpp"

// Explicit local recordings only. No input synthesis, process inspection,
// network, or game configuration changes. A bounded sidecar keeps clean video
// independent of its diagnostic annotations.
namespace wuwa_motion {
using Json=nlohmann::json;
inline std::mutex mutex;
inline std::ofstream file;
inline std::string path, id, error;
inline std::atomic_uint64_t until{};
inline uint64_t next_sample{}, next_flush{}, session_serial{};
inline uint32_t rows{}, render_rows{};
inline constexpr uint32_t render_row_limit=2048;
inline constexpr uint64_t output_byte_limit=60ull*1024*1024, footer_reserve=1024;
struct ByteBudget {
    uint64_t limit{output_byte_limit}, used{};
    bool data_fits(uint64_t size) const {
        return limit<=output_byte_limit && limit>=footer_reserve && used<=limit-footer_reserve
            && size<=limit-footer_reserve-used;
    }
    bool footer_fits(uint64_t size) const {
        return limit<=output_byte_limit && used<=limit && size<=limit-used;
    }
};
inline ByteBudget byte_budget;
inline bool truncated{}, render_truncated{}, footer_written{};
inline int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline bool active() { return GetTickCount64()<until.load(); }
// Caller holds mutex. Every normal stop, expiry, or bounded truncation ends in
// one JSONL footer. A filesystem failure remains explicit in status instead of
// pretending that a footer or partially written record reached disk.
inline void finish_locked(const char* reason) {
    until=0;
    if (!file.is_open()) return;
    try {
        const auto line=Json{{"type","recording_end"},{"version",1},{"id",id},
            {"unix_ms",unix_ms()},{"clock_ms",GetTickCount64()},
            {"rows",rows},{"render_rows",render_rows},{"bytes_before_footer",byte_budget.used},
            {"byte_limit",byte_budget.limit},{"truncated",truncated},
            {"render_stage0_truncated",render_truncated},{"reason",reason},{"error",error}}.dump()+'\n';
        if (line.size()>footer_reserve || !byte_budget.footer_fits(line.size())) {
            error="footer budget failed"; truncated=true;
        } else if (file) {
            file.write(line.data(),static_cast<std::streamsize>(line.size())); file.flush();
            if (file) { byte_budget.used+=line.size(); footer_written=true; }
            else { error="write failed"; truncated=true; }
        } else { error="write failed"; truncated=true; }
    } catch (...) { error="footer serialization failed"; truncated=true; }
    file.close();
}
inline bool write_data_locked(const Json& row) {
    try {
        const auto line=row.dump()+'\n';
        if (!byte_budget.data_fits(line.size())) {
            truncated=true; finish_locked("byte_limit"); return false;
        }
        file.write(line.data(),static_cast<std::streamsize>(line.size()));
        if (!file) { error="write failed"; truncated=true; finish_locked("write_error"); return false; }
        byte_budget.used+=line.size(); return true;
    } catch (...) {
        error="serialization failed"; truncated=true; finish_locked("serialization_error"); return false;
    }
}
inline Json status() {
    std::scoped_lock lock{mutex};
    if (!active() && file.is_open()) finish_locked("lease_expired");
    return {{"active",active()},{"path",path},{"id",id},{"rows",rows},{"render_rows",render_rows},
        {"bytes",byte_budget.used},{"byte_limit",byte_budget.limit},{"truncated",truncated},
        {"render_stage0_truncated",render_truncated},{"footer_written",footer_written},{"error",error}};
}
inline void stop(const std::string& expected={}) {
    std::scoped_lock lock{mutex};
    if (!expected.empty() && expected!=id) throw std::runtime_error("Recording identity changed");
    finish_locked(active()?"stopped":"lease_expired");
}
inline Json start(const std::filesystem::path& directory, unsigned seconds) {
    std::scoped_lock lock{mutex};
    if (active()) throw std::runtime_error("A motion recording is already active");
    if (seconds==0 || seconds>300) throw std::runtime_error("Motion recording duration must be 1..300 seconds");
    if (file.is_open()) finish_locked("lease_expired");
    id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(unix_ms())+"-"+std::to_string(++session_serial);
    const auto folder=directory/L"recordings";
    std::filesystem::create_directories(folder);
    const auto target=folder/("motion-"+id+".jsonl");
    if (std::filesystem::exists(target)) throw std::runtime_error("Recording already exists");
    const auto utf=target.u8string(); path.assign(utf.begin(),utf.end());
    file.clear(); file.open(target,std::ios::binary|std::ios::out); error.clear();
    if (!file) throw std::runtime_error("Cannot create motion recording");
    rows=0; render_rows=0; next_sample=0; next_flush=0; byte_budget={};
    truncated=false; render_truncated=false; footer_written=false;
    if (!write_data_locked(Json{{"type","header"},{"version",2},{"pid",GetCurrentProcessId()},{"id",id},
        {"unix_ms",unix_ms()},{"clock_ms",GetTickCount64()},
        {"byte_limit",output_byte_limit},{"footer_reserved_bytes",footer_reserve},{"recording_end_expected",true},
        {"scope","game and post-animation camera, tracking-space headset pose, origin, recent rendered-eye views and controller aim when available; optional render_stage0 rows describe CPU draw setup, not GPU completion; timestamps and ages are not exact GPU-frame synchronization; no video/audio"}}))
        throw std::runtime_error("Cannot write motion recording header");
    file.flush();
    if (!file) { error="write failed"; truncated=true; finish_locked("write_error"); throw std::runtime_error("Cannot flush motion recording header"); }
    until=GetTickCount64()+seconds*1000ull;
    return {{"active",true},{"path",path},{"id",id}};
}
inline Json pad(const XINPUT_GAMEPAD& p) {
    return {{"buttons",p.wButtons},{"lt",p.bLeftTrigger},{"rt",p.bRightTrigger},
        {"lx",p.sThumbLX},{"ly",p.sThumbLY},{"rx",p.sThumbRX},{"ry",p.sThumbRY}};
}
inline bool append_render_stage0(Json observation, const std::string& expected={}) {
    if (!active()) return false;
    // The render hook must never wait for the camera/recording controller.
    // Sampling is already limited per eye and phase by the producer. A hard
    // independent cap prevents diagnostics from starving the camera trace.
    std::unique_lock lock{mutex,std::try_to_lock};
    if (!lock.owns_lock() || !active() || !file.is_open() || (!expected.empty() && expected!=id)) return false;
    if (render_rows>=render_row_limit) { truncated=true; render_truncated=true; return false; }
    if (!write_data_locked(Json{{"type","render_stage0"},{"seq",render_rows},{"observation",std::move(observation)}})) return false;
    ++render_rows;
    return true;
}
inline bool append(Json camera, const std::string& expected={}) {
    if (!active()) return false;
    std::scoped_lock lock{mutex};
    const auto now=GetTickCount64();
    if (!active() || !file.is_open() || (!expected.empty() && expected!=id) || now<next_sample) return false;
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
    if (!write_data_locked(Json{{"type","frame"},{"seq",rows},{"clock_ms",now},{"unix_ms",unix_ms()},
        {"camera",std::move(camera)},{"inputs_available",available},{"inputs",std::move(inputs)},
        {"input_sequence",wuwa_input_sequence_bridge::recording_snapshot(now)}})) return false;
    ++rows;
    if (now>=next_flush) { file.flush(); next_flush=now+500; }
    if (!file) { error="write failed"; truncated=true; finish_locked("write_error"); return false; }
    if (rows>=10000) { error="row limit reached"; truncated=true; finish_locked("frame_row_limit"); }
    return true;
}
}
