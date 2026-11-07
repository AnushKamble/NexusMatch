#pragma once

#include "nexusmatch/types.hpp"
#include "nexusmatch/config.hpp"
#include "nexusmatch/order_node.hpp"
#include "nexusmatch/order_arena.hpp"
#include "nexusmatch/order_id_index.hpp"
#include "nexusmatch/price_ladder.hpp"
#include "nexusmatch/price_bitmap.hpp"
#include "nexusmatch/state_hash.hpp"
#include <span>
#include <cstdint>

namespace nexusmatch {

struct BookLevelSnapshot {
    std::uint32_t price_tick{0};
    std::uint64_t total_quantity{0};
    std::uint32_t order_count{0};
    Side side{Side::Buy};
};

class LimitOrderBook {
public:
    explicit LimitOrderBook(const EngineConfig& config);
    ~LimitOrderBook() = default;

    LimitOrderBook(const LimitOrderBook&) = delete;
    LimitOrderBook& operator=(const LimitOrderBook&) = delete;
    LimitOrderBook(LimitOrderBook&&) noexcept = default;
    LimitOrderBook& operator=(LimitOrderBook&&) noexcept = default;

    [[nodiscard]] Bbo bbo() const noexcept;
    [[nodiscard]] BboEntry best_bid() const noexcept;
    [[nodiscard]] BboEntry best_ask() const noexcept;

    // Snapshot written into caller-provided span without heap allocation
    std::size_t copy_depth_snapshot(Side side, std::span<BookLevelSnapshot> out_span) const noexcept;

    [[nodiscard]] OrderArena& arena() noexcept { return arena_; }
    [[nodiscard]] const OrderArena& arena() const noexcept { return arena_; }

    [[nodiscard]] OrderIdIndex& id_index() noexcept { return id_index_; }
    [[nodiscard]] const OrderIdIndex& id_index() const noexcept { return id_index_; }

    [[nodiscard]] PriceLadder& ladder() noexcept { return ladder_; }
    [[nodiscard]] const PriceLadder& ladder() const noexcept { return ladder_; }

    [[nodiscard]] PriceBitmap& bid_bitmap() noexcept { return bid_bitmap_; }
    [[nodiscard]] const PriceBitmap& bid_bitmap() const noexcept { return bid_bitmap_; }

    [[nodiscard]] PriceBitmap& ask_bitmap() noexcept { return ask_bitmap_; }
    [[nodiscard]] const PriceBitmap& ask_bitmap() const noexcept { return ask_bitmap_; }

    [[nodiscard]] std::uint32_t active_orders_count() const noexcept {
        return arena_.active_count();
    }

    [[nodiscard]] std::uint64_t compute_state_hash(const EngineStats& stats) const noexcept;

    [[nodiscard]] bool validate_invariants() const noexcept;

private:
    EngineConfig config_;
    OrderArena arena_;
    OrderIdIndex id_index_;
    PriceLadder ladder_;
    PriceBitmap bid_bitmap_;
    PriceBitmap ask_bitmap_;
};

} // namespace nexusmatch
