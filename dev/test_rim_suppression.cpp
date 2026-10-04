#include "../mod/uevr/src/utility/WuWaRimSuppressionPolicy.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

using namespace wuwa_rim;
namespace {
int checks{}, groups{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct FakeIO {
    Sample value{1, .5f, 0, L"0.5"};
    bool available{true}, ignore_zero{}, ignore_restore{}, fail_after_write{};
    int reads{};
    std::vector<std::wstring> writes;
    std::optional<Sample> read() { ++reads; return available ? std::optional<Sample>{value} : std::nullopt; }
    void write(const Sample& expected, const std::wstring& text) {
        check(expected.identity == value.identity && expected.value == value.value && expected.integer == value.integer,
            "write used a changed variable/value");
        writes.push_back(text);
        if (!(text == L"0" ? ignore_zero : ignore_restore)) {
            value.value = std::stof(text); value.integer = static_cast<int32_t>(value.value); value.text = text;
        }
        if (fail_after_write) available = false;
    }
};
}
int main() {
    {
        ++groups; Policy p; FakeIO io;
        check(!p.status().requested && !p.status().owned && !p.status().blocked, "default was enabled");
        p.tick(false,true,false,io);
        check(io.reads == 0 && io.writes.empty(), "default touched the console variable");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,false,io);
        check(p.status().owned && p.status().code == Code::Suppressed && io.value.value == 0, "half rim not suppressed");
        check(p.status().original->value == .5f && p.status().original->integer == 0 &&
            p.status().original->text == L"0.5", "original sample not retained");
        for (int i=0; i<20; ++i) p.tick(true,true,false,io);
        check(io.writes.size() == 1, "steady suppression enforced writes repeatedly");
        p.tick(false,true,false,io);
        check(!p.status().owned && io.value.value == .5f && io.writes.back() == L"0.5", "disable did not restore original");
    }
    {
        ++groups; Policy p; FakeIO io; io.value = {1,0,0,L"0"};
        p.tick(true,true,false,io);
        check(p.status().code == Code::AlreadyZero && io.writes.empty(), "preexisting zero was written");
        p.tick(false,true,false,io);
        check(!p.status().owned && io.writes.empty() && io.value.value == 0, "original zero was not preserved");
    }
    {
        ++groups; Policy p; FakeIO io; io.value = {1,2.75f,2,L"2.75"};
        p.tick(true,true,false,io); p.tick(false,true,false,io);
        check(io.value.value == 2.75f && io.value.integer == 2 && io.writes.back() == L"2.75", "non-default original lost");
    }
    {
        ++groups; Policy p; FakeIO io; io.ignore_zero = true;
        p.tick(true,true,false,io);
        check(p.status().blocked && !p.status().owned && p.status().code == Code::ApplyFailed, "ignored apply reported success");
        p.tick(true,true,false,io); p.tick(false,true,false,io);
        check(io.writes.size() == 1 && io.value.value == .5f, "ignored apply fought or overwrote baseline");
        io.ignore_zero = false; p.tick(true,true,false,io);
        check(p.status().owned && io.value.value == 0, "deliberate off/on did not retry apply");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,false,io); io.ignore_restore = true; p.tick(false,true,false,io);
        check(p.status().owned && p.status().blocked && p.status().code == Code::RestoreFailed &&
            p.status().original->value == .5f, "failed restore lost original ownership");
        const auto writes = io.writes.size();
        for (int i=0; i<20; ++i) p.tick(false,true,false,io);
        check(io.writes.size() == writes, "restore failure retried every tick");
        io.ignore_restore = false;
        p.tick(true,true,false,io);
        check(!p.status().owned && io.value.value == .5f && io.writes.back() == L"0.5", "off/on did not restore first");
        p.tick(true,true,false,io);
        check(p.status().owned && p.status().original->value == .5f && io.value.value == 0,
            "off/on captured its own zero as a new baseline");
        p.tick(false,true,false,io);
        check(io.value.value == .5f, "second disable lost original after restore failure");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,false,io); io.value = {1,.8f,0,L"0.8"};
        p.tick(true,true,false,io);
        check(!p.status().owned && p.status().blocked && p.status().code == Code::ExternalChange,
            "external change did not release and block");
        p.tick(true,false,false,io); p.tick(true,true,false,io); p.tick(false,true,false,io);
        check(io.value.value == .8f && io.writes.size() == 1, "external change was overwritten");
        p.tick(true,true,false,io);
        check(p.status().original->value == .8f, "deliberate retry did not adopt user's new original");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,false,io); io.value.identity = 2;
        p.tick(false,true,false,io);
        check(p.status().blocked && p.status().code == Code::PointerChanged && !p.status().owned,
            "changed variable did not release ownership");
        check(io.writes.size() == 1, "changed pointer was written during restoration");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,false,io); p.tick(true,false,false,io); // 2D or runtime inactive.
        check(io.value.value == .5f && !p.status().owned, "ineligible transition did not restore");
        p.tick(true,false,false,io);
        check(io.writes.size() == 2 && p.status().code == Code::Waiting, "ineligible mode reapplied");
        p.tick(true,true,false,io);
        check(io.value.value == 0 && p.status().original->value == .5f, "native stereo return did not reapply");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,true,io);
        check(io.reads == 0 && !p.status().owned && p.status().code == Code::TestActive,
            "active graphics test was modified");
        p.tick(true,true,false,io);
        check(p.status().owned, "ended test did not allow requested workaround");
    }
    {
        ++groups; Policy p; FakeIO io; io.available = false;
        p.tick(true,true,false,io);
        check(p.status().blocked && p.status().code == Code::Unavailable, "missing variable reported suppression");
        io.available = true; p.tick(true,true,false,io);
        check(io.writes.empty(), "unavailable state retried without deliberate rearm");
        p.tick(false,true,false,io); p.tick(true,true,false,io);
        check(p.status().owned, "unavailable state did not rearm");
    }
    {
        ++groups; Policy p; FakeIO io; io.fail_after_write = true;
        p.tick(true,true,false,io);
        check(p.status().owned && p.status().blocked && p.status().original->value == .5f,
            "lost apply readback discarded restore obligation");
        io.available = true; io.fail_after_write = false;
        p.tick(false,true,false,io); p.tick(true,true,false,io);
        check(io.value.value == .5f && !p.status().owned, "uncertain apply did not restore before rearm");
    }
    {
        ++groups; Policy p; FakeIO io; io.value.value = std::numeric_limits<float>::infinity();
        p.tick(true,true,false,io);
        check(p.status().blocked && io.writes.empty(), "nonfinite original was used");
    }
    {
        ++groups; Policy p; FakeIO io;
        p.tick(true,true,false,io); io.value.integer = 1; // A mismatched getter is not owned zero.
        p.tick(false,true,false,io);
        check(p.status().blocked && io.writes.size() == 1, "mismatched integer getter was overwritten");
    }
    std::cout << "PASS: " << groups << " rim suppression groups, " << checks << " checks\n";
}
