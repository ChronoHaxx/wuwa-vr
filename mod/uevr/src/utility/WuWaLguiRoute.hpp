#pragma once

// Focused compatibility capture for the live vtable sites demonstrated by the
// September 8 Indath build. This does not redirect textures. The wrappers
// forward the words used by his binary; that is not a verified game ABI.
// Live capture confirmed the first two sites are two-input parameter getters
// matching the saved image. Extra words may be ambient registers/caller stack;
// the getter's output points into caller stack memory, not a view structure.
#include "WuWaLguiProbe.hpp"
#include <mutex>

namespace wuwa_lgui_route {
namespace read_only = wuwa_lgui_probe::detail;
using Draw = uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using Execute = void (*)(uintptr_t, uintptr_t);
struct Target {
    uintptr_t ui{}, game{};
    uint32_t width{}, height{};
    uintptr_t capture{};
    bool redirect_enabled{true};
    bool menu_redirect_enabled{};
    bool native_stereo_fix{};
    bool independent_eyes{};
    bool world_labels_enabled{};
};
using Provider = Target (*)();
struct Sample {
    std::atomic<bool> ready{false};
    uint32_t site{}, thread{};
    uint64_t tick{};
    std::array<uintptr_t, 6> args{};
    Target target{};
    bool code_ok{}, a2_ok{}, a3_ok{}, a4_ok{}, ui_ok{};
    std::array<uint8_t, 0x400> code{};
    std::array<uint8_t, 0x1000> a2{};
    std::array<uint8_t, 0x200> a3{}, a4{}, ui{};
};
struct State {
    std::recursive_mutex mutex;
    bool active{}, attempted{};
    uintptr_t base{};
    Provider provider{};
    std::filesystem::path output;
    std::array<uintptr_t, 3> slots{}, originals{}, replacements{};
    std::array<std::atomic<uint64_t>, 3> hits{}, next_sample{};
    std::array<Sample, 48> samples{};
    std::atomic<size_t> reserved{0};
    size_t drained{};
    uint64_t last_summary{};
};
// Retained with a module reference after activation. A caller may have fetched
// a wrapper pointer just before shutdown restores the vtable.
inline State& state() { static auto* value = new State{}; return *value; }

inline bool write_slot(uintptr_t address, uintptr_t expected, uintptr_t replacement) noexcept {
    DWORD previous{};
    if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(void*), PAGE_READWRITE, &previous)) return false;
    const auto actual = reinterpret_cast<uintptr_t>(InterlockedCompareExchangePointer(
        reinterpret_cast<void* volatile*>(address), reinterpret_cast<void*>(replacement), reinterpret_cast<void*>(expected)));
    DWORD ignored{};
    const bool restored = VirtualProtect(reinterpret_cast<void*>(address), sizeof(void*), previous, &ignored) != 0;
    if (!restored) {
        // Do not report a failed install while leaving our pointer published.
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(address),
            reinterpret_cast<void*>(expected), reinterpret_cast<void*>(replacement));
        VirtualProtect(reinterpret_cast<void*>(address), sizeof(void*), previous, &ignored);
    }
    return actual == expected && restored;
}

inline void observe(size_t site, const std::array<uintptr_t, 6>& args) noexcept {
    auto& s = state();
    s.hits[site].fetch_add(1, std::memory_order_relaxed);
    try {
        const auto now = GetTickCount64();
        auto next = s.next_sample[site].load(std::memory_order_relaxed);
        if (now < next || !s.next_sample[site].compare_exchange_strong(next, now + 5000)) return;
        const auto index = s.reserved.fetch_add(1);
        if (index >= s.samples.size()) return;
        auto& sample = s.samples[index];
        sample.site = static_cast<uint32_t>(site);
        sample.thread = GetCurrentThreadId();
        sample.tick = now;
        sample.args = args;
        if (s.provider) sample.target = s.provider();
        sample.code_ok = read_only::read(s.originals[site], sample.code);
        sample.a2_ok = read_only::read(args[1], sample.a2);
        sample.a3_ok = read_only::read(args[2], sample.a3);
        sample.a4_ok = read_only::read(args[3], sample.a4);
        sample.ui_ok = read_only::read(sample.target.ui, sample.ui);
        sample.ready.store(true, std::memory_order_release);
    } catch (...) { /* Observation must never prevent the original call. */ }
}

template<size_t Site> inline uintptr_t draw(uintptr_t a1, uintptr_t a2, uintptr_t a3,
        uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    const auto last_error = GetLastError();
    auto& s = state();
    {
        std::lock_guard guard{s.mutex};
        if (s.active) observe(Site, {a1, a2, a3, a4, a5, a6});
    }
    SetLastError(last_error);
    return reinterpret_cast<Draw>(s.originals[Site])(a1, a2, a3, a4, a5, a6);
}
inline void execute(uintptr_t pass, uintptr_t command_list) {
    const auto last_error = GetLastError();
    auto& s = state();
    {
        std::lock_guard guard{s.mutex};
        if (s.active) observe(2, {pass, command_list, pass, 0, 0, 0});
    }
    SetLastError(last_error);
    reinterpret_cast<Execute>(s.originals[2])(pass, command_list);
}

inline void stop() noexcept {
    auto& s = state();
    try {
        std::lock_guard guard{s.mutex};
        s.active = false;
        for (size_t i = 0; i < s.slots.size(); ++i) {
            if (s.slots[i]) write_slot(s.slots[i], s.replacements[i], s.originals[i]);
        }
        s.provider = nullptr;
    } catch (...) {}
}

inline void start(HMODULE backend, Provider provider) {
    auto& s = state();
    std::lock_guard guard{s.mutex};
    if (s.attempted) return;
    s.attempted = true;
    const auto request_path = read_only::module_path(backend).parent_path() / "wuwa-lgui-route.request.json";
    if (!std::filesystem::exists(request_path)) return;
    read_only::require(std::filesystem::file_size(request_path) <= 4096, "Route request too large");
    std::ifstream input{request_path};
    const auto request = nlohmann::json::parse(input);
    read_only::require(request.at("version") == 1 && request.at("mode") == "observe", "Expected route version 1, mode observe");
    if (!request.at("enable").get<bool>()) return;
    const auto executable = GetModuleHandleW(nullptr);
    read_only::require(_wcsicmp(read_only::module_path(executable).filename().c_str(), L"Client-Win64-Shipping.exe") == 0,
        "Unexpected executable");
    s.base = reinterpret_cast<uintptr_t>(executable);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    read_only::require(read_only::read(s.base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew > 0 && dos.e_lfanew < 0x100000 && read_only::read(s.base + dos.e_lfanew, nt) &&
        nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp == read_only::timestamp &&
        nt.OptionalHeader.SizeOfImage == read_only::image_size, "Game identity mismatch");
    const auto parent = std::filesystem::path{request.at("output").get<std::string>()};
    read_only::require(parent.is_absolute() && std::filesystem::is_directory(parent), "Output directory must already exist");
    s.output = parent / ("route-" + std::to_string(GetCurrentProcessId()));
    read_only::require(!std::filesystem::exists(s.output), "Route output already exists");
    std::filesystem::create_directory(s.output);
    constexpr std::array<uintptr_t, 3> slots{0x7416cc0, 0x760b730, 0x7de5d78};
    constexpr std::array<uintptr_t, 3> functions{0xcdf220, 0xcdf2b0, 0x3228c50};
    s.replacements = {reinterpret_cast<uintptr_t>(&draw<0>), reinterpret_cast<uintptr_t>(&draw<1>), reinterpret_cast<uintptr_t>(&execute)};
    for (size_t i = 0; i < slots.size(); ++i) {
        read_only::require(read_only::read(s.base + slots[i], s.originals[i]) && s.originals[i] == s.base + functions[i] &&
            read_only::executable_site(s.originals[i], 32, executable), "Live vtable target mismatch");
    }
    HMODULE reference{};
    read_only::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&execute), &reference) != 0, "Cannot retain route module");
    // Intentionally retain reference until process exit, including rollback.
    s.provider = provider;
    try {
        for (size_t i = 0; i < slots.size(); ++i) {
            s.slots[i] = s.base + slots[i];
            read_only::require(write_slot(s.slots[i], s.originals[i], s.replacements[i]), "Cannot install route vtable hook");
        }
        s.active = true;
        spdlog::info("[WuWaLguiRoute] installed mode=observe base={:x} output={}", s.base, s.output.string());
    } catch (...) { stop(); throw; }
}

inline void tick(HMODULE backend, Provider provider) noexcept {
    auto& s = state();
    try {
        start(backend, provider);
        std::lock_guard guard{s.mutex};
        if (!s.active) return;
        for (; s.drained < (std::min)(s.reserved.load(), s.samples.size()); ++s.drained) {
            auto& sample = s.samples[s.drained];
            if (!sample.ready.load(std::memory_order_acquire)) break;
            nlohmann::json record{{"site", sample.site}, {"thread", sample.thread}, {"tick", sample.tick},
                {"base", s.base}, {"original", s.originals[sample.site]}, {"args", sample.args},
                {"ui_rhi", sample.target.ui}, {"game_rhi", sample.target.game},
                {"ui_width", sample.target.width}, {"ui_height", sample.target.height}};
            const auto prefix = "sample-" + std::to_string(s.drained);
            const auto save = [&](const char* name, const auto& bytes, bool valid) {
                record[std::string{name} + "_valid"] = valid;
                if (!valid) return;
                const auto filename = prefix + "-" + name + ".bin";
                std::ofstream file{s.output / filename, std::ios::binary | std::ios::trunc};
                file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                file.flush();
                if (!file.good()) throw std::runtime_error("Cannot publish route bytes");
                record[name] = filename;
            };
            save("code", sample.code, sample.code_ok);
            save("a2", sample.a2, sample.a2_ok);
            save("a3", sample.a3, sample.a3_ok);
            save("a4", sample.a4, sample.a4_ok);
            save("ui", sample.ui, sample.ui_ok);
            std::ofstream file{s.output / (prefix + ".json")};
            file << record.dump(2);
            file.flush();
            if (!file.good()) throw std::runtime_error("Cannot publish route metadata");
            spdlog::info("[WuWaLguiRoute] sample={} site={} a2={:x} a3={:x} ui={:x} size={}x{}",
                s.drained, sample.site, sample.args[1], sample.args[2], sample.target.ui, sample.target.width, sample.target.height);
        }
        const auto now = GetTickCount64();
        if (now - s.last_summary >= 5000) {
            s.last_summary = now;
            std::array<bool, 3> owned{};
            for (size_t i = 0; i < owned.size(); ++i) {
                uintptr_t current{};
                owned[i] = read_only::read(s.slots[i], current) && current == s.replacements[i];
            }
            spdlog::info("[WuWaLguiRoute] hits={}/{}/{} owned={}/{}/{} samples={} mode=observe",
                s.hits[0].load(), s.hits[1].load(), s.hits[2].load(), owned[0], owned[1], owned[2], s.drained);
        }
    } catch (const std::exception& error) {
        stop();
        try { spdlog::error("[WuWaLguiRoute] stopped: {}", error.what()); } catch (...) {}
    } catch (...) { stop(); }
}
} // namespace wuwa_lgui_route
