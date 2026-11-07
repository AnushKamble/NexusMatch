#pragma once

#include <cstdint>
#include <string_view>
#include <limits>

namespace nexusmatch {

// Internal arena invalid index sentinel
inline constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

enum class Side : std::uint8_t {
    Buy = 0,
    Sell = 1
};

[[nodiscard]] constexpr Side opposite_side(Side side) noexcept {
    return (side == Side::Buy) ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr std::string_view to_string(Side side) noexcept {
    return (side == Side::Buy) ? "BUY" : "SELL";
}

enum class OrderKind : std::uint8_t {
    Limit = 0,
    Market = 1,
    Cancel = 2
};

[[nodiscard]] constexpr std::string_view to_string(OrderKind kind) noexcept {
    switch (kind) {
        case OrderKind::Limit:  return "LIMIT";
        case OrderKind::Market: return "MARKET";
        case OrderKind::Cancel: return "CANCEL";
    }
    return "UNKNOWN";
}

enum class OrderRejectReason : std::uint8_t {
    None = 0,
    ZeroOrderId,
    ZeroQuantity,
    InvalidPriceTick,
    DuplicateOrderId,
    CapacityExhausted,
    InvalidEnum
};

[[nodiscard]] constexpr std::string_view to_string(OrderRejectReason reason) noexcept {
    switch (reason) {
        case OrderRejectReason::None: return "NONE";
        case OrderRejectReason::ZeroOrderId: return "ZERO_ORDER_ID";
        case OrderRejectReason::ZeroQuantity: return "ZERO_QUANTITY";
        case OrderRejectReason::InvalidPriceTick: return "INVALID_PRICE_TICK";
        case OrderRejectReason::DuplicateOrderId: return "DUPLICATE_ORDER_ID";
        case OrderRejectReason::CapacityExhausted: return "CAPACITY_EXHAUSTED";
        case OrderRejectReason::InvalidEnum: return "INVALID_ENUM";
    }
    return "UNKNOWN";
}

enum class CancelRejectReason : std::uint8_t {
    None = 0,
    OrderNotFound,
    ZeroOrderId
};

[[nodiscard]] constexpr std::string_view to_string(CancelRejectReason reason) noexcept {
    switch (reason) {
        case CancelRejectReason::None: return "NONE";
        case CancelRejectReason::OrderNotFound: return "ORDER_NOT_FOUND";
        case CancelRejectReason::ZeroOrderId: return "ZERO_ORDER_ID";
    }
    return "UNKNOWN";
}

// 24-byte tightly packed command struct without uninitialized padding
struct alignas(8) OrderCommand {
    OrderKind kind;
    Side side;
    std::uint16_t reserved{0};
    std::uint32_t owner_id{0};
    std::uint64_t order_id{0};
    std::uint32_t price_tick{0};
    std::uint32_t quantity{0};

    [[nodiscard]] static constexpr OrderCommand limit(
        Side side,
        std::uint64_t order_id,
        std::uint32_t owner_id,
        std::uint32_t price_tick,
        std::uint32_t quantity) noexcept
    {
        return OrderCommand{
            .kind = OrderKind::Limit,
            .side = side,
            .reserved = 0,
            .owner_id = owner_id,
            .order_id = order_id,
            .price_tick = price_tick,
            .quantity = quantity
        };
    }

    [[nodiscard]] static constexpr OrderCommand market(
        Side side,
        std::uint64_t order_id,
        std::uint32_t owner_id,
        std::uint32_t quantity) noexcept
    {
        return OrderCommand{
            .kind = OrderKind::Market,
            .side = side,
            .reserved = 0,
            .owner_id = owner_id,
            .order_id = order_id,
            .price_tick = 0,
            .quantity = quantity
        };
    }

    [[nodiscard]] static constexpr OrderCommand cancel(std::uint64_t order_id) noexcept {
        return OrderCommand{
            .kind = OrderKind::Cancel,
            .side = Side::Buy,
            .reserved = 0,
            .owner_id = 0,
            .order_id = order_id,
            .price_tick = 0,
            .quantity = 0
        };
    }
};

static_assert(sizeof(OrderCommand) == 24, "OrderCommand must be exactly 24 bytes");

struct BboEntry {
    std::uint32_t price_tick{0};
    std::uint64_t quantity{0};
    bool valid{false};
};

struct Bbo {
    BboEntry bid{};
    BboEntry ask{};
};

struct VolumeAccounting {
    std::uint64_t accepted_quantity{0};
    std::uint64_t executed_quantity{0};
    std::uint64_t resting_quantity{0};
    std::uint64_t cancelled_quantity{0};
    std::uint64_t rejected_quantity{0};

    [[nodiscard]] constexpr bool is_conserved() const noexcept {
        return accepted_quantity == (executed_quantity + resting_quantity + cancelled_quantity);
    }
};

struct EngineStats {
    VolumeAccounting buy_volume{};
    VolumeAccounting sell_volume{};
    std::uint64_t total_trades{0};
    std::uint64_t total_orders_accepted{0};
    std::uint64_t total_orders_rejected{0};
    std::uint64_t total_cancels_processed{0};
    std::uint64_t total_cancels_rejected{0};
    std::uint64_t total_self_trades_prevented{0};
    std::uint64_t active_orders_count{0};
};

} // namespace nexusmatch
