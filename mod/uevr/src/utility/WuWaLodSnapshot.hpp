#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace wuwa_lod {
template<class T> struct Field { T value{}; bool valid{}; };
using Vec = std::array<float, 4>;
struct View {
    uintptr_t address{};
    Field<uintptr_t> state{}, family{};
    Field<uint32_t> frame{};
    Field<int32_t> pass{};
    Field<std::array<float, 16>> projection{};
    Field<std::array<float, 16>> view_matrix{};
    Field<std::array<float, 3>> fallback_origin{};
    Field<std::array<uint8_t, 24>> mode_bytes{};
};
struct Binding {
    View view{};
    uintptr_t user{};
    Field<uintptr_t> element{};
    Field<uint32_t> first_instance{}, flags{};
    std::array<Field<Vec>, 2> cuts{}, origins{};
    bool lod_branch{};
};
enum class MatrixSourceRelation : uint8_t { external, view_plus_320, view_plus_7e0 };
struct ProducerContext {
    uintptr_t parameters{}, current_matrices{}, previous_matrices{};
    Field<uint32_t> caller_rva{};
    MatrixSourceRelation source_relation{};
};
enum class SampleResult : uint8_t { accepted, throttled, overflow, busy, invalid };

// Fixed storage and a nonblocking lock per eye. A shared outer callback lock
// does not serialize producers, so key publication and throttle updates must
// be protected together. No caller can observe a half-published slot identity.
class ProducerSampler {
public:
    static constexpr size_t eye_count = 2, context_count = 8;
    static constexpr uint64_t interval_ms = 100;
    SampleResult take(size_t eye, const ProducerContext& context, uint64_t now) noexcept {
        if (eye >= eye_count || !context.caller_rva.valid) return SampleResult::invalid;
        auto& state = eyes[eye];
        if (state.lock.test_and_set(std::memory_order_acquire)) return SampleResult::busy;
        struct Unlock {
            std::atomic_flag& lock;
            ~Unlock() { lock.clear(std::memory_order_release); }
        } unlock{state.lock};
        Slot* empty{};
        for (auto& slot : state.slots) {
            if (!slot.used) { if (!empty) empty = &slot; continue; }
            if (slot.caller_rva != context.caller_rva.value || slot.relation != context.source_relation) continue;
            // A delayed callback must not reopen an earlier time interval.
            if (now < slot.last_sample || now - slot.last_sample < interval_ms) return SampleResult::throttled;
            slot.last_sample = now;
            return SampleResult::accepted;
        }
        if (!empty) return SampleResult::overflow;
        *empty = {true, context.caller_rva.value, context.source_relation, now};
        return SampleResult::accepted;
    }
    // Called by trace control under the exclusive outer callback lock. Taking
    // the inner lock also makes the standalone sampler safe to reset.
    void reset() noexcept {
        for (auto& state : eyes) {
            while (state.lock.test_and_set(std::memory_order_acquire)) {}
            state.slots = {};
            state.lock.clear(std::memory_order_release);
        }
    }
private:
    struct Slot {
        bool used{};
        uint32_t caller_rva{};
        MatrixSourceRelation relation{};
        uint64_t last_sample{};
    };
    struct EyeSlots { std::atomic_flag lock = ATOMIC_FLAG_INIT; std::array<Slot, context_count> slots{}; };
    std::array<EyeSlots, eye_count> eyes{};
};
struct Uniforms {
    View view{};
    ProducerContext producer{};
    // CPU constants at the verified view-uniform builder, not a GPU readback.
    Field<float> game_time{}, real_time{}, delta_time{}, previous_game_time{}, previous_real_time{};
    Field<uint32_t> frame_number{}, state_frame_index{};
    // Raw producer outputs. Named matrix interpretation comes from the
    // separately decoded writer, never from an assumed stock engine layout.
    Field<std::array<float, 384>> matrix_block_0_600{};
};

template<class Read, class T>
void field(Read& read, uintptr_t base, uintptr_t offset, Field<T>& out) {
    out = {};
    if (base && offset <= UINTPTR_MAX - base && sizeof(T) <= UINTPTR_MAX - (base + offset))
        out.valid = read(base + offset, out.value);
    if (!out.valid) out.value = {};
}

// The verified 24ada230 prologue leaves RBP = entry RSP - c8. Its return
// address remains at RBP+c8 at the existing 24adbd0d observation site. This
// classifies pointer relationships only; it does not name matrix semantics.
template<class Read, class Executable>
ProducerContext producer_context(uintptr_t address, uintptr_t parameters,
    uintptr_t current, uintptr_t previous, uintptr_t rbp, uintptr_t image_base,
    uint32_t image_size, Read read, Executable executable) {
    ProducerContext context{};
    context.parameters = parameters; context.current_matrices = current; context.previous_matrices = previous;
    if (address && address <= UINTPTR_MAX - 0x320 && current == address + 0x320)
        context.source_relation = MatrixSourceRelation::view_plus_320;
    else if (address && address <= UINTPTR_MAX - 0x7e0 && current == address + 0x7e0)
        context.source_relation = MatrixSourceRelation::view_plus_7e0;
    Field<uintptr_t> caller{};
    field(read, rbp, 0xc8, caller);
    if (caller.valid && image_base && image_size && image_base <= UINTPTR_MAX - image_size &&
        caller.value >= image_base && caller.value - image_base < image_size && executable(caller.value)) {
        context.caller_rva = {uint32_t(caller.value - image_base), true};
    }
    return context;
}

// Offsets require the exact client code checks in WuWaLodProbe.hpp. Projection
// is consumed by the instancing binder; +0x6ec is the temporal-origin helper's
// fallback, not a promise that a cached temporal origin equals this value.
template<class Read> View view(uintptr_t address, uint32_t frame_offset, Read read) {
    View v{}; v.address = address;
    field(read, address, 0, v.family); field(read, address, 8, v.state);
    field(read, address, 0xc90, v.pass);
    field(read, address, 0x320, v.projection);
    field(read, address, 0x3e0, v.view_matrix);
    field(read, address, 0x6ec, v.fallback_origin);
    field(read, address, 0xfea, v.mode_bytes);
    // Accept only the offset independently discovered by UEVR in this build.
    if (frame_offset == 0x64 && v.family.valid)
        field(read, v.family.value, frame_offset, v.frame);
    return v;
}

template<class Read> Binding binding(uintptr_t rbp, uintptr_t user, uint32_t frame_offset, Read read) {
    Binding b{}; b.user = user;
    if (rbp < 0x80 || rbp > UINTPTR_MAX - 0x1b0) return b;
    Field<uintptr_t> address{};
    field(read, rbp - 0x70, 0, address);
    field(read, rbp, 0x1a8, b.element);
    if (address.valid) b.view = view(address.value, frame_offset, read);
    if (!b.element.valid || !b.element.value) return b;
    field(read, b.element.value, 0x58, b.first_instance);
    field(read, b.element.value, 0x64, b.flags);
    b.lod_branch = user && b.flags.valid && (b.flags.value & 0xf0) != 0;
    // All four local vectors are initialized before the verified hook site,
    // including the no-user-data / no-LOD-range branches. Mark those separately.
    field(read, rbp - 0x10, 0, b.cuts[0]); field(read, rbp, 0, b.cuts[1]);
    field(read, rbp - 0x30, 0, b.origins[0]); field(read, rbp - 0x20, 0, b.origins[1]);
    return b;
}

// Native member metadata and 0x54e5c80 prove these offsets. That complete
// helper is code-checked before arming. Its pre-epilogue observation has
// view=r14 and parameters=rbx; StateFrameIndex is not proven initialized there.
template<class Read> Uniforms uniforms(uintptr_t address, uintptr_t parameters, uint32_t frame_offset,
                                      Read read, bool state_index_ready = true) {
    Uniforms u{}; u.view = view(address, frame_offset, read);
    field(read, parameters, 0x918, u.previous_game_time);
    field(read, parameters, 0x91c, u.previous_real_time);
    field(read, parameters, 0x948, u.game_time);
    field(read, parameters, 0x94c, u.real_time);
    field(read, parameters, 0x950, u.delta_time);
    field(read, parameters, 0x960, u.frame_number);
    if (state_index_ready) field(read, parameters, 0x968, u.state_frame_index);
    field(read, parameters, 0, u.matrix_block_0_600);
    return u;
}
} // namespace wuwa_lod
