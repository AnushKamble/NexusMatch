#pragma once

#include "nexusmatch/types.hpp"
#include "nexusmatch/order_arena.hpp"
#include <cstdint>
#include <cassert>

namespace nexusmatch {

struct PriceLevel {
    std::uint32_t head_index{kInvalidIndex};
    std::uint32_t tail_index{kInvalidIndex};
    std::uint64_t total_quantity{0};

    [[nodiscard]] constexpr bool is_empty() const noexcept {
        return head_index == kInvalidIndex;
    }

    constexpr void reset() noexcept {
        head_index = kInvalidIndex;
        tail_index = kInvalidIndex;
        total_quantity = 0;
    }
};

static_assert(sizeof(PriceLevel) == 16, "PriceLevel must be 16 bytes");

class IntrusiveQueue {
public:
    static void append(PriceLevel& level, OrderArena& arena, std::uint32_t node_idx) noexcept {
        assert(node_idx != kInvalidIndex);
        OrderNode& node = arena[node_idx];
        node.next_index = kInvalidIndex;

        if (level.is_empty()) {
            node.prev_index = kInvalidIndex;
            level.head_index = node_idx;
            level.tail_index = node_idx;
        } else {
            node.prev_index = level.tail_index;
            arena[level.tail_index].next_index = node_idx;
            level.tail_index = node_idx;
        }

        level.total_quantity += node.remaining_qty;
    }

    static void unlink(PriceLevel& level, OrderArena& arena, std::uint32_t node_idx) noexcept {
        assert(node_idx != kInvalidIndex);
        OrderNode& node = arena[node_idx];

        assert(level.total_quantity >= node.remaining_qty);
        level.total_quantity -= node.remaining_qty;

        if (node.prev_index != kInvalidIndex) {
            arena[node.prev_index].next_index = node.next_index;
        } else {
            assert(level.head_index == node_idx);
            level.head_index = node.next_index;
        }

        if (node.next_index != kInvalidIndex) {
            arena[node.next_index].prev_index = node.prev_index;
        } else {
            assert(level.tail_index == node_idx);
            level.tail_index = node.prev_index;
        }

        node.prev_index = kInvalidIndex;
        node.next_index = kInvalidIndex;
    }

    static std::uint32_t pop_front(PriceLevel& level, OrderArena& arena) noexcept {
        if (level.is_empty()) {
            return kInvalidIndex;
        }
        std::uint32_t head = level.head_index;
        unlink(level, arena, head);
        return head;
    }

    [[nodiscard]] static bool validate_invariants(const PriceLevel& level, const OrderArena& arena) noexcept {
        if (level.head_index == kInvalidIndex) {
            return level.tail_index == kInvalidIndex && level.total_quantity == 0;
        }
        if (level.tail_index == kInvalidIndex) {
            return false;
        }

        std::uint32_t curr = level.head_index;
        std::uint32_t prev = kInvalidIndex;
        std::uint64_t sum_qty = 0;
        std::uint32_t count = 0;

        while (curr != kInvalidIndex) {
            const OrderNode& node = arena[curr];
            if (node.prev_index != prev) {
                return false; // Reciprocal link violation
            }
            sum_qty += node.remaining_qty;
            prev = curr;
            curr = node.next_index;
            ++count;
            if (count > arena.capacity()) {
                return false; // Cycle detected
            }
        }

        if (prev != level.tail_index) {
            return false;
        }
        if (arena[level.head_index].prev_index != kInvalidIndex) {
            return false;
        }
        if (arena[level.tail_index].next_index != kInvalidIndex) {
            return false;
        }

        return sum_qty == level.total_quantity;
    }
};

} // namespace nexusmatch
