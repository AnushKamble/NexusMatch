#pragma once

#include "nexusmatch/types.hpp"
#include <cstdint>
#include <string>
#include <stdexcept>

namespace nexusmatch {

struct EngineConfig {
    std::uint32_t price_tick_count{100'000}; // Ticks [1, price_tick_count - 1]
    std::uint32_t max_live_orders{1'000'000};
    bool reject_tick_zero{true};

    [[nodiscard]] bool validate(std::string* error_msg = nullptr) const noexcept {
        if (price_tick_count < 2) {
            if (error_msg) *error_msg = "price_tick_count must be at least 2";
            return false;
        }
        if (max_live_orders == 0) {
            if (error_msg) *error_msg = "max_live_orders must be greater than 0";
            return false;
        }
        if (max_live_orders >= kInvalidIndex) {
            if (error_msg) *error_msg = "max_live_orders must be strictly less than kInvalidIndex";
            return false;
        }
        return true;
    }
};

} // namespace nexusmatch
