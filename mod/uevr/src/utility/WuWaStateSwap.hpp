#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wuwa_stereo {

// Diagnostic only. Exchanges the view-state pointers of the two main views
// (FSceneView +0x8, copied from init+0x100 by the verified constructor) for one
// NSF pair and puts them back afterwards. Nothing is copied between the states
// and no state is shared: each view renders the pair with the other eye's own
// state object, so history, caches and any per-state registration move with
// it. Callers verify the build, family, pass fields and addresses, and limit the
// test in time. Windows-free so the policy can be tested off-target.
class StateSwapTransaction {
public:
    using Read = bool (*)(uintptr_t, uintptr_t&);
    using Write = bool (*)(uintptr_t, uintptr_t);
    enum class Result { skipped, applied, write_failed };
    // exchange: each view gets the other's state (E3). The share modes write one
    // field only, so both views reference one state for the pair: first_for_both
    // gives the second view the first view's state, second_for_both the reverse.
    enum class Mode { exchange, first_for_both, second_for_both };

    StateSwapTransaction(uintptr_t first_field, uintptr_t second_field, Read read, Write write,
        Mode mode = Mode::exchange)
        : fields_{first_field, second_field}, read_{read}, write_{write}, mode_{mode} {
        const auto distance = first_field > second_field ? first_field - second_field : second_field - first_field;
        if (!first_field || !second_field || distance < sizeof(uintptr_t) ||
            !read_(first_field, originals_[0]) || !read_(second_field, originals_[1]) ||
            !originals_[0] || !originals_[1] || originals_[0] == originals_[1]) return;
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (!writes(i)) continue;
            touched_[i] = true; // a failing write may still have changed the field
            uintptr_t check{};
            if (!write_(fields_[i], written(i)) || !read_(fields_[i], check) || check != written(i)) {
                result_ = Result::write_failed;
                restore();
                return;
            }
        }
        result_ = Result::applied;
    }
    ~StateSwapTransaction() { restore(); }
    StateSwapTransaction(const StateSwapTransaction&) = delete;
    StateSwapTransaction& operator=(const StateSwapTransaction&) = delete;

    Result result() const { return result_; }
    bool applied() const { return result_ == Result::applied; }
    uintptr_t original(std::size_t i) const { return i < originals_.size() ? originals_[i] : 0; }

    // Puts each original back only where the field still holds the value this
    // transaction wrote, or already holds the original. A different value means
    // something else changed the field: it is left alone and reported.
    bool restore() {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (!touched_[i]) continue;
            touched_[i] = false;
            uintptr_t value{};
            const bool ok = read_(fields_[i], value) &&
                (value == originals_[i] ||
                 (value == written(i) && write_(fields_[i], originals_[i]) &&
                  read_(fields_[i], value) && value == originals_[i]));
            restored_ = ok && restored_;
        }
        return restored_;
    }

private:
    bool writes(std::size_t i) const {
        return mode_ == Mode::exchange || (mode_ == Mode::first_for_both ? i == 1 : i == 0);
    }
    uintptr_t written(std::size_t i) const {
        return mode_ == Mode::exchange ? originals_[1 - i] : originals_[mode_ == Mode::first_for_both ? 0 : 1];
    }
    std::array<uintptr_t, 2> fields_{}, originals_{};
    std::array<bool, 2> touched_{};
    Read read_{};
    Write write_{};
    Mode mode_{Mode::exchange};
    Result result_{Result::skipped};
    bool restored_{true};
};
} // namespace wuwa_stereo
