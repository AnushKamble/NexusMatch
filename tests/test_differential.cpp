#include "nexusmatch/matching_engine.hpp"
#include "reference_engine.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <vector>

using namespace nexusmatch;
using namespace nexusmatch::test;

void run_differential_test(std::uint64_t seed, std::size_t num_commands) {
    EngineConfig config{
        .price_tick_count = 500,
        .max_live_orders = 1000,
        .reject_tick_zero = true
    };

    MatchingEngine engine(config);
    ReferenceEngine ref_engine(config);

    VectorEventSink engine_sink;
    VectorEventSink ref_sink;

    DeterministicRng rng(seed);

    std::vector<std::uint64_t> active_ids;
    std::uint64_t next_order_id = 1;

    for (std::size_t i = 0; i < num_commands; ++i) {
        engine_sink.clear();
        ref_sink.clear();

        OrderCommand cmd{};
        double r = rng.next_double();

        if (r < 0.60 || active_ids.empty()) {
            // New Limit Order
            Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
            std::uint32_t price = rng.uniform_u32(1, config.price_tick_count - 1);
            std::uint32_t qty = rng.uniform_u32(1, 100);
            std::uint32_t owner = rng.uniform_u32(1, 10);
            std::uint64_t id = next_order_id++;

            cmd = OrderCommand::limit(side, id, owner, price, qty);
            active_ids.push_back(id);
        } else if (r < 0.85) {
            // Cancel Order
            std::size_t idx = rng.uniform_u32(0, static_cast<std::uint32_t>(active_ids.size() - 1));
            std::uint64_t id = active_ids[idx];
            cmd = OrderCommand::cancel(id);

            active_ids[idx] = active_ids.back();
            active_ids.pop_back();
        } else {
            // Market Order
            Side side = (rng.next_double() < 0.5) ? Side::Buy : Side::Sell;
            std::uint32_t qty = rng.uniform_u32(1, 150);
            std::uint32_t owner = rng.uniform_u32(1, 10);
            std::uint64_t id = next_order_id++;

            cmd = OrderCommand::market(side, id, owner, qty);
        }

        // Process in both engines
        ref_engine.process(cmd, ref_sink);
        engine.process(cmd, engine_sink);

        // Compare emitted event count
        if (engine_sink.events.size() != ref_sink.events.size()) {
            std::cerr << "Event count mismatch at step " << i << " (seed " << seed << "): "
                      << "engine=" << engine_sink.events.size() << " ref=" << ref_sink.events.size()
                      << std::endl;
            for (const auto& ev : ref_sink.events) {
                std::cerr << "  REF: " << format_event(ev) << "\n";
            }
            for (const auto& ev : engine_sink.events) {
                std::cerr << "  ENG: " << format_event(ev) << "\n";
            }
            TEST_ASSERT(false);
        }

        // Compare event contents
        for (std::size_t j = 0; j < engine_sink.events.size(); ++j) {
            if (!events_equal(engine_sink.events[j], ref_sink.events[j])) {
                std::cerr << "Event mismatch at step " << i << ", event " << j << " (seed " << seed << "):\n"
                          << "  REF: " << format_event(ref_sink.events[j]) << "\n"
                          << "  ENG: " << format_event(engine_sink.events[j]) << "\n";
                TEST_ASSERT(false);
            }
        }

        // Compare BBO
        Bbo eng_bbo = engine.bbo();
        Bbo ref_bbo = ref_engine.bbo();
        TEST_ASSERT(eng_bbo.bid.valid == ref_bbo.bid.valid);
        TEST_ASSERT(eng_bbo.ask.valid == ref_bbo.ask.valid);
        if (eng_bbo.bid.valid) {
            TEST_ASSERT(eng_bbo.bid.price_tick == ref_bbo.bid.price_tick);
            TEST_ASSERT(eng_bbo.bid.quantity == ref_bbo.bid.quantity);
        }
        if (eng_bbo.ask.valid) {
            TEST_ASSERT(eng_bbo.ask.price_tick == ref_bbo.ask.price_tick);
            TEST_ASSERT(eng_bbo.ask.quantity == ref_bbo.ask.quantity);
        }

        // Compare active order count
        TEST_ASSERT(engine.book().active_orders_count() == ref_engine.active_orders_count());

        // Validate engine invariants
        TEST_ASSERT(engine.validate_invariants());
    }

    // Final volume accounting comparison
    const auto& es = engine.stats();
    const auto& rs = ref_engine.stats();

    TEST_ASSERT(es.total_trades == rs.total_trades);
    TEST_ASSERT(es.total_orders_accepted == rs.total_orders_accepted);
    TEST_ASSERT(es.total_cancels_processed == rs.total_cancels_processed);
    TEST_ASSERT(es.total_self_trades_prevented == rs.total_self_trades_prevented);

    TEST_ASSERT(es.buy_volume.accepted_quantity == rs.buy_volume.accepted_quantity);
    TEST_ASSERT(es.buy_volume.executed_quantity == rs.buy_volume.executed_quantity);
    TEST_ASSERT(es.buy_volume.resting_quantity == rs.buy_volume.resting_quantity);
    TEST_ASSERT(es.buy_volume.cancelled_quantity == rs.buy_volume.cancelled_quantity);

    TEST_ASSERT(es.sell_volume.accepted_quantity == rs.sell_volume.accepted_quantity);
    TEST_ASSERT(es.sell_volume.executed_quantity == rs.sell_volume.executed_quantity);
    TEST_ASSERT(es.sell_volume.resting_quantity == rs.sell_volume.resting_quantity);
    TEST_ASSERT(es.sell_volume.cancelled_quantity == rs.sell_volume.cancelled_quantity);

    std::cout << "Differential stream seed " << seed << " (" << num_commands << " cmds) passed. "
              << "Trades: " << es.total_trades << ", Active: " << engine.book().active_orders_count()
              << std::endl;
}

int main() {
    std::cout << "[NexusMatch Differential Testing Suite] Running..." << std::endl;

    std::vector<std::uint64_t> seeds = { 101, 202, 303, 404, 505, 999999 };
    for (std::uint64_t s : seeds) {
        run_differential_test(s, 5000);
    }

    std::cout << "[NexusMatch Differential Testing Suite] All seeds passed with 100% equivalence!" << std::endl;
    return 0;
}
