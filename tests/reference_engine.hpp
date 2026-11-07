#pragma once

#include "nexusmatch/types.hpp"
#include "nexusmatch/config.hpp"
#include "nexusmatch/event.hpp"
#include "nexusmatch/event_sink.hpp"
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include <algorithm>

namespace nexusmatch::test {

struct ReferenceOrder {
    std::uint64_t order_id;
    std::uint32_t owner_id;
    Side side;
    std::uint32_t price_tick;
    std::uint32_t remaining_qty;
};

class ReferenceEngine {
public:
    explicit ReferenceEngine(EngineConfig config) : config_(config) {}

    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }

    void process(const OrderCommand& cmd, IEventSink& sink) {
        switch (cmd.kind) {
            case OrderKind::Limit:
                process_limit(cmd, sink);
                break;
            case OrderKind::Market:
                process_market(cmd, sink);
                break;
            case OrderKind::Cancel:
                process_cancel(cmd, sink);
                break;
            default:
                sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::InvalidEnum));
                ++stats_.total_orders_rejected;
                break;
        }
    }

    [[nodiscard]] Bbo bbo() const noexcept {
        Bbo result;
        if (!bids_.empty()) {
            auto it = bids_.begin();
            result.bid.valid = true;
            result.bid.price_tick = it->first;
            result.bid.quantity = 0;
            for (const auto& ord : it->second) {
                result.bid.quantity += ord.remaining_qty;
            }
        }
        if (!asks_.empty()) {
            auto it = asks_.begin();
            result.ask.valid = true;
            result.ask.price_tick = it->first;
            result.ask.quantity = 0;
            for (const auto& ord : it->second) {
                result.ask.quantity += ord.remaining_qty;
            }
        }
        return result;
    }

    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }

    [[nodiscard]] std::size_t active_orders_count() const noexcept {
        return active_orders_.size();
    }

    // Canonical representation for state hash / differential snapshot
    [[nodiscard]] std::vector<ReferenceOrder> canonical_orders() const {
        std::vector<ReferenceOrder> orders;
        // Bids: descending by price, FIFO order
        for (const auto& [price, queue] : bids_) {
            for (const auto& ord : queue) {
                orders.push_back(ord);
            }
        }
        // Asks: ascending by price, FIFO order
        for (const auto& [price, queue] : asks_) {
            for (const auto& ord : queue) {
                orders.push_back(ord);
            }
        }
        return orders;
    }

private:
    void process_limit(const OrderCommand& cmd, IEventSink& sink) {
        // Validation
        if (cmd.order_id == 0) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::ZeroOrderId));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }
        if (cmd.quantity == 0) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::ZeroQuantity));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }
        if (config_.reject_tick_zero && cmd.price_tick == 0) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::InvalidPriceTick));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }
        if (cmd.price_tick >= config_.price_tick_count) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::InvalidPriceTick));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }
        if (active_orders_.contains(cmd.order_id)) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::DuplicateOrderId));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }

        std::uint32_t remaining = cmd.quantity;

        // Matching
        if (cmd.side == Side::Buy) {
            while (remaining > 0 && !asks_.empty()) {
                auto best_ask_it = asks_.begin();
                std::uint32_t ask_price = best_ask_it->first;
                if (ask_price > cmd.price_tick) {
                    break; // Price no longer crosses
                }

                auto& queue = best_ask_it->second;
                auto& maker = queue.front();

                // Self-trade prevention (Cancel Aggressor Remainder)
                if (maker.owner_id == cmd.owner_id) {
                    sink.emit(EngineEvent::make_self_trade_prevented(cmd.order_id, maker.order_id, remaining, Side::Buy));
                    sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, Side::Buy));
                    record_cancelled(Side::Buy, remaining);
                    ++stats_.total_self_trades_prevented;
                    remaining = 0;
                    break;
                }

                std::uint32_t fill = std::min(remaining, maker.remaining_qty);
                remaining -= fill;
                maker.remaining_qty -= fill;

                record_execution(Side::Buy, Side::Sell, fill);
                sink.emit(EngineEvent::make_trade(++stats_.total_trades, maker.order_id, cmd.order_id, ask_price, fill, Side::Sell));

                if (maker.remaining_qty == 0) {
                    active_orders_.erase(maker.order_id);
                    queue.pop_front();
                    if (queue.empty()) {
                        asks_.erase(best_ask_it);
                    }
                }
            }
        } else { // Sell
            while (remaining > 0 && !bids_.empty()) {
                auto best_bid_it = bids_.begin();
                std::uint32_t bid_price = best_bid_it->first;
                if (bid_price < cmd.price_tick) {
                    break; // Price no longer crosses
                }

                auto& queue = best_bid_it->second;
                auto& maker = queue.front();

                // Self-trade prevention (Cancel Aggressor Remainder)
                if (maker.owner_id == cmd.owner_id) {
                    sink.emit(EngineEvent::make_self_trade_prevented(cmd.order_id, maker.order_id, remaining, Side::Sell));
                    sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, Side::Sell));
                    record_cancelled(Side::Sell, remaining);
                    ++stats_.total_self_trades_prevented;
                    remaining = 0;
                    break;
                }

                std::uint32_t fill = std::min(remaining, maker.remaining_qty);
                remaining -= fill;
                maker.remaining_qty -= fill;

                record_execution(Side::Sell, Side::Buy, fill);
                sink.emit(EngineEvent::make_trade(++stats_.total_trades, maker.order_id, cmd.order_id, bid_price, fill, Side::Buy));

                if (maker.remaining_qty == 0) {
                    active_orders_.erase(maker.order_id);
                    queue.pop_front();
                    if (queue.empty()) {
                        bids_.erase(best_bid_it);
                    }
                }
            }
        }

        // Rest residual if any
        if (remaining > 0) {
            // Check capacity
            if (active_orders_.size() >= config_.max_live_orders) {
                // If no fills occurred, reject as capacity exhausted
                if (remaining == cmd.quantity) {
                    sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::CapacityExhausted));
                    record_rejected(cmd.side, remaining);
                } else {
                    // Executed partial, cancel residual due to capacity
                    sink.emit(EngineEvent::make_capacity_exhausted(cmd.order_id, remaining, cmd.side));
                    sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, cmd.side));
                    record_cancelled(cmd.side, remaining);
                }
                return;
            }

            ReferenceOrder rest_order{
                .order_id = cmd.order_id,
                .owner_id = cmd.owner_id,
                .side = cmd.side,
                .price_tick = cmd.price_tick,
                .remaining_qty = remaining
            };

            if (cmd.side == Side::Buy) {
                bids_[cmd.price_tick].push_back(rest_order);
            } else {
                asks_[cmd.price_tick].push_back(rest_order);
            }
            active_orders_[cmd.order_id] = cmd.price_tick;
            record_resting(cmd.side, remaining);
            sink.emit(EngineEvent::make_order_rested(cmd.order_id, cmd.owner_id, cmd.price_tick, remaining, cmd.side));
            ++stats_.total_orders_accepted;
        }
    }

    void process_market(const OrderCommand& cmd, IEventSink& sink) {
        if (cmd.order_id == 0) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::ZeroOrderId));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }
        if (cmd.quantity == 0) {
            sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::ZeroQuantity));
            record_rejected(cmd.side, cmd.quantity);
            return;
        }

        std::uint32_t remaining = cmd.quantity;

        if (cmd.side == Side::Buy) {
            while (remaining > 0 && !asks_.empty()) {
                auto best_ask_it = asks_.begin();
                std::uint32_t ask_price = best_ask_it->first;
                auto& queue = best_ask_it->second;
                auto& maker = queue.front();

                if (maker.owner_id == cmd.owner_id) {
                    sink.emit(EngineEvent::make_self_trade_prevented(cmd.order_id, maker.order_id, remaining, Side::Buy));
                    sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, Side::Buy));
                    record_cancelled(Side::Buy, remaining);
                    ++stats_.total_self_trades_prevented;
                    remaining = 0;
                    break;
                }

                std::uint32_t fill = std::min(remaining, maker.remaining_qty);
                remaining -= fill;
                maker.remaining_qty -= fill;

                record_execution(Side::Buy, Side::Sell, fill);
                sink.emit(EngineEvent::make_trade(++stats_.total_trades, maker.order_id, cmd.order_id, ask_price, fill, Side::Sell));

                if (maker.remaining_qty == 0) {
                    active_orders_.erase(maker.order_id);
                    queue.pop_front();
                    if (queue.empty()) {
                        asks_.erase(best_ask_it);
                    }
                }
            }
        } else { // Sell
            while (remaining > 0 && !bids_.empty()) {
                auto best_bid_it = bids_.begin();
                std::uint32_t bid_price = best_bid_it->first;
                auto& queue = best_bid_it->second;
                auto& maker = queue.front();

                if (maker.owner_id == cmd.owner_id) {
                    sink.emit(EngineEvent::make_self_trade_prevented(cmd.order_id, maker.order_id, remaining, Side::Sell));
                    sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, Side::Sell));
                    record_cancelled(Side::Sell, remaining);
                    ++stats_.total_self_trades_prevented;
                    remaining = 0;
                    break;
                }

                std::uint32_t fill = std::min(remaining, maker.remaining_qty);
                remaining -= fill;
                maker.remaining_qty -= fill;

                record_execution(Side::Sell, Side::Buy, fill);
                sink.emit(EngineEvent::make_trade(++stats_.total_trades, maker.order_id, cmd.order_id, bid_price, fill, Side::Buy));

                if (maker.remaining_qty == 0) {
                    active_orders_.erase(maker.order_id);
                    queue.pop_front();
                    if (queue.empty()) {
                        bids_.erase(best_bid_it);
                    }
                }
            }
        }

        // Market order never rests; cancel remainder
        if (remaining > 0) {
            sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, cmd.side));
            record_cancelled(cmd.side, remaining);
        }
    }

    void process_cancel(const OrderCommand& cmd, IEventSink& sink) {
        if (cmd.order_id == 0) {
            sink.emit(EngineEvent::make_cancel_rejected(cmd.order_id, CancelRejectReason::ZeroOrderId));
            ++stats_.total_cancels_rejected;
            return;
        }

        auto it = active_orders_.find(cmd.order_id);
        if (it == active_orders_.end()) {
            sink.emit(EngineEvent::make_cancel_rejected(cmd.order_id, CancelRejectReason::OrderNotFound));
            ++stats_.total_cancels_rejected;
            return;
        }

        std::uint32_t price = it->second;
        // Search in bids or asks
        bool found = false;
        if (auto bid_it = bids_.find(price); bid_it != bids_.end()) {
            auto& queue = bid_it->second;
            for (auto q_it = queue.begin(); q_it != queue.end(); ++q_it) {
                if (q_it->order_id == cmd.order_id) {
                    std::uint32_t qty = q_it->remaining_qty;
                    sink.emit(EngineEvent::make_order_cancelled(cmd.order_id, price, qty, Side::Buy));
                    record_cancel_resting(Side::Buy, qty);
                    queue.erase(q_it);
                    if (queue.empty()) {
                        bids_.erase(bid_it);
                    }
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            if (auto ask_it = asks_.find(price); ask_it != asks_.end()) {
                auto& queue = ask_it->second;
                for (auto q_it = queue.begin(); q_it != queue.end(); ++q_it) {
                    if (q_it->order_id == cmd.order_id) {
                        std::uint32_t qty = q_it->remaining_qty;
                        sink.emit(EngineEvent::make_order_cancelled(cmd.order_id, price, qty, Side::Sell));
                        record_cancel_resting(Side::Sell, qty);
                        queue.erase(q_it);
                        if (queue.empty()) {
                            asks_.erase(ask_it);
                        }
                        found = true;
                        break;
                    }
                }
            }
        }

        active_orders_.erase(it);
        ++stats_.total_cancels_processed;
    }

    void record_execution(Side taker_side, Side /*maker_side*/, std::uint32_t qty) {
        if (taker_side == Side::Buy) {
            stats_.buy_volume.accepted_quantity += qty;
            stats_.buy_volume.executed_quantity += qty;
            stats_.sell_volume.executed_quantity += qty;
            stats_.sell_volume.resting_quantity -= qty;
        } else {
            stats_.sell_volume.accepted_quantity += qty;
            stats_.sell_volume.executed_quantity += qty;
            stats_.buy_volume.executed_quantity += qty;
            stats_.buy_volume.resting_quantity -= qty;
        }
    }

    void record_resting(Side side, std::uint32_t qty) {
        if (side == Side::Buy) {
            stats_.buy_volume.accepted_quantity += qty;
            stats_.buy_volume.resting_quantity += qty;
        } else {
            stats_.sell_volume.accepted_quantity += qty;
            stats_.sell_volume.resting_quantity += qty;
        }
    }

    void record_cancelled(Side side, std::uint32_t qty) {
        if (side == Side::Buy) {
            stats_.buy_volume.accepted_quantity += qty;
            stats_.buy_volume.cancelled_quantity += qty;
        } else {
            stats_.sell_volume.accepted_quantity += qty;
            stats_.sell_volume.cancelled_quantity += qty;
        }
    }

    void record_cancel_resting(Side side, std::uint32_t qty) {
        if (side == Side::Buy) {
            stats_.buy_volume.resting_quantity -= qty;
            stats_.buy_volume.cancelled_quantity += qty;
        } else {
            stats_.sell_volume.resting_quantity -= qty;
            stats_.sell_volume.cancelled_quantity += qty;
        }
    }

    void record_rejected(Side side, std::uint32_t qty) {
        if (side == Side::Buy) {
            stats_.buy_volume.rejected_quantity += qty;
        } else {
            stats_.sell_volume.rejected_quantity += qty;
        }
        ++stats_.total_orders_rejected;
    }

    EngineConfig config_;
    std::map<std::uint32_t, std::deque<ReferenceOrder>, std::greater<std::uint32_t>> bids_;
    std::map<std::uint32_t, std::deque<ReferenceOrder>, std::less<std::uint32_t>> asks_;
    std::unordered_map<std::uint64_t, std::uint32_t> active_orders_;
    EngineStats stats_{};
};

} // namespace nexusmatch::test
