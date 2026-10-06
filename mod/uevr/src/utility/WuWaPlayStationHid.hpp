#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "WuWaPlayStationInput.hpp"
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace wuwa_ps_hid {
inline void module_anchor() {}
inline bool pin_backend() {
    HMODULE module{};
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&module_anchor), &module) != FALSE;
}
template<typename T> inline void release_on_process_exit(std::unique_ptr<T>& owner, bool process_exiting) {
    // ExitProcess has already terminated other threads; their mutex/IO state
    // must not be inspected, joined or destructed under the loader lock.
    if (process_exiting) (void)owner.release();
}
// Called only after a genuine successful XInput poll, before VR synthesis.
// An unrelated Xbox pad also takes priority: we cannot prove device identity
// through XInput. The UI states that limitation instead of guessing a match.
inline std::atomic<uint64_t> last_xinput{};
inline std::atomic<bool> hid_menu_held{}, xinput_menu_rearm{};
inline void observe_xinput(const XINPUT_GAMEPAD& pad, uint64_t now) {
    const auto previous = last_xinput.exchange(now);
    if (!wuwa_ps::recent(previous, now, 2000) && hid_menu_held.load()) xinput_menu_rearm.store(true);
    if (!hid_menu_held.load() && !pad.wButtons && pad.bLeftTrigger < 30 && pad.bRightTrigger < 30 &&
        std::abs(int(pad.sThumbLX)) < 8000 && std::abs(int(pad.sThumbLY)) < 8000 &&
        std::abs(int(pad.sThumbRX)) < 8000 && std::abs(int(pad.sThumbRY)) < 8000) xinput_menu_rearm.store(false);
}
inline bool suppress_xinput_menu() { return xinput_menu_rearm.load(); }

struct Snapshot {
    wuwa_ps::Pad pad{};
    uint64_t stamp{}, generation{}, started{};
    wuwa_ps::Model model{wuwa_ps::Model::unsupported};
    std::string status{"PlayStation reader has not started"};
};
class Reader {
public:
    using PinModule = bool (*)();
    explicit Reader(PinModule pin = &pin_backend) : m_pin{pin} {}
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    ~Reader() {
        if (m_stop) SetEvent(m_stop);
        if (m_thread.joinable()) m_thread.join();
        if (m_stop) CloseHandle(m_stop);
    }
    void start() {
        if (m_thread.joinable() || m_start_failed) return;
        // UEVR has no supported hot-unload path. Pin before creating a worker
        // so FreeLibrary cannot remove its code or invoke a joining destructor
        // under the loader lock. Confirmed process exit releases the owner.
        if (!m_pin || !m_pin()) {
            m_start_failed = true;
            publish({}, 0, wuwa_ps::Model::unsupported, "PlayStation reader unavailable: cannot retain backend module");
            return;
        }
        m_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!m_stop) { m_start_failed = true; publish({}, 0, wuwa_ps::Model::unsupported, "PlayStation reader unavailable"); return; }
        { std::scoped_lock lock{m_mutex}; m_snapshot.started = GetTickCount64(); }
        try { m_thread = std::thread{[this] { run(); }}; }
        catch (...) { m_start_failed = true; publish({}, 0, wuwa_ps::Model::unsupported, "PlayStation reader unavailable"); }
    }
    Snapshot snapshot() const { std::scoped_lock lock{m_mutex}; return m_snapshot; }
    bool active(const Snapshot& s, uint64_t now) const {
        return wuwa_ps::fallback_allowed(s.started, last_xinput.load(), s.stamp, now);
    }
    std::string status() const {
        const auto s = snapshot(); const auto now = GetTickCount64();
        if (wuwa_ps::recent(last_xinput.load(), now, 2000)) return "Xbox / Steam Input has priority; direct PlayStation shortcuts are waiting";
        if (active(s, now)) return s.model == wuwa_ps::Model::ds4 ? "DualShock 4 direct shortcuts (experimental)" : "DualSense direct shortcuts (experimental)";
        if (s.stamp && !wuwa_ps::recent(s.stamp, now, 250)) return "PlayStation reports stopped; release controls after reconnecting";
        return s.status;
    }
private:
    struct Device {
        HANDLE file{INVALID_HANDLE_VALUE}, event{};
        OVERLAPPED operation{};
        std::array<uint8_t, 1024> bytes{};
        DWORD length{};
        wuwa_ps::Model model{wuwa_ps::Model::unsupported};
        bool pending{};
        ~Device() {
            if (file != INVALID_HANDLE_VALUE) {
                if (pending) {
                    CancelIoEx(file, &operation);
                    DWORD ignored{};
                    // Storage must outlive the cancelled overlapped operation.
                    GetOverlappedResult(file, &operation, &ignored, TRUE);
                }
                CloseHandle(file);
            }
            if (event) CloseHandle(event);
        }
    };
    bool stopped() const { return WaitForSingleObject(m_stop, 0) == WAIT_OBJECT_0; }
    void publish(wuwa_ps::Pad pad, uint64_t stamp, wuwa_ps::Model model, const char* status) {
        std::scoped_lock lock{m_mutex};
        m_snapshot.pad = pad; m_snapshot.stamp = stamp; m_snapshot.model = model; m_snapshot.status = status;
    }
    std::unique_ptr<Device> discover() {
        GUID guid{}; HidD_GetHidGuid(&guid);
        const auto list = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (list == INVALID_HANDLE_VALUE) return {};
        struct Cleanup { HDEVINFO list; ~Cleanup() { SetupDiDestroyDeviceInfoList(list); } } cleanup{list};
        bool inaccessible{};
        for (DWORD i = 0; i < 512 && !stopped(); ++i) {
            SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(list, nullptr, &guid, i, &iface)) break;
            DWORD needed{};
            SetupDiGetDeviceInterfaceDetailW(list, &iface, nullptr, 0, &needed, nullptr);
            if (needed < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || needed > 32768) continue;
            std::vector<uint8_t> storage(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(list, &iface, detail, needed, nullptr, nullptr)) continue;
            // A zero-access metadata handle precedes the read handle. Never
            // open unrelated devices for input, and never request write access.
            const auto meta = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (meta == INVALID_HANDLE_VALUE) continue;
            HIDD_ATTRIBUTES attributes{}; attributes.Size = sizeof(attributes);
            const bool got = HidD_GetAttributes(meta, &attributes) != FALSE;
            const auto kind = got ? wuwa_ps::model(attributes.VendorID, attributes.ProductID) : wuwa_ps::Model::unsupported;
            HIDP_CAPS caps{}; PHIDP_PREPARSED_DATA preparsed{};
            bool valid{};
            if (kind != wuwa_ps::Model::unsupported && HidD_GetPreparsedData(meta, &preparsed)) {
                valid = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 1 &&
                    (caps.Usage == 4 || caps.Usage == 5) && caps.InputReportByteLength >= 10 && caps.InputReportByteLength <= 1024;
                HidD_FreePreparsedData(preparsed);
            }
            CloseHandle(meta);
            if (!valid) continue;
            auto device = std::make_unique<Device>();
            device->file = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (device->file == INVALID_HANDLE_VALUE) { inaccessible = true; continue; }
            device->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!device->event) continue;
            device->operation.hEvent = device->event; device->length = caps.InputReportByteLength; device->model = kind;
            { std::scoped_lock lock{m_mutex}; ++m_snapshot.generation; }
            publish({}, 0, kind, "PlayStation connected; waiting for input reports and release");
            return device;
        }
        publish({}, 0, wuwa_ps::Model::unsupported, inaccessible ?
            "PlayStation input is unavailable or held exclusively; use Steam Input if available" :
            "No supported direct PlayStation controller detected");
        return {};
    }
    void run() noexcept {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        try {
            while (!stopped()) {
                auto device = discover();
                if (!device) { WaitForSingleObject(m_stop, 3000); continue; }
                while (!stopped()) {
                    if (!device->pending) {
                        ResetEvent(device->event);
                        device->operation = {}; device->operation.hEvent = device->event;
                        DWORD size{};
                        if (ReadFile(device->file, device->bytes.data(), device->length, &size, &device->operation)) {
                            wuwa_ps::Pad pad{};
                            if (wuwa_ps::decode(device->model, device->bytes.data(), size, pad))
                                publish(pad, GetTickCount64(), device->model, "PlayStation input received");
                            else publish({}, 0, device->model, "Unsupported PlayStation input report; use Steam Input if available");
                            continue;
                        }
                        if (GetLastError() != ERROR_IO_PENDING) break;
                        device->pending = true;
                    }
                    HANDLE wait[] = {m_stop, device->event};
                    if (WaitForMultipleObjects(2, wait, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                    DWORD size{};
                    const bool ok = GetOverlappedResult(device->file, &device->operation, &size, FALSE) != FALSE;
                    device->pending = false;
                    if (!ok) break;
                    wuwa_ps::Pad pad{};
                    if (wuwa_ps::decode(device->model, device->bytes.data(), size, pad))
                        publish(pad, GetTickCount64(), device->model, "PlayStation input received");
                    else publish({}, 0, device->model, "Unsupported PlayStation input report; use Steam Input if available");
                }
                device.reset();
                publish({}, 0, wuwa_ps::Model::unsupported, "PlayStation disconnected; waiting to reconnect");
                WaitForSingleObject(m_stop, 500);
            }
        } catch (...) { publish({}, 0, wuwa_ps::Model::unsupported, "PlayStation reader stopped; use Steam Input if available"); }
    }
    HANDLE m_stop{};
    mutable std::mutex m_mutex;
    Snapshot m_snapshot{};
    bool m_start_failed{};
    PinModule m_pin{};
    std::thread m_thread;
};
}
