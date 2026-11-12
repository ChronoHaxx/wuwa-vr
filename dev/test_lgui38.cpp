// The reviewed UI port against the actual 3.8 mapped image, plus refusal cases.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "../mod/uevr/src/utility/WuWaLgui38.hpp"

namespace ui = wuwa_lgui_38;
int main() {
    int failed = 0;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { std::printf("FAIL %s\n", name); ++failed; }
    };
    const auto& first = ui::ranges.front();
    const auto unreadable = [](uint32_t, std::span<uint8_t>) { return false; };
    check(!ui::check(0x6ace6377, first.reference, first.size, first.reference_hash, unreadable),
        "3.7 must retain the original checker");
    check(!ui::check(0x6ae17e04, first.reference, first.size, first.reference_hash, unreadable),
        "another timestamp must not inherit 3.8 approval");
    check(ui::check(ui::timestamp, first.reference, first.size, first.reference_hash, unreadable) == false,
        "unreadable code must refuse");
    check(ui::check(ui::timestamp, 0x4116a70, 0x41, 0, unreadable) == false,
        "unreviewed world-label route must refuse");
    check(ui::layout(0x6ace6377).depth_flag == 0x25ac && ui::layout(ui::timestamp).depth_flag == 0x25f4,
        "layout must be selected by the exact build");
    check(ui::address(ui::timestamp, 0x410bfc0) == 0x3e65620,
        "menu must resolve to the thunk for its own execution function");
    const std::filesystem::path path{"../data/game-3.8/client-6ae17e03-kuro.bin"};
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::printf("SKIP real capture (not available); %d failures\n", failed); return failed ? 1 : 0; }
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    const auto read = [&](uint32_t rva, std::span<uint8_t> out) {
        if (uint64_t(rva) + out.size() > bytes.size()) return false;
        std::memcpy(out.data(), bytes.data() + rva, out.size()); return true;
    };
    for (const auto& range : ui::ranges) {
        const auto verify = [&](uint64_t reference_hash) {
            return ui::check(ui::timestamp, range.reference, range.size, reference_hash, read);
        };
        check(verify(range.reference_hash) == true, "reviewed range must match the capture");
        check(verify(range.reference_hash ^ 1) == false, "changed source contract must refuse");
        bytes[range.live + range.size - 1] ^= 1;
        check(verify(range.reference_hash) == false, "changed code byte must refuse");
        bytes[range.live + range.size - 1] ^= 1;
    }
    uint64_t thunk{};
    std::memcpy(&thunk, bytes.data() + 0x7d3a3b8 + 8, sizeof(thunk));
    check(thunk == 0x140000000ULL + 0x3e65620, "menu vtable must select the reviewed thunk");
    int32_t jump{};
    std::memcpy(&jump, bytes.data() + 0x3e65628, sizeof(jump));
    check(0x3e6562cLL + jump == 0x3e59e00, "menu thunk must jump to the reviewed body");
    std::printf("3.8 UI: %zu ranges, corruption/refusal and thunk checks; %d failures\n", ui::ranges.size(), failed);
    return failed ? 1 : 0;
}
