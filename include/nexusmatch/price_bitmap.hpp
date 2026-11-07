#pragma once

#include "nexusmatch/types.hpp"
#include <cstdint>
#include <vector>
#include <memory>
#include <span>
#include <bit>
#include <cassert>

namespace nexusmatch {

class PriceBitmap {
public:
    explicit PriceBitmap(std::uint32_t price_tick_count);
    ~PriceBitmap() = default;

    PriceBitmap(const PriceBitmap&) = delete;
    PriceBitmap& operator=(const PriceBitmap&) = delete;
    PriceBitmap(PriceBitmap&&) noexcept = default;
    PriceBitmap& operator=(PriceBitmap&&) noexcept = default;

    void set(std::uint32_t tick) noexcept;
    void clear(std::uint32_t tick) noexcept;
    [[nodiscard]] bool test(std::uint32_t tick) const noexcept;

    [[nodiscard]] std::uint32_t find_min() const noexcept;
    [[nodiscard]] std::uint32_t find_max() const noexcept;

    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] std::uint32_t tick_count() const noexcept { return price_tick_count_; }

    void reset() noexcept;

    // Diagnostic validation against linear scan
    [[nodiscard]] bool validate_invariants() const noexcept;

private:
    struct LevelInfo {
        std::uint32_t word_offset{0};
        std::uint32_t word_count{0};
    };

    std::uint32_t price_tick_count_;
    std::uint32_t num_levels_{0};
    std::vector<LevelInfo> levels_;
    std::vector<std::uint64_t> words_; // Flat contiguous storage for all hierarchy levels
};

} // namespace nexusmatch
