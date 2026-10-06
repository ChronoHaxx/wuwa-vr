#pragma once

#include <Windows.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

class WindowFilter {
public:
    static WindowFilter& get();
    WindowFilter();
    virtual ~WindowFilter();
    bool is_filtered(HWND hwnd);

    void filter_window(HWND hwnd) {
        std::scoped_lock lock{m_mutex};
        m_filtered_windows.insert(hwnd);
    }

private:
    static std::optional<bool> is_filtered_nocache(HWND hwnd);
    void run(std::stop_token stop);

    std::mutex m_mutex{};
    std::condition_variable_any m_jobs_ready{};
    std::deque<HWND> m_window_jobs{};
    std::unordered_map<HWND, std::chrono::steady_clock::time_point> m_pending_windows{};
    std::unordered_set<HWND> m_seen_windows{};
    std::unordered_set<HWND> m_filtered_windows{};
    std::jthread m_job_thread{};
};
