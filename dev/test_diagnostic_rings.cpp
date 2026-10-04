// Verify default construction over nonzero storage and the publish/consume/reset
// contract. This catches uninitialized ready flags after the MSVC memory fix.
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include "../mod/uevr/src/utility/WuWaRawSnapshot.hpp"
#include "../mod/uevr/src/utility/WuWaMeshBindingSnapshot.hpp"

template<class T, class Check> void dirty_construct(Check check) {
    auto* storage = ::operator new(sizeof(T), std::align_val_t(alignof(T)));
    std::memset(storage, 0xa5, sizeof(T));
    auto* value = ::new(storage) T; // default initialization, not value initialization
    check(*value);
    value->~T();
    ::operator delete(storage, std::align_val_t(alignof(T)));
}

template<class Ring, class Record> void queue() {
    dirty_construct<Ring>([](Ring& ring) {
        Record out{};
        assert(!ring.truncated());
        assert(!ring.pop(out));
        Record first{}, second{};
        assert(ring.append(first));
        assert(ring.append(second));
        assert(!ring.append(first));
        assert(ring.truncated());
        assert(ring.pop(out));
        assert(ring.pop(out));
        assert(!ring.pop(out));
        ring.reset();
        assert(!ring.truncated());
        assert(!ring.pop(out));
        assert(ring.append(first));
        assert(ring.pop(out));
        assert(!ring.pop(out));
    });
}

int main() {
    queue<wuwa_eye_diff::Ring<2>, wuwa_eye_diff::Sample>();
    queue<wuwa_view_ub_trace::Ring<2>, wuwa_view_ub_trace::Record>();
    queue<wuwa_mesh_binding::Ring<2>, wuwa_mesh_binding::Record>();
    dirty_construct<wuwa_raw_snapshot::Ring<2>>([](auto& ring) {
        assert(ring.free_slots() == 2 && !ring.truncated());
        assert(!ring.consume([](const auto&) { assert(false); }));
        for (unsigned i = 0; i < 2; ++i) {
            assert(ring.emplace([i](auto& value) {
                assert(value.ordinal == 0);
                for (const auto& block : value.views) {
                    assert(block.address == 0 && block.readable == 0);
                    for (const auto word : block.words) assert(word == 0);
                    for (const auto valid : block.valid) assert(valid == 0);
                }
                value.ordinal = i + 1;
            }));
        }
        assert(!ring.emplace([](auto&) { assert(false); }));
        assert(ring.truncated());
        unsigned expected = 1;
        while (ring.consume([&](const auto& value) { assert(value.ordinal == expected++); })) {}
        assert(expected == 3);
        ring.reset();
        assert(ring.free_slots() == 2 && !ring.truncated());
        assert(!ring.consume([](const auto&) { assert(false); }));
    });
    std::cout << "PASS: diagnostic rings initialize over dirty memory, publish, overflow, consume and reset\n";
}
