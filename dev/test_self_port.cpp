// Self-port matcher (mod/uevr/src/utility/WuWaSelfPortCore.hpp) against two real game builds.
// Needs the module captures in <workspace>/data (not in the repository); without them it only
// runs the synthetic checks. The table is generated from the 16 Oct 2026 build (0x6ace6377):
//  * on that build every site must map to itself and every range must pass;
//  * on the 3.7 build (0x6aa96d2d) every mapped site must equal the port tool's answer
//    (data/game-3.7-6ace6377/port-6ace6377.json, read in reverse) and every range must pass.
#pragma optimize("gt", on)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <windows.h>
#include <nlohmann/json.hpp>

#include "../mod/uevr/src/utility/WuWaSelfPortCore.hpp"

namespace fs = std::filesystem;
namespace port = wuwa_self_port;
static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

struct Capture final : port::Memory {
    std::vector<uint8_t> bytes;
    uint32_t timestamp{};
    bool copy(uint32_t rva, void* out, size_t size) const override {
        if (uint64_t(rva) + size > bytes.size()) return false;
        std::memcpy(out, bytes.data() + rva, size);
        return true;
    }
};

static bool load(const fs::path& path, Capture& c) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    c.bytes.assign(std::istreambuf_iterator<char>(in), {});
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(c.bytes.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(c.bytes.data() + dos->e_lfanew);
    c.timestamp = nt->FileHeader.TimeDateStamp;
    c.image_size = nt->OptionalHeader.SizeOfImage;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        const port::Span span{section->VirtualAddress, section->VirtualAddress + section->Misc.VirtualSize};
        (section->Characteristics & IMAGE_SCN_MEM_EXECUTE ? c.code : c.data).push_back(span);
    }
    c.va_base = 0x140000000;
    std::ifstream meta(path.string() + ".json");
    if (meta) c.va_base = nlohmann::json::parse(meta).at("module").at("base").get<uint64_t>();
    return true;
}

static void synthetic() {
    // A window matches wherever its compared bytes match, whatever the moved field holds.
    struct Flat final : port::Memory {
        std::vector<uint8_t> b;
        bool copy(uint32_t rva, void* out, size_t size) const override {
            if (uint64_t(rva) + size > b.size()) return false;
            std::memcpy(out, b.data() + rva, size); return true;
        }
    } flat;
    const auto& w = port::table::windows[0];
    flat.image_size = 0x10000; flat.b.assign(flat.image_size, 0xcc); flat.code = {{0, flat.image_size}};
    std::memcpy(flat.b.data() + 0x4000, port::detail::window_bytes(w), w.size);
    CHECK(port::detail::masked_equal(flat, 0x4000, w), "window does not match its own bytes");
    const auto* mask = port::detail::window_mask(w);
    for (uint32_t i = 0; i < w.size; ++i) if (!mask[i]) flat.b[0x4000 + i] ^= 0x5a;
    CHECK(port::detail::masked_equal(flat, 0x4000, w), "a moved field broke the match");
    for (uint32_t i = 0; i < w.size; ++i) if (mask[i]) { flat.b[0x4000 + i] ^= 0x5a; break; }
    CHECK(!port::detail::masked_equal(flat, 0x4000, w), "a changed compared byte still matched");
    CHECK(!port::detail::masked_equal(flat, -4, w) && !port::detail::masked_equal(flat, flat.image_size - 2, w),
        "a window outside the image matched");
    CHECK(port::table::reference_timestamp == 0x6ace6377, "table reference is not the 16 Oct build");
}

int main() {
    synthetic();
    const auto data = fs::path{"../data"};
    const auto reference = data / "game-3.7-6ace6377/client-6ace6377-kuro.bin";
    const auto older = data / "game-3.7/client-3-7-merged.bin";
    const auto report = data / "game-3.7-6ace6377/port-6ace6377.json";
    if (!fs::exists(reference) || !fs::exists(older) || !fs::exists(report)) {
        std::printf("%s (captures not found: synthetic checks only)\n", failures ? "FAIL" : "ok");
        return failures ? 1 : 0;
    }
    size_t sites = std::size(port::table::sites), ranges = std::size(port::table::ranges), unresolved = 0;
    for (const auto& s : port::table::sites) unresolved += s.kind == port::table::kind_unresolved;
    {
        Capture c; CHECK(load(reference, c), "cannot read the reference capture");
        const auto t0 = GetTickCount64();
        const auto r = port::locate(c);
        const auto ms = GetTickCount64() - t0;
        size_t moved = 0;
        for (const auto& [ref, m] : r.sites) moved += m.live != ref;
        CHECK(moved == 0, "%zu sites moved on the reference build", moved);
        CHECK(r.sites.size() == sites - unresolved, "reference build: %zu of %zu sites", r.sites.size(), sites - unresolved);
        CHECK(r.ranges_ok == ranges, "reference build: %u of %zu ranges", r.ranges_ok, ranges);
        std::printf("reference 0x%x: %zu/%zu sites, %u/%zu ranges, %llu ms\n", c.timestamp, r.sites.size(), sites,
            r.ranges_ok, ranges, (unsigned long long)ms);
    }
    {
        Capture c; CHECK(load(older, c), "cannot read the 3.7 capture");
        std::map<uint32_t, uint32_t> want; // 16 Oct RVA -> 3.7 RVA, from the port tool
        std::ifstream in(report);
        const auto parsed = nlohmann::json::parse(in); // kept: a range-for over parse(in).at() would dangle
        for (const auto& s : parsed.at("sites")) {
            const auto kind = s.at("kind").get<std::string>();
            if (s.at("status") == "ok" && (kind == "rva" || kind == "hash_range" || kind == "signature"))
                want[s.at("new").get<uint32_t>()] = s.at("old").get<uint32_t>();
        }
        const auto t0 = GetTickCount64();
        const auto r = port::locate(c);
        const auto ms = GetTickCount64() - t0;
        size_t agree = 0;
        for (const auto& [ref, m] : r.sites) {
            auto it = want.find(ref);
            if (it == want.end()) continue;
            CHECK(m.live == it->second, "0x%x: self-port 0x%x (%s), port tool 0x%x", ref, m.live, m.by, it->second);
            agree += m.live == it->second;
        }
        CHECK(r.sites.size() == sites - unresolved, "3.7 build: %zu of %zu sites", r.sites.size(), sites - unresolved);
        CHECK(r.ranges_ok == ranges && !r.ranges_changed, "3.7 build: %u ok, %u changed, %u unmapped of %zu ranges",
            r.ranges_ok, r.ranges_changed, r.ranges_unmapped, ranges);
        std::printf("3.7 0x%x: %zu/%zu sites (%zu agree with the port tool), %u/%zu ranges, %llu ms\n", c.timestamp,
            r.sites.size(), sites, agree, r.ranges_ok, ranges, (unsigned long long)ms);
    }
    if (failures) std::printf("FAIL %d checks\n", failures);
    return failures ? 1 : 0;
}
