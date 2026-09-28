#pragma once
#include "WuWaCodeCheck.hpp"
#include "WuWaSceneFramePolicy.hpp"
#include <atomic>
#include <mutex>

namespace wuwa_scene_frame {
namespace policy = wuwa_scene_frame_policy;
namespace memory = wuwa_lgui_probe::detail;
inline std::once_flag verification;
inline std::atomic<uintptr_t> verified_base{};
inline std::atomic<bool> attempted{}, faulted{};
inline std::atomic<uint64_t> prepared{}, rejected{}, rewound{}, completed{}, restored{}, failed{};
inline std::mutex error_mutex;
inline std::string error;

inline uintptr_t verify() noexcept {
    try {
        std::call_once(verification, [] {
            attempted=true;
            try {
                // The entry of BeginRenderingViewFamily is already hooked.
                // Verify its untouched dispatch/store and all three leaf methods.
                constexpr std::array<wuwa_code_compatibility::Range,5> ranges{{
                    {0x235d1d80,7,0x56f42c381393364eULL},
                    {0x235cdf60,7,0x9e7bd499b2e6b962ULL},
                    {0x203e8df0,4,0xcc8f62da6a2da724ULL},
                    {0x23601368,35,0x020ed1bd63e797d3ULL},
                    {0x2360138b,4,0x1b3b2f8a3f57e3edULL}}};
                verified_base=wuwa_code_check::verify("NSF shared scene frame",ranges);
            } catch (const std::exception& e) {
                const std::lock_guard lock{error_mutex};
                error=e.what(); faulted=true;
                spdlog::error("[WuWaSceneFrame] {}; generic counter write disabled",error);
            } catch (...) { faulted=true; }
        });
    } catch (...) { faulted=true; }
    return faulted.load() ? 0 : verified_base.load();
}

inline bool read(uintptr_t at, auto& value) { return memory::read(at,value); }

inline bool compare_exchange(uintptr_t at,uint32_t expected,uint32_t desired) noexcept {
    if (!at || (at & 3) || at>UINTPTR_MAX-sizeof(uint32_t)) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region))!=sizeof(region) ||
        region.State!=MEM_COMMIT || (region.Protect & PAGE_GUARD) ||
        (region.Protect & 0xff)!=PAGE_READWRITE || at<uintptr_t(region.BaseAddress) ||
        at-uintptr_t(region.BaseAddress)>region.RegionSize ||
        sizeof(uint32_t)>region.RegionSize-(at-uintptr_t(region.BaseAddress))) return false;
    __try {
        return uint32_t(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),
            LONG(desired),LONG(expected)))==expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

class Pair {
public:
    Pair(const void* family,bool requested) noexcept {
        if (!requested) return;
        if (const auto base=verify()) {
            snapshot=policy::prepare(base,uintptr_t(family),reader);
            if (snapshot) ++prepared; else ++rejected;
        }
    }
    bool ready() const { return snapshot.has_value(); }
    bool rewind() noexcept {
        if (!snapshot || pending || finished) return false;
        if (!policy::rewind(*snapshot,reader,compare_exchange)) { ++rejected; return false; }
        pending=true; ++rewound;
        return true;
    }
    void finish() noexcept {
        if (!pending) return;
        pending=false; finished=true;
        if (policy::advanced_once(*snapshot,reader)) { ++completed; return; }
        recover();
    }
    ~Pair() { if (pending) recover(); }
    Pair(const Pair&)=delete;
    Pair& operator=(const Pair&)=delete;
private:
    inline static constexpr auto reader=[](uintptr_t at,auto& value){return memory::read(at,value);};
    std::optional<policy::Pair> snapshot;
    bool pending{}, finished{};
    void recover() noexcept {
        if (policy::restore_if_unadvanced(*snapshot,reader,compare_exchange)) ++restored;
        ++failed;
        if (!faulted.exchange(true))
            spdlog::error("[WuWaSceneFrame] Second submission violated the verified frame contract; further writes disabled");
    }
};

inline nlohmann::json status() {
    const std::lock_guard lock{error_mutex};
    return {{"attempted",attempted.load()},{"verified",verified_base.load()!=0},
        {"faulted",faulted.load()},{"error",error},{"prepared",prepared.load()},
        {"rejected",rejected.load()},{"rewound",rewound.load()},{"completed",completed.load()},
        {"restored",restored.load()},{"failed",failed.load()},
        {"scope","One scene frame for both NSF eye submissions; unrelated object counter never modified"}};
}
} // namespace wuwa_scene_frame
