#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

// Treadmill runs. A run starts from the VR menu, the classic menu, or (with Reality Runner
// open) by itself after 10 seconds of walking; a run that started by itself ends after 5
// minutes without walking. While a run is on, the player's position goes once a second to
// <profile>/wuwa-runs/run-<local time>.jsonl, so the route needs no map screenshots.
// Each start and end appends a line to <profile>/wuwa-runs.jsonl ({"event","unix_ms",...})
// and flashes a magenta square in the desktop view for a few frames, so a recording of the
// desktop view (OBS) has an exact, findable sync frame. The headset image is not touched.
namespace wuwa_run {

// ---- Pure rules (dev/test_run_tracker.cpp) ----

constexpr int64_t walk_gap_ms = 1500;          // stick pushes closer than this are one walk
constexpr int64_t auto_start_ms = 10000;       // walking this long starts a run by itself
constexpr int64_t auto_end_ms = 5 * 60000;     // a run that started by itself ends after this long idle
constexpr double max_step_cm = 3000.0;         // more than 30 m between samples is a teleport, not walking
constexpr float walk_stick = 0.2f;             // left stick deflection that counts as walking

struct Totals {
    int64_t duration_ms{};
    int64_t walking_ms{};
    double distance_cm{};
};

enum class Decision { None, Start, End };

struct Policy {
    bool active{}, automatic{};
    int64_t started_ms{}, ended_ms{};
    int64_t last_walk_ms{}, walk_since_ms{};
    bool has_position{};
    double x{}, y{}, z{};
    Totals totals{};

    bool walking(int64_t now) const { return last_walk_ms != 0 && now - last_walk_ms <= walk_gap_ms; }

    // A moment of walking input (a physical left stick pushed) at `now`.
    void walk(int64_t now) {
        if (!walking(now)) walk_since_ms = now;
        else if (active) totals.walking_ms += now - last_walk_ms;
        last_walk_ms = now;
    }

    Decision decide(int64_t now, bool automatic_runs) const {
        if (!automatic_runs) return Decision::None;
        if (!active && walking(now) && now - walk_since_ms >= auto_start_ms) return Decision::Start;
        if (active && automatic && now - (std::max)(last_walk_ms, started_ms) >= auto_end_ms) return Decision::End;
        return Decision::None;
    }

    void start(int64_t now, bool by_walking) {
        active = true;
        automatic = by_walking;
        started_ms = now;
        ended_ms = 0;
        has_position = false;
        totals = {};
    }

    // An automatic end (trim_idle) stops the clock at the last step, not after the idle wait.
    void end(int64_t now, bool trim_idle) {
        const auto stop = trim_idle && last_walk_ms > started_ms ? (std::min)(now, last_walk_ms) : now;
        totals.duration_ms = (std::max)(int64_t{0}, stop - started_ms);
        ended_ms = stop;
        active = false;
    }

    // Once a second while active: the player's position in game units (cm).
    // Returns false for a jump too large to be walking (a teleport), which is not counted.
    bool position(double px, double py, double pz) {
        if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) return false;
        bool counted = true;
        if (has_position) {
            const double step = std::hypot(px - x, py - y);
            if (step <= max_step_cm) totals.distance_cm += step;
            else counted = false;
        }
        x = px; y = py; z = pz;
        has_position = true;
        return counted;
    }

    Totals current(int64_t now) const {
        auto out = totals;
        if (active) out.duration_ms = (std::max)(int64_t{0}, now - started_ms);
        return out;
    }
};

inline std::string clock_text(int64_t ms) {
    const auto seconds = (std::max)(int64_t{0}, ms / 1000);
    char text[32];
    if (seconds >= 3600) std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60);
    else std::snprintf(text, sizeof(text), "%lld:%02lld", seconds / 60, seconds % 60);
    return text;
}

// ---- Shared state and files ----

inline std::atomic<int> flash_frames{};
inline std::mutex mutex{};
inline Policy policy{};
inline std::ofstream route{};
inline std::string route_name{};
inline std::atomic<bool> treadmill_app{}; // Reality Runner is open (VR.cpp checks every few seconds)

inline int64_t unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

inline bool active() {
    std::scoped_lock lock{mutex};
    return policy.active;
}

inline Totals totals() {
    std::scoped_lock lock{mutex};
    return policy.current(unix_ms());
}

// True once a run has ended in this launch (totals() then holds that run).
inline bool has_finished_run() {
    std::scoped_lock lock{mutex};
    return !policy.active && policy.ended_ms != 0;
}

namespace detail {
inline bool append(const std::filesystem::path& profile, const std::string& line) {
    std::ofstream out(profile / "wuwa-runs.jsonl", std::ios::app | std::ios::binary);
    out << line << "\n";
    return static_cast<bool>(out);
}

inline std::string local_stamp() {
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}
}

// Starts a run (how: "manual" or "treadmill"). False if one is already on or the log failed.
inline bool start(const std::filesystem::path& profile, const char* how) {
    std::scoped_lock lock{mutex};
    if (policy.active) return false;
    const auto now = unix_ms();
    std::error_code ec;
    std::filesystem::create_directories(profile / "wuwa-runs", ec);
    route_name = "wuwa-runs/run-" + detail::local_stamp() + ".jsonl";
    route.close();
    route.open(profile / route_name, std::ios::app | std::ios::binary);
    policy.start(now, std::string_view{how} == "treadmill");
    flash_frames = 6;
    return detail::append(profile, std::string{"{\"event\":\"start\",\"unix_ms\":"} + std::to_string(now) + ",\"how\":\"" +
        how + "\",\"route\":\"" + route_name + "\"}");
}

inline bool end(const std::filesystem::path& profile, const char* how) {
    std::scoped_lock lock{mutex};
    if (!policy.active) return false;
    const auto now = unix_ms();
    policy.end(now, std::string_view{how} == "idle");
    route.close();
    flash_frames = 6;
    char summary[160];
    std::snprintf(summary, sizeof(summary), ",\"seconds\":%lld,\"walking_seconds\":%lld,\"distance_m\":%.0f}",
        policy.totals.duration_ms / 1000, policy.totals.walking_ms / 1000, policy.totals.distance_cm / 100.0);
    return detail::append(profile, std::string{"{\"event\":\"end\",\"unix_ms\":"} + std::to_string(now) + ",\"how\":\"" + how +
        "\",\"route\":\"" + route_name + "\"" + summary);
}

// Any thread (the XInput hook): a physical left stick at `deflection` (0..1).
inline void stick(float deflection) {
    if (deflection < walk_stick) return;
    std::scoped_lock lock{mutex};
    policy.walk(unix_ms());
}

// Game thread, every tick. `position` fills x/y/z (cm) and returns false when unknown.
// Starts or ends automatic runs and logs the route once a second.
template <typename PositionFn>
inline void tick(const std::filesystem::path& profile, bool automatic_runs, PositionFn&& position) {
    static int64_t last_sample = 0;
    const auto now = unix_ms();
    Decision decision;
    {
        std::scoped_lock lock{mutex};
        decision = policy.decide(now, automatic_runs && treadmill_app.load());
    }
    if (decision == Decision::Start) start(profile, "treadmill");
    else if (decision == Decision::End) end(profile, "idle");
    if (now - last_sample < 1000) return;
    last_sample = now;
    std::scoped_lock lock{mutex};
    if (!policy.active || !route.is_open()) return;
    double px{}, py{}, pz{};
    if (!position(px, py, pz)) return;
    const bool counted = policy.position(px, py, pz);
    char line[200];
    std::snprintf(line, sizeof(line), "{\"t\":%lld,\"x\":%.0f,\"y\":%.0f,\"z\":%.0f,\"walking\":%s%s}\n",
        (now - policy.started_ms) / 1000, px, py, pz, policy.walking(now) ? "true" : "false", counted ? "" : ",\"jump\":true");
    route << line;
    route.flush();
}

// Called once per desktop-view draw; true while the sync square should be shown.
inline bool take_flash() {
    auto left = flash_frames.load();
    while (left > 0 && !flash_frames.compare_exchange_weak(left, left - 1)) {}
    return left > 0;
}
}
