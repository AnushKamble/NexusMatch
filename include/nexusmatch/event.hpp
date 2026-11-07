#pragma once

#include "nexusmatch/types.hpp"
#include <cstdint>
#include <string_view>

namespace nexusmatch {

enum class EventType : std::uint8_t {
    Trade = 1,
    OrderRested = 2,
    OrderCancelled = 3,
    CancelRejected = 4,
    OrderRejected = 5,
    ResidualCancelled = 6,
    SelfTradePrevented = 7,
    CapacityExhausted = 8
};

[[nodiscard]] constexpr std::string_view to_string(EventType type) noexcept {
    switch (type) {
        case EventType::Trade: return "TRADE";
        case EventType::OrderRested: return "ORDER_RESTED";
        case EventType::OrderCancelled: return "ORDER_CANCELLED";
        case EventType::CancelRejected: return "CANCEL_REJECTED";
        case EventType::OrderRejected: return "ORDER_REJECTED";
        case EventType::ResidualCancelled: return "RESIDUAL_CANCELLED";
        case EventType::SelfTradePrevented: return "SELF_TRADE_PREVENTED";
        case EventType::CapacityExhausted: return "CAPACITY_EXHAUSTED";
    }
    return "UNKNOWN";
}

struct TradePayload {
    std::uint64_t trade_seq{0};
    std::uint64_t maker_order_id{0};
    std::uint64_t taker_order_id{0};
    std::uint32_t price_tick{0};
    std::uint32_t quantity{0};
    Side maker_side{Side::Sell};
};

struct OrderRestedPayload {
    std::uint64_t order_id{0};
    std::uint32_t owner_id{0};
    std::uint32_t price_tick{0};
    std::uint32_t remaining_qty{0};
    Side side{Side::Buy};
};

struct OrderCancelledPayload {
    std::uint64_t order_id{0};
    std::uint32_t price_tick{0};
    std::uint32_t cancelled_qty{0};
    Side side{Side::Buy};
};

struct CancelRejectedPayload {
    std::uint64_t order_id{0};
    CancelRejectReason reason{CancelRejectReason::OrderNotFound};
};

struct OrderRejectedPayload {
    std::uint64_t order_id{0};
    OrderRejectReason reason{OrderRejectReason::None};
};

struct ResidualCancelledPayload {
    std::uint64_t order_id{0};
    std::uint32_t cancelled_qty{0};
    Side side{Side::Buy};
};

struct SelfTradePreventedPayload {
    std::uint64_t taker_order_id{0};
    std::uint64_t maker_order_id{0};
    std::uint32_t cancelled_qty{0};
    Side side{Side::Buy};
};

struct CapacityExhaustedPayload {
    std::uint64_t order_id{0};
    std::uint32_t unrested_qty{0};
    Side side{Side::Buy};
};

struct alignas(8) EngineEvent {
    EventType type{EventType::Trade};
    std::uint8_t reserved[7]{0};
    union {
        TradePayload trade;
        OrderRestedPayload order_rested;
        OrderCancelledPayload order_cancelled;
        CancelRejectedPayload cancel_rejected;
        OrderRejectedPayload order_rejected;
        ResidualCancelledPayload residual_cancelled;
        SelfTradePreventedPayload self_trade_prevented;
        CapacityExhaustedPayload capacity_exhausted;
    };

    EngineEvent() noexcept : type(EventType::Trade), reserved{}, trade{} {}

    [[nodiscard]] static EngineEvent make_trade(
        std::uint64_t seq,
        std::uint64_t maker_id,
        std::uint64_t taker_id,
        std::uint32_t price,
        std::uint32_t qty,
        Side maker_side) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::Trade;
        ev.trade = TradePayload{
            .trade_seq = seq,
            .maker_order_id = maker_id,
            .taker_order_id = taker_id,
            .price_tick = price,
            .quantity = qty,
            .maker_side = maker_side
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_order_rested(
        std::uint64_t order_id,
        std::uint32_t owner_id,
        std::uint32_t price,
        std::uint32_t qty,
        Side side) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::OrderRested;
        ev.order_rested = OrderRestedPayload{
            .order_id = order_id,
            .owner_id = owner_id,
            .price_tick = price,
            .remaining_qty = qty,
            .side = side
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_order_cancelled(
        std::uint64_t order_id,
        std::uint32_t price,
        std::uint32_t cancelled_qty,
        Side side) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::OrderCancelled;
        ev.order_cancelled = OrderCancelledPayload{
            .order_id = order_id,
            .price_tick = price,
            .cancelled_qty = cancelled_qty,
            .side = side
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_cancel_rejected(
        std::uint64_t order_id,
        CancelRejectReason reason) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::CancelRejected;
        ev.cancel_rejected = CancelRejectedPayload{
            .order_id = order_id,
            .reason = reason
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_order_rejected(
        std::uint64_t order_id,
        OrderRejectReason reason) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::OrderRejected;
        ev.order_rejected = OrderRejectedPayload{
            .order_id = order_id,
            .reason = reason
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_residual_cancelled(
        std::uint64_t order_id,
        std::uint32_t cancelled_qty,
        Side side) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::ResidualCancelled;
        ev.residual_cancelled = ResidualCancelledPayload{
            .order_id = order_id,
            .cancelled_qty = cancelled_qty,
            .side = side
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_self_trade_prevented(
        std::uint64_t taker_id,
        std::uint64_t maker_id,
        std::uint32_t cancelled_qty,
        Side side) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::SelfTradePrevented;
        ev.self_trade_prevented = SelfTradePreventedPayload{
            .taker_order_id = taker_id,
            .maker_order_id = maker_id,
            .cancelled_qty = cancelled_qty,
            .side = side
        };
        return ev;
    }

    [[nodiscard]] static EngineEvent make_capacity_exhausted(
        std::uint64_t order_id,
        std::uint32_t unrested_qty,
        Side side) noexcept
    {
        EngineEvent ev;
        ev.type = EventType::CapacityExhausted;
        ev.capacity_exhausted = CapacityExhaustedPayload{
            .order_id = order_id,
            .unrested_qty = unrested_qty,
            .side = side
        };
        return ev;
    }
};

static_assert(sizeof(EngineEvent) <= 48, "EngineEvent size must be compact (<= 48 bytes)");

} // namespace nexusmatch
