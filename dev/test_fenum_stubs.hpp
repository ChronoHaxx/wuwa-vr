#pragma once
// Controlled SDK boundary for compiling the exact production FEnumProperty
// translation unit. Raw field buffers and ReadProcessMemory are real; engine
// names/object discovery are deterministic substitutes, never a running game.
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace fixture {
inline unsigned native_name_calls{}, unsafe_name_calls{}, object_class_calls{}, enum_class_lookups{};
inline bool reenter_lookup{};
}

namespace sdk {
class FEnumProperty;
class UClass;
class UObjectBase;
class UScriptStruct;
class UEnum;
struct FNumericProperty;
struct FName {
    int32_t id{}, number{};
    std::wstring to_string_no_numbers() const {
        FName value{}; SIZE_T bytes{};
        if (!ReadProcessMemory(GetCurrentProcess(), this, &value, sizeof(value), &bytes) || bytes != sizeof(value)) return L"None";
        if (value.number != 0) return L"None";
        switch (value.id) {
        case 1: return L"EnumProperty";
        case 2: return L"ByteProperty";
        case 3: return L"Enum";
        default: return L"None";
        }
    }
    std::wstring to_string() const {
        ++fixture::native_name_calls;
        const auto value = to_string_no_numbers();
        if (value == L"None") ++fixture::unsafe_name_calls;
        return value;
    }
};
class FFieldClass {
public:
    FName& get_name() const { return *reinterpret_cast<FName*>(const_cast<FFieldClass*>(this)); }
};
class FField {
public:
    FFieldClass* get_class() const { return *reinterpret_cast<FFieldClass* const*>(reinterpret_cast<const char*>(this) + 8); }
    FField* get_next() const { return *reinterpret_cast<FField* const*>(reinterpret_cast<const char*>(this) + 0x20); }
};
class FProperty : public FField {
protected:
    static inline uint32_t s_offset_offset{0x54};
};
class UObjectBase {
public:
    static uint32_t get_internal_index_offset() { return 0x0c; }
    static uint32_t get_class_private_offset() { return 0x10; }
    UClass* get_class() const {
        ++fixture::object_class_calls;
        return *reinterpret_cast<UClass* const*>(reinterpret_cast<const char*>(this) + 0x10);
    }
    FName& get_fname() const { return *reinterpret_cast<FName*>(reinterpret_cast<char*>(const_cast<UObjectBase*>(this)) + 0x18); }
};
class UClass : public UObjectBase {};
class UScriptStruct : public UObjectBase {
public:
    FField* get_child_properties() const { return *reinterpret_cast<FField* const*>(reinterpret_cast<const char*>(this) + 0x48); }
};
class UEnum : public UObjectBase {
public:
    static UClass* static_class();
};
struct FUObjectItem { UObjectBase* object{}; };
struct FUObjectArray {
    std::vector<FUObjectItem> items;
    static FUObjectArray* get();
    int32_t get_object_count() { return static_cast<int32_t>(items.size()); }
    FUObjectItem* get_object(int32_t index) { return index >= 0 && index < get_object_count() ? &items[index] : nullptr; }
};
inline FUObjectArray* test_objects{};
inline UScriptStruct* test_repmovement{};
inline UClass* test_enum_class{};
inline FUObjectArray* FUObjectArray::get() { return test_objects; }
template <typename T> T* find_uobject(const std::wstring&) { return reinterpret_cast<T*>(test_repmovement); }
}
