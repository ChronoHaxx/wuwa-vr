#pragma once
// Ends a recording when the game itself slows to a crawl (owner, 25 Oct 2026): in full VR a PC near its limits can
// fall to 2-8 fps while recording and stay there until the game is closed. The rule: the game's own frame rate (new
// frames it gave SteamVR), averaged over the last `seconds` of time it held the scene, is under `min_fps`. Time when
// SteamVR showed something else (a scene change, a hang) is left out, and the first `seconds` of a recording always
// pass. In the owner's recordings loading freezes lasted up to about 10 s, and every lag spiral (18, 20 and 25 Oct)
// averaged under 5 fps for 20 s or more: 5 fps for 20 s is the default the launcher passes.
#include <cstdint>
#include <deque>

class SlowGame {
public:
    SlowGame(double min_fps, double seconds) : m_min_fps(min_fps), m_seconds(seconds) {}
    bool enabled() const { return m_min_fps > 0 && m_seconds > 0; }
    // t: seconds since the recording began; frame: the game's frame index (Compositor_FrameTiming::m_nFrameIndex).
    void sample(double t, uint32_t frame) {
        if (m_have_last && !m_paused && t > m_last_t) m_active += t - m_last_t;
        // Frames the game drew while it did not hold the scene are left out with that time.
        if (m_have_last && m_paused) m_excluded += frame - m_last_frame;
        m_have_last = true; m_paused = false; m_last_t = t; m_last_frame = frame;
        frame -= m_excluded;
        // At most ten stored samples a second: the showcase loop polls every 2 ms.
        if (m_samples.size() > 1 && m_active - m_samples[m_samples.size() - 2].active < 0.1) m_samples.back() = {m_active, frame};
        else m_samples.push_back({m_active, frame});
        while (m_samples.size() > 2 && m_samples[1].active <= m_active - m_seconds) m_samples.pop_front();
    }
    // The scene left the game at t: the time until the next sample does not count.
    void pause(double t) {
        if (m_have_last && !m_paused && t > m_last_t) m_active += t - m_last_t;
        m_paused = true; m_last_t = t;
    }
    // The average over the window, or -1 until a whole window of the game's time has passed.
    double fps() const {
        if (m_samples.size() < 2) return -1;
        const auto& first = m_samples.front(); const auto& last = m_samples.back();
        const double span = last.active - first.active;
        if (first.active > m_active - m_seconds || span <= 0) return -1;
        return static_cast<uint32_t>(last.frame - first.frame) / span;
    }
    bool slow() const {
        if (!enabled()) return false;
        const double rate = fps();
        return rate >= 0 && rate < m_min_fps;
    }
    double active_seconds() const { return m_active; }
    size_t stored() const { return m_samples.size(); }

private:
    struct Sample { double active; uint32_t frame; };
    double m_min_fps, m_seconds, m_active{}, m_last_t{};
    uint32_t m_last_frame{}, m_excluded{};
    bool m_have_last{}, m_paused{};
    std::deque<Sample> m_samples;
};
