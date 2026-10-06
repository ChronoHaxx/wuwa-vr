#include <Windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include "WindowFilter.hpp"

// Runs either the original or repaired production WindowFilter with hidden windows.
// The original deadlock reproducer exits only its own process from a watchdog: normal
// destruction cannot join a worker deliberately blocked in synchronous WM_GETTEXT.
static WindowFilter* filter = nullptr;
static std::atomic<unsigned> text_calls{0};
static std::atomic<bool> reenter{false}, entered{false}, returned{false};
static std::atomic<bool> slow_response{false}, shutdown_done{false}, late_write_after_shutdown{false};

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_GETTEXT) {
        ++text_calls;
        if (slow_response.load()) {
            entered = true;
            Sleep(650);
            late_write_after_shutdown = shutdown_done.load();
            // Deliberately complete WM_GETTEXT after SendMessageTimeout AND
            // destruction of the production worker. USER owns the marshaled
            // receiver buffer until this WndProc completes.
            const auto result = DefWindowProcA(window, message, wp, lp);
            returned = true;
            return result;
        }
        if (reenter.load()) {
            entered = true;
            // On the HWND owner thread, synchronously invoked by production's
            // worker. The worker is waiting here while owning filter's mutex.
            filter->is_filtered(window);
            returned = true;
        }
    }
    return DefWindowProcA(window, message, wp, lp);
}

struct HiddenWindow {
    HWND handle{};
    explicit HiddenWindow(const char* title = "UE4SS own-process filter probe") {
        WNDCLASSA wc{};
        wc.lpfnWndProc = window_proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "WuWaWindowFilterOwnProcessRegression";
        require(RegisterClassA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "RegisterClass failed");
        handle = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPED,
            0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        require(handle != nullptr && !IsWindowVisible(handle), "Hidden window creation failed");
        char read_title[80]{};
        require(GetWindowTextA(handle, read_title, sizeof(read_title)) > 0 && std::string(read_title) == title,
            "WM_GETTEXT positive control failed");
        text_calls = 0;
    }
    ~HiddenWindow() {
        DestroyWindow(handle);
        UnregisterClassA("WuWaWindowFilterOwnProcessRegression", GetModuleHandleA(nullptr));
    }
};

static void pump_for(std::chrono::milliseconds duration) {
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until) {
        MSG message{};
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Sleep(1);
    }
}

int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3, "Use a test mode and optional --expect-original");
        const std::string mode{argv[1]};
        const bool original = argc == 3 && std::string(argv[2]) == "--expect-original";
        require(argc == 2 || original, "Unknown expectation");
        require(mode == "--active-control" || mode == "--empty-queue" || mode == "--query-lock" ||
            mode == "--nonpumping-shutdown" || mode == "--late-response", "Unknown mode");
        require(!original || mode == "--active-control" || mode == "--empty-queue" || mode == "--query-lock",
            "Teardown checks require repaired production source");
        std::cout << "scope\tproduction_WindowFilter\thidden_own_process_window=true\tgame=false\texpect_original="
            << original << "\tmode=" << mode << std::endl;
        HiddenWindow window;
        auto owned_filter = std::make_unique<WindowFilter>();
        filter = owned_filter.get();
        if (mode == "--empty-queue") {
            // The production worker polls at 100ms and currently returns when
            // empty. Leave it idle before the first request; do not inspect internals.
            Sleep(450);
            require(filter->is_filtered(window.handle), "First sighting should queue/filter the window");
            pump_for(std::chrono::milliseconds{2400});
            const bool filtered = filter->is_filtered(window.handle);
            if (original) {
                require(text_calls == 0 && !filtered, "Empty-queue exit was not reproduced");
                std::cout << "REPRODUCED\tempty_queue_worker_exit\ttitle_queries=0\tknown_blocked_title_allowed=true"
                    "\tobservation_after_queue_ms=2400" << std::endl;
            } else {
                require(text_calls > 0 && filtered, "Idle worker failed to resume and preserve title exclusion");
                std::cout << "PASS\tworker_survives_empty_queue\ttitle_queried=true\tknown_blocked_title_filtered=true" << std::endl;
            }
        } else if (mode == "--active-control") {
            require(filter->is_filtered(window.handle), "First sighting should queue/filter the window");
            pump_for(std::chrono::milliseconds{450});
            require(text_calls > 0 && filter->is_filtered(window.handle), "Active worker did not filter the known blocked title");
            std::cout << "PASS\tactive_worker_positive_control\ttitle_queried=true\tknown_blocked_title_filtered=true" << std::endl;
            require(filter->is_filtered(nullptr), "Null window should remain filtered");
            // A separate fresh filter/window is needed for the old implementation,
            // whose worker has already exited by this point.
            if (!original) {
                HiddenWindow pimax{"PimaxXR diagnostic"}, ordinary{"WuWa ordinary title"};
                require(filter->is_filtered(pimax.handle) && filter->is_filtered(ordinary.handle), "New windows must begin pending");
                pump_for(std::chrono::milliseconds{450});
                require(filter->is_filtered(pimax.handle) && !filter->is_filtered(ordinary.handle), "Title exclusions/ordinary allowance changed");
                filter->filter_window(ordinary.handle);
                require(filter->is_filtered(ordinary.handle), "Explicit filter_window no longer works");
                std::cout << "PASS\tpimax_exclusion_and_ordinary_allowance_and_explicit_filter" << std::endl;
            }
        } else if (mode == "--nonpumping-shutdown") {
            require(filter->is_filtered(window.handle), "First sighting should queue the window");
            Sleep(50); // Let the worker enter its timed query; never pump this HWND.
            const auto start = std::chrono::steady_clock::now();
            owned_filter.reset();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            require(elapsed < 1000 && text_calls == 0, "Nonpumping-window shutdown was not bounded");
            std::cout << "PASS\tnonpumping_window_shutdown\telapsed_ms=" << elapsed << "\ttitle_queries_dispatched=0" << std::endl;
        } else if (mode == "--late-response") {
            slow_response = true;
            require(filter->is_filtered(window.handle), "First sighting should queue the window");
            std::atomic<long long> shutdown_ms{-1};
            std::thread closer([&] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
                while (!entered && std::chrono::steady_clock::now() < deadline) Sleep(1);
                if (!entered) return;
                Sleep(300);
                const auto start = std::chrono::steady_clock::now();
                owned_filter.reset();
                shutdown_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                shutdown_done = true;
            });
            pump_for(std::chrono::milliseconds{900});
            closer.join();
            slow_response = false;
            require(entered && returned && shutdown_done && late_write_after_shutdown && shutdown_ms >= 0 && shutdown_ms < 500,
                "Delayed WndProc did not safely finish writing after bounded worker destruction");
            std::cout << "PASS\tlate_WM_GETTEXT_completion\tworker_destroyed_before_receiver_write=true\tshutdown_ms="
                << shutdown_ms << "\tcallback_threads_detached=false" << std::endl;
        } else {
            reenter = true;
            require(filter->is_filtered(window.handle), "First sighting should queue/filter the window");
            if (!original) {
                pump_for(std::chrono::milliseconds{600});
                require(entered && returned && filter->is_filtered(window.handle), "WM_GETTEXT filter reentry deadlocked or lost exclusion");
                std::cout << "PASS\tWM_GETTEXT_filter_reentry_returns\ttitle_exclusion_preserved=true" << std::endl;
            } else {
            std::thread watchdog([] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
                while (!entered && std::chrono::steady_clock::now() < deadline) Sleep(1);
                if (!entered) {
                    std::cerr << "FAIL\tNo production WM_GETTEXT observed within deadline" << std::endl;
                    ExitProcess(2);
                }
                Sleep(2300); // Longer than production's nominal 2s fail-open.
                if (returned) {
                    std::cerr << "NOT_REPRODUCED\tWM_GETTEXT reentry returned; query-under-lock deadlock absent" << std::endl;
                    ExitProcess(3);
                }
                std::cout << "REPRODUCED\tquery_under_lock_deadlock\tWM_GETTEXT_entered=true"
                    "\tnested_is_filtered_returned=false\tblocked_ms_at_least=2300"
                    "\twatchdog_exits_only_own_process=true" << std::endl;
                ExitProcess(0);
            });
            watchdog.detach();
            pump_for(std::chrono::seconds{7});
            throw std::runtime_error("Watchdog failed to exit query-lock test");
            }
        }
        require(!IsWindowVisible(window.handle), "Test window became visible");
        filter = nullptr;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL\t" << error.what() << std::endl;
        return 1;
    }
}
