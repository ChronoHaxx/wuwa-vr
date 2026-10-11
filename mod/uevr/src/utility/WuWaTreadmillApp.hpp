#pragma once
// Whether the Reality Runner companion app (RealityRunner.exe) is open, for automatic
// treadmill runs (WuWaRunMarker.hpp). The process list is read on a short-lived worker every
// 5 seconds, never on the game thread; only the process names are compared.
#include <windows.h>
#include <tlhelp32.h>

#include <chrono>
#include <cwchar>
#include <future>

#include "WuWaRunMarker.hpp"

namespace wuwa_treadmill {
inline bool scan() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry)) {
        found = _wcsicmp(entry.szExeFile, L"RealityRunner.exe") == 0;
    }
    CloseHandle(snapshot);
    return found;
}

// Game thread, every tick: starts a scan every 5 s and publishes the last answer.
inline void poll() {
    static std::future<bool> pending{};
    static auto last = std::chrono::steady_clock::time_point{};
    if (pending.valid()) {
        if (pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        wuwa_run::treadmill_app = pending.get();
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds(5)) return;
    last = now;
    pending = std::async(std::launch::async, scan);
}
}
