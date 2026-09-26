#pragma once
#include <cstdint>

namespace wuwa_test {
// 0 = unchanged; 1..4 keep XInput user 0..3. Invalid saved values fail open.
// This is per-process XInput routing, never device hiding or driver mutation.
constexpr bool filter_input_slot(int selection, uint32_t index) {
    return selection >= 1 && selection <= 4 && index < 4 && index + 1 != static_cast<uint32_t>(selection);
}
}
