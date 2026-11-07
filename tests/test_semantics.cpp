#include "reference_engine.hpp"
#include "test_utils.hpp"
#include <cassert>
#include <iostream>

using namespace nexusmatch;
using namespace nexusmatch::test;

void test_passive_limit_rests() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    OrderCommand cmd = OrderCommand::limit(Side::Buy, 101, 1, 100, 50);
    engine.process(cmd, sink);

    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderRested);
    TEST_ASSERT(sink.events[0].order_rested.order_id == 101);
    TEST_ASSERT(sink.events[0].order_rested.price_tick == 100);
    TEST_ASSERT(sink.events[0].order_rested.remaining_qty == 50);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(bbo.bid.valid);
    TEST_ASSERT(bbo.bid.price_tick == 100);
    TEST_ASSERT(bbo.bid.quantity == 50);
    TEST_ASSERT(!bbo.ask.valid);
    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}

void test_cross_one_order_maker_price() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // Maker rests Ask at 105 for qty 100
    engine.process(OrderCommand::limit(Side::Sell, 201, 1, 105, 100), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderRested);

    sink.clear();
    // Taker Buys at 110 for qty 40 (crosses at maker price 105)
    engine.process(OrderCommand::limit(Side::Buy, 202, 2, 110, 40), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.trade_seq == 1);
    TEST_ASSERT(sink.events[0].trade.maker_order_id == 201);
    TEST_ASSERT(sink.events[0].trade.taker_order_id == 202);
    TEST_ASSERT(sink.events[0].trade.price_tick == 105); // Maker price!
    TEST_ASSERT(sink.events[0].trade.quantity == 40);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(bbo.ask.valid);
    TEST_ASSERT(bbo.ask.price_tick == 105);
    TEST_ASSERT(bbo.ask.quantity == 60); // 100 - 40 = 60
    TEST_ASSERT(!bbo.bid.valid);

    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
    TEST_ASSERT(engine.stats().buy_volume.executed_quantity == 40);
    TEST_ASSERT(engine.stats().sell_volume.executed_quantity == 40);
}

void test_same_price_fifo() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // Three maker asks at 100
    engine.process(OrderCommand::limit(Side::Sell, 301, 1, 100, 30), sink);
    engine.process(OrderCommand::limit(Side::Sell, 302, 2, 100, 40), sink);
    engine.process(OrderCommand::limit(Side::Sell, 303, 3, 100, 50), sink);

    sink.clear();
    // Taker buys 50 at 100: should fill all 30 of 301, and 20 of 302 (FIFO order)
    engine.process(OrderCommand::limit(Side::Buy, 304, 4, 100, 50), sink);

    TEST_ASSERT(sink.events.size() == 2);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.maker_order_id == 301);
    TEST_ASSERT(sink.events[0].trade.quantity == 30);

    TEST_ASSERT(sink.events[1].type == EventType::Trade);
    TEST_ASSERT(sink.events[1].trade.maker_order_id == 302);
    TEST_ASSERT(sink.events[1].trade.quantity == 20);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(bbo.ask.valid);
    TEST_ASSERT(bbo.ask.price_tick == 100);
    TEST_ASSERT(bbo.ask.quantity == (20 + 50)); // 302 has 20 left, 303 has 50 left
    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}

void test_partial_taker_resting_residual() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // Maker Ask at 100 for 25
    engine.process(OrderCommand::limit(Side::Sell, 401, 1, 100, 25), sink);

    sink.clear();
    // Taker Buy at 100 for 60 -> fills 25, residuals 35 rests at 100
    engine.process(OrderCommand::limit(Side::Buy, 402, 2, 100, 60), sink);

    TEST_ASSERT(sink.events.size() == 2);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.maker_order_id == 401);
    TEST_ASSERT(sink.events[0].trade.quantity == 25);
    TEST_ASSERT(sink.events[1].type == EventType::OrderRested);
    TEST_ASSERT(sink.events[1].order_rested.order_id == 402);
    TEST_ASSERT(sink.events[1].order_rested.remaining_qty == 35);
    TEST_ASSERT(sink.events[1].order_rested.price_tick == 100);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(!bbo.ask.valid);
    TEST_ASSERT(bbo.bid.valid);
    TEST_ASSERT(bbo.bid.price_tick == 100);
    TEST_ASSERT(bbo.bid.quantity == 35);

    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}

void test_market_order_sweep_and_cancel_residual() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // Asks at 101 (qty 20) and 102 (qty 30)
    engine.process(OrderCommand::limit(Side::Sell, 501, 1, 101, 20), sink);
    engine.process(OrderCommand::limit(Side::Sell, 502, 2, 102, 30), sink);

    sink.clear();
    // Market Buy of 70: fills 20 at 101, 30 at 102, residual 20 cancelled
    engine.process(OrderCommand::market(Side::Buy, 503, 3, 70), sink);

    TEST_ASSERT(sink.events.size() == 3);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.price_tick == 101);
    TEST_ASSERT(sink.events[0].trade.quantity == 20);

    TEST_ASSERT(sink.events[1].type == EventType::Trade);
    TEST_ASSERT(sink.events[1].trade.price_tick == 102);
    TEST_ASSERT(sink.events[1].trade.quantity == 30);

    TEST_ASSERT(sink.events[2].type == EventType::ResidualCancelled);
    TEST_ASSERT(sink.events[2].residual_cancelled.order_id == 503);
    TEST_ASSERT(sink.events[2].residual_cancelled.cancelled_qty == 20);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(!bbo.ask.valid);
    TEST_ASSERT(!bbo.bid.valid);

    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}

void test_cancel_operations() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // Insert 3 bids at 50
    engine.process(OrderCommand::limit(Side::Buy, 601, 1, 50, 10), sink);
    engine.process(OrderCommand::limit(Side::Buy, 602, 2, 50, 20), sink);
    engine.process(OrderCommand::limit(Side::Buy, 603, 3, 50, 30), sink);

    // Cancel middle (602)
    sink.clear();
    engine.process(OrderCommand::cancel(602), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderCancelled);
    TEST_ASSERT(sink.events[0].order_cancelled.order_id == 602);
    TEST_ASSERT(sink.events[0].order_cancelled.cancelled_qty == 20);
    TEST_ASSERT(engine.bbo().bid.quantity == 40);

    // Cancel head (601)
    sink.clear();
    engine.process(OrderCommand::cancel(601), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderCancelled);
    TEST_ASSERT(sink.events[0].order_cancelled.order_id == 601);
    TEST_ASSERT(engine.bbo().bid.quantity == 30);

    // Cancel tail / sole (603)
    sink.clear();
    engine.process(OrderCommand::cancel(603), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderCancelled);
    TEST_ASSERT(!engine.bbo().bid.valid);

    // Cancel unknown
    sink.clear();
    engine.process(OrderCommand::cancel(9999), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::CancelRejected);
    TEST_ASSERT(sink.events[0].cancel_rejected.reason == CancelRejectReason::OrderNotFound);

    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}

void test_self_trade_prevention() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // Resting maker owned by owner 10
    engine.process(OrderCommand::limit(Side::Sell, 701, 10, 100, 50), sink);

    sink.clear();
    // Aggressor from owner 10 arrives (Buy 100, qty 30) -> STP triggers immediately
    engine.process(OrderCommand::limit(Side::Buy, 702, 10, 100, 30), sink);

    TEST_ASSERT(sink.events.size() == 2);
    TEST_ASSERT(sink.events[0].type == EventType::SelfTradePrevented);
    TEST_ASSERT(sink.events[0].self_trade_prevented.taker_order_id == 702);
    TEST_ASSERT(sink.events[0].self_trade_prevented.maker_order_id == 701);
    TEST_ASSERT(sink.events[0].self_trade_prevented.cancelled_qty == 30);
    TEST_ASSERT(sink.events[1].type == EventType::ResidualCancelled);

    // Resting maker 701 must remain intact with 50 qty!
    Bbo bbo = engine.bbo();
    TEST_ASSERT(bbo.ask.valid);
    TEST_ASSERT(bbo.ask.quantity == 50);

    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}

void test_self_trade_after_external_fill() {
    ReferenceEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    // External maker at 99 (owner 1, qty 20)
    engine.process(OrderCommand::limit(Side::Sell, 801, 1, 99, 20), sink);
    // Same-owner maker at 100 (owner 2, qty 50)
    engine.process(OrderCommand::limit(Side::Sell, 802, 2, 100, 50), sink);

    sink.clear();
    // Aggressor from owner 2 Buys at 100 for qty 40:
    // Should fill 20 against 801 at 99, then encounter 802 (same owner) at 100 and cancel remaining 20
    engine.process(OrderCommand::limit(Side::Buy, 803, 2, 100, 40), sink);

    TEST_ASSERT(sink.events.size() == 3);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.maker_order_id == 801);
    TEST_ASSERT(sink.events[0].trade.quantity == 20);
    TEST_ASSERT(sink.events[0].trade.price_tick == 99);

    TEST_ASSERT(sink.events[1].type == EventType::SelfTradePrevented);
    TEST_ASSERT(sink.events[1].self_trade_prevented.taker_order_id == 803);
    TEST_ASSERT(sink.events[1].self_trade_prevented.maker_order_id == 802);
    TEST_ASSERT(sink.events[1].self_trade_prevented.cancelled_qty == 20);

    TEST_ASSERT(sink.events[2].type == EventType::ResidualCancelled);
    TEST_ASSERT(sink.events[2].residual_cancelled.cancelled_qty == 20);

    // 802 is untouched
    TEST_ASSERT(engine.bbo().ask.valid);
    TEST_ASSERT(engine.bbo().ask.price_tick == 100);
    TEST_ASSERT(engine.bbo().ask.quantity == 50);

    TEST_ASSERT(engine.stats().buy_volume.is_conserved());
    TEST_ASSERT(engine.stats().sell_volume.is_conserved());
}


int main() {
    std::cout << "[NexusMatch Reference Engine Semantics Tests] Running..." << std::endl;
    test_passive_limit_rests();
    test_cross_one_order_maker_price();
    test_same_price_fifo();
    test_partial_taker_resting_residual();
    test_market_order_sweep_and_cancel_residual();
    test_cancel_operations();
    test_self_trade_prevention();
    test_self_trade_after_external_fill();
    std::cout << "[NexusMatch Reference Engine Semantics Tests] All 8 suites passed successfully!" << std::endl;
    return 0;
}
