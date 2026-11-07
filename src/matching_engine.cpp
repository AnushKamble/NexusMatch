#include "nexusmatch/matching_engine.hpp"
#include <algorithm>

namespace nexusmatch {

MatchingEngine::MatchingEngine(const EngineConfig& config)
    : config_(config),
      book_(config)
{
    std::string err;
    if (!config.validate(&err)) {
        throw std::invalid_argument("Invalid EngineConfig: " + err);
    }
}

void MatchingEngine::process(const OrderCommand& cmd, IEventSink& sink) noexcept {
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
            record_rejected(cmd.side, cmd.quantity);
            break;
    }
}

void MatchingEngine::process_limit(const OrderCommand& cmd, IEventSink& sink) noexcept {
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
    if (book_.id_index().find(cmd.order_id) != kInvalidIndex) {
        sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::DuplicateOrderId));
        record_rejected(cmd.side, cmd.quantity);
        return;
    }

    std::uint32_t remaining = cmd.quantity;

    if (cmd.side == Side::Buy) {
        while (remaining > 0) {
            std::uint32_t best_ask = book_.ask_bitmap().find_min();
            if (best_ask == kInvalidIndex || best_ask > cmd.price_tick) {
                break;
            }

            PriceLevel& level = book_.ladder().level(Side::Sell, best_ask);
            std::uint32_t maker_idx = level.head_index;
            assert(maker_idx != kInvalidIndex);
            OrderNode& maker = book_.arena()[maker_idx];

            // Self-Trade Prevention (Cancel Aggressor Remainder)
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
            level.total_quantity -= fill;

            record_execution(Side::Buy, Side::Sell, fill);
            sink.emit(EngineEvent::make_trade(++trade_seq_, maker.order_id, cmd.order_id, best_ask, fill, Side::Sell));
            ++stats_.total_trades;

            if (maker.remaining_qty == 0) {
                IntrusiveQueue::unlink(level, book_.arena(), maker_idx);
                bool erased = book_.id_index().erase(maker.order_id);
                (void)erased;
                assert(erased);
                book_.arena().release(maker_idx);

                if (level.is_empty()) {
                    book_.ask_bitmap().clear(best_ask);
                }
            }
        }
    } else { // Sell
        while (remaining > 0) {
            std::uint32_t best_bid = book_.bid_bitmap().find_max();
            if (best_bid == kInvalidIndex || best_bid < cmd.price_tick) {
                break;
            }

            PriceLevel& level = book_.ladder().level(Side::Buy, best_bid);
            std::uint32_t maker_idx = level.head_index;
            assert(maker_idx != kInvalidIndex);
            OrderNode& maker = book_.arena()[maker_idx];

            // Self-Trade Prevention (Cancel Aggressor Remainder)
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
            level.total_quantity -= fill;

            record_execution(Side::Sell, Side::Buy, fill);
            sink.emit(EngineEvent::make_trade(++trade_seq_, maker.order_id, cmd.order_id, best_bid, fill, Side::Buy));
            ++stats_.total_trades;

            if (maker.remaining_qty == 0) {
                IntrusiveQueue::unlink(level, book_.arena(), maker_idx);
                bool erased = book_.id_index().erase(maker.order_id);
                (void)erased;
                assert(erased);
                book_.arena().release(maker_idx);

                if (level.is_empty()) {
                    book_.bid_bitmap().clear(best_bid);
                }
            }
        }
    }

    if (remaining > 0) {
        if (book_.arena().is_full()) {
            if (remaining == cmd.quantity) {
                sink.emit(EngineEvent::make_order_rejected(cmd.order_id, OrderRejectReason::CapacityExhausted));
                record_rejected(cmd.side, remaining);
            } else {
                sink.emit(EngineEvent::make_capacity_exhausted(cmd.order_id, remaining, cmd.side));
                sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, cmd.side));
                record_cancelled(cmd.side, remaining);
            }
            return;
        }

        std::uint32_t node_idx = book_.arena().allocate();
        assert(node_idx != kInvalidIndex);

        OrderNode& node = book_.arena()[node_idx];
        node.order_id = cmd.order_id;
        node.price_tick = cmd.price_tick;
        node.remaining_qty = remaining;
        node.owner_id = cmd.owner_id;
        node.side = static_cast<std::uint8_t>(cmd.side);
        node.state = static_cast<std::uint8_t>(OrderNodeState::Active);

        IndexInsertResult insert_res = book_.id_index().insert(cmd.order_id, node_idx);
        assert(insert_res == IndexInsertResult::Success);
        (void)insert_res;

        PriceLevel& level = book_.ladder().level(cmd.side, cmd.price_tick);
        bool was_empty = level.is_empty();
        IntrusiveQueue::append(level, book_.arena(), node_idx);

        if (was_empty) {
            if (cmd.side == Side::Buy) {
                book_.bid_bitmap().set(cmd.price_tick);
            } else {
                book_.ask_bitmap().set(cmd.price_tick);
            }
        }

        record_resting(cmd.side, remaining);
        sink.emit(EngineEvent::make_order_rested(cmd.order_id, cmd.owner_id, cmd.price_tick, remaining, cmd.side));
        ++stats_.total_orders_accepted;
    }
}

void MatchingEngine::process_market(const OrderCommand& cmd, IEventSink& sink) noexcept {
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
        while (remaining > 0) {
            std::uint32_t best_ask = book_.ask_bitmap().find_min();
            if (best_ask == kInvalidIndex) {
                break;
            }

            PriceLevel& level = book_.ladder().level(Side::Sell, best_ask);
            std::uint32_t maker_idx = level.head_index;
            assert(maker_idx != kInvalidIndex);
            OrderNode& maker = book_.arena()[maker_idx];

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
            level.total_quantity -= fill;

            record_execution(Side::Buy, Side::Sell, fill);
            sink.emit(EngineEvent::make_trade(++trade_seq_, maker.order_id, cmd.order_id, best_ask, fill, Side::Sell));
            ++stats_.total_trades;

            if (maker.remaining_qty == 0) {
                IntrusiveQueue::unlink(level, book_.arena(), maker_idx);
                bool erased = book_.id_index().erase(maker.order_id);
                (void)erased;
                assert(erased);
                book_.arena().release(maker_idx);

                if (level.is_empty()) {
                    book_.ask_bitmap().clear(best_ask);
                }
            }
        }
    } else { // Sell
        while (remaining > 0) {
            std::uint32_t best_bid = book_.bid_bitmap().find_max();
            if (best_bid == kInvalidIndex) {
                break;
            }

            PriceLevel& level = book_.ladder().level(Side::Buy, best_bid);
            std::uint32_t maker_idx = level.head_index;
            assert(maker_idx != kInvalidIndex);
            OrderNode& maker = book_.arena()[maker_idx];

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
            level.total_quantity -= fill;

            record_execution(Side::Sell, Side::Buy, fill);
            sink.emit(EngineEvent::make_trade(++trade_seq_, maker.order_id, cmd.order_id, best_bid, fill, Side::Buy));
            ++stats_.total_trades;

            if (maker.remaining_qty == 0) {
                IntrusiveQueue::unlink(level, book_.arena(), maker_idx);
                bool erased = book_.id_index().erase(maker.order_id);
                (void)erased;
                assert(erased);
                book_.arena().release(maker_idx);

                if (level.is_empty()) {
                    book_.bid_bitmap().clear(best_bid);
                }
            }
        }
    }

    if (remaining > 0) {
        sink.emit(EngineEvent::make_residual_cancelled(cmd.order_id, remaining, cmd.side));
        record_cancelled(cmd.side, remaining);
    }
}

void MatchingEngine::process_cancel(const OrderCommand& cmd, IEventSink& sink) noexcept {
    if (cmd.order_id == 0) {
        sink.emit(EngineEvent::make_cancel_rejected(cmd.order_id, CancelRejectReason::ZeroOrderId));
        ++stats_.total_cancels_rejected;
        return;
    }

    std::uint32_t node_idx = book_.id_index().find(cmd.order_id);
    if (node_idx == kInvalidIndex) {
        sink.emit(EngineEvent::make_cancel_rejected(cmd.order_id, CancelRejectReason::OrderNotFound));
        ++stats_.total_cancels_rejected;
        return;
    }

    const OrderNode& node = book_.arena()[node_idx];
    Side side = static_cast<Side>(node.side);
    std::uint32_t price = node.price_tick;
    std::uint32_t cancelled_qty = node.remaining_qty;

    PriceLevel& level = book_.ladder().level(side, price);
    IntrusiveQueue::unlink(level, book_.arena(), node_idx);

    if (level.is_empty()) {
        if (side == Side::Buy) {
            book_.bid_bitmap().clear(price);
        } else {
            book_.ask_bitmap().clear(price);
        }
    }

    bool erased = book_.id_index().erase(cmd.order_id);
    (void)erased;
    assert(erased);
    book_.arena().release(node_idx);

    record_cancel_resting(side, cancelled_qty);
    sink.emit(EngineEvent::make_order_cancelled(cmd.order_id, price, cancelled_qty, side));
    ++stats_.total_cancels_processed;
}

void MatchingEngine::record_execution(Side taker_side, Side /*maker_side*/, std::uint32_t qty) noexcept {
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

void MatchingEngine::record_resting(Side side, std::uint32_t qty) noexcept {
    if (side == Side::Buy) {
        stats_.buy_volume.accepted_quantity += qty;
        stats_.buy_volume.resting_quantity += qty;
    } else {
        stats_.sell_volume.accepted_quantity += qty;
        stats_.sell_volume.resting_quantity += qty;
    }
}

void MatchingEngine::record_cancelled(Side side, std::uint32_t qty) noexcept {
    if (side == Side::Buy) {
        stats_.buy_volume.accepted_quantity += qty;
        stats_.buy_volume.cancelled_quantity += qty;
    } else {
        stats_.sell_volume.accepted_quantity += qty;
        stats_.sell_volume.cancelled_quantity += qty;
    }
}

void MatchingEngine::record_cancel_resting(Side side, std::uint32_t qty) noexcept {
    if (side == Side::Buy) {
        stats_.buy_volume.resting_quantity -= qty;
        stats_.buy_volume.cancelled_quantity += qty;
    } else {
        stats_.sell_volume.resting_quantity -= qty;
        stats_.sell_volume.cancelled_quantity += qty;
    }
}

void MatchingEngine::record_rejected(Side side, std::uint32_t qty) noexcept {
    if (side == Side::Buy) {
        stats_.buy_volume.rejected_quantity += qty;
    } else {
        stats_.sell_volume.rejected_quantity += qty;
    }
    ++stats_.total_orders_rejected;
}

bool MatchingEngine::validate_invariants() const noexcept {
    if (!book_.validate_invariants()) return false;
    if (!stats_.buy_volume.is_conserved()) return false;
    if (!stats_.sell_volume.is_conserved()) return false;
    if (stats_.buy_volume.executed_quantity != stats_.sell_volume.executed_quantity) return false;
    return true;
}

} // namespace nexusmatch
