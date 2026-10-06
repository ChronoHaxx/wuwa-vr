#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "FEnumProperty.hpp"

sdk::UClass* sdk::UEnum::static_class() {
    ++fixture::enum_class_lookups;
    if (fixture::reenter_lookup) sdk::FEnumProperty::update_offsets();
    return sdk::test_enum_class;
}

using Bytes = std::array<unsigned char, 0x200>;
template <typename T> void set(Bytes& data, size_t offset, T value) { std::memcpy(data.data() + offset, &value, sizeof(value)); }
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

int main(int argc, char** argv) try {
    const std::string mode = argc > 1 ? argv[1] : "valid80";
    const bool baseline = argc > 2 && std::string(argv[2]) == "--baseline";
    Bytes prop_a{}, prop_b{}, numeric_a{}, numeric_b{}, enum_a{}, enum_b{},
        field_class{}, byte_class{}, enum_class{}, trap_class{}, other_class{}, rep{}, impostor_a{}, impostor_b{};
    auto first = reinterpret_cast<sdk::FEnumProperty*>(prop_a.data());
    auto second = reinterpret_cast<sdk::FEnumProperty*>(prop_b.data());
    auto expected_enum_a = reinterpret_cast<sdk::UEnum*>(enum_a.data());
    auto expected_enum_b = reinterpret_cast<sdk::UEnum*>(enum_b.data());
    auto expected_class = reinterpret_cast<sdk::UClass*>(enum_class.data());
    auto numeric_class = reinterpret_cast<sdk::FFieldClass*>(byte_class.data());
    set(field_class, 0, sdk::FName{1, 0}); set(byte_class, 0, sdk::FName{2, 0});
    set(enum_class, 0x18, sdk::FName{3, 0});
    // Reading this as UObject::Class then calling FName::ToString is exactly
    // the prior bad path. The substitute counts that unsafe native boundary.
    set(trap_class, 0x18, sdk::FName{0x7fffffff, 0x12345678});
    for (auto p : {&prop_a, &prop_b}) set(*p, 8, reinterpret_cast<sdk::FFieldClass*>(field_class.data()));
    set(prop_a, 0x20, reinterpret_cast<sdk::FField*>(second));
    set(rep, 0x48, reinterpret_cast<sdk::FField*>(first));
    for (auto p : {&numeric_a, &numeric_b}) {
        set(*p, 8, numeric_class);
        set(*p, 0x10, reinterpret_cast<sdk::UClass*>(trap_class.data()));
    }
    set(enum_class, 0x0c, int32_t{0});
    set(enum_a, 0x0c, int32_t{1}); set(enum_b, 0x0c, int32_t{2});
    set(enum_a, 0x10, expected_class); set(enum_b, 0x10, expected_class);
    sdk::FUObjectArray objects;
    objects.items = {{expected_class}, {expected_enum_a}, {expected_enum_b}};
    sdk::test_objects = &objects;
    sdk::test_repmovement = reinterpret_cast<sdk::UScriptStruct*>(rep.data());
    sdk::test_enum_class = expected_class;
    const size_t underlying = mode == "shifted" ? 0xa0 : 0x80;
    const size_t enum_offset = mode == "shifted" ? 0xb8 : 0x88;
    set(prop_a, underlying, reinterpret_cast<sdk::FNumericProperty*>(numeric_a.data()));
    set(prop_b, underlying, reinterpret_cast<sdk::FNumericProperty*>(numeric_b.data()));
    set(prop_a, enum_offset, expected_enum_a); set(prop_b, enum_offset, expected_enum_b);
    bool expect_enum = true, expect_underlying = true;
    void* inaccessible{};
    if (mode == "wrongclass") {
        set(enum_b, 0x10, reinterpret_cast<sdk::UClass*>(other_class.data())); expect_enum = false;
    } else if (mode == "badindex") {
        set(enum_a, 0x0c, int32_t{-1}); set(enum_b, 0x0c, int32_t{0x7fffffff}); expect_enum = false;
    } else if (mode == "wrongidentity") {
        set(impostor_a, 0x0c, int32_t{1}); set(impostor_b, 0x0c, int32_t{2});
        set(impostor_a, 0x10, expected_class); set(impostor_b, 0x10, expected_class);
        set(prop_a, enum_offset, reinterpret_cast<sdk::UEnum*>(impostor_a.data()));
        set(prop_b, enum_offset, reinterpret_cast<sdk::UEnum*>(impostor_b.data())); expect_enum = false;
    } else if (mode == "unreadable") {
        inaccessible = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
        require(inaccessible != nullptr, "Allocate inaccessible fixture page");
        set(prop_a, enum_offset, reinterpret_cast<sdk::UEnum*>(inaccessible)); expect_enum = false;
    } else if (mode == "onesided") {
        set(prop_b, enum_offset, static_cast<sdk::UEnum*>(nullptr)); expect_enum = false;
    } else if (mode == "missingclass") {
        sdk::test_enum_class = nullptr; expect_enum = false;
    } else if (mode == "unregisteredclass") {
        objects.items[0].object = reinterpret_cast<sdk::UClass*>(other_class.data()); expect_enum = false;
    } else if (mode == "missingarray") {
        sdk::test_objects = nullptr; expect_enum = false;
    } else if (mode == "missingproperty") {
        sdk::test_repmovement = nullptr; expect_enum = expect_underlying = false;
    } else if (mode == "missingsecond") {
        set(prop_a, 0x20, static_cast<sdk::FField*>(nullptr)); expect_enum = expect_underlying = false;
    } else if (mode == "nounderlying") {
        set(prop_a, underlying, static_cast<sdk::FNumericProperty*>(nullptr));
        set(prop_b, underlying, static_cast<sdk::FNumericProperty*>(nullptr)); expect_underlying = false;
    } else if (mode == "reentry") {
        fixture::reenter_lookup = true;
    } else if (mode == "beforeinit") {
        set(prop_a, 0, reinterpret_cast<void*>(uintptr_t{0x12345678}));
        expect_enum = expect_underlying = false;
    }
    if (mode != "beforeinit") sdk::FEnumProperty::update_offsets();
    if (baseline) {
        require(mode == "valid80", "Baseline is only the controlled old unsafe path");
        require(fixture::unsafe_name_calls > 0 && fixture::object_class_calls > 0,
            "Baseline must demonstrate misinterpreted FField reaching native name conversion");
        std::cout << "PASS baseline reproduced unsafe FField->UObject conversion; native calls=" << fixture::native_name_calls << '\n';
        return 0;
    }
    require(first->get_enum() == (expect_enum ? expected_enum_a : nullptr), "Enum resolution/null refusal mismatch");
    require(first->get_underlying_prop() == (expect_underlying ? reinterpret_cast<sdk::FNumericProperty*>(numeric_a.data()) : nullptr),
        "Underlying resolution/null refusal mismatch");
    require(fixture::native_name_calls == 0 && fixture::object_class_calls == 0,
        "Candidate crossed the forbidden native name/UObject-method boundary");
    if (mode == "reentry") require(fixture::enum_class_lookups == 1, "Known class lookup re-entered discovery");
    if (inaccessible) VirtualFree(inaccessible, 0, MEM_RELEASE);
    std::cout << "PASS " << mode << " native_candidate_calls=0 enum=" << expect_enum << " underlying=" << expect_underlying << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n'; return 1;
}
