#include "nexusmatch/order_book.hpp"
#include <algorithm>

namespace nexusmatch {

LimitOrderBook::LimitOrderBook(const EngineConfig& config)
    : config_(config),
      arena_(config.max_live_orders),
      id_index_(config.max_live_orders),
      ladder_(config.price_tick_count),
      bid_bitmap_(config.price_tick_count),
      ask_bitmap_(config.price_tick_count)
{
}

BboEntry LimitOrderBook::best_bid() const noexcept {
    std::uint32_t tick = bid_bitmap_.find_max();
    if (tick == kInvalidIndex) {
        return BboEntry{.price_tick = 0, .quantity = 0, .valid = false};
    }
    return BboEntry{
        .price_tick = tick,
        .quantity = ladder_.level(Side::Buy, tick).total_quantity,
        .valid = true
    };
}

BboEntry LimitOrderBook::best_ask() const noexcept {
    std::uint32_t tick = ask_bitmap_.find_min();
    if (tick == kInvalidIndex) {
        return BboEntry{.price_tick = 0, .quantity = 0, .valid = false};
    }
    return BboEntry{
        .price_tick = tick,
        .quantity = ladder_.level(Side::Sell, tick).total_quantity,
        .valid = true
    };
}

Bbo LimitOrderBook::bbo() const noexcept {
    return Bbo{
        .bid = best_bid(),
        .ask = best_ask()
    };
}

std::size_t LimitOrderBook::copy_depth_snapshot(Side side, std::span<BookLevelSnapshot> out_span) const noexcept {
    std::size_t written = 0;
    if (side == Side::Buy) {
        std::uint32_t tick = bid_bitmap_.find_max();
        while (tick != kInvalidIndex && written < out_span.size()) {
            const PriceLevel& lvl = ladder_.level(Side::Buy, tick);
            if (!lvl.is_empty()) {
                std::uint32_t order_count = 0;
                std::uint32_t curr = lvl.head_index;
                while (curr != kInvalidIndex) {
                    ++order_count;
                    curr = arena_[curr].next_index;
                }
                out_span[written++] = BookLevelSnapshot{
                    .price_tick = tick,
                    .total_quantity = lvl.total_quantity,
                    .order_count = order_count,
                    .side = Side::Buy
                };
            }
            if (tick == 0) break;
            // Search next lower occupied tick
            --tick;
            while (tick > 0 && !bid_bitmap_.test(tick)) {
                --tick;
            }
            if (!bid_bitmap_.test(tick)) break;
        }
    } else {
        std::uint32_t tick = ask_bitmap_.find_min();
        while (tick != kInvalidIndex && written < out_span.size()) {
            const PriceLevel& lvl = ladder_.level(Side::Sell, tick);
            if (!lvl.is_empty()) {
                std::uint32_t order_count = 0;
                std::uint32_t curr = lvl.head_index;
                while (curr != kInvalidIndex) {
                    ++order_count;
                    curr = arena_[curr].next_index;
                }
                out_span[written++] = BookLevelSnapshot{
                    .price_tick = tick,
                    .total_quantity = lvl.total_quantity,
                    .order_count = order_count,
                    .side = Side::Sell
                };
            }
            ++tick;
            while (tick < config_.price_tick_count && !ask_bitmap_.test(tick)) {
                ++tick;
            }
            if (tick >= config_.price_tick_count || !ask_bitmap_.test(tick)) break;
        }
    }
    return written;
}

std::uint64_t LimitOrderBook::compute_state_hash(const EngineStats& stats) const noexcept {
    StateHasher hasher;

    // 1. Stats and accounting
    hasher.update_u64(stats.buy_volume.accepted_quantity);
    hasher.update_u64(stats.buy_volume.executed_quantity);
    hasher.update_u64(stats.buy_volume.resting_quantity);
    hasher.update_u64(stats.buy_volume.cancelled_quantity);

    hasher.update_u64(stats.sell_volume.accepted_quantity);
    hasher.update_u64(stats.sell_volume.executed_quantity);
    hasher.update_u64(stats.sell_volume.resting_quantity);
    hasher.update_u64(stats.sell_volume.cancelled_quantity);

    hasher.update_u64(stats.total_trades);
    hasher.update_u64(stats.total_orders_accepted);
    hasher.update_u64(stats.total_cancels_processed);

    // 2. Canonical Bids: Descending price ticks, FIFO queue order
    for (std::uint32_t t = config_.price_tick_count; t > 0; --t) {
        std::uint32_t tick = t - 1;
        if (bid_bitmap_.test(tick)) {
            const PriceLevel& lvl = ladder_.level(Side::Buy, tick);
            hasher.update_u8(0); // Buy
            hasher.update_u32(tick);
            hasher.update_u64(lvl.total_quantity);

            std::uint32_t curr = lvl.head_index;
            while (curr != kInvalidIndex) {
                const OrderNode& node = arena_[curr];
                hasher.update_u64(node.order_id);
                hasher.update_u32(node.owner_id);
                hasher.update_u32(node.remaining_qty);
                curr = node.next_index;
            }
        }
    }

    // 3. Canonical Asks: Ascending price ticks, FIFO queue order
    for (std::uint32_t tick = 0; tick < config_.price_tick_count; ++tick) {
        if (ask_bitmap_.test(tick)) {
            const PriceLevel& lvl = ladder_.level(Side::Sell, tick);
            hasher.update_u8(1); // Sell
            hasher.update_u32(tick);
            hasher.update_u64(lvl.total_quantity);

            std::uint32_t curr = lvl.head_index;
            while (curr != kInvalidIndex) {
                const OrderNode& node = arena_[curr];
                hasher.update_u64(node.order_id);
                hasher.update_u32(node.owner_id);
                hasher.update_u32(node.remaining_qty);
                curr = node.next_index;
            }
        }
    }

    return hasher.digest();
}

bool LimitOrderBook::validate_invariants() const noexcept {
    if (!arena_.validate_invariants()) return false;
    if (!id_index_.validate_invariants()) return false;
    if (!bid_bitmap_.validate_invariants()) return false;
    if (!ask_bitmap_.validate_invariants()) return false;

    // Check every active order in arena is in id_index and exactly one level
    std::uint32_t live_orders_in_levels = 0;

    for (std::uint32_t tick = 0; tick < config_.price_tick_count; ++tick) {
        // Buy side
        const PriceLevel& bid_lvl = ladder_.level(Side::Buy, tick);
        bool bid_occupied = !bid_lvl.is_empty();
        if (bid_bitmap_.test(tick) != bid_occupied) return false;
        if (bid_occupied) {
            if (!IntrusiveQueue::validate_invariants(bid_lvl, arena_)) return false;
            std::uint32_t curr = bid_lvl.head_index;
            while (curr != kInvalidIndex) {
                const OrderNode& node = arena_[curr];
                if (node.price_tick != tick) return false;
                if (node.side != static_cast<std::uint8_t>(Side::Buy)) return false;
                if (node.remaining_qty == 0) return false;
                if (id_index_.find(node.order_id) != curr) return false;
                ++live_orders_in_levels;
                curr = node.next_index;
            }
        }

        // Sell side
        const PriceLevel& ask_lvl = ladder_.level(Side::Sell, tick);
        bool ask_occupied = !ask_lvl.is_empty();
        if (ask_bitmap_.test(tick) != ask_occupied) return false;
        if (ask_occupied) {
            if (!IntrusiveQueue::validate_invariants(ask_lvl, arena_)) return false;
            std::uint32_t curr = ask_lvl.head_index;
            while (curr != kInvalidIndex) {
                const OrderNode& node = arena_[curr];
                if (node.price_tick != tick) return false;
                if (node.side != static_cast<std::uint8_t>(Side::Sell)) return false;
                if (node.remaining_qty == 0) return false;
                if (id_index_.find(node.order_id) != curr) return false;
                ++live_orders_in_levels;
                curr = node.next_index;
            }
        }
    }

    if (live_orders_in_levels != arena_.active_count()) return false;
    if (id_index_.size() != arena_.active_count()) return false;

    // No crossed book: best_bid < best_ask
    BboEntry bb = best_bid();
    BboEntry ba = best_ask();
    if (bb.valid && ba.valid) {
        if (bb.price_tick >= ba.price_tick) {
            return false; // Crossed book invariant violated!
        }
    }

    return true;
}

} // namespace nexusmatch
