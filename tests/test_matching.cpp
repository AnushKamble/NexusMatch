#include "nexusmatch/matching_engine.hpp"
#include "test_utils.hpp"
#include <iostream>

using namespace nexusmatch;
using namespace nexusmatch::test;

void test_engine_passive_limit() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Buy, 101, 1, 100, 50), sink);

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
    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_cross_maker_price() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Sell, 201, 1, 105, 100), sink);
    sink.clear();

    engine.process(OrderCommand::limit(Side::Buy, 202, 2, 110, 40), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.trade_seq == 1);
    TEST_ASSERT(sink.events[0].trade.maker_order_id == 201);
    TEST_ASSERT(sink.events[0].trade.taker_order_id == 202);
    TEST_ASSERT(sink.events[0].trade.price_tick == 105);
    TEST_ASSERT(sink.events[0].trade.quantity == 40);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(bbo.ask.valid);
    TEST_ASSERT(bbo.ask.price_tick == 105);
    TEST_ASSERT(bbo.ask.quantity == 60);
    TEST_ASSERT(!bbo.bid.valid);
    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_fifo() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Sell, 301, 1, 100, 30), sink);
    engine.process(OrderCommand::limit(Side::Sell, 302, 2, 100, 40), sink);
    engine.process(OrderCommand::limit(Side::Sell, 303, 3, 100, 50), sink);
    sink.clear();

    engine.process(OrderCommand::limit(Side::Buy, 304, 4, 100, 50), sink);
    TEST_ASSERT(sink.events.size() == 2);
    TEST_ASSERT(sink.events[0].trade.maker_order_id == 301);
    TEST_ASSERT(sink.events[0].trade.quantity == 30);
    TEST_ASSERT(sink.events[1].trade.maker_order_id == 302);
    TEST_ASSERT(sink.events[1].trade.quantity == 20);

    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_partial_taker_resting_residual() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Sell, 401, 1, 100, 25), sink);
    sink.clear();

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
    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_market_order() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Sell, 501, 1, 101, 20), sink);
    engine.process(OrderCommand::limit(Side::Sell, 502, 2, 102, 30), sink);
    sink.clear();

    engine.process(OrderCommand::market(Side::Buy, 503, 3, 70), sink);
    TEST_ASSERT(sink.events.size() == 3);
    TEST_ASSERT(sink.events[0].trade.price_tick == 101);
    TEST_ASSERT(sink.events[0].trade.quantity == 20);
    TEST_ASSERT(sink.events[1].trade.price_tick == 102);
    TEST_ASSERT(sink.events[1].trade.quantity == 30);
    TEST_ASSERT(sink.events[2].type == EventType::ResidualCancelled);
    TEST_ASSERT(sink.events[2].residual_cancelled.order_id == 503);
    TEST_ASSERT(sink.events[2].residual_cancelled.cancelled_qty == 20);

    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_cancels() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Buy, 601, 1, 50, 10), sink);
    engine.process(OrderCommand::limit(Side::Buy, 602, 2, 50, 20), sink);
    engine.process(OrderCommand::limit(Side::Buy, 603, 3, 50, 30), sink);

    // Cancel middle
    sink.clear();
    engine.process(OrderCommand::cancel(602), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderCancelled);
    TEST_ASSERT(sink.events[0].order_cancelled.order_id == 602);
    TEST_ASSERT(engine.bbo().bid.quantity == 40);
    TEST_ASSERT(engine.validate_invariants());

    // Cancel head
    sink.clear();
    engine.process(OrderCommand::cancel(601), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderCancelled);
    TEST_ASSERT(engine.bbo().bid.quantity == 30);
    TEST_ASSERT(engine.validate_invariants());

    // Cancel sole
    sink.clear();
    engine.process(OrderCommand::cancel(603), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderCancelled);
    TEST_ASSERT(!engine.bbo().bid.valid);
    TEST_ASSERT(engine.validate_invariants());

    // Cancel unknown
    sink.clear();
    engine.process(OrderCommand::cancel(9999), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::CancelRejected);
    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_stp() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Sell, 701, 10, 100, 50), sink);
    sink.clear();

    engine.process(OrderCommand::limit(Side::Buy, 702, 10, 100, 30), sink);
    TEST_ASSERT(sink.events.size() == 2);
    TEST_ASSERT(sink.events[0].type == EventType::SelfTradePrevented);
    TEST_ASSERT(sink.events[0].self_trade_prevented.taker_order_id == 702);
    TEST_ASSERT(sink.events[0].self_trade_prevented.maker_order_id == 701);
    TEST_ASSERT(sink.events[0].self_trade_prevented.cancelled_qty == 30);
    TEST_ASSERT(sink.events[1].type == EventType::ResidualCancelled);

    Bbo bbo = engine.bbo();
    TEST_ASSERT(bbo.ask.valid);
    TEST_ASSERT(bbo.ask.quantity == 50);
    TEST_ASSERT(engine.validate_invariants());
}

void test_engine_capacity_exhaustion() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 2});
    VectorEventSink sink;

    engine.process(OrderCommand::limit(Side::Buy, 1, 1, 100, 10), sink);
    engine.process(OrderCommand::limit(Side::Buy, 2, 2, 101, 10), sink);
    TEST_ASSERT(engine.book().active_orders_count() == 2);

    // Book is full. New order that cannot cross must be rejected
    sink.clear();
    engine.process(OrderCommand::limit(Side::Buy, 3, 3, 99, 10), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderRejected);
    TEST_ASSERT(sink.events[0].order_rejected.reason == OrderRejectReason::CapacityExhausted);

    // Order 4 crosses order 2 (qty 10), which frees 1 slot, so order 4's residual of 10 rests in that freed slot
    sink.clear();
    engine.process(OrderCommand::limit(Side::Sell, 4, 4, 101, 20), sink);
    TEST_ASSERT(sink.events.size() == 2);
    TEST_ASSERT(sink.events[0].type == EventType::Trade);
    TEST_ASSERT(sink.events[0].trade.quantity == 10);
    TEST_ASSERT(sink.events[1].type == EventType::OrderRested);
    TEST_ASSERT(sink.events[1].order_rested.order_id == 4);
    TEST_ASSERT(sink.events[1].order_rested.remaining_qty == 10);

    TEST_ASSERT(engine.validate_invariants());
}

int main() {
    std::cout << "[NexusMatch MatchingEngine Tests] Running..." << std::endl;
    test_engine_passive_limit();
    test_engine_cross_maker_price();
    test_engine_fifo();
    test_engine_partial_taker_resting_residual();
    test_engine_market_order();
    test_engine_cancels();
    test_engine_stp();
    test_engine_capacity_exhaustion();
    std::cout << "[NexusMatch MatchingEngine Tests] All passed successfully!" << std::endl;
    return 0;
}
