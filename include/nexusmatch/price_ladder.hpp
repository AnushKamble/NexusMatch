#pragma once

#include "nexusmatch/types.hpp"
#include "nexusmatch/intrusive_queue.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <cassert>

namespace nexusmatch {

class PriceLadder {
public:
    explicit PriceLadder(std::uint32_t price_tick_count)
        : price_tick_count_(price_tick_count),
          bid_levels_(std::make_unique<PriceLevel[]>(price_tick_count)),
          ask_levels_(std::make_unique<PriceLevel[]>(price_tick_count))
    {
        if (price_tick_count == 0) {
            throw std::invalid_argument("PriceLadder tick count must be > 0");
        }
        for (std::uint32_t i = 0; i < price_tick_count; ++i) {
            bid_levels_[i].reset();
            ask_levels_[i].reset();
        }
    }

    ~PriceLadder() = default;

    PriceLadder(const PriceLadder&) = delete;
    PriceLadder& operator=(const PriceLadder&) = delete;
    PriceLadder(PriceLadder&&) noexcept = default;
    PriceLadder& operator=(PriceLadder&&) noexcept = default;

    [[nodiscard]] PriceLevel& level(Side side, std::uint32_t tick) noexcept {
        assert(tick < price_tick_count_);
        return (side == Side::Buy) ? bid_levels_[tick] : ask_levels_[tick];
    }

    [[nodiscard]] const PriceLevel& level(Side side, std::uint32_t tick) const noexcept {
        assert(tick < price_tick_count_);
        return (side == Side::Buy) ? bid_levels_[tick] : ask_levels_[tick];
    }

    [[nodiscard]] std::uint32_t tick_count() const noexcept { return price_tick_count_; }

    void reset() noexcept {
        for (std::uint32_t i = 0; i < price_tick_count_; ++i) {
            bid_levels_[i].reset();
            ask_levels_[i].reset();
        }
    }

private:
    std::uint32_t price_tick_count_;
    std::unique_ptr<PriceLevel[]> bid_levels_;
    std::unique_ptr<PriceLevel[]> ask_levels_;
};

} // namespace nexusmatch
