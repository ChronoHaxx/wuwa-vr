#pragma once
#include <initializer_list>
#include <utility>
#include <sol/sol.hpp>

namespace wuwa_lua {
// The pinned sol2 variadic new_enum expands a template for every name/value
// pair. Populate homogeneous entries in a loop, then retain that overload's
// exact read-only wrapper (including __pairs, absent from its list overload).
inline sol::table read_only_enum(sol::table& parent, const sol::string_view& name,
    std::initializer_list<std::pair<sol::string_view, int>> entries) {
    auto target = parent.create(0, static_cast<int>(entries.size()));
    for (const auto& entry : entries) target.set(entry.first, entry.second);
    auto metatable = parent.create_with(
        sol::meta_function::new_index, sol::detail::fail_on_newindex,
        sol::meta_function::index, target,
        sol::meta_function::pairs, sol::stack::stack_detail::readonly_pairs);
    return parent.create_named(name, sol::metatable_key, metatable);
}
}
