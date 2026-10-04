// Standalone Lua only: no UEVR initialization, game, window or runtime.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <imgui.h>
#include "../mod/uevr/src/utility/WuWaLuaEnum.hpp"

int main() {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string);
    auto parent = lua.create_table();
    lua["original"] = parent.new_enum("Original", "Zero", 0, "Negative", -7, "Flag", 1 << 14);
    lua["bounded"] = wuwa_lua::read_only_enum(parent, "Bounded",
        {{"Zero", 0}, {"Negative", -7}, {"Flag", 1 << 14}});
    // The fixture is generated from the unchanged upstream registration. The
    // preparation check compares every name/value token with the source patch.
    lua["keys"] = wuwa_lua::read_only_enum(parent, "ImGuiKey", {
#include "key-enum-fixture.inc"
    });
    const std::pair<sol::string_view, int> expected_entries[] = {
#include "key-enum-fixture.inc"
    };
    auto expected = lua.create_table();
    for (const auto& entry : expected_entries) expected.set(entry.first, entry.second);
    lua["expected"] = expected;
    lua["key_count"] = sizeof(expected_entries) / sizeof(expected_entries[0]);
    const auto result = lua.safe_script(R"(
        local function collect(t)
            local out, count = {}, 0
            for key, value in pairs(t) do out[key] = value; count = count + 1 end
            return out, count
        end
        local a, na = collect(original)
        local b, nb = collect(bounded)
        assert(na == 3 and nb == na)
        for key, value in pairs(a) do assert(b[key] == value and bounded[key] == original[key]) end
        assert(original.Missing == nil and bounded.Missing == nil)
        local function mutation_error(t, key)
            local ok, err = pcall(function() t[key] = 123 end)
            assert(not ok)
            local message = err:match(': ([^:]+)$')
            assert(message == 'cannot modify the elements of an enumeration table')
            return message
        end
        assert(mutation_error(original, 'Zero') == mutation_error(bounded, 'Zero'))
        assert(mutation_error(original, 'NewValue') == mutation_error(bounded, 'NewValue'))
        local actual, count = collect(keys)
        assert(count == key_count and count == 143)
        for key, value in pairs(expected) do
            assert(keys[key] == value and actual[key] == value)
            assert(type(keys[key]) == 'number')
            mutation_error(keys, key)
        end
        mutation_error(keys, 'NonexistentKey')
        assert(keys.NonexistentKey == nil)
    )", sol::script_pass_on_error);
    if (!result.valid()) {
        const sol::error error = result;
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "PASS 143 exact enum values, numeric types, missing lookup, pairs and readonly existing/new-key errors; original overload parity\n";
}
