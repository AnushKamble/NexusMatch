#pragma once

#include "nexusmatch/types.hpp"
#include <cstdint>

namespace nexusmatch {

enum class OrderNodeState : std::uint8_t {
    Free = 0,
    Active = 1
};

struct alignas(8) OrderNode {
    std::uint64_t order_id{0};       // 8 bytes
    std::uint32_t price_tick{0};     // 4 bytes
    std::uint32_t remaining_qty{0};  // 4 bytes
    std::uint32_t prev_index{kInvalidIndex}; // 4 bytes
    std::uint32_t next_index{kInvalidIndex}; // 4 bytes
    std::uint32_t owner_id{0};       // 4 bytes
    std::uint8_t side{0};            // 1 byte (0 = Buy, 1 = Sell)
    std::uint8_t state{0};           // 1 byte (OrderNodeState)
    std::uint16_t reserved{0};       // 2 bytes

    void reset() noexcept {
        order_id = 0;
        price_tick = 0;
        remaining_qty = 0;
        prev_index = kInvalidIndex;
        next_index = kInvalidIndex;
        owner_id = 0;
        side = 0;
        state = static_cast<std::uint8_t>(OrderNodeState::Free);
        reserved = 0;
    }
};

static_assert(sizeof(OrderNode) == 32, "OrderNode must be exactly 32 bytes");

} // namespace nexusmatch
