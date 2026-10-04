#include <chrono>
#include <array>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <intrin.h>
#include <spdlog/spdlog.h>
#include <utility/String.hpp>
#include <utility/Scan.hpp>

#include <SafetyHook.hpp>

#include "Framework.hpp"
#include "Mods.hpp"
#include "XInputHook.hpp"
#include "mods/VR.hpp"
#include "utility/WuWaInputTrace.hpp"
#include "utility/WuWaInputSequenceBridge.hpp"

namespace {
void trace_xinput(uint32_t api, uint32_t index, uint32_t raw_result, const XINPUT_STATE& raw,
                  uint32_t result, const XINPUT_STATE* state, uint32_t changed_by, uintptr_t caller, bool passthrough, int slot_filter) {
    if (index >= XUSER_MAX_COUNT) {
        return;
    }
    // Only OS-owned XInput structures and our own state are read here. Never
    // follow inferred game-object pointers for diagnostics.
    struct Sample {
        uint32_t raw_result{}, result{};
        WORD raw_buttons{}, buttons{};
        BYTE raw_lt{}, raw_rt{}, lt{}, rt{};
        uint32_t changed_by{};
        HWND foreground{};
        int xr_state{};
        int slot_filter{};
        bool menu{}, motion{}, muted{}, passthrough{}, initialized{};
    };
    auto vr = VR::get();
    const auto foreground = GetForegroundWindow();
    // A thread alone does not distinguish ImGui's poll from a game poll on the
    // same thread. Keep the actual return address as part of the reader key.
    const auto thread = GetCurrentThreadId();
    const auto game_thread = thread == GetWindowThreadProcessId(g_framework->get_window(), nullptr);
    const auto xr_state = vr->get_runtime()->is_openxr() ? (int)vr->get_openxr_runtime()->session_state : -1;
    const auto menu = g_framework->is_drawing_ui();
    const auto motion = vr->is_using_controllers();
    const auto muted = wuwa_test::motion_input_muted();
    const auto delivered_state = result == ERROR_SUCCESS && state != nullptr ? *state : XINPUT_STATE{};
    const auto& delivered = delivered_state.Gamepad;
    const auto buttons = delivered.wButtons;
    DWORD foreground_pid{};
    GetWindowThreadProcessId(foreground, &foreground_pid);
    wuwa_test::publish_input({GetTickCount64(), api, index, raw_result, result, changed_by,
        raw, delivered_state, foreground_pid, xr_state, foreground == g_framework->get_window(), menu, motion, muted});
    if (!wuwa_test::tracing_input()) return;
    struct Reader {
        uint32_t api{}, index{}, thread{};
        uintptr_t caller{}, module{};
        bool game_module{}, used{};
        Sample previous{};
    };
    static std::mutex mutex;
    static std::array<Reader, 64> readers{};
    static uint64_t recorded_epoch{};
    std::unique_lock lock{mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
        ++wuwa_test::input_trace_dropped;
        return;
    }
    const auto epoch = wuwa_test::input_trace_epoch.load();
    if (recorded_epoch != epoch) {
        readers = {};
        recorded_epoch = epoch;
    }
    Reader* reader{};
    Reader* vacant{};
    for (auto& entry : readers) {
        if (!entry.used) {
            if (vacant == nullptr) vacant = &entry;
        } else if (entry.api == api && entry.index == index && entry.thread == thread && entry.caller == caller) {
            reader = &entry;
            break;
        }
    }
    if (reader == nullptr) {
        if (vacant == nullptr) {
            ++wuwa_test::input_trace_reader_overflow;
            return;
        }
        reader = vacant;
        reader->api = api;
        reader->index = index;
        reader->thread = thread;
        reader->caller = caller;
        reader->used = true;
        MEMORY_BASIC_INFORMATION info{};
        char module_name[MAX_PATH]{};
        if (VirtualQuery((const void*)caller, &info, sizeof(info)) == sizeof(info) && info.Type == MEM_IMAGE) {
            reader->module = (uintptr_t)info.AllocationBase;
            reader->game_module = info.AllocationBase == GetModuleHandleW(nullptr);
            GetModuleFileNameA((HMODULE)info.AllocationBase, module_name, MAX_PATH);
        }
        module_name[MAX_PATH - 1] = 0;
        const std::string_view path{module_name};
        const auto separator = path.find_last_of("\\/");
        spdlog::info("[WuWaInputReader] epoch={} api={} user={} thread={} game_thread={} caller={:x} module={} rva={:x} game_module={}",
            epoch, api, index, thread, game_thread, caller,
            path.substr(separator == std::string_view::npos ? 0 : separator + 1),
            reader->module ? caller - reader->module : 0, reader->game_module);
    }
    auto& old = reader->previous;
    if (old.initialized && old.raw_result == raw_result && old.result == result
        && old.raw_buttons == raw.Gamepad.wButtons && old.buttons == buttons
        && old.raw_lt == raw.Gamepad.bLeftTrigger && old.raw_rt == raw.Gamepad.bRightTrigger
        && old.lt == delivered.bLeftTrigger && old.rt == delivered.bRightTrigger && old.changed_by == changed_by
        && old.foreground == foreground && old.xr_state == xr_state && old.menu == menu
        && old.motion == motion && old.muted == muted && old.passthrough == passthrough && old.slot_filter == slot_filter) {
        return;
    }
    old = {raw_result, result, raw.Gamepad.wButtons, buttons,
        raw.Gamepad.bLeftTrigger, raw.Gamepad.bRightTrigger, delivered.bLeftTrigger, delivered.bRightTrigger,
        changed_by, foreground, xr_state, slot_filter, menu, motion, muted, passthrough, true};
    const auto row = wuwa_test::input_trace_rows.fetch_add(1, std::memory_order_relaxed);
    if (row >= 1000) {
        return;
    }
    spdlog::info("[WuWaInput] row={} api={} user={} thread={} game_thread={} raw_result={} result={} raw_buttons={:04x} buttons={:04x} "
                 "raw_lstick={},{} lstick={},{} raw_rstick={},{} rstick={},{} "
                 "foreground={:x} foreground_pid={} game_window={} menu={} xr_state={} motion={} motion_muted={} "
                 "raw_packet={} packet={} raw_triggers={},{} triggers={},{} changed_by={:02x} "
                 "epoch={} caller={:x} caller_game={} passthrough={} slot_filter={}",
        row, api, index, thread, game_thread, raw_result, result, raw.Gamepad.wButtons, buttons,
        raw.Gamepad.sThumbLX, raw.Gamepad.sThumbLY, delivered.sThumbLX, delivered.sThumbLY,
        raw.Gamepad.sThumbRX, raw.Gamepad.sThumbRY, delivered.sThumbRX, delivered.sThumbRY,
        (uintptr_t)foreground, foreground_pid, foreground == g_framework->get_window(), menu, xr_state, motion, muted,
        raw.dwPacketNumber, delivered_state.dwPacketNumber, raw.Gamepad.bLeftTrigger, raw.Gamepad.bRightTrigger,
        delivered.bLeftTrigger, delivered.bRightTrigger, changed_by,
        epoch, caller, reader->game_module, passthrough, slot_filter);
}

uint32_t stage_bit(std::string_view name) {
    if (name == "FrameworkConfig") return 1;
    if (name == "VR") return 2;
    if (name == "UObjectHook") return 4;
    if (name == "PluginLoader") return 8;
    if (name == "LuaLoader") return 16;
    return 32;
}

wuwa_input_sequence_bridge::RuntimeGates sequence_gates(bool passthrough,int slot_filter) {
    const auto window=g_framework->get_window();
    return {window && GetForegroundWindow()==window,g_framework->is_drawing_ui(),
        passthrough,VR::get()->is_using_controllers(),slot_filter};
}

// Observe boundaries of the existing mod chain; never restore/override its
// returned state. Failed XInput calls do not provide readable state data.
void run_input_mods(uint32_t& result, uint32_t index, XINPUT_STATE* state, bool observe, uint32_t& changed_by) {
    for (auto& mod : g_framework->get_mods()->get_mods()) {
        const auto before_result = result;
        const XINPUT_STATE before = observe && result == ERROR_SUCCESS && state != nullptr ? *state : XINPUT_STATE{};
        mod->on_xinput_get_state(&result, index, state);
        if (observe && (result != before_result || (result == ERROR_SUCCESS && state != nullptr
            && (!wuwa_test::same_gamepad(before.Gamepad, state->Gamepad) || before.dwPacketNumber != state->dwPacketNumber)))) {
            changed_by |= stage_bit(mod->get_name());
        }
    }
}
}

XInputHook* g_hook{nullptr};

namespace {
uintptr_t resolve_jmp(uint8_t* instr) {
    try {
        const auto decoded = utility::decode_one(instr);

        if (decoded) {
            const auto mnem = std::string_view{decoded->Mnemonic};

            if (mnem.starts_with("JMP")) {
                const auto target = utility::resolve_displacement((uintptr_t)instr);

                if (target.has_value()) {
                    if (instr[0] == 0xFF && instr[1] == 0x25) {
                        const auto real_target = *(uintptr_t*)*target;

                        if (real_target == 0) {
                            return (uintptr_t)instr;
                        }

                        return resolve_jmp((uint8_t*)real_target);
                    }

                    return resolve_jmp((uint8_t*)target.value());
                }
            }
        }
    } catch(...) {
        SPDLOG_ERROR("[XInputHook] recursive_resolve_jmp exception");
    }

    return (uintptr_t)instr;
}

// The observed run loaded xinput1_4 before injection; xinput1_3 did not appear
// within the old ten-second deadline. Keep waiting for the latter without
// claiming that its presence proves which API the game actually polls.
// The jthread stop token bounds shutdown even if the DLL never loads.
HMODULE wait_for_dll(const std::string& name, std::stop_token stop, std::optional<std::chrono::seconds> limit) {
    const auto start_time = std::chrono::steady_clock::now();
    bool reported{false};

    while (!stop.stop_requested()) {
        const auto elapsed = std::chrono::steady_clock::now() - start_time;

        if (const auto dll = GetModuleHandleA(name.c_str()); dll != nullptr) {
            if (reported) {
                spdlog::info("[XInputHook] {} loaded after {} seconds; hooking it now", name,
                    std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
            }

            return dll;
        }

        if (limit.has_value() && elapsed > *limit) {
            spdlog::error("[XInputHook] Failed to find {} after {} seconds", name, limit->count());
            return nullptr;
        }

        if (!reported && elapsed > std::chrono::seconds(10)) {
            spdlog::info("[XInputHook] {} not loaded after 10 seconds; still waiting for the game to load it", name);
            reported = true;
        }

        std::this_thread::sleep_for(reported ? std::chrono::milliseconds(250) : std::chrono::milliseconds(10));
    }

    return nullptr;
}

void hook_export(HMODULE dll, const char* export_name, const char* label, safetyhook::InlineHook& hook, void* destination) {
    const auto fn = (void*)GetProcAddress(dll, export_name);

    if (fn == nullptr) {
        spdlog::error("[XInputHook] Failed to find {}", export_name);
        return;
    }

    hook = safetyhook::create_inline(fn, destination);

    if (hook) {
        return;
    }

    spdlog::error("Failed to hook {} ({}), trying jmp", export_name, label);

    // Check if there is a jmp instruction at the start of the function and try to hook that instead
    const auto jmp_addr = resolve_jmp((uint8_t*)fn);

    if (jmp_addr != (uintptr_t)fn) {
        hook = safetyhook::create_inline((void*)jmp_addr, destination);

        if (!hook) {
            spdlog::error("Failed to hook {} ({}) (jmp)", export_name, label);
        }
    } else {
        spdlog::error("Cannot try jmp hook for {} ({}) (jmp) (recursive_resolve_jmp failed)", export_name, label);
    }
}

void perform_hooks(std::stop_token stop, const std::string& dll_name, const char* label, std::optional<std::chrono::seconds> limit,
                   safetyhook::InlineHook& get_state, void* get_state_destination,
                   safetyhook::InlineHook& set_state, void* set_state_destination) {
    const auto dll = wait_for_dll(dll_name, stop, limit);

    if (dll != nullptr) {
        std::scoped_lock _{g_framework->get_hook_monitor_mutex()};

        hook_export(dll, "XInputGetState", label, get_state, get_state_destination);
        hook_export(dll, "XInputSetState", label, set_state, set_state_destination);
    }

    spdlog::info("[XInputHook] Done ({})", label);
}
}

XInputHook::XInputHook() {
    g_hook = this;
    spdlog::info("[XInputHook] Entry");

    // We use threads because this may take a while to find the DLLs,
    // and it shouldn't cause any issues anyway
    spdlog::info("[XInputHook] Starting hook thread");
    m_hook_thread_1_4 = std::make_unique<std::jthread>([this](std::stop_token stop) {
        perform_hooks(stop, "xinput1_4.dll", "1_4", std::chrono::seconds(10),
            m_xinput_1_4_get_state_hook, (void*)&get_state_hook_1_4, m_xinput_1_4_set_state_hook, (void*)&set_state_hook_1_4);
    });
    m_hook_thread_1_3 = std::make_unique<std::jthread>([this](std::stop_token stop) {
        const auto limit = wuwa_test::is_wuwa() ? std::optional<std::chrono::seconds>{}
            : std::optional{std::chrono::seconds(10)};
        perform_hooks(stop, "xinput1_3.dll", "1_3", limit,
            m_xinput_1_3_get_state_hook, (void*)&get_state_hook_1_3, m_xinput_1_3_set_state_hook, (void*)&set_state_hook_1_3);
    });
    spdlog::info("[XInputHook] Hook thread started");
}

uint32_t XInputHook::get_state_hook_1_4(uint32_t user_index, XINPUT_STATE* state) {
    const auto caller = (uintptr_t)_ReturnAddress();
    if (!g_framework->is_ready()) {
        return g_hook->m_xinput_1_4_get_state_hook.call<uint32_t>(user_index, state);
    }

    auto ret = g_hook->m_xinput_1_4_get_state_hook.call<uint32_t>(user_index, state);
    const bool wuwa=wuwa_test::is_wuwa();
    const auto trace = wuwa_test::observing_input() || (wuwa && wuwa_input_sequence_bridge::active.load());
    const auto raw_result = ret;
    const XINPUT_STATE raw = ret == ERROR_SUCCESS && state != nullptr ? *state : XINPUT_STATE{};

    uint32_t changed_by{};
    const auto passthrough = VR::get()->physical_gamepad_passthrough();
    const auto slot_filter = VR::get()->gamepad_slot_filter();
    const bool filtered=wuwa_test::filter_input_slot(slot_filter,user_index);
    if (filtered) {
        if (state != nullptr) *state = {};
        ret = ERROR_DEVICE_NOT_CONNECTED;
        changed_by = 64; // Explicit device isolation, not lost button mapping.
    }
    const auto feed=wuwa?wuwa_input_sequence_bridge::before_mods(14,user_index,raw_result,raw,
        ret,state,caller,sequence_gates(passthrough,slot_filter)):wuwa_input_sequence_bridge::Poll{};
    if(feed.generated) changed_by|=128;
    if(!filtered && !passthrough) run_input_mods(ret,user_index,state,trace,changed_by);
    if(wuwa) wuwa_input_sequence_bridge::after_mods(feed,ret,state);
    if(wuwa) VR::get()->stamp_sightseeing_packet(ret,user_index,state);

    if (trace) {
        trace_xinput(14, user_index, raw_result, raw, ret, state, changed_by, caller, passthrough, slot_filter);
    }

    return ret;
}

uint32_t XInputHook::set_state_hook_1_4(uint32_t user_index, XINPUT_VIBRATION* vibration) {
    if (!g_framework->is_ready()) {
        return g_hook->m_xinput_1_4_set_state_hook.call<uint32_t>(user_index, vibration);
    }

    auto ret = g_hook->m_xinput_1_4_set_state_hook.call<uint32_t>(user_index, vibration);
    if (VR::get()->physical_gamepad_passthrough()) return ret;

    const auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_xinput_set_state(&ret, user_index, vibration);
    }

    return ret;
}

uint32_t XInputHook::get_state_hook_1_3(uint32_t user_index, XINPUT_STATE* state) {
    const auto caller = (uintptr_t)_ReturnAddress();
    if (!g_framework->is_ready()) {
        return g_hook->m_xinput_1_3_get_state_hook.call<uint32_t>(user_index, state);
    }

    auto ret = g_hook->m_xinput_1_3_get_state_hook.call<uint32_t>(user_index, state);
    const bool wuwa=wuwa_test::is_wuwa();
    const auto trace = wuwa_test::observing_input() || (wuwa && wuwa_input_sequence_bridge::active.load());
    const auto raw_result = ret;
    const XINPUT_STATE raw = ret == ERROR_SUCCESS && state != nullptr ? *state : XINPUT_STATE{};

    uint32_t changed_by{};
    const auto passthrough = VR::get()->physical_gamepad_passthrough();
    const auto slot_filter = VR::get()->gamepad_slot_filter();
    const bool filtered=wuwa_test::filter_input_slot(slot_filter,user_index);
    if (filtered) {
        if (state != nullptr) *state = {};
        ret = ERROR_DEVICE_NOT_CONNECTED;
        changed_by = 64;
    }
    const auto feed=wuwa?wuwa_input_sequence_bridge::before_mods(13,user_index,raw_result,raw,
        ret,state,caller,sequence_gates(passthrough,slot_filter)):wuwa_input_sequence_bridge::Poll{};
    if(feed.generated) changed_by|=128;
    if(!filtered && !passthrough) run_input_mods(ret,user_index,state,trace,changed_by);
    if(wuwa) wuwa_input_sequence_bridge::after_mods(feed,ret,state);
    if(wuwa) VR::get()->stamp_sightseeing_packet(ret,user_index,state);

    if (trace) {
        trace_xinput(13, user_index, raw_result, raw, ret, state, changed_by, caller, passthrough, slot_filter);
    }

    return ret;
}

uint32_t XInputHook::set_state_hook_1_3(uint32_t user_index, XINPUT_VIBRATION* vibration) {
    if (!g_framework->is_ready()) {
        return g_hook->m_xinput_1_3_set_state_hook.call<uint32_t>(user_index, vibration);
    }

    auto ret = g_hook->m_xinput_1_3_set_state_hook.call<uint32_t>(user_index, vibration);
    if (VR::get()->physical_gamepad_passthrough()) return ret;

    const auto& mods = g_framework->get_mods()->get_mods();

    for (auto& mod : mods) {
        mod->on_xinput_set_state(&ret, user_index, vibration);
    }

    return ret;
}
