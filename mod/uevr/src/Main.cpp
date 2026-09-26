// dllmain
#include <windows.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <utility/Thread.hpp>

#include "Framework.hpp"

void startup_thread(HMODULE poc_module) {
    g_framework = std::make_unique<Framework>(poc_module);
}

BOOL APIENTRY DllMain(HANDLE handle, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH && reserved != nullptr) {
        // MSVC invokes user DllMain before CRT global destruction on detach.
        // Only a lock-free flag: no diagnostic cleanup under the loader lock.
        wuwa_lgui_probe::notify_process_exiting();
    }
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)startup_thread, handle, 0, nullptr);
    }

    return TRUE;
}
