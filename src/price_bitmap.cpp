#include "nexusmatch/price_bitmap.hpp"
#include <algorithm>
#include <stdexcept>

namespace nexusmatch {

PriceBitmap::PriceBitmap(std::uint32_t price_tick_count)
    : price_tick_count_(price_tick_count)
{
    if (price_tick_count == 0) {
        throw std::invalid_argument("price_tick_count must be > 0");
    }

    // Build hierarchy
    std::uint32_t current_items = price_tick_count;
    std::uint32_t total_words = 0;

    while (true) {
        std::uint32_t words_needed = (current_items + 63U) / 64U;
        levels_.push_back(LevelInfo{
            .word_offset = total_words,
            .word_count = words_needed
        });
        total_words += words_needed;
        if (words_needed <= 1) {
            break; // Top level fits in 1 word
        }
        current_items = words_needed;
    }

    num_levels_ = static_cast<std::uint32_t>(levels_.size());
    words_.resize(total_words, 0ULL);
}

void PriceBitmap::set(std::uint32_t tick) noexcept {
    assert(tick < price_tick_count_);

    std::uint32_t item = tick;
    for (std::uint32_t lvl = 0; lvl < num_levels_; ++lvl) {
        std::uint32_t word_idx = levels_[lvl].word_offset + (item / 64U);
        std::uint32_t bit_idx = item % 64U;
        std::uint64_t mask = 1ULL << bit_idx;
        std::uint64_t prev = words_[word_idx];

        words_[word_idx] = prev | mask;

        // If the word was already nonzero before setting this bit, summary bits above are already set
        if (prev != 0ULL) {
            break;
        }

        // Transition from 0 -> nonzero, propagate to parent level
        item = item / 64U;
    }
}

void PriceBitmap::clear(std::uint32_t tick) noexcept {
    assert(tick < price_tick_count_);

    std::uint32_t item = tick;
    for (std::uint32_t lvl = 0; lvl < num_levels_; ++lvl) {
        std::uint32_t word_idx = levels_[lvl].word_offset + (item / 64U);
        std::uint32_t bit_idx = item % 64U;
        std::uint64_t mask = 1ULL << bit_idx;

        words_[word_idx] &= ~mask;

        // If the word is still nonzero after clearing, summary bits above remain set
        if (words_[word_idx] != 0ULL) {
            break;
        }

        // Word became 0, propagate clear to parent level
        item = item / 64U;
    }
}

bool PriceBitmap::test(std::uint32_t tick) const noexcept {
    if (tick >= price_tick_count_) return false;
    std::uint32_t word_idx = levels_[0].word_offset + (tick / 64U);
    std::uint32_t bit_idx = tick % 64U;
    return (words_[word_idx] & (1ULL << bit_idx)) != 0ULL;
}

std::uint32_t PriceBitmap::find_min() const noexcept {
    if (is_empty()) {
        return kInvalidIndex;
    }

    std::uint32_t word_in_level = 0;

    // Traverse from top level down to level 0
    for (int lvl = static_cast<int>(num_levels_) - 1; lvl >= 0; --lvl) {
        std::uint32_t abs_word = levels_[static_cast<std::size_t>(lvl)].word_offset + word_in_level;
        std::uint64_t w = words_[abs_word];
        assert(w != 0ULL);

        int bit = std::countr_zero(w);
        if (lvl == 0) {
            std::uint32_t tick = word_in_level * 64U + static_cast<std::uint32_t>(bit);
            return (tick < price_tick_count_) ? tick : kInvalidIndex;
        }
        word_in_level = word_in_level * 64U + static_cast<std::uint32_t>(bit);
    }

    return kInvalidIndex;
}

std::uint32_t PriceBitmap::find_max() const noexcept {
    if (is_empty()) {
        return kInvalidIndex;
    }

    std::uint32_t word_in_level = 0;

    for (int lvl = static_cast<int>(num_levels_) - 1; lvl >= 0; --lvl) {
        std::uint32_t abs_word = levels_[static_cast<std::size_t>(lvl)].word_offset + word_in_level;
        std::uint64_t w = words_[abs_word];
        assert(w != 0ULL);

        int lz = std::countl_zero(w);
        int bit = 63 - lz;

        if (lvl == 0) {
            std::uint32_t tick = word_in_level * 64U + static_cast<std::uint32_t>(bit);
            return (tick < price_tick_count_) ? tick : kInvalidIndex;
        }
        word_in_level = word_in_level * 64U + static_cast<std::uint32_t>(bit);
    }

    return kInvalidIndex;
}

bool PriceBitmap::is_empty() const noexcept {
    std::uint32_t top_word = levels_.back().word_offset;
    return words_[top_word] == 0ULL;
}

void PriceBitmap::reset() noexcept {
    std::fill(words_.begin(), words_.end(), 0ULL);
}

bool PriceBitmap::validate_invariants() const noexcept {
    // Validate each level against the level beneath it
    for (std::uint32_t lvl = 1; lvl < num_levels_; ++lvl) {
        const auto& lower = levels_[lvl - 1];
        const auto& upper = levels_[lvl];

        for (std::uint32_t i = 0; i < lower.word_count; ++i) {
            bool lower_nonzero = (words_[lower.word_offset + i] != 0ULL);
            std::uint32_t upper_word = upper.word_offset + (i / 64U);
            std::uint32_t upper_bit = i % 64U;
            bool upper_bit_set = (words_[upper_word] & (1ULL << upper_bit)) != 0ULL;

            if (lower_nonzero != upper_bit_set) {
                return false;
            }
        }
    }

    // Verify find_min and find_max match simple linear scan on level 0
    std::uint32_t linear_min = kInvalidIndex;
    std::uint32_t linear_max = kInvalidIndex;

    for (std::uint32_t t = 0; t < price_tick_count_; ++t) {
        if (test(t)) {
            if (linear_min == kInvalidIndex) linear_min = t;
            linear_max = t;
        }
    }

    if (find_min() != linear_min) return false;
    if (find_max() != linear_max) return false;

    return true;
}

} // namespace nexusmatch
