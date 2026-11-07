#pragma once

#include <cstdint>
#include <string_view>

namespace nexusmatch {

class StateHasher {
public:
    constexpr StateHasher() noexcept : hash_(0xcbf29ce484222325ULL) {} // FNV-1a 64-bit offset basis

    constexpr void update_u64(std::uint64_t val) noexcept {
        for (int i = 0; i < 8; ++i) {
            update_u8(static_cast<std::uint8_t>(val >> (i * 8)));
        }
    }

    constexpr void update_u32(std::uint32_t val) noexcept {
        for (int i = 0; i < 4; ++i) {
            update_u8(static_cast<std::uint8_t>(val >> (i * 8)));
        }
    }

    constexpr void update_u8(std::uint8_t byte) noexcept {
        hash_ ^= byte;
        hash_ *= 0x100000001b3ULL; // FNV-1a prime
    }

    [[nodiscard]] constexpr std::uint64_t digest() const noexcept {
        return hash_;
    }

private:
    std::uint64_t hash_;
};

} // namespace nexusmatch
