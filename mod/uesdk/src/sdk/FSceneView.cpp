#include <unordered_set>
#include <algorithm>
#include <cstdint>

#include <spdlog/spdlog.h>

#include <Windows.h>

#include <utility/Scan.hpp>
#include <utility/Module.hpp>

#include "Utility.hpp"
#include "EngineModule.hpp"
#include "FSceneView.hpp"

namespace sdk {
void FSceneViewInitOptionsBase::update_offsets(FSceneViewInitOptionsBase* options) {
    if (s_attempted_update || options == nullptr) {
        return;
    }

    s_attempted_update = true;
    std::optional<uint32_t> first_valid_sequential_vtable_offset{};

    // Let's start at 0x80 to skip past the 2 matrices at the start, to reduce false positives.
    // As far back as 4.8 this heuristic holds true as far as I can tell.
    for (uint32_t i = 0x80; i < 0x200; i += sizeof(void*)) try {
        const auto ptr = *(uintptr_t*)((uintptr_t)options + i);

        if (ptr == 0 || IsBadReadPtr((void*)ptr, sizeof(void*)) || (ptr & 1) != 0) {
            continue;
        }

        const auto ptr_vtable = *(uintptr_t**)ptr;

        // Vtable should be valid
        if (ptr_vtable == nullptr || IsBadReadPtr((void*)ptr_vtable, sizeof(void*)) || !utility::get_module_within((void*)ptr_vtable)) {
            continue;
        }

        // First function pointer in the vtable should be valid
        if (ptr_vtable[0] == 0 || IsBadReadPtr((void*)ptr_vtable[0], sizeof(void*)) || !utility::get_module_within((void*)ptr_vtable[0])) {
            continue;
        }

        SPDLOG_INFO("[FSceneViewInitOptions] Spotted a valid vtable at offset {:x}", i);

        const auto prev_ptr = *(uintptr_t*)((uintptr_t)options + i - sizeof(void*));

        if (prev_ptr == 0 || IsBadReadPtr((void*)prev_ptr, sizeof(void*)) || (prev_ptr & 1) != 0) {
            continue;
        }

        const auto prev_ptr_vtable = *(uintptr_t**)prev_ptr;
        const auto prev_ptr_vtable_is_valid = prev_ptr_vtable != nullptr && !IsBadReadPtr(prev_ptr_vtable, sizeof(void*)) && utility::get_module_within(prev_ptr_vtable);
        const auto prev_ptr_fn = prev_ptr_vtable_is_valid ? prev_ptr_vtable[0] : 0;
        const auto prev_ptr_fn_is_valid = prev_ptr_fn != 0 && !IsBadReadPtr((void*)prev_ptr_fn, sizeof(void*)) && utility::get_module_within((void*)prev_ptr_fn);
        const bool prev_ptr_is_scene_view_family_ue4 = !prev_ptr_vtable_is_valid || !prev_ptr_fn_is_valid;

        // Second valid pointer is the scene state... usually.
        // The pointer preceding it should be the view family.
        if (prev_ptr_is_scene_view_family_ue4) {
            s_view_family_offset = i - (uint32_t)sizeof(void*);
            s_scene_state_offset = i;
            SPDLOG_INFO("[FSceneViewInitOptions] Found view family offset at {:x}", *s_view_family_offset);
            SPDLOG_INFO("[FSceneViewInitOptions] Found scene state offset at {:x}", *s_scene_state_offset);
            break;
        }


        // If we haven't found the view family offset yet, we can use the first valid vtable offset as a fallback.
        if (!first_valid_sequential_vtable_offset) {
            first_valid_sequential_vtable_offset = i;
        }
    } catch(...) {

    }

    if (!s_view_family_offset || !s_scene_state_offset) {
        SPDLOG_ERROR("[FSceneViewInitOptions] Failed to find offsets for FSceneViewInitOptions, attempting to fallback to UE5 offsets");

        if (first_valid_sequential_vtable_offset) {
            // On UE5, the FSceneViewFamily has a virtual destructor instead of a normal one.
            SPDLOG_INFO("[FSceneViewInitOptions] Attempting to use fallback offset at {:x}", *first_valid_sequential_vtable_offset);
            s_view_family_offset = *first_valid_sequential_vtable_offset - (uint32_t)sizeof(void*);
            s_scene_state_offset = *first_valid_sequential_vtable_offset;
        } else {
            SPDLOG_ERROR("[FSceneViewInitOptions] Failed to find fallback offsets for FSceneViewInitOptions");
            return;
        }
    }

    //s_stereo_pass_offset = *s_scene_state_offset + 0x50; // Usually true. If not, weird engine changes have been made.
    s_stereo_pass_offset = *s_view_family_offset + (uint32_t)(offsetof(FSceneViewInitOptionsUE4, stereo_pass) - offsetof(FSceneViewInitOptionsUE4, family));
    static_assert((offsetof(FSceneViewInitOptionsUE4, stereo_pass) - offsetof(FSceneViewInitOptionsUE4, family) == 0x58), "FSceneViewInitOptionsUE4 stereo_pass offset is not 0x58");

    SPDLOG_INFO("[FSceneViewInitOptions] Found stereo pass offset at {:x}", *s_stereo_pass_offset);
}

void FSceneView::constructor(const FSceneViewInitOptions* options) {
    const auto fn_addr = get_constructor_address();

    if (!fn_addr) {
        return;
    }

    const auto fn = (void* (*)(FSceneView*, const FSceneViewInitOptions*))*fn_addr;

    fn(this, options);
}

std::optional<uintptr_t> FSceneView::get_constructor_address() {
    static auto fn_addr = []() -> std::optional<uintptr_t> {
        SPDLOG_INFO("Searching for FSceneView constructor...");

        const auto module = sdk::get_ue_module(L"Engine");

        if (module == nullptr) {
            return std::nullopt;
        }

        // We need to find the string references "vr.InstancedStereo" and "r.TranslucentSortPolicy"
        // These two strings will reside together within the same function (the constructor)
        // ADDENDUM: We are only finding the references for "r.TranslucentSortPolicy" now
        // we are still making use of "vr.InstancedStereo" but we are checking whether instructions
        // reference data that == L"vr.InstancedStereo" instead of checking for the string reference itself
        const auto translucent_strings = utility::scan_strings(module, L"r.TranslucentSortPolicy");

        if (translucent_strings.empty()) {
            SPDLOG_ERROR("[FSceneView] Failed to find string references for FSceneView constructor");
            return std::nullopt;
        }

        SPDLOG_INFO("[FSceneView] Found string references for FSceneView constructor");

        std::vector<uintptr_t> translucent_string_refs{};

        for (const auto& translucent_string : translucent_strings) {
            SPDLOG_INFO("[FSceneView] Found r.TranslucentSortPolicy string at 0x{:x}", translucent_string);

            const auto translucent_string_refs_ = utility::scan_displacement_references(module, translucent_string);

            translucent_string_refs.insert(translucent_string_refs.end(), translucent_string_refs_.begin(), translucent_string_refs_.end());
        }

        if (translucent_string_refs.empty()) {
            SPDLOG_ERROR("[FSceneView] Failed to find references for FSceneView constructor");
            return std::nullopt;
        }

        // For use with a fallback method
        std::vector<uintptr_t> translucent_functions{};

        const auto add_candidate = [&translucent_functions](uintptr_t fn) {
            if (std::find(translucent_functions.begin(), translucent_functions.end(), fn) == translucent_functions.end()) {
                translucent_functions.push_back(fn);
            }
        };

        for (const auto& translucent_ref : translucent_string_refs) {
            SPDLOG_INFO("[FSceneView] Found r.TranslucentSortPolicy reference at 0x{:x}", translucent_ref);

            const auto translucent_func = utility::find_function_start_with_call(translucent_ref);

            if (translucent_func) {
                add_candidate(*translucent_func);
                continue;
            }

            SPDLOG_WARN("[FSceneView] Failed to find function start (with call) for reference at 0x{:x}", translucent_ref);

            // find_function_start_with_call only accepts a function start that some
            // direct CALL instruction references. A constructor that is inlined at
            // its call sites, or dispatched indirectly through a register or vtable,
            // has no such reference, so every candidate start is rejected and this
            // whole reference gets dropped without ever being searched.
            //
            // Observed in Wuthering Waves 2.6.5.0: of two r.TranslucentSortPolicy
            // references, one resolved fine and the other was discarded here, so the
            // exhaustive search below only ever ran against one candidate and the
            // constructor was never found. Fall back to a plain function start so
            // the dropped reference still gets its turn.
            if (const auto unwind_func = utility::find_function_start_unwind(translucent_ref)) {
                SPDLOG_INFO("[FSceneView] Falling back to unwind function start 0x{:x} for reference at 0x{:x}", *unwind_func, translucent_ref);
                add_candidate(*unwind_func);
                continue;
            }

            if (const auto plain_func = utility::find_function_start(translucent_ref)) {
                SPDLOG_INFO("[FSceneView] Falling back to scanned function start 0x{:x} for reference at 0x{:x}", *plain_func, translucent_ref);
                add_candidate(*plain_func);
                continue;
            }

            SPDLOG_WARN("[FSceneView] No function start could be resolved for reference at 0x{:x}", translucent_ref);
        }

        // previously we naively checked if the vr.InstancedStereo string ref was
        // in the same function as one of the r.TranslucentSortPolicy string refs
        // however, at some point, UE decided to
        // move the vr.InstancedStereo string ref inside of its own function
        // however, this function is still called from the constructor
        // so we can exhaustively disassemble all code paths from the r.TranslucentSortPolicy string refs
        // until we find a reference to the vr.InstancedStereo string along the way
        // this is kind of a scorched earth method, but it works
        SPDLOG_INFO("[FSceneView] Exhaustively searching for FSceneView constructor");

        std::unordered_set<uintptr_t> seen_ips{};

        const wchar_t target_string[] = L"vr.InstancedStereo";
        const size_t target_length = sizeof(target_string) - sizeof(wchar_t);  // Do not count the null terminator

        struct FunctionRef {
            uintptr_t fn;
            size_t refcount;
        };

        std::vector<FunctionRef> correct_functions{};

        for (const auto& translucent_function : translucent_functions) {
            SPDLOG_INFO("[FSceneView] Exhaustively searching for references to vr.InstancedStereo in 0x{:x}", translucent_function);

            bool is_correct_function = false;

            // seen_ips must not carry across candidates. The callback BREAKs on any
            // already-seen ip, so with more than one candidate in the list an earlier
            // walk can starve a later one of its search before it starts -- silently,
            // and in exactly the case the fallback above exists to handle.
            seen_ips.clear();

            utility::exhaustive_decode((uint8_t*)translucent_function, 3000, [&](INSTRUX& ix, uintptr_t ip) -> utility::ExhaustionResult {
                if (seen_ips.contains(ip) || is_correct_function) {
                    return utility::ExhaustionResult::BREAK;
                }

                seen_ips.insert(ip);

                // Looking for something like "lea rdx, "vr.InstancedStereo""
                // but we will assume it can be any kind of instruction that references the string
                const auto displacement = utility::resolve_displacement(ip);

                if (!displacement) {
                    return utility::ExhaustionResult::CONTINUE;
                }

                // Directly check the data at the displacement instead
                // because modular builds have the string in a different DLL
                // and hardcoding which DLL its in seems sloppy
                try {
                    const auto potential_string = (wchar_t*)*displacement;

                    if (IsBadReadPtr(potential_string, target_length)) {
                        return utility::ExhaustionResult::CONTINUE;
                    }

                    if (std::memcmp(potential_string, target_string, target_length) == 0) {
                        FunctionRef ref{};
                        ref.fn = translucent_function;

                        utility::scan_relative_reference(module, translucent_function, [&](uintptr_t call_ip) -> bool {
                            if (auto resolved = utility::resolve_instruction(call_ip)) {
                                if (utility::resolve_displacement(resolved->addr).value_or(0) == translucent_function) {
                                    ref.refcount++;
                                }
                            }
                            return false;
                        });

                        SPDLOG_INFO("[FSceneView] Found correct displacement at 0x{:x}", ip);
                        SPDLOG_INFO("[FSceneView] Found {} references to potential FSceneView constructor at 0x{:x}", ref.refcount, translucent_function);

                        is_correct_function = true;
                        correct_functions.push_back(ref);
                        return utility::ExhaustionResult::BREAK;
                    }
                } catch(...) {

                }

                return utility::ExhaustionResult::CONTINUE;
            });

            if (is_correct_function) {
                SPDLOG_INFO("[FSceneView] Found potential FSceneView constructor at 0x{:x}", translucent_function);
            }
        }

        // The one with highest refs is the correct one. Or if there's only one correct_function, that's the one.
        std::sort(correct_functions.begin(), correct_functions.end(), [](const FunctionRef& a, const FunctionRef& b) {
            return a.refcount > b.refcount;
        });

        if (!correct_functions.empty()) {
            SPDLOG_INFO("[FSceneView] Found FSceneView constructor at 0x{:x}", correct_functions[0].fn);
            return correct_functions[0].fn;
        }

        SPDLOG_ERROR("[FSceneView] Failed to find FSceneView constructor");
        return std::nullopt;
    }();

    return fn_addr;
}
}