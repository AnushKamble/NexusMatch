#pragma once

#include "nexusmatch/types.hpp"
#include "nexusmatch/event.hpp"
#include <vector>
#include <random>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <cstdlib>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: (" #cond ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while(0)

namespace nexusmatch::test {

// Deterministic Pseudo-Random Generator for tests (SplitMix64 or xoshiro256**)
class DeterministicRng {
public:
    explicit DeterministicRng(std::uint64_t seed) : state_(seed ? seed : 0x853c49e6748fea9bULL) {}

    std::uint64_t next_u64() noexcept {
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    std::uint32_t next_u32() noexcept {
        return static_cast<std::uint32_t>(next_u64());
    }

    std::uint32_t uniform_u32(std::uint32_t min_val, std::uint32_t max_val) noexcept {
        if (min_val >= max_val) return min_val;
        return min_val + static_cast<std::uint32_t>(next_u64() % (max_val - min_val + 1));
    }

    double next_double() noexcept {
        return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
    }

private:
    std::uint64_t state_;
};

inline bool events_equal(const EngineEvent& a, const EngineEvent& b) noexcept {
    if (a.type != b.type) return false;
    switch (a.type) {
        case EventType::Trade:
            return a.trade.trade_seq == b.trade.trade_seq &&
                   a.trade.maker_order_id == b.trade.maker_order_id &&
                   a.trade.taker_order_id == b.trade.taker_order_id &&
                   a.trade.price_tick == b.trade.price_tick &&
                   a.trade.quantity == b.trade.quantity &&
                   a.trade.maker_side == b.trade.maker_side;
        case EventType::OrderRested:
            return a.order_rested.order_id == b.order_rested.order_id &&
                   a.order_rested.owner_id == b.order_rested.owner_id &&
                   a.order_rested.price_tick == b.order_rested.price_tick &&
                   a.order_rested.remaining_qty == b.order_rested.remaining_qty &&
                   a.order_rested.side == b.order_rested.side;
        case EventType::OrderCancelled:
            return a.order_cancelled.order_id == b.order_cancelled.order_id &&
                   a.order_cancelled.price_tick == b.order_cancelled.price_tick &&
                   a.order_cancelled.cancelled_qty == b.order_cancelled.cancelled_qty &&
                   a.order_cancelled.side == b.order_cancelled.side;
        case EventType::CancelRejected:
            return a.cancel_rejected.order_id == b.cancel_rejected.order_id &&
                   a.cancel_rejected.reason == b.cancel_rejected.reason;
        case EventType::OrderRejected:
            return a.order_rejected.order_id == b.order_rejected.order_id &&
                   a.order_rejected.reason == b.order_rejected.reason;
        case EventType::ResidualCancelled:
            return a.residual_cancelled.order_id == b.residual_cancelled.order_id &&
                   a.residual_cancelled.cancelled_qty == b.residual_cancelled.cancelled_qty &&
                   a.residual_cancelled.side == b.residual_cancelled.side;
        case EventType::SelfTradePrevented:
            return a.self_trade_prevented.taker_order_id == b.self_trade_prevented.taker_order_id &&
                   a.self_trade_prevented.maker_order_id == b.self_trade_prevented.maker_order_id &&
                   a.self_trade_prevented.cancelled_qty == b.self_trade_prevented.cancelled_qty &&
                   a.self_trade_prevented.side == b.self_trade_prevented.side;
        case EventType::CapacityExhausted:
            return a.capacity_exhausted.order_id == b.capacity_exhausted.order_id &&
                   a.capacity_exhausted.unrested_qty == b.capacity_exhausted.unrested_qty &&
                   a.capacity_exhausted.side == b.capacity_exhausted.side;
    }
    return false;
}

inline std::string format_event(const EngineEvent& ev) {
    std::ostringstream oss;
    oss << "[" << to_string(ev.type) << "] ";
    switch (ev.type) {
        case EventType::Trade:
            oss << "seq=" << ev.trade.trade_seq
                << " maker=" << ev.trade.maker_order_id
                << " taker=" << ev.trade.taker_order_id
                << " price=" << ev.trade.price_tick
                << " qty=" << ev.trade.quantity
                << " maker_side=" << to_string(ev.trade.maker_side);
            break;
        case EventType::OrderRested:
            oss << "id=" << ev.order_rested.order_id
                << " owner=" << ev.order_rested.owner_id
                << " price=" << ev.order_rested.price_tick
                << " qty=" << ev.order_rested.remaining_qty
                << " side=" << to_string(ev.order_rested.side);
            break;
        case EventType::OrderCancelled:
            oss << "id=" << ev.order_cancelled.order_id
                << " price=" << ev.order_cancelled.price_tick
                << " cancelled=" << ev.order_cancelled.cancelled_qty
                << " side=" << to_string(ev.order_cancelled.side);
            break;
        case EventType::CancelRejected:
            oss << "id=" << ev.cancel_rejected.order_id
                << " reason=" << to_string(ev.cancel_rejected.reason);
            break;
        case EventType::OrderRejected:
            oss << "id=" << ev.order_rejected.order_id
                << " reason=" << to_string(ev.order_rejected.reason);
            break;
        case EventType::ResidualCancelled:
            oss << "id=" << ev.residual_cancelled.order_id
                << " cancelled=" << ev.residual_cancelled.cancelled_qty
                << " side=" << to_string(ev.residual_cancelled.side);
            break;
        case EventType::SelfTradePrevented:
            oss << "taker=" << ev.self_trade_prevented.taker_order_id
                << " maker=" << ev.self_trade_prevented.maker_order_id
                << " cancelled=" << ev.self_trade_prevented.cancelled_qty
                << " side=" << to_string(ev.self_trade_prevented.side);
            break;
        case EventType::CapacityExhausted:
            oss << "id=" << ev.capacity_exhausted.order_id
                << " unrested=" << ev.capacity_exhausted.unrested_qty
                << " side=" << to_string(ev.capacity_exhausted.side);
            break;
    }
    return oss.str();
}

} // namespace nexusmatch::test
