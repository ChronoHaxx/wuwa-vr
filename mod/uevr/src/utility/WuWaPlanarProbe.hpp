#pragma once
// Shared hook for bounded observation and an independently enabled serial-eye
// parameter correction. Observation alone remains read-only.
#include <chrono>
#include <mutex>
#include "WuWaLguiProbe.hpp"
#include "WuWaPlanarSnapshot.hpp"
#include "WuWaStereoWrite.hpp"
#include "WuWaCodeCheck.hpp"

namespace wuwa_planar_probe {
using Json = nlohmann::json;
namespace memory = wuwa_lgui_probe::detail;
constexpr uintptr_t function_rva = 0x233eb5d0, site_rva = 0x233eba31;
constexpr size_t function_size = 0x66b, capacity = 512;
constexpr uint64_t function_hash = 0x0cc2b50ad9e4d8bfULL;
constexpr std::array<uint8_t, 8> site_bytes{0x49,0x8b,0xce,0xe8,0x47,0xe9,0x6d,0x01};

inline uintptr_t verified_site() {
    constexpr std::array<wuwa_code_compatibility::Range, 1> ranges{{
        {uint32_t(function_rva), uint32_t(function_size), function_hash}}};
    return wuwa_code_check::verify("Planar eye parameters", ranges) + site_rva;
}

class Probe;
inline SRWLOCK callback_lock = SRWLOCK_INIT;
inline Probe* active{}; // Protected by callback_lock, never read from a stale TLS pointer.
inline std::mutex control;
inline Probe* owner{}; // Explicit teardown only; ExitProcess must not run a locking static destructor.
// One hook and backend reference retained after disabling: SafetyHook's stub
// tail can still be returning even when the last C++ callback has finished.
inline safetyhook::MidHook* retired{};
inline HMODULE retained_backend{};
inline std::atomic<bool> correction_enabled{}, correction_fault{};
inline std::atomic<uint64_t> correction_calls{}, correction_applied{}, correction_skipped{};
inline std::string correction_error; // guarded by control
inline bool correction_attempted{};
struct ExclusiveCallbackGuard {
    ExclusiveCallbackGuard() { AcquireSRWLockExclusive(&callback_lock); }
    ~ExclusiveCallbackGuard() { ReleaseSRWLockExclusive(&callback_lock); }
    ExclusiveCallbackGuard(const ExclusiveCallbackGuard&) = delete;
};
template<class T> Json json_field(const Field<T>& f) { return f.valid ? Json(f.value) : Json(nullptr); }
template<class T> Json json_pair(const std::array<Field<T>, 2>& f) {
    return Json::array({json_field(f[0]), json_field(f[1])});
}

class Probe final {
    struct Record { Snapshot data{}; uint64_t tick{}, unix_ms{}; uint32_t thread{}; bool corrected{}; };
    struct Slot { Record record{}; std::atomic<bool> ready{false}; };
    std::array<Slot, capacity> m_slots{};
    std::array<memory::Sampler, 3> m_samplers{};
    std::atomic<uint32_t> m_next{};
    std::atomic<uint64_t> m_until{}, m_calls{};
    std::atomic<bool> m_stopped{};
    uint64_t m_start{};
    uint32_t m_drained{}, m_written{};
    std::mutex m_io;
    std::ofstream m_file;
    std::filesystem::path m_path;
    std::string m_error;
    std::unique_ptr<safetyhook::MidHook> m_hook;
    HMODULE m_backend{};
    bool m_enabled{};
    std::jthread m_worker;

    static void callback(safetyhook::Context& context) noexcept {
        const auto last_error = GetLastError();
        // Never wait for a start/stop operation in the game render callback.
        if (TryAcquireSRWLockShared(&callback_lock)) {
            auto* probe = active;
            try { if (probe) probe->capture(context); }
            catch (...) { if (probe) probe->m_until.store(0); correction_fault.store(true); }
            ReleaseSRWLockShared(&callback_lock);
        }
        SetLastError(last_error);
    }
    void capture(const safetyhook::Context& context) {
        const bool correcting = correction_enabled.load(std::memory_order_relaxed) && !correction_fault.load();
        const auto until = m_until.load(std::memory_order_acquire);
        if (!until && !correcting) return;
        const auto now = GetTickCount64();
        const bool tracing = until && now < until;
        if (until && !tracing) m_until.store(0, std::memory_order_release);
        if (!tracing && !correcting) return;
        if (tracing) m_calls.fetch_add(1, std::memory_order_relaxed);
        int32_t pass{};
        memory::read_field(context.r14, 0xc90, pass);
        const size_t eye = pass == 2 ? 0 : pass == 3 ? 1 : 2;
        uint32_t sample{};
        const bool sampled = tracing && m_samplers[eye].take((now - m_start) / memory::window_ms, sample);
        if (!sampled && !correcting) return;
        // At this instruction r14=view, rsi=proxy, rbx=parameters, rbp=the
        // candidate target. The virtual extent result lives at original RSP+16,
        // i.e. site RSP+0xd8 after the verified prologue's 0xc8-byte adjustment.
        const auto data = snapshot(context.r14, context.rsi, context.rbx,
            context.rsp <= UINTPTR_MAX - 0xd8 ? context.rsp + 0xd8 : 0, context.rbp,
            [](uintptr_t at, auto& value) { return memory::read(at, value); });
        bool corrected{};
        if (correcting) {
            ++correction_calls;
            if (const auto patch = wuwa_stereo_parameters::plan(data)) {
                corrected = wuwa_stereo_parameters::apply(context.rbx, *patch);
                if (corrected) ++correction_applied;
                else correction_fault.store(true);
            } else ++correction_skipped;
        }
        if (!sampled) return;
        const auto index = m_next.fetch_add(1, std::memory_order_relaxed);
        if (index >= capacity) { m_until.store(0, std::memory_order_release); return; }
        auto& slot = m_slots[index];
        slot.record.data = data; // The original parameters, before an optional correction.
        slot.record.corrected = corrected;
        slot.record.tick = now;
        slot.record.unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        slot.record.thread = GetCurrentThreadId();
        slot.ready.store(true, std::memory_order_release);
    }
    void drain_locked() noexcept {
        try {
            while (m_drained < capacity && m_slots[m_drained].ready.load(std::memory_order_acquire)) {
                const auto& r = m_slots[m_drained].record;
                const auto& s = r.data;
                Json row{{"type", "sample"}, {"sample", ++m_drained}, {"unix_ms", r.unix_ms},
                    {"tick_ms", r.tick}, {"thread", r.thread}, {"view", s.view}, {"proxy", s.proxy},
                    {"parameters", s.parameters}, {"candidate_target", s.candidate_target},
                    {"family", json_field(s.family)}, {"view_count", json_field(s.view_count)},
                    {"views", json_pair(s.views)}, {"passes", json_pair(s.passes)},
                    {"stereo_pass", json_field(s.stereo_pass)}, {"view_rect", json_field(s.view_rect)},
                    {"unscaled_rect", json_field(s.unscaled_rect)}, {"proxy_target", json_field(s.proxy_target)},
                    {"proxy_stereo", json_field(s.proxy_stereo)}, {"output_stereo", json_field(s.output_stereo)},
                    {"target_extent", json_field(s.target_extent)}, {"proxy_rects", json_pair(s.proxy_rects)},
                    {"proxy_matrices", json_pair(s.proxy_matrices)}, {"output_matrices", json_pair(s.output_matrices)},
                    {"scale_bias", json_pair(s.scale_bias)}, {"bounds", json_field(s.bounds)},
                    {"view_rects", json_pair(s.view_rects)}, {"instanced", json_pair(s.instanced)},
                    {"multiview", json_pair(s.multiview)}, {"serial_eye_correction", r.corrected}};
                m_file << row.dump() << '\n';
                if (!m_file) throw std::runtime_error("Reflection diagnostic write failed");
                ++m_written;
            }
            if (m_file.is_open()) m_file.flush();
        } catch (const std::exception& e) { m_error = e.what(); m_until.store(0); }
        catch (...) { m_error = "Reflection diagnostic exception"; m_until.store(0); }
    }
public:
    Probe() {
        const auto site = verified_site();
        memory::require(site != 0, "Reflection probe refused: game identity or parameter-builder code differs");
        memory::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&callback), &m_backend) != 0, "Cannot retain reflection probe backend");
        try {
            auto hook = safetyhook::MidHook::create(reinterpret_cast<void*>(site), callback, safetyhook::MidHook::StartDisabled);
            memory::require(hook.has_value(), "Cannot create reflection observation hook");
            m_hook = std::make_unique<safetyhook::MidHook>(std::move(*hook));
            AcquireSRWLockExclusive(&callback_lock); active = this; ReleaseSRWLockExclusive(&callback_lock);
            const auto result = m_hook->enable();
            memory::require(result.has_value(), "Cannot enable reflection observation hook");
            m_enabled = true;
            m_worker = std::jthread([this](std::stop_token stop) {
                while (!stop.stop_requested()) {
                    { const std::lock_guard guard{m_io}; drain_locked(); }
                    Sleep(100);
                }
            });
        } catch (...) { shutdown(); throw; }
    }
    ~Probe() { shutdown(); }
    void begin(const std::filesystem::path& directory, int seconds) {
        memory::require(seconds >= 1 && seconds <= 120, "Reflection trace duration must be 1..120 seconds");
        memory::require(m_until.load() <= GetTickCount64(), "A reflection trace is already active");
        const auto folder = directory / "diagnostics";
        std::filesystem::create_directories(folder);
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto path = folder / ("planar-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(stamp) + ".jsonl");
        memory::require(!std::filesystem::exists(path), "Reflection trace path already exists");
        std::ofstream file(path, std::ios::binary);
        memory::require(file.good(), "Cannot create reflection trace");
        file << Json{{"type", "header"}, {"version", 1}, {"pid", GetCurrentProcessId()}, {"unix_ms", stamp},
            {"seconds", seconds}, {"game_timestamp", memory::timestamp}, {"site_rva", site_rva},
            {"stage", "after_eye_parameters_before_platform_texture_selection"},
            {"texture_note", "candidate target only; final mobile fallback binding is not observed"},
            {"mutation", "observation is read-only; serial-eye parameter correction has an independent toggle"},
            {"serial_eye_correction_enabled", correction_enabled.load()},
            {"sample_limit", capacity}, {"burst_per_pass_per_5_seconds", memory::samples_per_window}}.dump() << '\n';
        memory::require(file.good(), "Cannot write reflection trace header");
        const ExclusiveCallbackGuard callbacks;
        { // No callbacks while resetting this bounded pool; worker shares m_io.
            const std::lock_guard guard{m_io};
            m_until.store(0); drain_locked(); m_file = std::move(file); m_path = path;
            m_error.clear(); m_next.store(0); m_calls.store(0); m_drained = m_written = 0;
            for (auto& slot : m_slots) slot.ready.store(false);
            for (auto& sampler : m_samplers) {
                sampler.calls.store(0); sampler.window_count.store(UINT64_MAX); sampler.samples.store(0);
            }
            m_start = GetTickCount64(); m_until.store(m_start + seconds * 1000, std::memory_order_release);
        }
    }
    void end() {
        AcquireSRWLockExclusive(&callback_lock); m_until.store(0); ReleaseSRWLockExclusive(&callback_lock);
        const std::lock_guard guard{m_io}; drain_locked();
    }
    Json status() {
        const std::lock_guard guard{m_io};
        const auto until = m_until.load(), now = GetTickCount64();
        const auto utf8 = m_path.u8string();
        return {{"installed", true}, {"active", until > now}, {"remaining_ms", until > now ? until - now : 0},
            {"path", std::string(utf8.begin(), utf8.end())}, {"samples", m_written}, {"calls", m_calls.load()},
            {"limit_reached", m_next.load() >= capacity}, {"error", m_error},
            {"pass_through", !correction_enabled.load()}};
    }
    void shutdown() noexcept {
        if (m_stopped.exchange(true)) return;
        m_until.store(0);
        // Thread suspension in SafetyHook must never happen while holding the
        // lock a suspended game callback could own.
        if (m_hook) { try { (void)m_hook->disable(); } catch (...) {} }
        AcquireSRWLockExclusive(&callback_lock);
        if (active == this) active = nullptr;
        ReleaseSRWLockExclusive(&callback_lock);
        if (m_worker.joinable()) { m_worker.request_stop(); m_worker.join(); }
        { const std::lock_guard guard{m_io}; drain_locked(); }
        if (m_enabled) { retired = m_hook.release(); retained_backend = m_backend; m_backend = nullptr; }
        else { m_hook.reset(); if (m_backend) { FreeLibrary(m_backend); m_backend = nullptr; } }
    }
};

inline Json status() {
    const std::lock_guard guard{control};
    return owner ? owner->status() : Json{{"installed", false}, {"active", false}, {"pass_through", true}};
}
inline Json request(const std::filesystem::path& directory, int seconds) {
    if (seconds < 0 || seconds > 120) throw std::runtime_error("Reflection trace duration must be 0..120 seconds");
    const std::lock_guard guard{control};
    if (seconds) { if (!owner) owner = new Probe; owner->begin(directory, seconds); }
    else if (owner) owner->end();
    return owner ? owner->status() : Json{{"installed", false}, {"active", false}};
}
inline void configure_correction(bool enabled) noexcept {
    if (!enabled) { correction_enabled.store(false); return; }
    if (correction_enabled.load() || correction_fault.load()) return;
    const std::lock_guard guard{control};
    try {
        if (!owner && !correction_attempted) { correction_attempted=true; owner=new Probe; }
        if (owner) correction_enabled.store(true);
    } catch (const std::exception& e) { correction_error=e.what(); correction_fault=true; }
    catch (...) { correction_error="Reflection correction initialization failed"; correction_fault=true; }
}
inline Json correction_status() {
    const std::lock_guard guard{control};
    return {{"enabled",correction_enabled.load()}, {"faulted",correction_fault.load()},
        {"calls",correction_calls.load()}, {"applied",correction_applied.load()},
        {"skipped",correction_skipped.load()}, {"error",correction_error}};
}
inline void shutdown(bool process_exiting) noexcept {
    correction_enabled.store(false);
    if (process_exiting) { owner = nullptr; return; } // OS reclaims it, including any lock-owning killed thread.
    const std::lock_guard guard{control}; delete owner; owner = nullptr;
}
} // namespace wuwa_planar_probe
