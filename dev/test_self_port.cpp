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
#include "../mod/uevr/src/utility/WuWaLgui38.hpp"
#include "../mod/uevr/src/utility/WuWaRender38.hpp"

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

// The 3.8 image moved up by `shift` bytes: every rel32 distance stays valid and va_base moves down by
// the same amount, so absolute pointers stay valid too. A stand-in for another 3.8 build in which all
// reviewed code moved (the 6ae17bd7 player client is not captured).
struct Shifted final : port::Memory {
    const Capture& inner;
    uint32_t shift;
    Shifted(const Capture& c, uint32_t s) : inner{c}, shift{s} {
        image_size = c.image_size + s; va_base = c.va_base - s;
        for (const auto& x : c.code) code.push_back({x.begin + s, x.end + s});
        for (const auto& x : c.data) data.push_back({x.begin + s, x.end + s});
    }
    bool copy(uint32_t rva, void* out, size_t size) const override {
        return rva >= shift && inner.copy(rva - shift, out, size);
    }
};

static wuwa_build_38::Build sibling(const port::Memory& m, const port::Result& r) {
    wuwa_build_38::Build b{0x6ae17bd7};
    b.map = [&r](uint32_t rva38) -> std::optional<uint32_t> {
        const auto it = r.sites.find(rva38);
        return it == r.sites.end() ? std::nullopt : std::optional<uint32_t>{it->second.live};
    };
    b.unchanged = [&m, &r](uint32_t rva38, uint32_t size, uint64_t hash38) {
        return port::range_unchanged(m, r, rva38, size, hash38, port::reviewed38_table);
    };
    return b;
}

// The reviewed 3.8 tables reached through the 3.8 self-port table (WuWaSelfPortTable38.hpp).
static void reviewed38(const fs::path& data) {
    const auto path38 = data / "game-3.8/client-6ae17e03-kuro.bin";
    const auto path37 = data / "game-3.7-6ace6377/client-6ace6377-kuro.bin";
    CHECK(port::reviewed38_table.reference_timestamp == wuwa_build_38::timestamp, "3.8 table is not from the reviewed build");
    {   // A Build with no scan is the exact-build rule as before.
        const wuwa_build_38::Build exact{wuwa_build_38::timestamp}, other{0x6ae17bd7};
        CHECK(exact.reviewed() && exact.family() && !exact.sibling(), "reviewed build not recognised");
        CHECK(!other.family() && !wuwa_lgui_38::address(other, 0x410bfc0), "an unscanned build inherited 3.8");
        CHECK(wuwa_lgui_38::address(exact, 0x7feada8) == wuwa_lgui_38::menu_vtable, "menu vtable on the reviewed build");
    }
    Capture c38;
    if (!load(path38, c38)) { std::printf("3.8: capture not found, skipped\n"); return; }
    const size_t sites = std::size(port::table38::sites), ranges = std::size(port::table38::ranges);
    {   // On the reviewed build itself (the forced-scan case): everything maps to itself.
        const auto r = port::locate(c38, port::reviewed38_table);
        size_t moved = 0;
        for (const auto& [ref, m] : r.sites) moved += m.live != ref;
        CHECK(r.sites.size() == sites && moved == 0, "3.8 on itself: %zu/%zu sites, %zu moved", r.sites.size(), sites, moved);
        CHECK(r.ranges_ok == ranges, "3.8 on itself: %u/%zu ranges", r.ranges_ok, ranges);
    }
    {   // Moved: every site at +shift, every reviewed range accepted there, and a changed byte refused.
        constexpr uint32_t shift = 0x1000;
        Shifted moved{c38, shift};
        const auto t0 = GetTickCount64();
        const auto r = port::locate(moved, port::reviewed38_table);
        const auto ms = GetTickCount64() - t0;
        size_t off = 0;
        for (const auto& [ref, m] : r.sites) off += m.live != ref + shift;
        CHECK(r.sites.size() == sites && off == 0, "3.8 moved: %zu/%zu sites, %zu not at +shift", r.sites.size(), sites, off);
        CHECK(r.ranges_ok == ranges, "3.8 moved: %u/%zu ranges", r.ranges_ok, ranges);
        const auto b = sibling(moved, r);
        CHECK(b.sibling() && b.family() && !b.reviewed(), "moved build is not a 3.8 sibling");
        const auto no_read = [](uint32_t, std::span<uint8_t>) { return false; };
        size_t ui_ok = 0, render_ok = 0;
        for (const auto& x : wuwa_lgui_38::ranges) {
            ui_ok += wuwa_lgui_38::check(b, x.reference, x.size, x.reference_hash, no_read) == true;
            CHECK(wuwa_lgui_38::address(b, x.reference) == x.live + shift, "UI %x not at +shift", x.reference);
        }
        for (const auto& x : wuwa_render_38::ranges) {
            render_ok += wuwa_render_38::check(b, x.reference, x.size, x.reference_hash, no_read) == true;
            CHECK(wuwa_render_38::address(b, x.reference + 1) == x.live + shift + 1, "render %x+1 not at +shift", x.reference);
        }
        CHECK(ui_ok == wuwa_lgui_38::ranges.size(), "UI ranges on the moved build: %zu/%zu", ui_ok, wuwa_lgui_38::ranges.size());
        CHECK(render_ok == wuwa_render_38::ranges.size(), "render ranges on the moved build: %zu/%zu", render_ok, wuwa_render_38::ranges.size());
        CHECK(wuwa_lgui_38::check(b, wuwa_lgui_38::ranges[0].reference, wuwa_lgui_38::ranges[0].size,
            wuwa_lgui_38::ranges[0].reference_hash ^ 1, no_read) == false, "a changed source contract was accepted");
        CHECK(wuwa_lgui_38::address(b, 0x7feada8) == wuwa_lgui_38::menu_vtable + shift, "menu vtable not at +shift");
        CHECK(&wuwa_lgui_38::layout(b) == &wuwa_lgui_38::reviewed_layout, "a 3.8 sibling must use the 3.8 layout");
        const auto clv = b.at(wuwa_render_38::clv_site);
        std::array<uint8_t, wuwa_render_38::clv_code.size()> bytes{};
        CHECK(clv == wuwa_render_38::clv_site + shift && moved.copy(*clv, bytes.data(), bytes.size()) &&
            bytes == wuwa_render_38::clv_code, "far-lighting site not found at +shift");
        // One compared byte changed inside a reviewed UI range: that range alone is refused.
        Capture changed = c38;
        const auto& first = wuwa_lgui_38::ranges[0];
        const auto& tr = *std::find_if(port::table38::ranges, std::end(port::table38::ranges),
            [&](const auto& x) { return x.rva == first.live && x.size == first.size; });
        std::vector<bool> masked(first.size);
        for (uint16_t i = 0; i < tr.fields; ++i)
            for (uint8_t k = 0; k < port::table38::fields[tr.first_field + i].length; ++k)
                masked[port::table38::fields[tr.first_field + i].offset + k] = true;
        uint32_t at = first.size - 1;
        while (at && masked[at]) --at;
        changed.bytes[first.live + at] ^= 0x01;
        Shifted moved_changed{changed, shift};
        const auto rc = port::locate(moved_changed, port::reviewed38_table);
        const auto bc = sibling(moved_changed, rc);
        CHECK(wuwa_lgui_38::check(bc, first.reference, first.size, first.reference_hash, no_read) == false,
            "a changed byte in a reviewed range was accepted");
        CHECK(wuwa_lgui_38::check(bc, wuwa_lgui_38::ranges[1].reference, wuwa_lgui_38::ranges[1].size,
            wuwa_lgui_38::ranges[1].reference_hash, no_read) == true, "one changed range refused its neighbours");
        std::printf("3.8 table: %zu sites, %zu ranges; moved +0x%x: %zu/%zu sites, %u/%zu ranges, UI %zu/%zu, render %zu/%zu, %llu ms\n",
            sites, ranges, shift, r.sites.size(), sites, r.ranges_ok, ranges, ui_ok, wuwa_lgui_38::ranges.size(), render_ok,
            wuwa_render_38::ranges.size(), (unsigned long long)ms);
    }
    Capture c37;
    if (load(path37, c37)) { // 3.7 must not pass for 3.8 (the plugin requires 90% of ranges unchanged).
        const auto r = port::locate(c37, port::reviewed38_table);
        CHECK(size_t(r.ranges_ok) * 10 < ranges * 9, "3.7 passes the 3.8 check: %u/%zu ranges", r.ranges_ok, ranges);
        std::printf("3.8 table on 3.7 0x%x: %zu/%zu sites, %u/%zu ranges unchanged (needs 90%% to count as 3.8)\n",
            c37.timestamp, r.sites.size(), sites, r.ranges_ok, ranges);
    }
}

int main() {
    synthetic();
    reviewed38(fs::path{"../data"});
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
