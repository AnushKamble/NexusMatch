#include "nexusmatch/matching_engine.hpp"
#include "nexusmatch/command_wal.hpp"
#include "benchmarks/workload_generator.hpp"
#include "benchmarks/benchmark_timer.hpp"
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace nexusmatch;
using namespace nexusmatch::bench;

void print_help() {
    std::cout << "NexusMatch - Ultra-Low-Latency Limit Order Book & Matching Engine (C++20)\n\n"
              << "Usage: ./build/nexusmatch_demo [options]\n\n"
              << "Options:\n"
              << "  --scenario <name>    Workload scenario: mixed (default), cancel-heavy, trade-heavy\n"
              << "  --orders <N>         Number of orders to execute (default: 500000)\n"
              << "  --seed <S>           RNG seed for deterministic stream (default: 42)\n"
              << "  --wal <path>         Write commands to Write-Ahead Log\n"
              << "  --replay <path>      Replay commands from WAL and verify state hash\n"
              << "  --help               Display this help message\n";
}

int main(int argc, char** argv) {
    std::string scenario = "mixed";
    std::size_t num_orders = 500'000;
    std::uint64_t seed = 42;
    std::string wal_out_path = "";
    std::string replay_path = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            print_help();
            return 0;
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--orders" && i + 1 < argc) {
            num_orders = std::stoull(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::stoull(argv[++i]);
        } else if (arg == "--wal" && i + 1 < argc) {
            wal_out_path = argv[++i];
        } else if (arg == "--replay" && i + 1 < argc) {
            replay_path = argv[++i];
        }
    }

    EngineConfig config{
        .price_tick_count = 100'000,
        .max_live_orders = 1'000'000,
        .reject_tick_zero = true
    };

    MatchingEngine engine(config);
    CountEventSink sink;

    if (!replay_path.empty()) {
        std::cout << "[NexusMatch Replay Mode] Reading WAL from: " << replay_path << std::endl;
        CommandWalReader reader(replay_path);
        WalReadStatus status = reader.open_and_validate_header();
        if (status != WalReadStatus::Success) {
            std::cerr << "Failed to validate WAL header: " << to_string(status) << std::endl;
            return 1;
        }

        WalRecord record;
        std::size_t replayed = 0;
        SteadyTimer timer;
        timer.start();

        while ((status = reader.read_next(record)) == WalReadStatus::Success) {
            engine.process(record.command, sink);
            ++replayed;
        }
        timer.stop();

        if (status != WalReadStatus::EndOfFile) {
            std::cerr << "WAL reading encountered error: " << to_string(status) << std::endl;
            return 1;
        }

        std::cout << "Replayed " << replayed << " commands in " << timer.elapsed_seconds() << "s "
                  << "(" << (static_cast<double>(replayed) / timer.elapsed_seconds()) << " ops/s).\n";
        std::cout << "Final Engine State Hash: " << engine.state_hash() << std::endl;
        return 0;
    }

    std::cout << "=================================================================\n";
    std::cout << "         NEXUSMATCH ULTRA-LOW-LATENCY DEMONSTRATION             \n";
    std::cout << "=================================================================\n";
    std::cout << "Configuration:\n"
              << "  Scenario:         " << scenario << "\n"
              << "  Orders to run:    " << num_orders << " (Synthetic stream)\n"
              << "  Seed:             " << seed << "\n"
              << "  Price Ticks:      " << config.price_tick_count << "\n"
              << "  Arena Capacity:   " << config.max_live_orders << " orders\n";

    std::vector<OrderCommand> commands;
    if (scenario == "cancel-heavy") {
        commands = WorkloadGenerator::generate_cancel_heavy(num_orders, seed);
    } else if (scenario == "trade-heavy") {
        commands = WorkloadGenerator::generate_trade_heavy(num_orders, seed);
    } else {
        WorkloadConfig w_cfg{
            .num_orders = num_orders,
            .seed = seed,
            .price_tick_min = 100,
            .price_tick_max = 500,
            .limit_ratio = 0.60,
            .cancel_ratio = 0.30,
            .market_ratio = 0.10
        };
        commands = WorkloadGenerator::generate_mixed(w_cfg);
    }

    std::unique_ptr<CommandWalWriter> wal_writer;
    if (!wal_out_path.empty()) {
        std::cout << "  WAL Output:       " << wal_out_path << "\n";
        wal_writer = std::make_unique<CommandWalWriter>(wal_out_path);
    }

    std::cout << "\nExecuting commands..." << std::endl;

    SteadyTimer timer;
    timer.start();

    for (const auto& cmd : commands) {
        if (wal_writer) {
            wal_writer->append(cmd);
        }
        engine.process(cmd, sink);
    }

    timer.stop();
    if (wal_writer) {
        wal_writer->flush();
    }

    double elapsed_s = timer.elapsed_seconds();
    double throughput = static_cast<double>(num_orders) / elapsed_s;

    Bbo bbo = engine.bbo();
    const auto& stats = engine.stats();

    std::cout << "\nExecution Summary:\n"
              << "-----------------------------------------------------------------\n"
              << "  Elapsed Time:     " << std::fixed << std::setprecision(4) << elapsed_s << " s\n"
              << "  Throughput:       " << std::fixed << std::setprecision(0) << throughput << " ops/sec\n"
              << "  Total Trades:     " << stats.total_trades << " fills\n"
              << "  Orders Rested:    " << stats.total_orders_accepted << "\n"
              << "  Cancels Handled:  " << stats.total_cancels_processed << "\n"
              << "  Active in Book:   " << engine.book().active_orders_count() << "\n"
              << "  State Hash:       " << engine.state_hash() << "\n";

    std::cout << "\nFinal Book Top of Book (BBO):\n"
              << "-----------------------------------------------------------------\n";
    if (bbo.bid.valid) {
        std::cout << "  Best Bid:  Price=" << bbo.bid.price_tick << " | Total Qty=" << bbo.bid.quantity << "\n";
    } else {
        std::cout << "  Best Bid:  [EMPTY]\n";
    }
    if (bbo.ask.valid) {
        std::cout << "  Best Ask:  Price=" << bbo.ask.price_tick << " | Total Qty=" << bbo.ask.quantity << "\n";
    } else {
        std::cout << "  Best Ask:  [EMPTY]\n";
    }
    std::cout << "=================================================================\n";

    return 0;
}
