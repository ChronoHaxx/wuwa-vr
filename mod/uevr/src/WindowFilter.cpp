#include "WindowFilter.hpp"

// Keep the existing process-owned instance; no thread-local initialization.
std::unique_ptr<WindowFilter> g_window_filter{};
static std::once_flag g_window_filter_once{};

WindowFilter& WindowFilter::get() {
    std::call_once(g_window_filter_once, [] { g_window_filter = std::make_unique<WindowFilter>(); });
    return *g_window_filter;
}

WindowFilter::WindowFilter() {
    m_job_thread = std::jthread([this](std::stop_token stop) { run(stop); });
}

WindowFilter::~WindowFilter() {
    m_job_thread.request_stop();
    m_jobs_ready.notify_all();
    if (m_job_thread.joinable()) m_job_thread.join();
}

void WindowFilter::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        HWND hwnd{};
        {
            std::unique_lock lock{m_mutex};
            // An empty queue is idle, not the end of the worker's lifetime.
            if (!m_jobs_ready.wait(lock, stop, [this] { return !m_window_jobs.empty(); })) return;
            hwnd = m_window_jobs.front();
            m_window_jobs.pop_front();
        }

        // Never hold the filter mutex while asking another thread for a title.
        // Its WndProc can itself Present (and query this filter).
        const auto filtered = is_filtered_nocache(hwnd);
        {
            std::scoped_lock lock{m_mutex};
            if (filtered.has_value()) {
                if (*filtered) m_filtered_windows.insert(hwnd);
                m_pending_windows.erase(hwnd);
            }
            // A timed-out/invalid query stays unknown until its own 2s deadline.
            // Preserve the former fail-open behavior without repeated messages.
        }
    }
}

bool WindowFilter::is_filtered(HWND hwnd) {
    if (hwnd == nullptr) return true;
    std::scoped_lock lock{m_mutex};
    if (m_filtered_windows.contains(hwnd)) return true;
    const auto now = std::chrono::steady_clock::now();
    if (const auto pending = m_pending_windows.find(hwnd); pending != m_pending_windows.end()) {
        // Per-request age replaces the racy shared worker heartbeat. Pending
        // title work cannot block Present waiting for the worker or its mutex.
        if (now - pending->second <= std::chrono::seconds{2}) return true;
        m_pending_windows.erase(pending);
        return false;
    }
    if (m_seen_windows.insert(hwnd).second) {
        m_pending_windows.emplace(hwnd, now);
        m_window_jobs.push_back(hwnd);
        m_jobs_ready.notify_one();
        return true;
    }
    return false;
}

std::optional<bool> WindowFilter::is_filtered_nocache(HWND hwnd) {
    DWORD process{};
    const auto thread = GetWindowThreadProcessId(hwnd, &process);
    if (thread == 0) return std::nullopt;
    char title[256]{};
    if (process != GetCurrentProcessId()) {
        // GetWindowText reads the system caption for another process, without
        // dispatching WM_GETTEXT; retain the original cross-process semantics.
        GetWindowTextA(hwnd, title, sizeof(title));
    } else {
        // This private worker creates no HWND or shared input queue. Refuse a
        // same-thread target because SendMessageTimeout ignores that timeout.
        if (thread == GetCurrentThreadId()) return std::nullopt;
        DWORD_PTR result{};
        // WM_GETTEXT is a system-marshaled message, NOT a custom pointer message.
        // USER retains any in-flight receiver buffer until its WndProc returns,
        // including after a timeout. No callback or stack pointer is retained by us.
        // https://devblogs.microsoft.com/oldnewthing/20110915-00/?p=9643
        if (SendMessageTimeoutA(hwnd, WM_GETTEXT, sizeof(title), reinterpret_cast<LPARAM>(title),
            SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 200, &result) == 0) {
            return std::nullopt;
        }
    }
    title[sizeof(title) - 1] = '\0';
    const std::string_view name{title};
    return name.find("UE4SS") != std::string_view::npos || name.find("PimaxXR") != std::string_view::npos;
}
