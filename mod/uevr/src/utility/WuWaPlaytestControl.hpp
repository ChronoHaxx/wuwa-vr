#pragma once

// UI-thread client only. The existing external helper owns session persistence.
// No game writes, recording, device access or shell actions are exposed here.
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <stdexcept>

namespace wuwa_playtest {
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
inline bool hex_id(const std::string& value) {
    return value.size()==32 && value.find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline bool result_status(const std::string& value) {
    return value=="pass" || value=="fail" || value=="blocked" || value=="not_tested";
}
inline nlohmann::json read_object(const std::filesystem::path& path) {
    std::ifstream input{path,std::ios::binary};
    if (!input) throw std::runtime_error("Playtest launcher is not connected.");
    std::string buffer(65537,'\0');
    input.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
    buffer.resize(static_cast<size_t>(input.gcount()));
    if (buffer.size()>65536) throw std::runtime_error("Playtest status is too large.");
    auto result=nlohmann::json::parse(buffer);
    if (!result.is_object()) throw std::runtime_error("Invalid playtest status.");
    return result;
}
inline bool identity_matches(const nlohmann::json& value,uint32_t pid,int64_t created,int64_t now) {
    try {
        if (!value.at("version").is_number_integer() || value.at("version")!=1 ||
            !value.at("pid").is_number_integer() || value.at("pid")!=pid ||
            !value.at("created_ms").is_number_integer() || !value.at("unix_ms").is_number_integer() ||
            !value.at("available").is_boolean() || !value.at("voice_busy").is_boolean() ||
            !value.at("connection_error").is_string()) return false;
        const auto nonce=value.at("bridge_session").get<std::string>();
        const auto stamp=value.at("unix_ms").get<int64_t>();
        const auto birth=value.at("created_ms").get<int64_t>();
        if (!hex_id(nonce) || created<=0 || birth<created-2 || birth>created+2 ||
            stamp>now+1000 || stamp<now-5000) return false;
        const auto& reply=value.at("last_request");
        if (!reply.is_object() || (!reply.empty() &&
            (!reply.at("id").is_string() || !reply.at("ok").is_boolean() ||
             !reply.at("action").is_string() || !reply.at("message").is_string()))) return false;
        const auto& session=value.at("playtest");
        if (session.is_null()) return !value.at("available").get<bool>();
        if (!session.is_object() || !hex_id(session.at("id").get<std::string>()) ||
            !session.at("build_name").is_string() || !session.at("checks").is_array() ||
            !session.at("recording_count").is_number_integer()) return false;
        const auto state=session.at("state").get<std::string>();
        if (state!="active" && state!="finished") return false;
        if (value.at("available").get<bool>() && state!="active") return false;
        const auto& checks=session.at("checks");
        if (checks.empty() || checks.size()>32) return false;
        for (const auto& check:checks) {
            const auto id=check.at("id").get<std::string>();
            if (id.empty() || id.size()>96 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos ||
                !result_status(check.at("status").get<std::string>())) return false;
            for (const auto* key:{"title","instructions"})
                if (!check.at(key).is_object() || !check.at(key).at("en").is_string()) return false;
        }
        return true;
    } catch (...) { return false; }
}

class Client {
public:
    void poll(const std::filesystem::path& profile,bool force=false) noexcept {
        const auto tick=GetTickCount64();
        if (!force && tick<m_next_poll) return;
        m_next_poll=tick+500;
        m_directory=profile/L"playtest-control";
        m_connected=false;
        try {
            auto value=read_object(m_directory/L"server.json");
            if (!identity_matches(value,GetCurrentProcessId(),process_created_ms(),unix_ms()))
                throw std::runtime_error("Playtest launcher status is stale or belongs to another game.");
            if (!m_pending.empty() && value.at("bridge_session")!=m_pending_nonce) {
                m_pending.clear();
                m_message="Playtest launcher connection changed. Check whether the previous action was saved.";
                m_failed=true;
            }
            m_status=std::move(value);
            m_connected=true;
            const auto& reply=m_status.at("last_request");
            if (!m_pending.empty() && reply.value("id",std::string{})==m_pending) {
                m_message=reply.value("message",std::string{"Playtest request failed."});
                m_failed=!reply.value("ok",false);
                m_ack=reply;
                m_pending.clear();
            }
        } catch (const std::exception& e) {
            m_connection_error=e.what();
        }
        if (!m_pending.empty() && tick>m_pending_until) {
            m_pending.clear();
            m_message="Playtest request was not acknowledged. Check the launcher before repeating it.";
            m_failed=true;
        }
    }
    bool connected() const { return m_connected; }
    bool available() const { return m_connected && m_status.value("available",false); }
    bool pending() const { return !m_pending.empty(); }
    bool failed() const { return m_failed; }
    bool voice_busy() const { return m_connected && m_status.value("voice_busy",false); }
    std::string pending_id() const { return m_pending; }
    std::string message() const { return m_message; }
    std::string connection_error() const {
        return m_connected ? m_status.value("connection_error",std::string{}) : m_connection_error;
    }
    nlohmann::json acknowledgement() const { return m_ack; }
    nlohmann::json session() const {
        return m_connected ? m_status.value("playtest",nlohmann::json{}) : nlohmann::json{};
    }
    bool submit(const std::filesystem::path& profile,const std::string& session_id,
                const std::string& action,const std::string& item_id="",const std::string& value="",
                bool confirm_untested=false) noexcept {
        poll(profile,true);
        std::filesystem::path temporary;
        try {
            if (!available() || pending()) throw std::runtime_error("Wait for an active playtest in the launcher.");
            const auto current=session();
            if (current.at("id")!=session_id)
                throw std::runtime_error("The playtest session changed. Review it before saving.");
            if (action=="result" || action=="note") {
                bool found=false;
                for (const auto& check:current.at("checks")) if (check.at("id")==item_id) found=true;
                if (!found) throw std::runtime_error("Choose a checklist item in this playtest.");
            } else if (action!="finish" && action!="link-recording") {
                throw std::runtime_error("Unknown playtest action.");
            } else if (!item_id.empty()) throw std::runtime_error("Session actions cannot name a checklist item.");
            if (action=="result" && !result_status(value)) throw std::runtime_error("Choose a playtest result.");
            if (action=="note" && (value.empty() || value.size()>4000 || value.find_first_not_of(" \t\r\n")==std::string::npos))
                throw std::runtime_error("Write a short observation before saving.");
            if (action=="finish" && (!confirm_untested || voice_busy()))
                throw std::runtime_error("Stop voice activity and confirm untested checks before finishing.");
            const auto id=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(++m_serial);
            nlohmann::json request{{"version",1},{"bridge_session",m_status.at("bridge_session")},{"id",id},
                {"pid",GetCurrentProcessId()},{"created_ms",process_created_ms()},
                {"expires_ms",unix_ms()+4000},{"playtest_session",session_id},{"item_id",item_id},{"action",action}};
            if (action=="result") request["status"]=value;
            if (action=="note") request["note"]=value;
            if (action=="finish") request["confirm_untested"]=confirm_untested;
            const auto target=m_directory/("request-"+std::to_string(GetCurrentProcessId())+".json");
            temporary=m_directory/(id+".tmp");
            const auto encoded=request.dump();
            if (encoded.size()>16384) throw std::runtime_error("Playtest request is too large.");
            { std::ofstream output{temporary,std::ios::binary}; output<<encoded;output.close();
              if (!output) throw std::runtime_error("Cannot write playtest request."); }
            // Never overwrite an unacknowledged action from this process.
            if (!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Another playtest request is pending. Wait for the launcher.");
            m_pending=id;m_pending_nonce=m_status.at("bridge_session").get<std::string>();
            m_pending_until=GetTickCount64()+6000;m_message="Waiting for playtest acknowledgement...";
            m_failed=false;return true;
        } catch (const std::exception& e) {
            if (!temporary.empty()) { std::error_code ec;std::filesystem::remove(temporary,ec); }
            m_message=e.what();m_failed=true;return false;
        }
    }
private:
    std::filesystem::path m_directory;
    nlohmann::json m_status=nlohmann::json::object(),m_ack=nlohmann::json::object();
    std::string m_message,m_connection_error,m_pending,m_pending_nonce;
    uint64_t m_next_poll{},m_pending_until{},m_serial{};
    bool m_connected{},m_failed{};
};

// Defined once in WuWaControlsComponent.cpp; VR.cpp only calls this function.
void draw_controls(const std::filesystem::path& profile);
} // namespace wuwa_playtest
