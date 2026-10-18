#include "../mod/uevr/src/utility/WuWaTerrainLodPolicy.hpp"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace wuwa_terrain_lod;
namespace {
int checks{}, groups{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct FakeIO {
    Sample value{1, 2.f, 2, L"2"};
    bool available{true}, ignore_writes{};
    int reads{};
    std::vector<std::wstring> writes;
    std::optional<Sample> read() { ++reads; return available ? std::optional<Sample>{value} : std::nullopt; }
    void write(const Sample& expected, const std::wstring& text) {
        check(expected.identity == value.identity && expected.value == value.value && expected.integer == value.integer,
            "write used a changed variable/value");
        writes.push_back(text);
        if (!ignore_writes) set(std::stof(text));
    }
    void set(float v) { value.value = v; value.integer = static_cast<int32_t>(v); value.text = std::to_wstring(value.integer); }
};
}
int main() {
    {
        ++groups; Policy p; FakeIO io;
        p.tick(false, true, false, 0, io);
        check(io.reads == 0 && io.writes.empty() && p.status().code == Code::Off, "off touched the variable");
        p.tick(true, false, false, 0, io);
        check(io.reads == 0 && p.status().code == Code::Waiting, "screens touched the variable");
        p.tick(true, true, true, 0, io);
        check(io.reads == 0 && p.status().code == Code::TestActive, "a graphics comparison did not defer");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        check(io.value.value == 1.f && p.status().owned && p.status().code == Code::Applied, "Full VR did not apply mode 1");
        check(p.status().original && p.status().original->value == 2.f, "the game's value was not kept");
        const auto writes = io.writes.size();
        p.tick(true, true, false, 1000, io);
        check(io.writes.size() == writes, "steady Full VR wrote again");
        p.tick(true, false, false, 2000, io);
        check(io.value.value == 2.f && !p.status().owned && p.status().code == Code::Restored, "screens did not restore the game's value");
        p.tick(true, true, false, 3000, io);
        check(io.value.value == 1.f && p.status().owned, "Full VR again did not apply");
        p.tick(false, true, false, 4000, io);
        check(io.value.value == 2.f && !p.status().owned, "turning it off did not restore");
    }
    {
        ++groups; Policy p; FakeIO io; io.set(1.f);
        p.tick(true, true, false, 0, io);
        check(io.writes.empty() && p.status().owned && p.status().code == Code::AlreadySet, "mode 1 already was rewritten");
        p.tick(true, false, false, 1000, io);
        check(io.writes.empty() && io.value.value == 1.f && p.status().code == Code::Restored, "restore of an unchanged value wrote");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        io.set(3.f); // The game sets its own value, for example after a graphics-quality change.
        p.tick(true, true, false, 1000, io);
        check(io.value.value == 1.f && p.status().original->value == 3.f && p.status().readopts == 1,
            "a new game value was not adopted and replaced");
        p.tick(true, false, false, 2000, io);
        check(io.value.value == 3.f, "the adopted game value was not restored");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        for (int i = 1; i <= static_cast<int>(readopt_limit); ++i) {
            io.set(2.f);
            p.tick(true, true, false, i * 1000, io);
            check(io.value.value == 1.f && !p.status().blocked, "a re-adoption inside the limit stopped");
        }
        io.set(2.f);
        p.tick(true, true, false, 9000, io);
        check(p.status().blocked && !p.status().owned && io.value.value == 2.f && p.status().code == Code::GameKeepsChanging,
            "a fight with the game was not stopped");
        const auto writes = io.writes.size();
        p.tick(true, true, false, 10000, io);
        check(io.writes.size() == writes, "blocked policy wrote");
        p.tick(false, true, false, 11000, io);
        p.tick(true, true, false, 12000, io);
        check(io.value.value == 1.f && p.status().owned, "toggling off and on did not retry");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        for (int i = 1; i <= 8; ++i) { // Spread over minutes: a loading screen now and then is not a fight.
            io.set(2.f);
            p.tick(true, true, false, i * 61000ull, io);
        }
        check(!p.status().blocked && io.value.value == 1.f && p.status().readopts == 8, "occasional game changes stopped the fix");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        io.set(0.f); // Changed by something else while on the screens' way out.
        p.tick(true, false, false, 1000, io);
        check(io.value.value == 0.f && !p.status().owned && p.status().code == Code::LeftChanged, "restore overwrote another change");
    }
    {
        ++groups; Policy p; FakeIO io; io.available = false;
        p.tick(true, true, false, 0, io);
        check(p.status().blocked && !p.status().owned && p.status().code == Code::Unavailable && io.writes.empty(),
            "a missing variable was written or retried");
        io.available = true;
        p.tick(true, true, false, 1000, io);
        check(io.writes.empty(), "blocked missing variable retried without a toggle");
    }
    {
        ++groups; Policy p; FakeIO io; io.ignore_writes = true;
        p.tick(true, true, false, 0, io);
        check(p.status().blocked && !p.status().owned && p.status().code == Code::ApplyFailed, "an ignored write was accepted");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        io.ignore_writes = true;
        p.tick(true, false, false, 1000, io);
        check(p.status().blocked && p.status().owned && p.status().original && p.status().code == Code::RestoreFailed,
            "a failed restore dropped the game's value");
        io.ignore_writes = false;
        p.tick(false, false, false, 2000, io);
        p.tick(true, false, false, 3000, io);
        check(io.value.value == 2.f && !p.status().owned && p.status().code == Code::Restored, "a toggle did not retry the restore");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true, true, false, 0, io);
        io.value.identity = 2;
        p.tick(true, true, false, 1000, io);
        check(p.status().blocked && !p.status().owned && p.status().code == Code::PointerChanged, "a changed variable was trusted");
    }
    std::cout << "terrain LOD policy: " << groups << " groups, " << checks << " checks passed\n";
}
