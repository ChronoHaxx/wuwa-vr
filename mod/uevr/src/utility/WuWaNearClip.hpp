#pragma once

// Close-up near clip (test 15). Story close-ups can put the game's camera just
// behind someone's head: outside the narrow 2D frame, but inside the headset's
// wide view, so in VR the head fills one eye. The camera script
// (02_WuWaVR_PolarControls.lua) asks, every frame of a close-up, for a larger
// near clip plane, already kept 10 cm short of the person in focus and anyone
// it finds in the 2D frame (WuWaControls_NearClipNow). The game's own value
// comes back as soon as the request stops or lapses.
//
// UEVR's viewport-client hook runs before the scripts' one in the same frame,
// so a request made on the game thread is applied at once; otherwise the first
// frame of a new shot would still show what the plane is meant to hide.
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

namespace wuwa_near_clip {
// The plane the script wants, in cm (0: the game's own). A request lapses after
// a second, so a stopped or failing script cannot leave the plane raised.
inline std::atomic<float> wanted{0.0f};
inline std::atomic<uint64_t> wanted_at{0};
// Test 16: when the camera script last found a character it would hide (Hide characters at
// your eyes is off), for the note that names the shortcut (WuWaControls_NearSeen).
inline std::atomic<uint64_t> near_seen_ms{0};

class Applier {
public:
    // Game thread, before the viewport draws. `plane` is GNearClippingPlane, in cm. Only ever raises it.
    void apply(float& plane) {
        m_plane = &plane;
        m_thread = GetCurrentThreadId();
        const float want = GetTickCount64() - wanted_at.load() < 1000 ? wanted.load() : 0.0f;
        if (m_raised && plane != m_written) {
            // Something else set the plane (the game, a console command): that is its own value now.
            m_original = plane;
            m_raised = false;
        }
        const float own = m_raised ? m_original : plane;
        if (want > own) {
            if (!m_raised) m_original = plane;
            if (!m_raised || want != m_written) spdlog::info("[WuWaNearClip] {:.0f} cm in this close-up (game's own {:.0f} cm)", want, m_original);
            m_raised = true;
            m_written = want;
            plane = want;
        } else if (m_raised) {
            spdlog::info("[WuWaNearClip] back to the game's own {:.0f} cm", m_original);
            plane = m_original;
            m_raised = false;
        }
    }

    void apply_if_game_thread() {
        if (m_plane != nullptr && m_thread == GetCurrentThreadId()) apply(*m_plane);
    }

    // UEVR's own near clip option is on and writes the plane itself: stand down without writing.
    void release() {
        m_raised = false;
        m_plane = nullptr;
    }

private:
    float* m_plane{};
    DWORD m_thread{};
    float m_original{}, m_written{};
    bool m_raised{false};
};

inline Applier applier{};

inline void request(const std::string& text) {
    float value = 0.0f;
    try { value = std::stof(text); } catch (...) { value = 0.0f; }
    wanted.store(std::isfinite(value) && value > 0.0f && value <= 100.0f ? value : 0.0f);
    wanted_at.store(GetTickCount64());
    applier.apply_if_game_thread();
}
}
