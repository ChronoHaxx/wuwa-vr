#pragma once
#include <atomic>
#include <cstdint>

namespace wuwa_plugin {
// An odd epoch means teardown is active. A ticket is checked before waiting
// for the callback lock and again after acquiring it: a queued old event must
// not be delivered to callbacks registered by replacement scripts/plugins.
class DispatchGate {
public:
    using Ticket = uint64_t;
    Ticket ticket() const { return m_epoch.load(std::memory_order_acquire); }
    bool permits(Ticket value) const {
        return (value & 1) == 0 && value == ticket();
    }
    class Unload {
    public:
        explicit Unload(DispatchGate& gate) {
            auto expected = gate.ticket();
            if ((expected & 1) == 0 && gate.m_epoch.compare_exchange_strong(
                    expected, expected + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
                m_gate = &gate;
            }
        }
        ~Unload() {
            if (m_gate) m_gate->m_epoch.fetch_add(1, std::memory_order_release);
        }
        Unload(const Unload&) = delete;
        Unload& operator=(const Unload&) = delete;
        explicit operator bool() const { return m_gate != nullptr; }
    private:
        DispatchGate* m_gate{};
    };
private:
    std::atomic<Ticket> m_epoch{};
};
}
