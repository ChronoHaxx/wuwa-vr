#pragma once

// The external launcher owns capture, output paths and process verification.
// UI-thread use only, polled only while the Recording section is open.
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>

namespace wuwa_video {
inline int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
inline int64_t process_created_ms() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    const auto stamp=(uint64_t{created.dwHighDateTime}<<32)|created.dwLowDateTime;
    return static_cast<int64_t>(stamp/10000)-11644473600000ll;
}
inline nlohmann::json read_object(const std::filesystem::path& path) {
    std::ifstream input{path,std::ios::binary};
    if (!input) throw std::runtime_error("Recording launcher is not connected.");
    std::string buffer(16385,'\0');
    input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    buffer.resize(static_cast<size_t>(input.gcount()));
    if (buffer.size()>16384) throw std::runtime_error("Recording status is too large.");
    auto value=nlohmann::json::parse(buffer);
    if (!value.is_object()) throw std::runtime_error("Invalid recording status.");
    return value;
}
inline bool identity_matches(const nlohmann::json& value,uint32_t pid,int64_t created,int64_t now) {
    try {
        const auto session=value.at("session").get<std::string>();
        const auto state=value.at("state").get<std::string>();
        const auto stamp=value.at("unix_ms").get<int64_t>();
        const auto birth=value.at("created_ms").get<int64_t>();
        const auto reply=value.at("last_request");
        if (!reply.is_object()) return false;
        if (!reply.empty() && (!reply.at("id").is_string() || !reply.at("ok").is_boolean() || !reply.at("message").is_string())) return false;
        if (!value.at("available").is_boolean() || !value.at("recording_id").is_string() ||
            !value.at("message").is_string() || !value.at("folder").is_string() ||
            !value.at("created_ms").is_number_integer() || !value.at("unix_ms").is_number_integer()) return false;
        return value.at("version").is_number_integer() && value.at("version")==1 &&
            value.at("pid").is_number_integer() && value.at("pid")==pid && created>0 &&
            birth>=created-2 && birth<=created+2 && stamp<=now+1000 && stamp>=now-5000 &&
            session.size()==32 && session.find_first_not_of("0123456789abcdef")==std::string::npos &&
            (state=="idle" || state=="starting" || state=="recording" || state=="finishing" ||
             state=="saved" || state=="busy" || state=="error");
    } catch (...) { return false; }
}
class Client {
public:
    void poll(const std::filesystem::path& profile,bool force=false) noexcept {
        const auto tick=GetTickCount64();
        if (!force && tick<m_next_poll) return;
        m_next_poll=tick+500;
        m_directory=profile/L"video-control";
        m_connected=false;
        try {
            auto value=read_object(m_directory/L"server.json");
            if (!identity_matches(value,GetCurrentProcessId(),process_created_ms(),unix_ms()))
                throw std::runtime_error("Recording launcher status is stale or belongs to another game.");
            m_status=std::move(value);
            m_connected=true;
            const auto reply=m_status.value("last_request",nlohmann::json::object());
            if (!m_pending.empty() && reply.value("id",std::string{})==m_pending) {
                m_error=reply.value("ok",false) ? "" : reply.value("message",std::string{"Recording request failed."});
                m_pending.clear();
            }
        } catch (...) { /* A stale/missing launcher never blocks the game. */ }
        if (!m_pending.empty() && tick>m_pending_until) {
            m_pending.clear();
            m_error="Recording request was not acknowledged. Check the launcher.";
        }
    }
    bool connected() const { return m_connected; }
    bool available() const { return m_connected && m_status.value("available",false); }
    bool pending() const { return !m_pending.empty(); }
    std::string state() const { return m_connected ? m_status.value("state",std::string{}) : "offline"; }
    std::string message() const {
        if (!m_error.empty()) return m_error;
        return m_connected ? m_status.value("message",std::string{}) : "";
    }
    std::string folder() const { return m_connected ? m_status.value("folder",std::string{}) : ""; }
    bool submit(const std::filesystem::path& profile,bool start,int fps=30,int width=1024,bool telemetry=true) noexcept {
        poll(profile,true);
        try {
            if (!available() || pending()) throw std::runtime_error("Wait for the recording launcher.");
            const auto current=state();
            if (start && (current=="starting" || current=="recording" || current=="finishing" || current=="busy"))
                throw std::runtime_error("Another operation is still running.");
            if (!start && current!="starting" && current!="recording") throw std::runtime_error("No recording is active.");
            if ((fps!=30 && fps!=45 && fps!=60) || (width!=720 && width!=1024 && width!=1280))
                throw std::runtime_error("Unsupported recording settings.");
            const auto id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++m_serial);
            nlohmann::json request{{"version",1},{"session",m_status.at("session")},{"id",id},
                {"pid",GetCurrentProcessId()},{"created_ms",process_created_ms()},
                {"expires_ms",unix_ms()+4000},{"action",start?"start":"stop"}};
            if (start) { request["fps"]=fps;request["eye_width"]=width;request["telemetry"]=telemetry; }
            else request["recording_id"]=m_status.at("recording_id");
            const auto target=m_directory/("request-"+std::to_string(GetCurrentProcessId())+".json");
            const auto temporary=m_directory/(id+".tmp");
            { std::ofstream out{temporary,std::ios::binary}; out<<request.dump();out.close();
              if (!out) { std::error_code ec;std::filesystem::remove(temporary,ec);
                  throw std::runtime_error("Cannot write recording request."); } }
            // Never overwrite an unacknowledged command, including a stop.
            if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)) {
                std::error_code ec;std::filesystem::remove(temporary,ec);
                throw std::runtime_error("Another recording request is pending.");
            }
            m_pending=id;m_pending_until=GetTickCount64()+6000;m_error.clear();return true;
        } catch (const std::exception& e) { m_error=e.what();return false; }
    }
private:
    std::filesystem::path m_directory;
    nlohmann::json m_status=nlohmann::json::object();
    std::string m_error,m_pending;
    uint64_t m_next_poll{},m_pending_until{},m_serial{};
    bool m_connected{};
};
} // namespace wuwa_video
