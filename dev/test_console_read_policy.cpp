#include "../mod/uevr/src/utility/WuWaConsoleReadPolicy.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

using namespace wuwa_console_read;
namespace {
int checks{};
void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
    ++checks;
}
bool contains(std::string_view name) {
    for (const auto entry : snapshot_names) if (entry == name) return true;
    return false;
}
}
int main() {
    check(valid_name("r.ShadowQuality", Access::Read), "renderer read rejected");
    check(valid_name("r.ShadowQuality", Access::Write), "existing renderer write rejected");
    check(valid_name("sg.ShadowQuality", Access::Read), "scalability group read rejected");
    check(!valid_name("sg.ShadowQuality", Access::Write), "read expansion enabled group writes");
    for (const auto name : {"", "r.", "sg.", "foo", "t.MaxFPS", "vr.RoundRobinOcclusion",
        "r.ShadowQuality 0", "sg.ShadowQuality=3", "r.ShadowQuality;quit",
        "r.ShadowQuality\nquit", "r.ShadowQuality\rquit", "r.ShadowQuality\t0",
        "r.ShadowQuality|quit", "r.ShadowQuality#x", "r.ShadowQuality/../x", R"(r."x")"}) {
        check(!valid_name(name, Access::Read), "invalid read name/injection accepted");
        check(!valid_name(name, Access::Write), "invalid write name/injection accepted");
    }
    const std::string embedded_null{"r.ShadowQuality\0quit", 20};
    check(!valid_name(embedded_null, Access::Read), "embedded NUL truncated into a different lookup");
    check(valid_name("r." + std::string(94, 'a'), Access::Read), "96-character boundary rejected");
    check(!valid_name("r." + std::string(95, 'a'), Access::Read), "unbounded variable name accepted");
    for (unsigned command = 0; command <= 1; ++command)
        for (unsigned integer = 0; integer <= 1; ++integer)
            for (unsigned number = 0; number <= 1; ++number)
                check(numeric_accessors_available(command, integer, number) ==
                    (command && integer && number), "incomplete SDK discovery accepted as numeric zero");
    check(cached_access(false, true, 4, 7, 8) == CachedAccess::Busy,
        "occupied SDK cache allowed a potentially blocking snapshot read");
    check(cached_access(true, false, 4, 7, 8) == CachedAccess::NotCached,
        "cold cache entry allowed discovery instead of reporting unavailable");
    check(cached_access(true, true, 4, 7, 8) == CachedAccess::Ready,
        "complete cached accessors rejected; numeric values including zero should remain readable");
    for (unsigned command = 0; command <= 1; ++command)
        for (unsigned integer = 0; integer <= 1; ++integer)
            for (unsigned number = 0; number <= 1; ++number) {
                const auto expected = command && integer && number ? CachedAccess::Ready : CachedAccess::Incomplete;
                check(cached_access(true, true, command, integer, number) == expected,
                    "partially populated SDK cache bypassed numeric accessor validation");
                check(cached_access(false, false, command, integer, number) == CachedAccess::Busy,
                    "cache metadata inspected without owning its mutex");
            }
    check(cached_access_error(CachedAccess::Ready) == nullptr, "successful cached read gained an error");
    check(std::string_view{cached_access_error(CachedAccess::NotCached)} == "sdk_numeric_accessors_not_cached",
        "cold cache did not report distinct unavailable reason");
    check(std::string_view{cached_access_error(CachedAccess::Incomplete)} == "sdk_numeric_accessors_unavailable",
        "incomplete cached getter layout could be confused with a numeric zero");
    check(std::string_view{cached_access_error(CachedAccess::Busy)} == "sdk_numeric_accessor_cache_busy",
        "busy cache did not report a retryable reason");
    check(snapshot_names.size() <= 48, "snapshot exceeded bounded lookup budget");
    for (std::size_t i = 0; i < snapshot_names.size(); ++i) {
        check(valid_name(snapshot_names[i], Access::Read), "snapshot contains rejected/non-variable name");
        bool unique = true;
        for (std::size_t j = 0; j < i; ++j) unique = unique && snapshot_names[i] != snapshot_names[j];
        check(unique, "duplicate snapshot lookup");
        if (snapshot_names[i].substr(0, 3) == "sg.")
            check(!valid_name(snapshot_names[i], Access::Write), "snapshot group gained write permission");
    }
    for (const auto name : {"sg.ResolutionQuality", "sg.ViewDistanceQuality", "sg.AntiAliasingQuality",
        "sg.PostProcessQuality", "sg.ShadowQuality", "sg.TextureQuality", "sg.EffectsQuality",
        "sg.FoliageQuality", "sg.ShadingQuality", "sg.ReflectionQuality", "r.VSync",
        "r.VolumetricCloud", "sg.GlobalIlluminationQuality", "r.ReflectionMethod"})
        check(contains(name), "inherited startup setting missing from fixed snapshot");
    for (const auto name : {"r.ShadowQuality", "r.Shadow.MaxResolution", "r.Shadow.CSM.MaxCascades",
        "r.OneFrameThreadLag", "r.NGX.DLSS.Enable", "r.RayTracing.Shadows", "r.ToonRimWidthFactor",
        "r.CLV.RefreshEveryFrame"}) check(contains(name), "current diagnostic leaf missing from snapshot");
    check(!contains("r.ArbitraryCallerName"), "caller-supplied variable leaked into fixed list");
    std::cout << "PASS: " << checks << " console read policy checks (pure values; no game or Windows APIs)\n";
}
