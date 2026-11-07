#include "nexusmatch/matching_engine.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <vector>
#include <unordered_set>

using namespace nexusmatch;
using namespace nexusmatch::test;

// Comprehensive 18-point invariant checker
void verify_all_18_invariants(const MatchingEngine& engine) {
    const auto& book = engine.book();
    const auto& arena = book.arena();
    const auto& id_index = book.id_index();
    const auto& ladder = book.ladder();
    const auto& bid_bm = book.bid_bitmap();
    const auto& ask_bm = book.ask_bitmap();
    const auto& stats = engine.stats();
    const auto& config = engine.config();

    // 18. Arena active/free counts sum to capacity
    TEST_ASSERT(arena.active_count() + arena.free_count() == arena.capacity());

    // 14. Volume conservation per side
    TEST_ASSERT(stats.buy_volume.is_conserved());
    TEST_ASSERT(stats.sell_volume.is_conserved());

    // 15. Buy executed quantity equals sell executed quantity
    TEST_ASSERT(stats.buy_volume.executed_quantity == stats.sell_volume.executed_quantity);

    // 16. No crossed book
    BboEntry bb = book.best_bid();
    BboEntry ba = book.best_ask();
    if (bb.valid && ba.valid) {
        TEST_ASSERT(bb.price_tick < ba.price_tick);
    }

    // 10. Best bid/ask equal bitmap-derived max/min
    if (bb.valid) {
        TEST_ASSERT(bb.price_tick == bid_bm.find_max());
        TEST_ASSERT(bb.quantity == ladder.level(Side::Buy, bb.price_tick).total_quantity);
    } else {
        TEST_ASSERT(bid_bm.find_max() == kInvalidIndex);
    }
    if (ba.valid) {
        TEST_ASSERT(ba.price_tick == ask_bm.find_min());
        TEST_ASSERT(ba.quantity == ladder.level(Side::Sell, ba.price_tick).total_quantity);
    } else {
        TEST_ASSERT(ask_bm.find_min() == kInvalidIndex);
    }

    std::unordered_set<std::uint64_t> seen_ids;
    std::unordered_set<std::uint32_t> seen_active_nodes;
    std::uint64_t total_resting_buy_qty = 0;
    std::uint64_t total_resting_sell_qty = 0;

    for (std::uint32_t tick = 0; tick < config.price_tick_count; ++tick) {
        // Buy side
        const PriceLevel& bid_lvl = ladder.level(Side::Buy, tick);
        // 9. Bitmap bit equals level non-empty status
        TEST_ASSERT(bid_bm.test(tick) == !bid_lvl.is_empty());

        // 5. Head/tail empty state matches
        if (bid_lvl.head_index == kInvalidIndex) {
            TEST_ASSERT(bid_lvl.tail_index == kInvalidIndex);
            TEST_ASSERT(bid_lvl.total_quantity == 0);
        } else {
            TEST_ASSERT(bid_lvl.tail_index != kInvalidIndex);
            // 6. Head.prev and tail.next are invalid
            TEST_ASSERT(arena[bid_lvl.head_index].prev_index == kInvalidIndex);
            TEST_ASSERT(arena[bid_lvl.tail_index].next_index == kInvalidIndex);

            std::uint32_t curr = bid_lvl.head_index;
            std::uint32_t prev = kInvalidIndex;
            std::uint64_t lvl_sum = 0;

            while (curr != kInvalidIndex) {
                const OrderNode& node = arena[curr];
                // 3 & 4. Node is Active, not Free
                TEST_ASSERT(node.state == static_cast<std::uint8_t>(OrderNodeState::Active));
                // 2. Every live node appears exactly once in one level queue
                TEST_ASSERT(seen_active_nodes.insert(curr).second);
                // 1 & 17. Every live ID appears exactly once
                TEST_ASSERT(seen_ids.insert(node.order_id).second);
                // 13. ID index points to active node
                TEST_ASSERT(id_index.find(node.order_id) == curr);
                // 11. Active quantity positive
                TEST_ASSERT(node.remaining_qty > 0);
                // 12. Active price in bounds
                TEST_ASSERT(node.price_tick == tick);
                TEST_ASSERT(node.side == static_cast<std::uint8_t>(Side::Buy));

                // 7. Adjacent links reciprocal
                TEST_ASSERT(node.prev_index == prev);

                lvl_sum += node.remaining_qty;
                prev = curr;
                curr = node.next_index;
            }
            TEST_ASSERT(prev == bid_lvl.tail_index);
            // 8. Level aggregate equals sum of remaining quantities
            TEST_ASSERT(lvl_sum == bid_lvl.total_quantity);
            total_resting_buy_qty += lvl_sum;
        }

        // Sell side
        const PriceLevel& ask_lvl = ladder.level(Side::Sell, tick);
        TEST_ASSERT(ask_bm.test(tick) == !ask_lvl.is_empty());

        if (ask_lvl.head_index == kInvalidIndex) {
            TEST_ASSERT(ask_lvl.tail_index == kInvalidIndex);
            TEST_ASSERT(ask_lvl.total_quantity == 0);
        } else {
            TEST_ASSERT(ask_lvl.tail_index != kInvalidIndex);
            TEST_ASSERT(arena[ask_lvl.head_index].prev_index == kInvalidIndex);
            TEST_ASSERT(arena[ask_lvl.tail_index].next_index == kInvalidIndex);

            std::uint32_t curr = ask_lvl.head_index;
            std::uint32_t prev = kInvalidIndex;
            std::uint64_t lvl_sum = 0;

            while (curr != kInvalidIndex) {
                const OrderNode& node = arena[curr];
                TEST_ASSERT(node.state == static_cast<std::uint8_t>(OrderNodeState::Active));
                TEST_ASSERT(seen_active_nodes.insert(curr).second);
                TEST_ASSERT(seen_ids.insert(node.order_id).second);
                TEST_ASSERT(id_index.find(node.order_id) == curr);
                TEST_ASSERT(node.remaining_qty > 0);
                TEST_ASSERT(node.price_tick == tick);
                TEST_ASSERT(node.side == static_cast<std::uint8_t>(Side::Sell));
                TEST_ASSERT(node.prev_index == prev);

                lvl_sum += node.remaining_qty;
                prev = curr;
                curr = node.next_index;
            }
            TEST_ASSERT(prev == ask_lvl.tail_index);
            TEST_ASSERT(lvl_sum == ask_lvl.total_quantity);
            total_resting_sell_qty += lvl_sum;
        }
    }

    TEST_ASSERT(seen_active_nodes.size() == arena.active_count());
    TEST_ASSERT(seen_ids.size() == id_index.size());
    TEST_ASSERT(total_resting_buy_qty == stats.buy_volume.resting_quantity);
    TEST_ASSERT(total_resting_sell_qty == stats.sell_volume.resting_quantity);
}

void test_deep_queue_invariants() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 100, .max_live_orders = 5000});
    VectorEventSink sink;

    // Queue 2000 orders at price tick 50
    for (std::uint64_t id = 1; id <= 2000; ++id) {
        engine.process(OrderCommand::limit(Side::Buy, id, static_cast<std::uint32_t>(id), 50, 10), sink);
    }
    verify_all_18_invariants(engine);

    // Cancel 500 scattered orders
    for (std::uint64_t id = 1; id <= 2000; id += 4) {
        engine.process(OrderCommand::cancel(id), sink);
    }
    verify_all_18_invariants(engine);

    // Cross 500 orders with market sells
    engine.process(OrderCommand::market(Side::Sell, 99999, 999, 5000), sink);
    verify_all_18_invariants(engine);
}

void test_wide_ladder_invariants() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 50000, .max_live_orders = 5000});
    VectorEventSink sink;
    DeterministicRng rng(777);

    for (std::uint64_t id = 1; id <= 3000; ++id) {
        Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
        std::uint32_t price = (side == Side::Buy) ? rng.uniform_u32(1, 24000) : rng.uniform_u32(26000, 49999);
        engine.process(OrderCommand::limit(side, id, 1, price, 10), sink);
    }
    verify_all_18_invariants(engine);
}

void test_adversarial_malformed_commands() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 1000, .max_live_orders = 100, .reject_tick_zero = true});
    VectorEventSink sink;

    // Zero Order ID
    sink.clear();
    engine.process(OrderCommand::limit(Side::Buy, 0, 1, 100, 10), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::OrderRejected);
    TEST_ASSERT(sink.events[0].order_rejected.reason == OrderRejectReason::ZeroOrderId);

    // Zero Quantity
    sink.clear();
    engine.process(OrderCommand::limit(Side::Buy, 1, 1, 100, 0), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].order_rejected.reason == OrderRejectReason::ZeroQuantity);

    // Tick Zero
    sink.clear();
    engine.process(OrderCommand::limit(Side::Buy, 2, 1, 0, 10), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].order_rejected.reason == OrderRejectReason::InvalidPriceTick);

    // Tick Out of Bounds
    sink.clear();
    engine.process(OrderCommand::limit(Side::Buy, 3, 1, 1000, 10), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].order_rejected.reason == OrderRejectReason::InvalidPriceTick);

    // Active Duplicate ID
    engine.process(OrderCommand::limit(Side::Buy, 10, 1, 100, 10), sink);
    sink.clear();
    engine.process(OrderCommand::limit(Side::Buy, 10, 1, 100, 10), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].order_rejected.reason == OrderRejectReason::DuplicateOrderId);

    // Cancel unknown ID
    sink.clear();
    engine.process(OrderCommand::cancel(9999), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].type == EventType::CancelRejected);

    // Cancel zero order ID
    sink.clear();
    engine.process(OrderCommand::cancel(0), sink);
    TEST_ASSERT(sink.events.size() == 1);
    TEST_ASSERT(sink.events[0].cancel_rejected.reason == CancelRejectReason::ZeroOrderId);

    verify_all_18_invariants(engine);
}

void test_fuzzing_stream_invariants() {
    MatchingEngine engine(EngineConfig{.price_tick_count = 200, .max_live_orders = 500});
    VectorEventSink sink;
    DeterministicRng rng(424242);

    std::vector<std::uint64_t> active_ids;
    std::uint64_t next_id = 1;

    for (int step = 0; step < 10000; ++step) {
        double r = rng.next_double();
        if (r < 0.55 || active_ids.empty()) {
            Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
            std::uint32_t price = rng.uniform_u32(1, 199);
            std::uint32_t qty = rng.uniform_u32(1, 100);
            std::uint32_t owner = rng.uniform_u32(1, 5);
            std::uint64_t id = next_id++;
            engine.process(OrderCommand::limit(side, id, owner, price, qty), sink);
            active_ids.push_back(id);
        } else if (r < 0.85) {
            std::size_t idx = rng.uniform_u32(0, static_cast<std::uint32_t>(active_ids.size() - 1));
            std::uint64_t id = active_ids[idx];
            engine.process(OrderCommand::cancel(id), sink);
            active_ids[idx] = active_ids.back();
            active_ids.pop_back();
        } else {
            Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
            std::uint32_t qty = rng.uniform_u32(1, 200);
            std::uint32_t owner = rng.uniform_u32(1, 5);
            std::uint64_t id = next_id++;
            engine.process(OrderCommand::market(side, id, owner, qty), sink);
        }

        if (step % 50 == 0) {
            verify_all_18_invariants(engine);
        }
    }
    verify_all_18_invariants(engine);
}

int main() {
    std::cout << "[NexusMatch 18-Invariant Verification & Fuzzing Suite] Running..." << std::endl;
    test_deep_queue_invariants();
    test_wide_ladder_invariants();
    test_adversarial_malformed_commands();
    test_fuzzing_stream_invariants();
    std::cout << "[NexusMatch 18-Invariant Verification & Fuzzing Suite] All tests PASSED!" << std::endl;
    return 0;
}
