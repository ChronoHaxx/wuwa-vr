#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wuwa_stereo {

// Bounded transaction for the two known pass fields in each of two views.
// Callers validate the build, family and writable addresses. No allocation,
// guessed layout, state sharing or persistent object ownership is involved.
class ScenePassTransaction {
public:
    using Read = bool (*)(uintptr_t, uint32_t&);
    using Write = bool (*)(uintptr_t, uint32_t);
    ScenePassTransaction(std::array<uintptr_t, 4> fields,
                         std::array<uint32_t, 4> originals,
                         uint32_t replacement, Read read, Write write)
        : fields_{fields}, originals_{originals}, replacement_{replacement}, read_{read}, write_{write} {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            uint32_t value{};
            if (!fields_[i] || !read_(fields_[i], value) || value != originals_[i]) return;
            for (std::size_t j = 0; j < i; ++j) if (fields_[i] == fields_[j]) return;
        }
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            touched_[i] = true; // a failing write may have partially completed
            if (!write_(fields_[i], replacement_)) { restore(); return; }
        }
        applied_ = true;
    }
    ~ScenePassTransaction() { restore(); }
    ScenePassTransaction(const ScenePassTransaction&) = delete;
    ScenePassTransaction& operator=(const ScenePassTransaction&) = delete;
    bool applied() const { return applied_; }
    bool restore() {
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            if (!touched_[i]) continue;
            touched_[i] = false;
            uint32_t value{};
            const bool ok = read_(fields_[i], value) &&
                (value == originals_[i] || (value == replacement_ && write_(fields_[i], originals_[i])));
            restored_ = ok && restored_;
        }
        return restored_;
    }
private:
    std::array<uintptr_t, 4> fields_{};
    std::array<uint32_t, 4> originals_{};
    std::array<bool, 4> touched_{};
    uint32_t replacement_{};
    Read read_{};
    Write write_{};
    bool applied_{}, restored_{true};
};
} // namespace wuwa_stereo
