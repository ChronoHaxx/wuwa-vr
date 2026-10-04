#pragma once
#include <cwchar>
#include <mutex>
#include <windows.h>
#include <nlohmann/json.hpp>
#include <sdk/CVar.hpp>
#include <sdk/ConsoleManager.hpp>
#include "WuWaRimSuppressionPolicy.hpp"

namespace wuwa_rim {
inline constexpr const char* variable_name = "r.ToonRimWidthFactorForToonDepth";
inline constexpr const wchar_t* variable_wname = L"r.ToonRimWidthFactorForToonDepth";
// Game thread owns policy/IO. Other threads can copy only cached status.
inline Policy policy;
inline std::mutex status_mutex;
inline Status cached;
inline uint64_t last_observed_ms{};
inline bool last_wanted{}, last_eligible{}, last_conflict{};
inline Status status() { std::scoped_lock lock{status_mutex}; return cached; }
inline bool busy() { const auto s = status(); return s.requested || s.owned; }

// AsCommand/GetInt/GetFloat return zero/null if discovery fails. Check the
// SDK-discovered accessors before accepting that as an actual zero value.
// The inherited member-pointer probe uses the base object, without downcasts.
struct Access : sdk::IConsoleVariable {
    static bool available(sdk::IConsoleObject* object) {
        if (!object) return false;
        const auto probe = &Access::locate_vtable_indices;
        const auto info = (object->*probe)();
        return info && info->as_console_command_index && info->set_vtable_index &&
            info->get_int_vtable_index && info->get_float_vtable_index;
    }
};
struct ConsoleIO {
    sdk::IConsoleVariable* find() {
        const auto manager = sdk::FConsoleManager::get();
        if (!manager) return nullptr;
        auto* object = manager->find(variable_wname);
        if (!Access::available(object) || object->AsCommand()) return nullptr;
        return static_cast<sdk::IConsoleVariable*>(object);
    }
    std::optional<Sample> read() {
        try {
            auto* var = find();
            if (!var) return std::nullopt;
            const float value = var->GetFloat();
            const auto integer = var->GetInt();
            if (!std::isfinite(value)) return std::nullopt;
            wchar_t text[64]{};
            if (static_cast<float>(integer) == value) swprintf_s(text, L"%d", integer);
            else swprintf_s(text, L"%.9g", value); // Float round trip, same path as console tests.
            return Sample{reinterpret_cast<uintptr_t>(var), value, integer, text};
        } catch (...) { return std::nullopt; }
    }
    void write(const Sample& expected, const std::wstring& text) {
        try {
            auto* var = find();
            if (var && reinterpret_cast<uintptr_t>(var) == expected.identity &&
                var->GetFloat() == expected.value && var->GetInt() == expected.integer) var->Set(text.c_str());
        } catch (...) {} // Policy must verify with an independent readback.
    }
};
// Every game tick; transitions take priority, steady ownership is observed at
// most once a second. No config writes or forced per-frame CVar enforcement.
inline void tick(bool wanted, bool eligible, bool conflict) {
    const auto now = GetTickCount64();
    const bool transition = wanted != last_wanted || eligible != last_eligible || conflict != last_conflict;
    last_wanted = wanted; last_eligible = eligible; last_conflict = conflict;
    if (!transition && last_observed_ms && now >= last_observed_ms && now-last_observed_ms < 1000) return;
    last_observed_ms = now;
    ConsoleIO io;
    policy.tick(wanted, eligible, conflict, io);
    std::scoped_lock lock{status_mutex}; cached = policy.status();
}
inline nlohmann::json diagnostic_status() {
    const auto s = status();
    nlohmann::json result{{"requested",s.requested},{"eligible",s.eligible},{"owned",s.owned},
        {"blocked",s.blocked},{"status",message(s.code)},{"variable",variable_name}};
    if (s.original) { result["original_float"] = s.original->value; result["original_int"] = s.original->integer; }
    return result;
}
} // namespace wuwa_rim
