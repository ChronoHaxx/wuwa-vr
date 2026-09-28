#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace wuwa_stereo {
// Owned by the existing frame-thread request pump. Does not save settings.
class CandidateLease {
    uint64_t m_until{};
    std::string m_id;
    std::array<bool,3> m_values{};
public:
    bool active(uint64_t now) const {return m_until>now;}
    const std::string& id() const {return m_id;}
    const auto& values() const {return m_values;}
    uint64_t remaining(uint64_t now) const {return active(now) ? m_until-now:0;}
    void begin(uint64_t now,int seconds,std::string id,std::array<bool,3> values) {
        if (active(now)) throw std::runtime_error("Another stereo candidate comparison is active");
        if (seconds<1 || seconds>60 || id.empty() || id.size()>64 || now>UINT64_MAX-60000)
            throw std::runtime_error("Invalid stereo candidate comparison");
        m_id=std::move(id); m_values=values; m_until=now+uint64_t(seconds)*1000;
    }
    void end(uint64_t now,const std::string& id) {
        if (active(now) && id!=m_id) throw std::runtime_error("Stereo comparison lease id differs");
        m_until=0;
    }
    bool value(uint64_t now,size_t i,bool configured) const {
        return active(now) && i<m_values.size() ? m_values[i]:configured;
    }
};
} // namespace wuwa_stereo
