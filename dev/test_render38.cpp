// The reviewed 3.8 rendering routes (WuWaRender38.hpp) against the 3.7 and 3.8 mapped images, plus refusal
// cases; the god-ray patterns are read from WuWaGodRay.hpp itself so they cannot drift from what ships.
// The captures stay outside the repo (data/game-3.7-6ace6377, data/game-3.8). They prove the table was
// transcribed from real code; only the simulator log proves the hooks run (docs/RENDER-3.8-PORT.md).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include "../mod/uevr/src/utility/WuWaRender38.hpp"

namespace r38 = wuwa_render_38;
using Bytes = std::vector<uint8_t>;

static bool load(const char* path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

// "40 57 ? ..." -> count of matches in the image (stops at 3).
static int matches(const Bytes& image, const std::string& pattern, size_t& first) {
    std::vector<int> want;
    std::istringstream tokens(pattern);
    for (std::string t; tokens >> t;) want.push_back(t == "?" || t == "??" ? -1 : std::stoi(t, nullptr, 16));
    int hits = 0;
    for (size_t i = 0; i + want.size() <= image.size() && hits < 3; ++i) {
        if (image[i] != want[0]) continue;
        size_t k = 1;
        while (k < want.size() && (want[k] < 0 || image[i + k] == want[k])) ++k;
        if (k == want.size()) { if (!hits) first = i; ++hits; }
    }
    return hits;
}

int main() {
    int failed = 0;
    const auto check = [&](bool ok, const char* name) {
        if (!ok) { std::printf("FAIL %s\n", name); ++failed; }
    };
    const auto unreadable = [](uint32_t, std::span<uint8_t>) { return false; };
    const auto& first = r38::ranges.front();
    check(!r38::check(0x6ace6377, first.reference, first.size, first.reference_hash, unreadable).has_value(),
        "3.7 must keep its own checker");
    check(!r38::check(0x6ae17e04, first.reference, first.size, first.reference_hash, unreadable).has_value(),
        "another timestamp must not inherit 3.8 approval");
    check(r38::check(r38::timestamp, first.reference, first.size, first.reference_hash, unreadable) == false,
        "unreadable code must refuse");
    check(r38::check(r38::timestamp, first.reference, first.size + 1, first.reference_hash, unreadable) == false,
        "a listed range with another size must refuse");
    check(!r38::check(r38::timestamp, 0x3d86450, 0x205a, 0x057f3ffc165d34d6ULL, unreadable).has_value(),
        "an unlisted range (reflection capture) keeps the generic rule");
    check(r38::address(r38::timestamp, 0x526f1a0 + 0x299) == 0x5014d50u + 0x299, "mid-hook keeps its distance");
    check(!r38::address(0x6ace6377, 0x526f1a0).has_value(), "3.7 addresses come from the self-port");

    Bytes old_image, image;
    if (!load("../data/game-3.7-6ace6377/client-6ace6377-kuro.bin", old_image) ||
        !load("../data/game-3.8/client-6ae17e03-kuro.bin", image)) {
        std::printf("SKIP captures (not available); %d failures\n", failed);
        return failed ? 1 : 0;
    }
    const auto read = [&](uint32_t rva, std::span<uint8_t> out) {
        if (uint64_t(rva) + out.size() > image.size()) return false;
        std::memcpy(out.data(), image.data() + rva, out.size()); return true;
    };
    for (const auto& range : r38::ranges) {
        check(r38::fingerprint({old_image.data() + range.reference, range.size}) == range.reference_hash,
            "reference hash must match the 3.7 capture");
        const auto verify = [&](uint64_t reference_hash) {
            return r38::check(r38::timestamp, range.reference, range.size, reference_hash, read);
        };
        check(verify(range.reference_hash) == true, "reviewed range must match the 3.8 capture");
        check(verify(range.reference_hash ^ 1) == false, "changed source contract must refuse");
        image[range.live + range.size - 1] ^= 1;
        check(verify(range.reference_hash) == false, "changed code byte must refuse");
        image[range.live + range.size - 1] ^= 1;
    }
    // The mono mid-hook's instruction bytes are the same in both builds (up to the RIP load at +0x2b3).
    check(std::memcmp(old_image.data() + 0x526f1a0 + 0x299, image.data() + 0x5014d50 + 0x299, 0x1a) == 0,
        "mono authored camera mid-hook bytes must match 3.7");
    // Far lighting: the view state's volume address code in both builds.
    const uint8_t clv37[] = {0x48, 0x8b, 0xb6, 0x88, 0x1e, 0x00, 0x00, 0x48, 0x81, 0xc6, 0xa8, 0x27, 0x00, 0x00};
    check(std::memcmp(old_image.data() + 0x3eaf36c, clv37, sizeof(clv37)) == 0, "3.7 far-lighting site");
    check(std::memcmp(image.data() + r38::clv_site, r38::clv_code.data(), r38::clv_code.size()) == 0,
        "3.8 far-lighting site");
    // God rays: each shipped pattern finds exactly one place in its own build (the 3.7 capture was taken
    // with the hook installed, so only its body after the patched prologue is compared there).
    std::ifstream header("mod/uevr/src/utility/WuWaGodRay.hpp");
    std::string text{std::istreambuf_iterator<char>(header), {}};
    const auto start = text.find("pass_patterns{{");
    const auto stop = text.find("}};", start);
    check(start != std::string::npos && stop != std::string::npos, "god-ray patterns present");
    std::vector<std::string> patterns;
    if (start != std::string::npos && stop != std::string::npos) {
        const std::string block = text.substr(start, stop - start);
        const std::regex entry(R"re(\{((?:\s*"[^"]*")+),\s*0x[0-9a-f]+\})re");
        for (std::sregex_iterator it(block.begin(), block.end(), entry), end; it != end; ++it) {
            std::string joined;
            const std::regex piece(R"re("([^"]*)")re");
            const std::string body = (*it)[1];
            for (std::sregex_iterator p(body.begin(), body.end(), piece), e; p != e; ++p) joined += (*p)[1];
            patterns.push_back(joined);
        }
    }
    check(patterns.size() == 2, "two god-ray patterns");
    if (patterns.size() == 2) {
        size_t at{};
        check(matches(image, patterns[1], at) == 1 && at == 0x37abe30, "3.8 god-ray pattern: one hit at 0x37abe30");
        check(matches(image, patterns[0], at) == 0, "3.7 god-ray pattern must not match 3.8");
        check(matches(old_image, patterns[1], at) == 0, "3.8 god-ray pattern must not match 3.7");
        check(matches(old_image, patterns[0].substr(6 * 3), at) == 1 && at == 0x3aa3426,
            "3.7 god-ray pattern body (after the hooked prologue) at 0x3aa3426");
    }
    std::printf("3.8 render: %zu ranges, far-lighting sites, god-ray patterns; %d failures\n", r38::ranges.size(), failed);
    return failed ? 1 : 0;
}
