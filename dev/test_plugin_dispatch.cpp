#include "../mod/uevr/src/utility/WuWaPluginDispatch.hpp"
#include <cassert>
#include <future>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>

using wuwa_plugin::DispatchGate;

int main() {
    DispatchGate gate;
    std::shared_mutex callbacks;
    int calls = 0;
    const auto dispatch = [&] {
        const auto ticket = gate.ticket();
        if (!gate.permits(ticket)) return;
        std::shared_lock lock{callbacks};
        if (gate.permits(ticket)) ++calls;
    };
    dispatch();
    assert(calls == 1);
    const auto old_ticket = gate.ticket();
    {
        DispatchGate::Unload unload{gate};
        assert(unload && !gate.permits(old_ticket));
        std::unique_lock lock{callbacks};
        dispatch(); // Same-thread DLL teardown cannot wait on its own mutex.
        auto foreign_teardown = std::async(std::launch::async, dispatch);
        assert(foreign_teardown.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        foreign_teardown.get(); // Unload can safely join a dispatching worker.
        DispatchGate::Unload nested{gate};
        assert(!nested && calls == 1);
    }
    assert(!gate.permits(old_ticket)); // Even after unload completes.
    dispatch();
    assert(calls == 2);

    // Deterministically pause an already-entered dispatch before its shared
    // lock, finish unload, then check that replacement callbacks stay untouched.
    std::promise<void> entered, continue_dispatch;
    auto resume = continue_dispatch.get_future();
    auto queued = std::async(std::launch::async, [&] {
        const auto ticket = gate.ticket();
        assert(gate.permits(ticket));
        entered.set_value();
        resume.wait();
        std::shared_lock lock{callbacks};
        if (gate.permits(ticket)) ++calls;
    });
    entered.get_future().wait();
    { DispatchGate::Unload unload{gate}; assert(unload); std::unique_lock lock{callbacks}; }
    continue_dispatch.set_value();
    queued.get();
    assert(calls == 2);
    try { DispatchGate::Unload unload{gate}; assert(unload); throw std::runtime_error("fixture unload failure"); }
    catch (const std::runtime_error&) { }
    dispatch();
    assert(calls == 3); // Exception unwinding restores ordinary dispatch.
    std::cout << "PASS: teardown dispatch, joined worker, nested unload, stale queued event and exception cleanup\n";
}
