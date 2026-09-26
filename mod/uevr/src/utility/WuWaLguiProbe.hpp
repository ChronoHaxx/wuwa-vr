#pragma once

// Local pass-through diagnostic. Opt in beside the loaded backend DLL with
// wuwa-lgui-probe.request.json: {"version":1,"enable":true}.
// Exact signatures come from the complete client-inprocess-v3-20260908 capture.

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include <windows.h>
#include <nlohmann/json.hpp>
#include <safetyhook.hpp>
#include <spdlog/spdlog.h>

namespace wuwa_lgui_probe {
class Probe;

// CPU markers only: copy_returned does not imply GPU completion or that the
// existing copy helper reached a GPU submission instead of returning early.
inline void mark_ui_copy(const char* stage, uintptr_t ui_rhi, uintptr_t native_resource,
    uint64_t frame, uintptr_t game_rhi = 0, uintptr_t capture_rhi = 0) noexcept;

namespace detail {
inline SRWLOCK active_lock = SRWLOCK_INIT;
inline Probe* active{}; // Access only while holding active_lock.
inline std::atomic<bool> attempted{false};
inline std::atomic<bool> process_exiting{false};
static_assert(std::atomic<bool>::is_always_lock_free);
constexpr uintptr_t setup_rva = 0x2379fcb0;
constexpr uintptr_t execute_rva = 0x23782760;
constexpr uint32_t image_size = 1029861376;
constexpr uint32_t timestamp = 0x6a74963e;
constexpr std::array<uint8_t, 32> setup_signature{
    0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x4c,0x24,0x08,0x55,
    0x56,0x48,0x8d,0xac,0x24,0x78,0xfe,0xff,0xff,0x48,0x81,0xec,0x88,0x02,0x00,0x00};
constexpr std::array<uint8_t, 32> execute_signature{
    0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0x10,
    0xe4,0xff,0xff,0xb8,0xf0,0x1c,0x00,0x00,0xe8,0x63,0x63,0x30,0x03,0x48,0x2b,0xe0};

inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

inline std::filesystem::path module_path(HMODULE module) {
    std::wstring value(32768, L'\0');
    const auto size = GetModuleFileNameW(module, value.data(), static_cast<DWORD>(value.size()));
    require(size != 0 && size < value.size(), "Cannot resolve module path");
    value.resize(size);
    return std::filesystem::path{value};
}

// No C++ objects requiring unwinding inside SEH. This catches common read
// faults, not arbitrary process-specific handlers or process termination.
__declspec(noinline) inline bool guarded_copy(void* destination, const void* source, size_t size) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ||
                 GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR ||
                 GetExceptionCode() == EXCEPTION_GUARD_PAGE) ?
                    EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

inline bool readable(const MEMORY_BASIC_INFORMATION& region) noexcept {
    if (region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD) != 0) return false;
    switch (region.Protect & 0xff) {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return true;
    default: return false;
    }
}

inline bool executable_site(uintptr_t address, size_t size, HMODULE executable) noexcept {
    if (address == 0 || size == 0 || address > UINTPTR_MAX - size) return false;
    const auto end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) != sizeof(region) ||
            !readable(region) || region.AllocationBase != executable) return false;
        const auto protection = region.Protect & 0xff;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
        if (begin > address || region.RegionSize > UINTPTR_MAX - begin || begin + region.RegionSize <= address) return false;
        address = (std::min)(end, begin + region.RegionSize);
    }
    return true;
}

template<class T> inline bool read(uintptr_t address, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    value = {};
    if (address == 0 || address > UINTPTR_MAX - sizeof(T)) return false;
    std::array<uint8_t, sizeof(T)> bytes{};
    size_t copied{};
    while (copied < bytes.size()) {
        MEMORY_BASIC_INFORMATION region{};
        const auto cursor = address + copied;
        if (VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) != sizeof(region) || !readable(region)) return false;
        const auto begin = reinterpret_cast<uintptr_t>(region.BaseAddress);
        if (begin > cursor || region.RegionSize > UINTPTR_MAX - begin) return false;
        const auto end = begin + region.RegionSize;
        if (end <= cursor) return false;
        const auto size = (std::min)(bytes.size() - copied, end - cursor);
        if (!guarded_copy(bytes.data() + copied, reinterpret_cast<void*>(cursor), size)) return false;
        copied += size;
    }
    std::memcpy(&value, bytes.data(), sizeof(T));
    return true;
}

template<class T> inline bool read_field(uintptr_t object, uintptr_t offset, T& value) {
    value = {};
    return object != 0 && object <= UINTPTR_MAX - offset && read(object + offset, value);
}

enum class Site : uint8_t { setup, execute, before_copy, copy_returned };
constexpr size_t site_count = 4;
constexpr uint32_t sample_limit = 512;
constexpr uint64_t window_ms = 5000;
constexpr uint64_t samples_per_window = 8;

struct Sampler {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> window_count{UINT64_MAX};
    std::atomic<uint32_t> samples{0};
    bool take(uint64_t window, uint32_t& ordinal) noexcept {
        calls.fetch_add(1, std::memory_order_relaxed);
        if (samples.load(std::memory_order_relaxed) >= sample_limit) return false;
        auto state = window_count.load(std::memory_order_relaxed);
        for (;;) {
            // A delayed callback must not reopen an earlier shared window.
            if (state != UINT64_MAX && (state >> 8) > window) return false;
            const bool same_window = (state >> 8) == window;
            const auto count = same_window ? (state & 0xff) : 0;
            if (count >= samples_per_window) return false;
            const auto desired = (window << 8) | (count + 1);
            if (window_count.compare_exchange_weak(state, desired, std::memory_order_relaxed)) break;
        }
        auto sampled = samples.load(std::memory_order_relaxed);
        do {
            if (sampled >= sample_limit) return false;
        } while (!samples.compare_exchange_weak(sampled, sampled + 1, std::memory_order_relaxed));
        ordinal = sampled + 1;
        return true;
    }
};

struct Record {
    Site site{};
    uint32_t sample{}, thread{};
    uint64_t sequence{}, tick{}, window{}, frame{};
    uintptr_t rsp{}, caller{}, caller_rva{}, renderer{}, graph{}, command{}, capture{};
    uintptr_t view{}, family{}, family_target{}, pass{}, parameters{};
    uintptr_t color{}, depth{}, color_rhi{}, depth_rhi{};
    uintptr_t ui_rhi{}, native_resource{}, game_rhi{}, capture_rhi{};
    int32_t argument{}, draw_count{};
    std::array<int32_t, 4> view_rect{}, capture_rect{};
    uint8_t enabled{};
    bool caller_valid{}, caller_in_module{}, view_valid{}, argument_valid{}, renderer_valid{};
    bool family_valid{}, family_target_valid{}, draw_count_valid{}, view_rect_valid{};
    bool capture_rect_valid{}, enabled_valid{}, parameters_valid{}, color_valid{}, depth_valid{};
    bool color_rhi_valid{}, depth_rhi_valid{}, callback_exception{};
};
static_assert(std::is_trivially_copyable_v<Record>);
struct Slot { Record value{}; std::atomic<bool> ready{false}; };
struct Hooks { safetyhook::MidHook setup{}, execute{}; };

// Diagnostic-only bounded retention: MidHook::disable restores entry bytes,
// but this library does not promise quiescence of its assembly stub tails.
// Retain exactly two disabled hooks and one backend reference after activation.
// There is at most one opted-in installation attempt per loaded backend.
inline Hooks* retired_hooks{};
inline HMODULE retained_backend{};
} // namespace detail

class Probe final {
public:
    static std::unique_ptr<Probe> create_if_requested(HMODULE backend) noexcept {
        try {
            const auto request_path = detail::module_path(backend).parent_path() / L"wuwa-lgui-probe.request.json";
            if (!std::filesystem::exists(request_path)) return {};
            if (detail::attempted.exchange(true)) return {};
            detail::require(std::filesystem::file_size(request_path) <= 4096, "Probe request exceeds 4096 bytes");
            std::ifstream request{request_path, std::ios::binary};
            detail::require(request.good(), "Cannot read probe request");
            const auto config = nlohmann::json::parse(request);
            detail::require(config.at("version").is_number_integer() && config.at("version") == 1 &&
                config.at("enable").is_boolean(), "Expected probe request version 1 and Boolean enable");
            if (!config.at("enable").get<bool>()) return {};
            auto probe = std::unique_ptr<Probe>{new Probe{}};
            probe->install();
            return probe;
        } catch (const std::exception& error) {
            try { spdlog::error("[WuWaLguiProbe] installation rejected: {}", error.what()); } catch (...) {}
        } catch (...) {
            try { spdlog::error("[WuWaLguiProbe] installation rejected by exception"); } catch (...) {}
        }
        return {};
    }

    ~Probe() noexcept { stop(); }
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;

private:
    friend void mark_ui_copy(const char*, uintptr_t, uintptr_t, uint64_t, uintptr_t, uintptr_t) noexcept;
    Probe() : m_logger{spdlog::default_logger()} { detail::require(m_logger != nullptr, "No diagnostic logger"); }

    void install() {
        const auto executable = GetModuleHandleW(nullptr);
        detail::require(_wcsicmp(detail::module_path(executable).filename().c_str(), L"Client-Win64-Shipping.exe") == 0,
            "Unexpected executable name");
        m_base = reinterpret_cast<uintptr_t>(executable);
        IMAGE_DOS_HEADER dos{};
        detail::require(detail::read(m_base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
            dos.e_lfanew >= sizeof(dos) && dos.e_lfanew < 1024 * 1024, "Invalid executable DOS header");
        IMAGE_NT_HEADERS64 nt{};
        detail::require(detail::read_field(m_base, dos.e_lfanew, nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
            nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
            nt.FileHeader.TimeDateStamp == detail::timestamp && nt.OptionalHeader.SizeOfImage == detail::image_size,
            "Unexpected executable PE identity");
        std::array<uint8_t, 32> setup{}, execute{};
        detail::require(detail::executable_site(m_base + detail::setup_rva, setup.size(), executable) &&
            detail::executable_site(m_base + detail::execute_rva, execute.size(), executable),
            "LGUI entries are not executable readable memory belonging to the main module");
        const bool setup_read = detail::read_field(m_base, detail::setup_rva, setup);
        const bool execute_read = detail::read_field(m_base, detail::execute_rva, execute);
        // Validate BOTH sites before constructing either hook; no partial gate.
        detail::require(setup_read && execute_read && setup == detail::setup_signature && execute == detail::execute_signature,
            "LGUI entry signatures do not match complete v3 capture");
        detail::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&Probe::setup_callback), &m_backend_reference) != 0, "Cannot retain probe backend");
        m_hooks = std::make_unique<detail::Hooks>();
        auto setup_hook = safetyhook::MidHook::create(reinterpret_cast<void*>(m_base + detail::setup_rva),
            &Probe::setup_callback, safetyhook::MidHook::StartDisabled);
        detail::require(setup_hook.has_value(), "Cannot create LGUI setup MidHook");
        m_hooks->setup = std::move(*setup_hook);
        auto execute_hook = safetyhook::MidHook::create(reinterpret_cast<void*>(m_base + detail::execute_rva),
            &Probe::execute_callback, safetyhook::MidHook::StartDisabled);
        detail::require(execute_hook.has_value(), "Cannot create LGUI execution MidHook");
        m_hooks->execute = std::move(*execute_hook);
        m_worker = std::jthread{[this](std::stop_token stop) noexcept {
            while (!stop.stop_requested()) { drain(); Sleep(100); }
            drain(true);
        }};
        AcquireSRWLockExclusive(&detail::active_lock);
        const bool vacant = detail::active == nullptr;
        if (vacant) detail::active = this;
        ReleaseSRWLockExclusive(&detail::active_lock);
        detail::require(vacant, "Another LGUI probe is already published");
        auto setup_enabled = m_hooks->setup.enable();
        detail::require(setup_enabled.has_value(), "Cannot enable LGUI setup MidHook");
        m_ever_enabled = true;
        auto execute_enabled = m_hooks->execute.enable();
        detail::require(execute_enabled.has_value(), "Cannot enable LGUI execution MidHook");
        m_start_tick = GetTickCount64();
        m_accepting.store(true, std::memory_order_release);
        m_logger->info("[WuWaLguiProbe] installed: base={:x} setup_rva={:x} execute_rva={:x} tick={} windows_ms=5000 burst=8 limit_per_site=512 mode=pass_through",
            m_base, detail::setup_rva, detail::execute_rva, m_start_tick);
        m_logger->flush();
    }

    void stop() noexcept {
        if (m_stopped.exchange(true)) return;
        m_accepting.store(false, std::memory_order_release);
        // Do not hold active_lock while SafetyHook traps threads during disable.
        if (m_hooks) {
            try { if (m_hooks->setup) { auto result = m_hooks->setup.disable(); if (!result) note_exception(); } } catch (...) { note_exception(); }
            try { if (m_hooks->execute) { auto result = m_hooks->execute.disable(); if (!result) note_exception(); } } catch (...) { note_exception(); }
        }
        AcquireSRWLockExclusive(&detail::active_lock);
        if (detail::active == this) detail::active = nullptr;
        ReleaseSRWLockExclusive(&detail::active_lock);
        // Exclusive unpublication waits for all record writers holding a shared
        // guard. Late stub callbacks see nullptr, while retained code stays valid.
        if (m_worker.joinable()) { m_worker.request_stop(); m_worker.join(); }
        if (m_ever_enabled) {
            detail::retired_hooks = m_hooks.release();
            detail::retained_backend = m_backend_reference;
            m_backend_reference = nullptr; // Deliberately retained until process exit.
        } else {
            m_hooks.reset();
            if (m_backend_reference) { FreeLibrary(m_backend_reference); m_backend_reference = nullptr; }
        }
    }

    static void setup_callback(safetyhook::Context& context) noexcept { dispatch(context, detail::Site::setup); }
    static void execute_callback(safetyhook::Context& context) noexcept { dispatch(context, detail::Site::execute); }
    static void dispatch(const safetyhook::Context& context, detail::Site site) noexcept {
        const auto last_error = GetLastError();
        AcquireSRWLockShared(&detail::active_lock);
        auto* probe = detail::active;
        try { if (probe) probe->record_context(context, site); }
        catch (...) { if (probe) probe->note_exception(); }
        ReleaseSRWLockShared(&detail::active_lock);
        SetLastError(last_error);
    }

    detail::Slot* reserve(detail::Site site) noexcept {
        if (!m_accepting.load(std::memory_order_acquire)) return nullptr;
        const auto tick = GetTickCount64();
        const auto window = (tick - m_start_tick) / detail::window_ms;
        uint32_t sample{};
        if (!m_samplers[static_cast<size_t>(site)].take(window, sample)) return nullptr;
        const auto index = m_next.fetch_add(1, std::memory_order_relaxed);
        if (index >= m_slots.size()) return nullptr;
        auto& slot = m_slots[index];
        slot.value.site = site;
        slot.value.sample = sample;
        slot.value.sequence = index + 1;
        slot.value.tick = tick;
        slot.value.window = window;
        slot.value.thread = GetCurrentThreadId();
        return &slot;
    }

    void read_view(detail::Record& record) {
        record.family_valid = detail::read_field(record.view, 0, record.family);
        record.family_target_valid = detail::read_field(record.family, 0x18, record.family_target);
        record.draw_count_valid = detail::read_field(record.view, 0x9b2c, record.draw_count);
        record.view_rect_valid = detail::read_field(record.view, 0x2f8, record.view_rect);
    }

    void record_context(const safetyhook::Context& context, detail::Site site) {
        auto* slot = reserve(site);
        if (!slot) return;
        auto& r = slot->value;
        try {
            // Context64 docs and mid_hook.x86_64-windows.asm establish rsp as
            // the ORIGINAL hook-site stack pointer. Never write any context field.
            r.rsp = context.rsp;
            if (site == detail::Site::setup) {
                r.renderer = context.rcx; r.graph = context.rdx;
                r.color = context.r8; r.depth = context.r9;
                r.renderer_valid = r.color_valid = r.depth_valid = true; // Raw register identities.
                r.caller_valid = detail::read(r.rsp, r.caller);
                r.caller_in_module = r.caller_valid && r.caller >= m_base && r.caller - m_base < detail::image_size;
                if (r.caller_in_module) r.caller_rva = r.caller - m_base;
                r.view_valid = detail::read_field(r.rsp, 0x28, r.view);
                r.argument_valid = detail::read_field(r.rsp, 0x30, r.argument);
            } else {
                r.capture = context.rcx; r.command = context.rdx;
                r.renderer_valid = detail::read_field(r.capture, 0, r.renderer);
                r.view_valid = detail::read_field(r.capture, 8, r.view);
                r.capture_rect_valid = detail::read_field(r.capture, 0x14, r.capture_rect);
                r.enabled_valid = detail::read_field(r.capture, 0x30, r.enabled);
                if (r.capture >= 0xf0) r.pass = r.capture - 0xf0;
                r.parameters_valid = detail::read_field(r.pass, 0x10, r.parameters);
                r.color_valid = detail::read_field(r.parameters, 0x10, r.color);
                r.depth_valid = detail::read_field(r.parameters, 0xd0, r.depth);
            }
            read_view(r);
            r.color_rhi_valid = detail::read_field(r.color, 0x10, r.color_rhi);
            r.depth_rhi_valid = detail::read_field(r.depth, 0x10, r.depth_rhi);
        } catch (...) { r.callback_exception = true; note_exception(); }
        slot->ready.store(true, std::memory_order_release);
    }

    void record_ui(const char* stage, uintptr_t ui_rhi, uintptr_t native, uint64_t frame,
        uintptr_t game_rhi, uintptr_t capture_rhi) {
        if (stage == nullptr) return;
        detail::Site site;
        if (std::strcmp(stage, "before_copy") == 0) site = detail::Site::before_copy;
        else if (std::strcmp(stage, "copy_returned") == 0) site = detail::Site::copy_returned;
        else return;
        auto* slot = reserve(site);
        if (!slot) return;
        auto& r = slot->value;
        r.ui_rhi = ui_rhi; r.native_resource = native; r.frame = frame;
        r.game_rhi = game_rhi; r.capture_rhi = capture_rhi;
        slot->ready.store(true, std::memory_order_release);
    }

    void note_exception() noexcept { m_exceptions.fetch_add(1, std::memory_order_relaxed); }
    void drain(bool final = false) noexcept {
        // Only the worker formats/logs: no per-call heap allocation or logging
        // lock in either MidHook callback or the UI marker.
        try {
            bool wrote{};
            const auto end = (std::min)(m_next.load(std::memory_order_acquire), m_slots.size());
            while (m_drained < end && m_slots[m_drained].ready.load(std::memory_order_acquire)) {
                const auto& r = m_slots[m_drained].value;
                if (r.site == detail::Site::setup || r.site == detail::Site::execute) {
                    m_logger->info("[WuWaLguiProbe] site={} seq={} tick={} window={} sample={} tid={} rsp={:x} renderer={:x}/{} graph={:x} cmd={:x} capture={:x} caller={:x}/{} caller_rva={:x}/{} view={:x}/{} arg={}/{} family={:x}/{} target={:x}/{} count={}/{} viewrect=({},{},{},{})/{} capturerect=({},{},{},{})/{} enabled={}/{} pass={:x} params={:x}/{} color_rdg={:x}/{} depth_rdg={:x}/{} color_rhi={:x}/{} depth_rhi={:x}/{} callback_exception={}",
                        r.site == detail::Site::setup ? "setup" : "execute", r.sequence, r.tick, r.window, r.sample, r.thread,
                        r.rsp, r.renderer, r.renderer_valid, r.graph, r.command, r.capture, r.caller, r.caller_valid,
                        r.caller_rva, r.caller_in_module, r.view, r.view_valid, r.argument, r.argument_valid,
                        r.family, r.family_valid, r.family_target, r.family_target_valid, r.draw_count, r.draw_count_valid,
                        r.view_rect[0], r.view_rect[1], r.view_rect[2], r.view_rect[3], r.view_rect_valid,
                        r.capture_rect[0], r.capture_rect[1], r.capture_rect[2], r.capture_rect[3], r.capture_rect_valid,
                        r.enabled, r.enabled_valid, r.pass, r.parameters, r.parameters_valid, r.color, r.color_valid,
                        r.depth, r.depth_valid, r.color_rhi, r.color_rhi_valid, r.depth_rhi, r.depth_rhi_valid, r.callback_exception);
                } else {
                    m_logger->info("[WuWaLguiProbe] site=ui_copy stage={} seq={} tick={} window={} sample={} tid={} frame={} ui_rhi={:x} native={:x} game_rhi={:x} capture_rhi={:x}",
                        r.site == detail::Site::before_copy ? "before_copy" : "copy_returned", r.sequence, r.tick, r.window,
                        r.sample, r.thread, r.frame, r.ui_rhi, r.native_resource, r.game_rhi, r.capture_rhi);
                }
                ++m_drained; wrote = true;
            }
            const auto exceptions = m_exceptions.load(std::memory_order_relaxed);
            if (exceptions != m_logged_exceptions) {
                m_logger->error("[WuWaLguiProbe] caught callback/lifecycle exceptions: total={}", exceptions);
                m_logged_exceptions = exceptions; wrote = true;
            }
            const auto tick = GetTickCount64();
            if (final || (m_accepting.load(std::memory_order_acquire) && tick - m_last_summary_tick >= detail::window_ms)) {
                m_logger->info("[WuWaLguiProbe] totals tick={} final={} reserved_seq={} drained={} setup_hits={} setup_samples={} execute_hits={} execute_samples={} before_copy_hits={} before_copy_samples={} copy_returned_hits={} copy_returned_samples={} exceptions={}",
                    tick, final, m_next.load(std::memory_order_relaxed), m_drained,
                    m_samplers[0].calls.load(std::memory_order_relaxed), m_samplers[0].samples.load(std::memory_order_relaxed),
                    m_samplers[1].calls.load(std::memory_order_relaxed), m_samplers[1].samples.load(std::memory_order_relaxed),
                    m_samplers[2].calls.load(std::memory_order_relaxed), m_samplers[2].samples.load(std::memory_order_relaxed),
                    m_samplers[3].calls.load(std::memory_order_relaxed), m_samplers[3].samples.load(std::memory_order_relaxed), exceptions);
                m_last_summary_tick = tick; wrote = true;
            }
            if (wrote) m_logger->flush();
        } catch (...) { /* Diagnostic logging must not terminate its worker. */ }
    }

    std::shared_ptr<spdlog::logger> m_logger;
    std::unique_ptr<detail::Hooks> m_hooks{};
    HMODULE m_backend_reference{};
    uintptr_t m_base{};
    uint64_t m_start_tick{};
    bool m_ever_enabled{};
    std::atomic<bool> m_accepting{false}, m_stopped{false};
    std::array<detail::Sampler, detail::site_count> m_samplers{};
    std::array<detail::Slot, detail::site_count * detail::sample_limit> m_slots{};
    std::atomic<size_t> m_next{0};
    std::atomic<uint64_t> m_exceptions{0};
    size_t m_drained{};
    uint64_t m_logged_exceptions{}, m_last_summary_tick{};
    std::jthread m_worker{};
};

inline void mark_ui_copy(const char* stage, uintptr_t ui_rhi, uintptr_t native_resource,
    uint64_t frame, uintptr_t game_rhi, uintptr_t capture_rhi) noexcept {
    const auto last_error = GetLastError();
    AcquireSRWLockShared(&detail::active_lock);
    auto* probe = detail::active;
    try { if (probe) probe->record_ui(stage, ui_rhi, native_resource, frame, game_rhi, capture_rhi); }
    catch (...) { if (probe) probe->note_exception(); }
    ReleaseSRWLockShared(&detail::active_lock);
    SetLastError(last_error);
}
} // namespace wuwa_lgui_probe
