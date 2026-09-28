#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace wuwa_code_compatibility {
// Both shipped variants were identified in the working LGUI route. A matching
// header is only a prerequisite: every requested code range must also match.
constexpr uint32_t timestamp = 0x6a74963e;
constexpr std::array<uint32_t, 2> image_sizes{0x3d627000, 0x3d625000};
struct Identity { uint16_t machine, magic; uint32_t timestamp, size; };
struct Range { uint32_t rva, size; uint64_t hash; };
enum class Failure { none, machine, magic, timestamp, image_size, range, unreadable, code };
struct Result {
    Failure failure{};
    uint32_t rva{};
    uint64_t expected{}, actual{};
    explicit operator bool() const { return failure == Failure::none; }
};
inline uint64_t fingerprint(std::span<const uint8_t> bytes) noexcept {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const auto value : bytes) { hash ^= value; hash *= 0x100000001b3ULL; }
    return hash;
}
template<class ReadCode> Result verify(Identity image, std::span<const Range> ranges, ReadCode read_code) {
    if (image.machine != 0x8664) return {Failure::machine, 0, 0x8664, image.machine};
    if (image.magic != 0x20b) return {Failure::magic, 0, 0x20b, image.magic};
    if (image.timestamp != timestamp) return {Failure::timestamp, 0, timestamp, image.timestamp};
    if (image.size != image_sizes[0] && image.size != image_sizes[1])
        return {Failure::image_size, 0, image_sizes[1], image.size};
    for (const auto& range : ranges) {
        if (!range.size || range.rva >= image.size || range.size > image.size - range.rva)
            return {Failure::range, range.rva, range.size, image.size};
        std::vector<uint8_t> bytes(range.size);
        if (!read_code(range.rva, std::span<uint8_t>{bytes}))
            return {Failure::unreadable, range.rva, range.hash, 0};
        const auto actual = fingerprint(bytes);
        if (actual != range.hash) return {Failure::code, range.rva, range.hash, actual};
    }
    return {};
}
} // namespace wuwa_code_compatibility
