#include <spdlog/spdlog.h>
#include <utility/String.hpp>
#include <Windows.h>

#include "UObjectArray.hpp"
#include "UClass.hpp"
#include "UEnum.hpp"

#include "FEnumProperty.hpp"

namespace sdk {
namespace {
template <typename T> bool read_candidate(const void* address, T& value) {
    SIZE_T read{};
    return address != nullptr && ReadProcessMemory(GetCurrentProcess(), address,
        &value, sizeof(value), &read) && read == sizeof(value);
}

bool registered_object(FUObjectArray* objects, const UObjectBase* object) try {
    if (objects == nullptr || object == nullptr) return false;
    int32_t index{};
    if (!read_candidate(reinterpret_cast<const char*>(object) + UObjectBase::get_internal_index_offset(), index) ||
        index < 0 || index >= objects->get_object_count()) return false;
    const auto item = objects->get_object(index);
    UObjectBase* registered{};
    return item != nullptr && read_candidate(&item->object, registered) && registered == object;
} catch (...) {
    return false;
}

bool registered_enum(FUObjectArray* objects, const UEnum* candidate, const UClass* enum_class) {
    // FField/FProperty is not a UObject on newer engines. A readable pointer,
    // or even a plausible class/name field, does not establish UObject identity.
    if (!registered_object(objects, candidate)) return false;
    UClass* candidate_class{};
    return read_candidate(reinterpret_cast<const char*>(candidate) + UObjectBase::get_class_private_offset(), candidate_class) &&
        candidate_class == enum_class;
}
}

void FEnumProperty::update_offsets() {
    if (s_updated_offsets) {
        return;
    }

    s_updated_offsets = true;

    SPDLOG_INFO("[FEnumProperty::update_offsets] Updating offsets");

    const auto repmovement = sdk::find_uobject<sdk::UScriptStruct>(L"ScriptStruct /Script/Engine.RepMovement");

    if (repmovement == nullptr) {
        SPDLOG_ERROR("[FEnumProperty::update_offsets] Failed to find RepMovement");
        return;
    }

    // Find the first EnumProperty
    FEnumProperty* first_enum_property{nullptr};
    FEnumProperty* second_enum_property{nullptr};

    for (auto prop = repmovement->get_child_properties(); prop != nullptr; prop = prop->get_next()) {
        const auto c = prop->get_class();

        if (c == nullptr) {
            continue;
        }

        if (c->get_name().to_string_no_numbers() == L"EnumProperty") {
            if (first_enum_property == nullptr) {
                first_enum_property = (FEnumProperty*)prop;
            } else if (second_enum_property == nullptr) {
                second_enum_property = (FEnumProperty*)prop;
            } else {
                break;
            }
        }
    }

    if (first_enum_property == nullptr) {
        SPDLOG_ERROR("[FEnumProperty::update_offsets] Failed to find first enum property");
        return;
    }

    if (second_enum_property == nullptr) {
        SPDLOG_ERROR("[FEnumProperty::update_offsets] Failed to find second enum property");
        return;
    }

    const auto first_enum_c = first_enum_property->get_class();
    const auto second_enum_c = second_enum_property->get_class();

    if (first_enum_c == nullptr || second_enum_c == nullptr) {
        SPDLOG_ERROR("[FEnumProperty::update_offsets] Failed to find first or second enum class");
        return;
    }

    // Start from FProperty's offset offset, and bruteforce until we find the correct offset
    const auto initial_start = FProperty::s_offset_offset + 4 + sizeof(void*) + sizeof(void*);
    // align up to sizeof(void*)
    const auto start = (initial_start + sizeof(void*) - 1) & ~(sizeof(void*) - 1);

    // underlying prop
    for (auto i = start; i < start + 0x100; i += sizeof(void*)) try {
        sdk::FProperty* potential_numeric_property_a{};
        sdk::FProperty* potential_numeric_property_b{};
        if (!read_candidate(reinterpret_cast<const char*>(first_enum_property) + i, potential_numeric_property_a) ||
            !read_candidate(reinterpret_cast<const char*>(second_enum_property) + i, potential_numeric_property_b)) continue;

        if (potential_numeric_property_a == nullptr || potential_numeric_property_b == nullptr) {
            continue;
        }
        
        const auto potential_numeric_property_a_c = potential_numeric_property_a->get_class();
        const auto potential_numeric_property_b_c = potential_numeric_property_b->get_class();
        
        if (potential_numeric_property_a_c != nullptr && potential_numeric_property_b_c != nullptr) {
            const auto potential_prop_a_c_name = potential_numeric_property_a_c->get_name().to_string_no_numbers();
            const auto potential_prop_b_c_name = potential_numeric_property_b_c->get_name().to_string_no_numbers();

            if (potential_prop_a_c_name == L"ByteProperty" && potential_prop_b_c_name == L"ByteProperty") {
                s_underlying_prop_offset = i;
                SPDLOG_INFO("[FEnumProperty::update_offsets] Found underlying prop offset: 0x{:X}", i);
                break;
            }
        }
    } catch(...) {
        continue;
    }

    // GUObjectArray phase 2 clears its once flag before calling us. Together
    // with s_updated_offsets above, the known-class lookup cannot re-enter
    // this discovery. Only registered objects are used for class identity.
    const auto objects = FUObjectArray::get();
    const auto enum_class = UEnum::static_class();
    if (!registered_object(objects, enum_class)) {
        SPDLOG_ERROR("[FEnumProperty::update_offsets] Enum class identity unavailable; enum access disabled");
        return;
    }

    // UEnum. Do not invoke engine FName conversion on arbitrary candidate
    // bytes: even a catch-all cannot recover every invalid ToString call.
    for (auto i = start; i < start + 0x100; i += sizeof(void*)) try {
        sdk::UEnum* potential_enum_a{};
        sdk::UEnum* potential_enum_b{};
        if (!read_candidate(reinterpret_cast<const char*>(first_enum_property) + i, potential_enum_a) ||
            !read_candidate(reinterpret_cast<const char*>(second_enum_property) + i, potential_enum_b)) continue;

        if (potential_enum_a == nullptr || potential_enum_b == nullptr) {
            continue;
        }
        
        if (registered_enum(objects, potential_enum_a, enum_class) &&
            registered_enum(objects, potential_enum_b, enum_class)) {
            s_enum_offset = i;
            SPDLOG_INFO("[FEnumProperty::update_offsets] Found registered enum offset: 0x{:X}", i);
            break;
        }
    } catch(...) {
        continue;
    }
    if (s_underlying_prop_offset == 0 || s_enum_offset == 0) {
        SPDLOG_ERROR("[FEnumProperty::update_offsets] Incomplete offsets: underlying=0x{:X} enum=0x{:X}; unresolved access disabled",
            s_underlying_prop_offset, s_enum_offset);
    }
}
}
