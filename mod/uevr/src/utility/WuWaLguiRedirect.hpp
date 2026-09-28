#pragma once

// Opt-in experiment pinned to the inspected September 8 WuWa code. See
// _docs/lgui-external-texture-route-2026-09-19.md in the parent project.
// Uses the game's external-texture registration, not copied graph objects.
#include "WuWaLguiRoute.hpp"
#include "WuWaLguiCompatibility.hpp"
#include "WuWaLguiTargetPolicy.hpp"
#include "WuWaStage0Payload.hpp"
#include "WuWaWorldLabelStereo.hpp"
#include "WuWaMenuSignals.hpp"
#include "WuWaMotionTrace.hpp"
#include "Logging.hpp"
#include <cmath>
#include <intrin.h>

namespace wuwa_lgui_redirect {
namespace checked = wuwa_lgui_probe::detail;
using Provider = wuwa_lgui_route::Provider;
enum class Mode { register_only, redirect };
enum class ViewContext : size_t { hud, tonemap, capture, count };

constexpr uintptr_t setup_rva = 0x237a18c0;
constexpr uintptr_t pass_rva = 0x2377f7a0;
constexpr uintptr_t menu_setup_rva = 0x2379fcb0;
constexpr uintptr_t menu_pass_rva = 0x2377f2d0;
constexpr uintptr_t wrap_rva = 0x23e57e10;
constexpr uintptr_t register_rva = 0x23e31570;
constexpr uintptr_t graph_vtable_rva = 0x2759ec60;
constexpr uintptr_t pooled_vtable_rva = 0x278b8d10;
constexpr uintptr_t release_rva = 0x23e668d0;
constexpr uintptr_t addref_rva = 0x23e533e0;
// September 19 disk variant: same PE timestamp, image smaller by two pages.
// Accept its metadata only together with the code checks below, before hooks.
constexpr uint32_t september19_image_size = 1029853184;
constexpr std::array<std::array<uint8_t, 32>, 4> signatures{{
    {0x4c,0x8b,0xdc,0x4d,0x89,0x4b,0x20,0x4d,0x89,0x43,0x18,0x55,0x41,0x55,0x41,0x56,0x49,0x8d,0xab,0x68,0xfe,0xff,0xff,0x48,0x81,0xec,0x80,0x02,0x00,0x00,0x48,0x8b},
    {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x41,0x0f,0xb7,0xd9,0x4c,0x89,0x44,0x24,0x20,0x48,0x8b,0xfa,0x48,0x8b,0xf1,0xe8,0x0d},
    {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48,0x81,0xec,0x80,0x00,0x00,0x00,0x48,0x8b,0x02,0x48},
    {0x40,0x53,0x55,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xec,0xc8,0x00,0x00,0x00,0x48,0x8b,0x05,0x50,0xde,0xd0,0x13,0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0xb0}
}};

// Hidden return storage, bare FRHITexture*, persistent name. The wrapped target
// owns RHI references; RegisterExternalTexture retains it for graph execution.
using Wrap = uintptr_t* (*)(uintptr_t*, uintptr_t, const wchar_t*);
using Register = uintptr_t (*)(uintptr_t, const uintptr_t*, const wchar_t*, uint8_t, uint8_t);
using Release = uint32_t (*)(uintptr_t);
inline constexpr wchar_t target_name[] = L"UEVR.WuWaLGUI";

struct State {
    std::mutex administration;
    SRWLOCK callbacks = SRWLOCK_INIT;
    std::atomic<bool> active{false};
    bool attempted{};
    Mode mode{Mode::register_only};
    uintptr_t base{};
    Provider provider{};
    safetyhook::InlineHook setup{};
    safetyhook::MidHook pass{};
    safetyhook::InlineHook menu_setup{};
    safetyhook::MidHook menu_pass{};
    safetyhook::MidHook stage0_construct{}, stage0_dispatch{};
    safetyhook::InlineHook world_setup{};
    std::atomic<uint64_t> world_calls{}, world_prepared{}, world_layout_skips{}, world_guard_skips{};
    std::atomic<uint64_t> world_caller_skips{}, world_disabled_skips{}, world_mode_skips{}, world_samples{};
    std::array<std::atomic<uint64_t>,16> next_stage0_sample{};
    std::atomic<bool> menu_ready{false};
    std::atomic<uint64_t> calls{}, no_target{}, foreign_color{}, depth_bound{},
        registered{}, redirected{}, fitted{}, capture_suppressed{}, capture_pending{}, invalid{}, callback_errors{};
    std::atomic<uint64_t> menu_candidates{}, menu_redirected{}, menu_fitted{};
    std::atomic<uint64_t> stage3_calls{}, stage3_redirected{}, stage3_fitted{}, stage3_empty{}, stage3_samples{};
    std::atomic<uint64_t> stage3_capture_suppressed{}, stage3_capture_pending{};
    struct MenuObservation {
        RejectedDescription target{};
        uint8_t depth{}, constrained{};
        bool populated{};
    };
    std::mutex menu_observations_mutex;
    std::array<MenuObservation, 16> menu_observations{};
    size_t menu_observation_count{};
    std::array<std::atomic<uint64_t>, size_t(ViewContext::count) * 8 * 64> menu_stage_samples{};
    std::array<std::atomic<uint64_t>, size_t(ViewContext::count)> next_stage_sample{};
    std::atomic<uint64_t> invalid_graph{}, invalid_view{}, invalid_wrap{}, invalid_release{},
        invalid_registration{}, invalid_pass_storage{}, invalid_pass_rect{}, invalid_pass_write{};
    std::array<std::atomic<uint64_t>, static_cast<size_t>(GraphStatus::count)> graph_rejections{};
    std::atomic<uintptr_t> last_source{}, last_ui{}, last_graph{}, last_capture{}, last_fitted_ui{};
    uint64_t last_summary{};
    // Distinct rejected colour targets (reason + debug name + extent), bounded.
    struct RejectedTarget { RejectedDescription description{}; uint64_t count{}; uint64_t logged{}; std::string text{}; };
    std::mutex rejected_mutex;
    std::array<RejectedTarget, 24> rejected_targets{};
    size_t rejected_target_count{};
};

// Retain hooks, trampoline tails, and a module reference until process exit.
// disable() is not a guarantee that no thread is still leaving a hook stub.
inline State& state() { static auto* value = new State{}; return *value; }

inline void record_invalid(State& s, std::atomic<uint64_t>& reason) noexcept {
    ++reason;
    ++s.invalid;
}

template<size_t N> inline bool verify_code(HMODULE executable, uint32_t image_size,
        const std::array<compatibility::CodeRange, N>& ranges, const char* route) {
    const auto base = reinterpret_cast<uintptr_t>(executable);
    size_t matched{}, total_bytes{};
    for (const auto& range : ranges) {
        const bool in_image = range.size > 0 && range.rva < image_size &&
            range.size <= image_size - range.rva && base <= UINTPTR_MAX - image_size;
        std::vector<uint8_t> bytes(range.size);
        const bool readable = in_image && checked::executable_site(base + range.rva, range.size, executable) &&
            checked::guarded_copy(bytes.data(), reinterpret_cast<const void*>(base + range.rva), range.size);
        uint64_t hash = 0xcbf29ce484222325ULL;
        if (readable) {
            for (const auto value : bytes) { hash ^= value; hash *= 0x100000001b3ULL; }
        }
        if (readable && hash == range.fingerprint) {
            ++matched;
        } else {
            spdlog::error("[WuWaLguiRedirect] code mismatch rva={:x} size={} readable={} expected={:x} actual={:x}",
                range.rva, range.size, readable, range.fingerprint, hash);
        }
        total_bytes += range.size;
    }
    spdlog::info("[WuWaLguiRedirect] code compatibility route={} matched={}/{} bytes={}",
        route, matched, ranges.size(), total_bytes);
    return matched == ranges.size();
}

struct CallbackGuard {
    State& s;
    DWORD error{GetLastError()};
    explicit CallbackGuard(State& value) : s{value} { AcquireSRWLockShared(&s.callbacks); }
    ~CallbackGuard() { ReleaseSRWLockShared(&s.callbacks); SetLastError(error); }
};

inline bool extent(uintptr_t texture, std::array<int32_t, 2>& size) noexcept {
    return checked::read_field(texture, 0x54, size) && size[0] >= 64 && size[0] <= 16384 &&
        size[1] >= 64 && size[1] <= 16384;
}

inline bool graph_resource(uintptr_t graph, uintptr_t& rhi,
        GraphStatus* diagnostic = nullptr, uintptr_t* observed_vtable = nullptr) noexcept {
    uintptr_t vtable{};
    GraphStatus status{GraphStatus::valid};
    if (!checked::read(graph, vtable)) status = GraphStatus::unreadable_object;
    else if (vtable != state().base + graph_vtable_rva) status = GraphStatus::other_vtable;
    else if (!checked::read_field(graph, 0x10, rhi)) status = GraphStatus::unreadable_rhi;
    else if (rhi == 0) status = GraphStatus::null_rhi;
    if (diagnostic != nullptr) *diagnostic = status;
    if (observed_vtable != nullptr) *observed_vtable = vtable;
    return status == GraphStatus::valid;
}

// Describe a rejected colour without trusting it: guarded reads only, nothing is
// written or called. Offsets are verified in the Sep 8 capture: the RDG
// resource base constructor 0x23e1b900 stores the debug name at +0x8, type at
// +0x18 and flag bits at +0x19; texture constructor 0x210b6870 copies the
// descriptor extent to +0x54/+0x58 and the pixel format to +0x68. Do not follow
// these offsets on another class. Repeated rejects need no string allocation.
inline RejectedDescription read_rejected(uintptr_t color, uintptr_t vtable, GraphStatus status) noexcept {
    const auto& s = state();
    RejectedDescription result{};
    result.status = status;
    if (vtable >= s.base && vtable - s.base < checked::image_size) result.vtable_rva = vtable - s.base;
    if (status == GraphStatus::unreadable_object || vtable != s.base + graph_vtable_rva) return result;
    uintptr_t name_pointer{};
    std::array<wchar_t, 48> text{};
    if (checked::read_field(color, 0x8, name_pointer) && name_pointer != 0 && checked::read(name_pointer, text)) {
        std::array<char, 48> narrow{};
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == L'\0') { result.name = narrow; break; }
            if (text[i] < 0x20 || text[i] > 0x7e) break;
            narrow[i] = static_cast<char>(text[i]);
        }
    }
    result.metadata_valid = checked::read_field(color, 0x54, result.size)
        && checked::read_field(color, 0x68, result.format)
        && checked::read_field(color, 0x18, result.type)
        && checked::read_field(color, 0x19, result.flags);
    return result;
}

inline std::string describe_rejected(const RejectedDescription& value) {
    return fmt::format("reason={} name={} metadata_valid={} extent={}x{} format={} type={} flags={:02x} vtable_rva={:x}",
        static_cast<size_t>(value.status), value.name.data(), value.metadata_valid,
        value.size[0], value.size[1], value.format, value.type, value.flags, value.vtable_rva);
}

inline nlohmann::json sample_view_gate(uintptr_t view) {
    // Raw inputs to the shipped 0x22ea9870 eligibility helper, inspected in
    // _docs/lgui-menu-stage0-contract-2026-09-24.md. No predicate is bypassed
    // and no helper is called. Unknown reads stay unknown, never "false".
    // These observations distinguish the main view from the native capture;
    // neither a count nor these flags alone prove an NPC label was rendered.
    uint32_t flags{}, stereo_pass{};
    uint8_t byte_ffe{}, byte_1000{};
    nlohmann::json result;
    result["flags_d964"] = checked::read_field(view, 0xd964, flags) ? nlohmann::json(flags) : nlohmann::json(nullptr);
    result["byte_ffe"] = checked::read_field(view, 0xffe, byte_ffe) ? nlohmann::json(byte_ffe) : nlohmann::json(nullptr);
    result["byte_1000"] = checked::read_field(view, 0x1000, byte_1000) ? nlohmann::json(byte_1000) : nlohmann::json(nullptr);
    result["stereo_pass_c90"] = checked::read_field(view, 0xc90, stereo_pass) ? nlohmann::json(stereo_pass) : nlohmann::json(nullptr);
    return result;
}

inline void sample_menu_stages(State& s, uintptr_t renderer, uintptr_t view, ViewContext view_context,
        const wuwa_lgui_route::Target& target) noexcept {
    // The setup's early gate walks renderer+0x108/+0x110, stride 0xe8d0;
    // it does not inspect only the view argument. Both count arrays are
    // required. See _docs/lgui-cvar-render-route-2026-09-08.md. These are
    // read-only observations, not claims that visibility predicates passed.
    const size_t context = size_t(view_context);
    if (context >= size_t(ViewContext::count)) return;
    constexpr std::array<const char*, 3> names{"hud", "tonemap", "capture"};
    const auto now = GetTickCount64();
    auto next = s.next_stage_sample[context].load(std::memory_order_relaxed);
    if (now < next || !s.next_stage_sample[context].compare_exchange_strong(next, now + 500)) return;
    try {
        uintptr_t list{};
        int32_t count{};
        if (!checked::read_field(renderer, 0x108, list) || !checked::read_field(renderer, 0x110, count) ||
            !list || count < 1 || count > 8 || list > UINTPTR_MAX - size_t(count) * 0xe8d0) {
            SPDLOG_INFO_EVERY_N_SEC(5, "[WuWaLguiRedirect] menu_stages unavailable context={} renderer={:x} list={:x} count={}",
                names[context], renderer, list, count);
            return;
        }
        struct Sample {
            uintptr_t address{};
            std::array<int32_t, 6> early{}, dispatch{};
            uint32_t valid_mask{}, populated_mask{};
        };
        std::array<Sample, 8> samples{};
        size_t mask{};
        bool view_in_array{};
        for (int32_t j = 0; j < count; ++j) {
            auto& sample = samples[j];
            sample.address = list + size_t(j) * 0xe8d0;
            view_in_array |= sample.address == view;
            for (size_t i = 0; i < 6; ++i) {
                const bool a = checked::read_field(sample.address, 0x2104 + i * 4, sample.early[i]);
                const bool b = checked::read_field(sample.address, 0x91cc + i * 0x320, sample.dispatch[i]);
                if (!a || sample.early[i] < 0 || sample.early[i] > 100000) sample.early[i] = -1;
                if (!b || sample.dispatch[i] < 0 || sample.dispatch[i] > 100000) sample.dispatch[i] = -1;
                if (sample.early[i] >= 0 && sample.dispatch[i] >= 0) sample.valid_mask |= 1U << i;
                if (sample.early[i] > 0 && sample.dispatch[i] > 0) sample.populated_mask |= 1U << i;
            }
            mask |= sample.populated_mask;
        }
        // A mode change can reduce a two-view family to one. Give that path
        // its own initial samples, then retain a sparse continuing record.
        // The previous lifetime quota missed later NPC/menu states with the
        // same populated mask. The 500ms outer throttle still bounds work.
        const auto bucket = (context * 8 + size_t(count - 1)) * 64 + mask;
        const auto occurrence = s.menu_stage_samples[bucket].fetch_add(1, std::memory_order_relaxed) + 1;
        if (occurrence > 2 && occurrence % 20 != 0) return;
        auto details = nlohmann::json::array();
        for (int32_t j = 0; j < count; ++j) {
            const auto& sample = samples[j];
            details.push_back({{"index", j}, {"address", sample.address}, {"early_counts", sample.early},
                {"dispatch_counts", sample.dispatch}, {"valid_mask", sample.valid_mask}, {"populated_mask", sample.populated_mask},
                {"raw_view_gate", sample_view_gate(sample.address)}});
        }
        const nlohmann::json render_context{{"independent_eyes", target.independent_eyes},
            {"native_stereo_fix", target.native_stereo_fix}};
        spdlog::info("[WuWaLguiRedirect] menu_stages context={} mask={:02x} sample={} renderer={:x} view={:x} view_in_array={} stages={} argument_gate={} render_context={}",
            names[context], mask, occurrence, renderer, view, view_in_array, details.dump(),
            sample_view_gate(view).dump(), render_context.dump());
    } catch (...) { ++s.callback_errors; }
}

inline void note_rejected(State& s, const RejectedDescription& description) noexcept {
    try {
        // Best-effort per-target counts; the reason counters remain exact.
        // A periodic log writer must not stall a render callback.
        std::unique_lock lock{s.rejected_mutex, std::try_to_lock};
        if (!lock.owns_lock()) return;
        for (size_t i = 0; i < s.rejected_target_count; ++i) {
            if (s.rejected_targets[i].description == description) { ++s.rejected_targets[i].count; return; }
        }
        if (s.rejected_target_count == s.rejected_targets.size()) return;
        auto formatted = describe_rejected(description);
        s.rejected_targets[s.rejected_target_count++] = {description, 1, 0, formatted};
        lock.unlock();
        spdlog::info("[WuWaLguiRedirect] graph_reject_new {}", formatted);
    } catch (...) {}
}

inline bool writable(uintptr_t address, size_t size) noexcept {
    if (!address || !size || address > UINTPTR_MAX - size) return false;
    const auto end = address + size;
    while (address < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info) ||
            !checked::readable(info)) return false;
        const auto protection = info.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto begin = reinterpret_cast<uintptr_t>(info.BaseAddress);
        if (begin > address || info.RegionSize > UINTPTR_MAX - begin || begin + info.RegionSize <= address) return false;
        address = (std::min)(end, begin + info.RegionSize);
    }
    return true;
}

struct SetupRoute { uintptr_t color{}; bool suppress{}; bool menu{}; MenuTargets targets{}; };
struct SetupContext { uintptr_t graph{}; bool menu{}, stage3{}; MenuTargets targets{}; };
inline thread_local SetupContext active_setup{};
inline thread_local MenuFitReceipt menu_fit_receipt{};
struct ScopedSetupContext {
    SetupContext previous{active_setup};
    explicit ScopedSetupContext(SetupContext value) { active_setup = value; }
    ~ScopedSetupContext() { active_setup = previous; }
};

inline uintptr_t register_ui_target(State& s, const wuwa_lgui_route::Target& target,
        uintptr_t builder, const std::array<int32_t, 2>& ui_size) {
        uintptr_t pooled{};
        reinterpret_cast<Wrap>(s.base + wrap_rva)(&pooled, target.ui, target_name);
        if (!pooled) { record_invalid(s, s.invalid_wrap); return 0; }
        uintptr_t vtable{}, release{};
        if (!checked::read(pooled, vtable) || vtable != s.base + pooled_vtable_rva ||
            !checked::read_field(vtable, 0x40, release) || release != s.base + release_rva) {
            // A layout mismatch is exceptional: do not invoke an unknown virtual.
            // Stop further engine calls. Retain this one wrapper until exit.
            s.active.store(false, std::memory_order_release);
            record_invalid(s, s.invalid_release);
            return 0;
        }
        struct OwnedTarget {
            uintptr_t value, release;
            ~OwnedTarget() { reinterpret_cast<Release>(release)(value); }
        } owned{pooled, release};
        // Selector 0 uses the targetable RHI at pooled+8. Flags 0 are the
        // ordinary registration mode. Both are traced in the shipped function.
        const auto graph = reinterpret_cast<Register>(s.base + register_rva)(
            builder, &pooled, target_name, 0, 0);
        uintptr_t result_rhi{};
        std::array<int32_t, 2> result_size{};
        if (!graph_resource(graph, result_rhi) || result_rhi != target.ui ||
            !extent(graph, result_size) || result_size != ui_size) { record_invalid(s, s.invalid_registration); return 0; }
        ++s.registered;
        s.last_graph.store(graph, std::memory_order_relaxed);
    return graph;
}

inline SetupRoute route_setup(uintptr_t renderer, uintptr_t builder, uintptr_t color, uintptr_t view) noexcept {
    auto& s = state();
    CallbackGuard guard{s};
    const SetupRoute unchanged{color, false};
    if (!s.active.load(std::memory_order_acquire)) return unchanged;
    s.calls.fetch_add(1, std::memory_order_relaxed);
    try {
        const auto target = s.provider ? s.provider() : wuwa_lgui_route::Target{};
        if (!target.redirect_enabled) return unchanged;
        std::array<int32_t, 2> ui_size{};
        if (!target.ui || !target.game || !extent(target.ui, ui_size)) { ++s.no_target; return unchanged; }
        uintptr_t source{};
        uint8_t depth_flag{};
        GraphStatus graph_status{};
        uintptr_t graph_vtable{};
        bool transient_menu{};
        if (!graph_resource(color, source, &graph_status, &graph_vtable)) {
            const auto description = read_rejected(color, graph_vtable, graph_status);
            std::array<int32_t, 2> game_size{};
            const bool known_menu_target = extent(target.game, game_size) &&
                is_transient_menu_candidate(description, game_size, target.native_stereo_fix);
            if (known_menu_target) sample_menu_stages(s, renderer, view, ViewContext::tonemap, target);
            if (target.menu_redirect_enabled && known_menu_target) {
                transient_menu = true;
                if (++s.menu_candidates <= 3) {
                    spdlog::info("[WuWaLguiRedirect] menu_candidate color={:x} game_size={}x{} {}",
                        color, game_size[0], game_size[1], describe_rejected(description));
                }
            } else {
                record_invalid(s, s.invalid_graph);
                const auto category = static_cast<size_t>(graph_status);
                const auto count = s.graph_rejections[category].fetch_add(1, std::memory_order_relaxed) + 1;
                note_rejected(s, description);
                // Three examples per reason, not per-frame logging. Never inspect
                // the RHI field of an object whose class guard did not match.
                if (count <= 3) {
                    spdlog::info("[WuWaLguiRedirect] graph_sample reason={} color={:x} vtable={:x} expected_vtable={:x} rhi={:x} game={:x} ui={:x} capture={:x} {}",
                        category, color, graph_vtable, s.base + graph_vtable_rva, source,
                        target.game, target.ui, target.capture, describe_rejected(description));
                }
                return unchanged;
            }
        }
        if (!transient_menu && source == target.game) sample_menu_stages(s, renderer, view, ViewContext::hud, target);
        else if (!transient_menu && target.capture && source == target.capture)
            sample_menu_stages(s, renderer, view, ViewContext::capture, target);
        if (!checked::read_field(view, 0x254c, depth_flag)) { record_invalid(s, s.invalid_view); return unchanged; }
        s.last_source.store(source, std::memory_order_relaxed);
        s.last_ui.store(target.ui, std::memory_order_relaxed);
        s.last_capture.store(target.capture, std::memory_order_relaxed);
        if (source == target.ui) { ++s.foreign_color; return unchanged; }
        // September 20 gameplay matched the remaining HUD color RHI exactly
        // to the native-stereo scene capture (displayed left with swap-eyes).
        // Suppress only its screen-HUD setup, after a main-target UI pass has
        // been fitted to this UI texture. Preserve unknown/depth-bound draws
        // and the entire scene capture that supplies the other eye's scene.
        if (s.mode == Mode::redirect && target.capture && target.capture != target.game && source == target.capture) {
            if (depth_flag) { ++s.depth_bound; return unchanged; }
            const auto fitted_ui = s.last_fitted_ui.load(std::memory_order_acquire);
            if (fitted_ui != target.ui) {
                ++s.capture_pending;
                return unchanged;
            }
            ++s.capture_suppressed;
            return {color, true};
        }
        if (!transient_menu && source != target.game) { ++s.foreign_color; return unchanged; }
        if (depth_flag) { ++s.depth_bound; return unchanged; }

        const auto graph = register_ui_target(s, target, builder, ui_size);
        if (!graph) return unchanged;
        if (s.mode == Mode::redirect) {
            ++s.redirected;
            if (transient_menu) {
                ++s.menu_redirected;
                wuwa_menu::transient_menu_at.store(GetTickCount64(),std::memory_order_relaxed);
            }
            return {graph, false, transient_menu};
        }
    } catch (...) { ++s.callback_errors; }
    return unchanged;
}

// Verified setup ABI: four pointer registers, view at entry RSP+0x28, one
// 32-bit stack argument at +0x30, and void return (including no-mesh exits).
// Use a normal return for a duplicate; do not edit the game's return stack.
inline void setup_callback(uintptr_t renderer, uintptr_t builder, uintptr_t color,
        uintptr_t depth, uintptr_t view, uint32_t arg6) {
    const auto route = route_setup(renderer, builder, color, view);
    // The pass constructor runs synchronously inside this setup. Correlate
    // it with this exact invocation, not a cached graph shared by HUD calls.
    ScopedSetupContext scope{{route.color != color ? route.color : 0, route.menu, false}};
    // route_setup releases our callback lock before the original can enter
    // pass_callback. Keep game exceptions outside the instrumentation catch.
    if (!route.suppress) state().setup.call<void>(renderer, builder, route.color, depth, view, arg6);
}

// Stage 3 is a separate screen-canvas route. Its NSF main/capture identities
// were observed on September 27. It has its own successful-draw receipt;
// stage 2's last_fitted_ui is not sufficient to suppress a menu copy.
inline SetupRoute route_menu_setup(uintptr_t builder, uintptr_t color, uintptr_t depth, uintptr_t view) noexcept {
    auto& s = state();
    CallbackGuard guard{s};
    const SetupRoute unchanged{color, false};
    if (!s.active.load(std::memory_order_acquire) || !s.menu_ready.load(std::memory_order_acquire)) return unchanged;
    ++s.stage3_calls;
    try {
        const auto target = s.provider ? s.provider() : wuwa_lgui_route::Target{};
        uintptr_t source{}, vtable{};
        GraphStatus status{};
        graph_resource(color, source, &status, &vtable);
        const auto description = read_rejected(color, vtable, status);
        if (source == target.game || !target.redirect_enabled || !target.menu_redirect_enabled)
            menu_fit_receipt.reset();
        int32_t early{}, dispatch{};
        uint8_t depth_flag{}, constrain_flag{};
        float aspect{};
        const bool view_ok = checked::read_field(view, 0x2110, early) && checked::read_field(view, 0x9b2c, dispatch) &&
            checked::read_field(view, 0x254c, depth_flag) && checked::read_field(view, 0x2d9, constrain_flag) &&
            checked::read_field(view, 0x2dc, aspect);
        const bool populated = view_ok && early > 0 && early <= 100000 && dispatch > 0 && dispatch <= 100000;
        {
            std::unique_lock lock{s.menu_observations_mutex, std::try_to_lock};
            if (lock.owns_lock() && s.menu_observation_count < s.menu_observations.size()) {
                bool seen{};
                for (size_t i = 0; i < s.menu_observation_count; ++i) {
                    const auto& sample = s.menu_observations[i];
                    if (sample.target == description && sample.depth == depth_flag &&
                        sample.constrained == constrain_flag && sample.populated == populated) { seen = true; break; }
                }
                if (!seen) {
                    s.menu_observations[s.menu_observation_count++] = {description, depth_flag, constrain_flag, populated};
                    ++s.stage3_samples;
                    lock.unlock();
                    spdlog::info("[WuWaLguiRedirect] stage3_target color={:x} rhi={:x} game_match={} ui_match={} capture_match={} depth={:x} depth_flag={} view_ok={} constrained={} aspect={} counts={}/{} menu_enabled={} {}",
                        color, source, source != 0 && source == target.game, source != 0 && source == target.ui,
                        source != 0 && source == target.capture, depth, depth_flag, view_ok, constrain_flag,
                        aspect, early, dispatch, target.menu_redirect_enabled, describe_rejected(description));
                }
            }
        }
        if (!populated) { ++s.stage3_empty; return unchanged; }
        if (depth_flag || !std::isfinite(aspect) || (constrain_flag && aspect > 0.0f)) return unchanged;
        std::array<int32_t, 2> ui_size{}, game_size{}, capture_size{};
        if (!target.ui || !target.game || !extent(target.ui, ui_size) || !extent(target.game, game_size) ||
            !source) return unchanged;
        if (target.capture && source == target.capture) extent(target.capture, capture_size);
        const MenuTargets targets{target.ui, target.game, target.capture};
        const auto route = stage3_route(description, targets, source, game_size, capture_size, target.native_stereo_fix);
        // Menu detection is also used by camera suspension and hidden-UI hints.
        // It must not disappear merely because extraction is switched off.
        if (route != MenuRoute::unchanged)
            wuwa_menu::screen_overlay_at.store(GetTickCount64(),std::memory_order_relaxed);
        if (!target.redirect_enabled || !target.menu_redirect_enabled) return unchanged;
        if (route == MenuRoute::capture && s.mode == Mode::redirect) {
            if (!menu_fit_receipt.consume(targets, GetTickCount64())) {
                ++s.stage3_capture_pending;
                return unchanged;
            }
            ++s.stage3_capture_suppressed;
            return {color, true, true};
        }
        if (route != MenuRoute::main) return unchanged;
        const auto graph = register_ui_target(s, target, builder, ui_size);
        if (graph && s.mode == Mode::redirect) {
            ++s.stage3_redirected;
            wuwa_menu::screen_overlay_at.store(GetTickCount64(),std::memory_order_relaxed);
            return {graph, false, true, targets};
        }
    } catch (...) { ++s.callback_errors; }
    return unchanged;
}

// The stage 3 setup has the same six-argument ABI, but its separate pass
// constructor and dispatcher must not be confused with the HUD stage.
inline void menu_setup_callback(uintptr_t renderer, uintptr_t builder, uintptr_t color,
        uintptr_t depth, uintptr_t view, uint32_t arg6) {
    const auto route = route_menu_setup(builder, color, depth, view);
    ScopedSetupContext scope{{route.color != color ? route.color : 0, route.menu, true,
        route.targets}};
    if (!route.suppress) state().menu_setup.call<void>(renderer, builder, route.color, depth, view, arg6);
}

inline void fit_pass(safetyhook::Context& context, bool stage3) noexcept {
    auto& s = state();
    CallbackGuard guard{s};
    if (!s.active.load(std::memory_order_acquire) || s.mode != Mode::redirect ||
        !active_setup.graph || active_setup.stage3 != stage3) return;
    try {
        uintptr_t color{}, rhi{}, capture{};
        std::array<int32_t, 2> ui_size{};
        const auto target = s.provider ? s.provider() : wuwa_lgui_route::Target{};
        // R8 is the real pass parameter-data argument. Its +0x10 binding is
        // proven by the metadata and converter, not a scan for equal pointers.
        if (!checked::read_field(context.r8, 0x10, color) || !graph_resource(color, rhi) ||
            color != active_setup.graph || !target.ui || rhi != target.ui || !extent(color, ui_size)) return;
        // This constructor copies exactly 0x100 bytes of its fifth argument.
        // Change only its declared rectangle before that copy; no view or
        // projection object is changed. The engine owns the deferred payload.
        if (!checked::read_field(context.rsp, 0x28, capture) || capture > UINTPTR_MAX - 0x100 ||
            !writable(capture + 0x14, 16)) { record_invalid(s, s.invalid_pass_storage); return; }
        std::array<int32_t, 4> original{};
        if (!checked::read_field(capture, 0x14, original) || original[0] < 0 || original[1] < 0 ||
            original[2] <= original[0] || original[3] <= original[1] ||
            original[2] > 32768 || original[3] > 32768) { record_invalid(s, s.invalid_pass_rect); return; }
        const std::array<int32_t, 4> fitted{0, 0, ui_size[0], ui_size[1]};
        if (checked::guarded_copy(reinterpret_cast<void*>(capture + 0x14), fitted.data(), sizeof(fitted))) {
            if (stage3) {
                if (active_setup.targets == MenuTargets{target.ui, target.game, target.capture})
                    menu_fit_receipt.fitted(active_setup.targets, GetTickCount64());
                if (s.stage3_fitted.fetch_add(1) == 0) {
                    spdlog::info("[WuWaLguiRedirect] stage3_fit original={},{},{},{} ui={}x{}",
                        original[0], original[1], original[2], original[3], ui_size[0], ui_size[1]);
                }
            } else {
                ++s.fitted;
                if (active_setup.menu) ++s.menu_fitted;
                s.last_fitted_ui.store(target.ui, std::memory_order_release);
            }
        }
        else record_invalid(s, s.invalid_pass_write);
    } catch (...) { ++s.callback_errors; }
}

inline void pass_callback(safetyhook::Context& context) noexcept { fit_pass(context, false); }
inline void menu_pass_callback(safetyhook::Context& context) noexcept { fit_pass(context, true); }

struct WorldRoute {
    uintptr_t views{};
    wuwa_world_labels::TextureRect color{}, depth{};
    std::optional<wuwa_world_labels::Plan> plan;
};

inline WorldRoute world_route(uintptr_t renderer, uintptr_t color, uintptr_t depth,
        uintptr_t view, uint32_t view_index, uintptr_t caller) noexcept {
    auto& s = state();
    CallbackGuard guard{s};
    WorldRoute result{};
    if (!s.active.load(std::memory_order_acquire)) return result;
    ++s.world_calls;
    try {
        const auto target = s.provider ? s.provider() : wuwa_lgui_route::Target{};
        if (caller != s.base + 0x2379fc83) { ++s.world_caller_skips; return result; }
        if (!target.world_labels_enabled) { ++s.world_disabled_skips; return result; }
        if (target.native_stereo_fix || target.independent_eyes) { ++s.world_mode_skips; return result; }
        // Only the verified last-view caller is expanded. Do not replay a pass
        // from another caller, monoscopic capture, Native Fix or AFR family.
        wuwa_world_labels::Layout layout{};
        uintptr_t views{}, cv{}, dv{}, color_rhi{};
        if (!checked::read_field(renderer, 0x108, views) || !views ||
            !checked::read_field(renderer, 0x110, layout.views) || layout.views != 2 ||
            views > UINTPTR_MAX - 0xe8d0 || view != views + 0xe8d0 ||
            !checked::read(color, result.color) || !checked::read(depth, result.depth) ||
            !checked::read(result.color.graph, cv) || !checked::read(result.depth.graph, dv) ||
            cv != s.base + graph_vtable_rva || dv != cv ||
            !graph_resource(result.color.graph, color_rhi) || color_rhi != target.game ||
            !extent(target.game, layout.game_extent) ||
            !extent(result.color.graph, layout.color_extent) || !extent(result.depth.graph, layout.depth_extent) ||
            !checked::read_field(views, 0xc90, layout.first_pass) ||
            !checked::read_field(view, 0xc90, layout.second_pass) ||
            !checked::read_field(views, 0x2104, layout.first_early_count) ||
            !checked::read_field(views, 0x91cc, layout.first_dispatch_count)) {
            ++s.world_guard_skips;
            return result;
        }
        layout.argument_index = 1;
        layout.argument_view_index = view_index;
        layout.color_input = result.color.rect;
        layout.depth_input = result.depth.rect;
        for (size_t i = 0; i < 2; ++i) {
            const auto eye = views + i * 0xe8d0;
            if (!checked::read_field(eye, 0x2f8, layout.rects_2f8[i]) ||
                !checked::read_field(eye, 0x1e30, layout.rects_1e30[i])) {
                ++s.world_guard_skips;
                return result;
            }
        }
        if (layout.first_early_count > 0 && layout.first_dispatch_count > 0 && ++s.world_samples <= 3) {
            uintptr_t rhi{};
            GraphStatus status{};
            graph_resource(result.color.graph, rhi, &status);
            spdlog::info("[WuWaWorldLabels] input {}", nlohmann::json{
                {"argument_view_index", view_index}, {"color_graph", result.color.graph},
                {"color_rhi", rhi}, {"game_rhi", target.game}, {"matches_game", rhi == target.game},
                {"color_description", describe_rejected(read_rejected(result.color.graph, cv, status))},
                {"color_input", layout.color_input}, {"depth_input", layout.depth_input}}.dump());
        }
        result.plan = wuwa_world_labels::plan(layout);
        if (result.plan) {
            result.views = views;
            if (++s.world_prepared <= 2) {
                spdlog::info("[WuWaWorldLabels] prepared {}", nlohmann::json{
                    {"color", result.plan->color}, {"depth", result.plan->depth},
                    {"color_extent", layout.color_extent}, {"depth_extent", layout.depth_extent},
                    {"argument_view_index", view_index}}.dump());
            }
        } else if (layout.first_early_count > 0 && layout.first_dispatch_count > 0 && ++s.world_layout_skips <= 3) {
            spdlog::info("[WuWaWorldLabels] unsupported layout; original retained {}", nlohmann::json{
                {"game_extent", layout.game_extent}, {"color_extent", layout.color_extent},
                {"depth_extent", layout.depth_extent}, {"color_input", layout.color_input},
                {"depth_input", layout.depth_input}, {"rects_2f8", layout.rects_2f8},
                {"rects_1e30", layout.rects_1e30}, {"passes", {layout.first_pass, layout.second_pass}},
                {"argument_view_index", view_index}}.dump());
        }
    } catch (...) { result.plan.reset(); ++s.callback_errors; }
    return result;
}

inline void world_setup_callback(uintptr_t renderer, uintptr_t builder, uintptr_t color,
        uintptr_t depth, uintptr_t view, uint32_t view_index) {
    const auto route = world_route(renderer, color, depth, view, view_index, reinterpret_cast<uintptr_t>(_ReturnAddress()));
    // All original engine calls remain outside the instrumentation catch. The
    // verified setup copies these local records into graph-owned pass storage.
    // Add the missing view, then retain the original call and pointers exactly.
    // The sixth argument is the renderer's view index: traced through the
    // post-process delegate and used by the per-view uniform builder. View 0
    // must get index 0, not the index from the last-view call we intercepted.
    if (route.plan) {
        const wuwa_world_labels::TextureRect c{route.color.graph, route.plan->color[0]};
        const wuwa_world_labels::TextureRect d{route.depth.graph, route.plan->depth[0]};
        state().world_setup.call<void>(renderer, builder, reinterpret_cast<uintptr_t>(&c),
            reinterpret_cast<uintptr_t>(&d), route.views, uint32_t{0});
    }
    state().world_setup.call<void>(renderer, builder, color, depth, view, view_index);
}

inline void install_world_labels(State& s, HMODULE executable, uint32_t image_size) noexcept {
    try {
        checked::require(verify_code(executable, image_size, compatibility::world_label_code_ranges,
            "world-labels"), "World-label route code mismatch");
        auto hook = safetyhook::InlineHook::create(reinterpret_cast<void*>(s.base + 0x237a01d0),
            &world_setup_callback, safetyhook::InlineHook::StartDisabled);
        checked::require(hook.has_value(), "Cannot create world-label setup hook");
        s.world_setup = std::move(*hook);
        checked::require(s.world_setup.enable().has_value(), "Cannot enable world-label setup hook");
        spdlog::info("[WuWaWorldLabels] candidate installed; per-eye rectangles required; controlled by VR_WuWaWorldLabelsStereo");
    } catch (const std::exception& e) {
        try { if (s.world_setup && !s.world_setup.disable()) ++s.callback_errors; } catch (...) { ++s.callback_errors; }
        spdlog::warn("[WuWaWorldLabels] unavailable; existing HUD/menu route retained: {}", e.what());
    }
}

inline void observe_stage0(safetyhook::Context& context,bool dispatch) noexcept {
    if (!wuwa_motion::active()) return;
    auto& s=state();
    CallbackGuard guard{s};
    if (!s.active.load(std::memory_order_acquire)) return;
    try {
        std::string recording_id;
        {
            std::unique_lock recording_lock{wuwa_motion::mutex,std::try_to_lock};
            if (!recording_lock.owns_lock() || !wuwa_motion::active()) return;
            recording_id=wuwa_motion::id;
        }
        uintptr_t address{};
        if (dispatch) address=context.rsi;
        else if (!checked::read_field(context.rsp,0x28,address)) return;
        wuwa_lgui_stage0::Payload payload{};
        if (!checked::read(address,payload) || !payload.renderer || !payload.view) return;
        uintptr_t views{};int32_t count{};
        if (!checked::read_field(payload.renderer,0x108,views) || !checked::read_field(payload.renderer,0x110,count) ||
            !views || count<1 || count>8 || payload.view<views) return;
        const auto distance=payload.view-views;
        if (distance%0xe8d0 || distance/0xe8d0>=static_cast<uint64_t>(count)) return;
        const auto index=static_cast<size_t>(distance/0xe8d0);
        const auto now=GetTickCount64();auto& next=s.next_stage0_sample[(dispatch ? 8 : 0)+index];
        auto prior=next.load(std::memory_order_relaxed);
        if (now<prior || !next.compare_exchange_strong(prior,now+1000)) return;
        nlohmann::json sample{{"clock_ms",now},{"unix_ms",wuwa_motion::unix_ms()},
            {"event",dispatch ? "before_draw_dispatch" : "pass_constructed"},{"view_index",index},
            {"renderer",payload.renderer},{"view",payload.view},{"view_count",count},
            {"viewport",payload.viewport},{"color_extent",payload.color_extent},{"color_rect",payload.color_rect},
            {"depth_extent",payload.depth_extent},{"depth_rect",payload.depth_rect},{"scale",payload.scale},
            {"viewport_valid",wuwa_lgui_stage0::rectangle_valid(payload.viewport,payload.color_extent)},
            {"color_rect_valid",wuwa_lgui_stage0::rectangle_valid(payload.color_rect,payload.color_extent)},
            {"depth_rect_valid",wuwa_lgui_stage0::rectangle_valid(payload.depth_rect,payload.depth_extent)},
            {"view_gate",sample_view_gate(payload.view)}};
        if (dispatch) {
            // Verified RBP-relative local uniform storage after both viewport
            // helpers, immediately before the existing draw dispatcher CALL.
            std::array<float,64> matrices{};std::array<float,4> depth_uv{};
            if (checked::read_field(context.rbp,0x410,matrices)) sample["uniform_prefix_64_floats"]=matrices;
            if (checked::read_field(context.rbp,0x830,depth_uv)) sample["depth_scale_bias"]=depth_uv;
        }
        wuwa_motion::append_render_stage0(std::move(sample),recording_id);
    } catch (...) { ++s.callback_errors; }
}
inline void stage0_construct_callback(safetyhook::Context& context) noexcept { observe_stage0(context,false); }
inline void stage0_dispatch_callback(safetyhook::Context& context) noexcept { observe_stage0(context,true); }
inline void disable_stage0_probe(State& s) noexcept {
    try { if(s.stage0_construct && !s.stage0_construct.disable()) ++s.callback_errors; } catch (...) { ++s.callback_errors; }
    try { if(s.stage0_dispatch && !s.stage0_dispatch.disable()) ++s.callback_errors; } catch (...) { ++s.callback_errors; }
}
inline void install_stage0_probe(State& s,HMODULE executable,uint32_t image_size) noexcept {
    try {
        checked::require(verify_code(executable,image_size,compatibility::stage0_probe_code_ranges,"stage0-recording"),"Stage-0 probe code mismatch");
        auto constructor=safetyhook::MidHook::create(reinterpret_cast<void*>(s.base+0x2377f3a0),&stage0_construct_callback,safetyhook::MidHook::StartDisabled);
        auto dispatch=safetyhook::MidHook::create(reinterpret_cast<void*>(s.base+0x23782e7c),&stage0_dispatch_callback,safetyhook::MidHook::StartDisabled);
        checked::require(constructor.has_value() && dispatch.has_value(),"Cannot create stage-0 recording probe");
        s.stage0_construct=std::move(*constructor);s.stage0_dispatch=std::move(*dispatch);
        checked::require(s.stage0_construct.enable().has_value() && s.stage0_dispatch.enable().has_value(),"Cannot enable stage-0 recording probe");
        spdlog::info("[WuWaLguiStage0] Read-only probe installed; samples only during motion recording. No render parameters are changed.");
    } catch (const std::exception& e) {
        disable_stage0_probe(s);
        spdlog::warn("[WuWaLguiStage0] Probe unavailable; HUD/menu route retained: {}",e.what());
    }
}

inline void disable_menu(State& s) noexcept {
    s.menu_ready.store(false, std::memory_order_release);
    try { if (s.menu_setup) { const auto result = s.menu_setup.disable(); if (!result) ++s.callback_errors; } } catch (...) { ++s.callback_errors; }
    try { if (s.menu_pass) { const auto result = s.menu_pass.disable(); if (!result) ++s.callback_errors; } } catch (...) { ++s.callback_errors; }
}

inline void install_menu(State& s, HMODULE executable, uint32_t image_size) noexcept {
    try {
        checked::require(verify_code(executable, image_size, compatibility::menu_code_ranges, "menu-stage3"),
            "Menu stage code changed; preserving HUD-only hooks");
        uintptr_t thunk{};
        checked::require(checked::read(s.base + 0x277fb6f8 + sizeof(uintptr_t), thunk) &&
            thunk == s.base + 0x23791340, "Menu execute vtable changed; preserving HUD-only hooks");
        auto setup = safetyhook::InlineHook::create(reinterpret_cast<void*>(s.base + menu_setup_rva),
            &menu_setup_callback, safetyhook::InlineHook::StartDisabled);
        checked::require(setup.has_value(), "Cannot create menu setup hook");
        s.menu_setup = std::move(*setup);
        auto pass = safetyhook::MidHook::create(reinterpret_cast<void*>(s.base + menu_pass_rva),
            &menu_pass_callback, safetyhook::MidHook::StartDisabled);
        checked::require(pass.has_value(), "Cannot create menu pass hook");
        s.menu_pass = std::move(*pass);
        checked::require(s.menu_pass.enable().has_value(), "Cannot enable menu pass hook");
        checked::require(s.menu_setup.enable().has_value(), "Cannot enable menu setup hook");
        s.menu_ready.store(true, std::memory_order_release);
        spdlog::info("[WuWaLguiRedirect] stage3 installed setup={:x} pass={:x}; extraction follows menu toggle", menu_setup_rva, menu_pass_rva);
    } catch (const std::exception& error) {
        disable_menu(s);
        try { spdlog::error("[WuWaLguiRedirect] stage3 unavailable: {}", error.what()); } catch (...) {}
    } catch (...) { disable_menu(s); }
}

// Caller holds administration. Never hold callbacks exclusively while
// SafetyHook suspends/relocates threads during disable.
inline void disable(State& s) noexcept {
    s.active.store(false, std::memory_order_release);
    disable_menu(s);
    disable_stage0_probe(s);
    try { if (s.world_setup && !s.world_setup.disable()) ++s.callback_errors; } catch (...) { ++s.callback_errors; }
    try { if (s.setup) { const auto result = s.setup.disable(); if (!result) ++s.callback_errors; } } catch (...) { ++s.callback_errors; }
    try { if (s.pass) { const auto result = s.pass.disable(); if (!result) ++s.callback_errors; } } catch (...) { ++s.callback_errors; }
    AcquireSRWLockExclusive(&s.callbacks);
    s.provider = nullptr;
    ReleaseSRWLockExclusive(&s.callbacks);
}

inline void stop() noexcept {
    auto& s = state();
    std::lock_guard lock{s.administration};
    disable(s);
}

inline void tick(HMODULE backend, Provider provider) noexcept {
    auto& s = state();
    std::lock_guard lock{s.administration};
    try {
        if (!s.attempted) {
            s.attempted = true;
            const auto directory = checked::module_path(backend).parent_path();
            const auto path = directory / "wuwa-lgui-redirect.request.json";
            if (!std::filesystem::exists(path)) return;
            checked::require(std::filesystem::file_size(path) <= 4096, "Redirect request too large");
            std::ifstream input{path};
            const auto request = nlohmann::json::parse(input);
            checked::require(request.at("version") == 1 && request.at("enable").is_boolean(), "Expected version 1 and Boolean enable");
            if (!request.at("enable").get<bool>()) return;
            const auto mode = request.at("mode").get<std::string>();
            checked::require(mode == "register" || mode == "redirect", "Expected mode register or redirect");
            s.mode = mode == "redirect" ? Mode::redirect : Mode::register_only;
            checked::require(!std::filesystem::exists(directory / "wuwa-lgui-route.request.json") &&
                !std::filesystem::exists(directory / "wuwa-lgui-probe.request.json"), "Disarm other LGUI probes first");
            const auto executable = GetModuleHandleW(nullptr);
            checked::require(_wcsicmp(checked::module_path(executable).filename().c_str(), L"Client-Win64-Shipping.exe") == 0,
                "Unexpected executable");
            s.base = reinterpret_cast<uintptr_t>(executable);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS64 nt{};
            checked::require(checked::read(s.base, dos) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
                dos.e_lfanew >= static_cast<LONG>(sizeof(dos)) && dos.e_lfanew < 0x100000 && checked::read_field(s.base, dos.e_lfanew, nt),
                "Cannot read executable PE headers");
            spdlog::info("[WuWaLguiRedirect] PE identity base={:x} timestamp={:x} image_size={} machine={:x} magic={:x}",
                s.base, nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage, nt.FileHeader.Machine, nt.OptionalHeader.Magic);
            checked::require(
                nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
                nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp == checked::timestamp &&
                (nt.OptionalHeader.SizeOfImage == checked::image_size || nt.OptionalHeader.SizeOfImage == september19_image_size),
                "Game identity mismatch");
            checked::require(verify_code(executable, nt.OptionalHeader.SizeOfImage, compatibility::code_ranges, "hud"), "Inspected LGUI/graph code changed; hooks remain disabled");
            constexpr std::array<uintptr_t, 4> sites{setup_rva, pass_rva, wrap_rva, register_rva};
            for (size_t i = 0; i < sites.size(); ++i) {
                std::array<uint8_t, 32> bytes{};
                checked::require(checked::executable_site(s.base + sites[i], bytes.size(), executable) &&
                    checked::read(s.base + sites[i], bytes) && bytes == signatures[i], "Engine signature mismatch");
            }
            uintptr_t release{}, addref{};
            checked::require(checked::read(s.base + pooled_vtable_rva + 0x38, addref) && addref == s.base + addref_rva &&
                checked::read(s.base + pooled_vtable_rva + 0x40, release) &&
                release == s.base + release_rva && checked::executable_site(release, 32, executable), "Pooled reference-counting vtable mismatch");
            HMODULE retained{};
            checked::require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(&setup_callback), &retained) != 0, "Cannot retain redirect module");
            auto setup = safetyhook::InlineHook::create(reinterpret_cast<void*>(s.base + setup_rva), &setup_callback,
                safetyhook::InlineHook::StartDisabled);
            checked::require(setup.has_value(), "Cannot create setup hook");
            s.setup = std::move(*setup);
            auto pass = safetyhook::MidHook::create(reinterpret_cast<void*>(s.base + pass_rva), &pass_callback,
                safetyhook::MidHook::StartDisabled);
            checked::require(pass.has_value(), "Cannot create pass hook");
            s.pass = std::move(*pass);
            s.provider = provider;
            checked::require(s.pass.enable().has_value(), "Cannot enable pass hook");
            checked::require(s.setup.enable().has_value(), "Cannot enable setup hook");
            install_menu(s, executable, nt.OptionalHeader.SizeOfImage);
            // Fingerprints include the unmodified stage-0 execution body; do
            // this before installing its optional recording mid-hooks.
            if(request.value("repair_world_labels",false)) install_world_labels(s,executable,nt.OptionalHeader.SizeOfImage);
            if(request.value("record_stages",false)) install_stage0_probe(s,executable,nt.OptionalHeader.SizeOfImage);
            s.active.store(true, std::memory_order_release);
            spdlog::info("[WuWaLguiRedirect] installed mode={} base={:x} setup={:x} wrapper={:x} register={:x}",
                mode, s.base, setup_rva, wrap_rva, register_rva);
        }
        if (!s.setup) return;
        const auto now = GetTickCount64();
        if (now - s.last_summary >= 5000) {
            s.last_summary = now;
            spdlog::info("[WuWaLguiRedirect] mode={} active={} calls={} registered={} redirected={} fitted={} capture_suppressed={} capture_pending={} no_target={} foreign_color={} depth_bound={} invalid={} exceptions={} source={:x} ui={:x} graph={:x} capture={:x}",
                s.mode == Mode::redirect ? "redirect" : "register", s.active.load(), s.calls.load(), s.registered.load(),
                s.redirected.load(), s.fitted.load(), s.capture_suppressed.load(), s.capture_pending.load(),
                s.no_target.load(), s.foreign_color.load(), s.depth_bound.load(), s.invalid.load(), s.callback_errors.load(),
                s.last_source.load(), s.last_ui.load(), s.last_graph.load(), s.last_capture.load());
            const auto target = s.provider ? s.provider() : wuwa_lgui_route::Target{};
            spdlog::info("[WuWaWorldLabels] installed={} enabled={} calls={} prepared={} unsupported_layout={} guard_skips={} caller_skips={} disabled_skips={} mode_skips={}",
                bool(s.world_setup), target.world_labels_enabled, s.world_calls.load(), s.world_prepared.load(),
                s.world_layout_skips.load(), s.world_guard_skips.load(), s.world_caller_skips.load(),
                s.world_disabled_skips.load(), s.world_mode_skips.load());
            spdlog::info("[WuWaLguiRedirect] menu_test enabled={} native_fix={} candidates={} redirected={} fitted={}",
                target.menu_redirect_enabled, target.native_stereo_fix, s.menu_candidates.load(), s.menu_redirected.load(), s.menu_fitted.load());
            spdlog::info("[WuWaLguiRedirect] stage3 ready={} calls={} empty={} redirected={} fitted={} capture_suppressed={} capture_pending={}",
                s.menu_ready.load(), s.stage3_calls.load(), s.stage3_empty.load(), s.stage3_redirected.load(), s.stage3_fitted.load(),
                s.stage3_capture_suppressed.load(), s.stage3_capture_pending.load());
            if (s.invalid.load() != 0) {
                spdlog::info("[WuWaLguiRedirect] rejected graph={} view={} wrap={} release={} registration={} pass_storage={} pass_rect={} pass_write={}",
                    s.invalid_graph.load(), s.invalid_view.load(), s.invalid_wrap.load(), s.invalid_release.load(),
                    s.invalid_registration.load(), s.invalid_pass_storage.load(), s.invalid_pass_rect.load(), s.invalid_pass_write.load());
                spdlog::info("[WuWaLguiRedirect] rejected_graph unreadable_object={} other_vtable={} unreadable_rhi={} null_rhi={}",
                    s.graph_rejections[static_cast<size_t>(GraphStatus::unreadable_object)].load(),
                    s.graph_rejections[static_cast<size_t>(GraphStatus::other_vtable)].load(),
                    s.graph_rejections[static_cast<size_t>(GraphStatus::unreadable_rhi)].load(),
                    s.graph_rejections[static_cast<size_t>(GraphStatus::null_rhi)].load());
                // Only targets that were rejected again since the last summary.
                std::lock_guard rejected{s.rejected_mutex};
                for (size_t i = 0; i < s.rejected_target_count; ++i) {
                    auto& entry = s.rejected_targets[i];
                    if (entry.count == entry.logged) continue;
                    spdlog::info("[WuWaLguiRedirect] rejected_target total={} new={} {}",
                        entry.count, entry.count - entry.logged, entry.text);
                    entry.logged = entry.count;
                }
            }
        }
    } catch (const std::exception& error) {
        disable(s);
        try { spdlog::error("[WuWaLguiRedirect] rejected: {}", error.what()); } catch (...) {}
    } catch (...) { disable(s); }
}
} // namespace wuwa_lgui_redirect
