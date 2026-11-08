#define NEXUSMATCH_ENABLE_ALLOCATION_HOOKS 1

#include "nexusmatch/matching_engine.hpp"
#include "tests/reference_engine.hpp"
#include "benchmarks/benchmark_timer.hpp"
#include "benchmarks/workload_generator.hpp"
#include "benchmarks/allocation_counter.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

using namespace nexusmatch;
using namespace nexusmatch::bench;

struct BenchmarkResult {
    std::string scenario;
    std::string engine_type;
    std::size_t order_count{0};
    double duration_seconds{0.0};
    double throughput_ops_sec{0.0};
    LatencyDistribution latency{};
    std::size_t hot_path_allocations{0};
    std::uint64_t total_trades{0};
    std::uint64_t state_hash{0};
};

void print_result_table(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n=========================================================================================================\n";
    std::cout << "                                  NEXUSMATCH PERFORMANCE BENCHMARK REPORT                                \n";
    std::cout << "=========================================================================================================\n";
    std::cout << std::left
              << std::setw(14) << "Engine"
              << std::setw(16) << "Scenario"
              << std::setw(12) << "Orders"
              << std::setw(16) << "Throughput (ops/s)"
              << std::setw(10) << "p50 (ns)"
              << std::setw(10) << "p95 (ns)"
              << std::setw(10) << "p99 (ns)"
              << std::setw(10) << "p99.9(ns)"
              << std::setw(8)  << "Allocs"
              << "\n";
    std::cout << "---------------------------------------------------------------------------------------------------------\n";

    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(14) << r.engine_type
                  << std::setw(16) << r.scenario
                  << std::setw(12) << r.order_count
                  << std::setw(16) << std::fixed << std::setprecision(0) << r.throughput_ops_sec
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.latency.p50_ns
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.latency.p95_ns
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.latency.p99_ns
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.latency.p999_ns
                  << std::setw(8)  << r.hot_path_allocations
                  << "\n";
    }
    std::cout << "=========================================================================================================\n\n";
}

void write_json(const std::string& path, const std::vector<BenchmarkResult>& results) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n  \"results\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n"
            << "      \"engine\": \"" << r.engine_type << "\",\n"
            << "      \"scenario\": \"" << r.scenario << "\",\n"
            << "      \"orders\": " << r.order_count << ",\n"
            << "      \"throughput_ops_sec\": " << r.throughput_ops_sec << ",\n"
            << "      \"p50_ns\": " << r.latency.p50_ns << ",\n"
            << "      \"p90_ns\": " << r.latency.p90_ns << ",\n"
            << "      \"p95_ns\": " << r.latency.p95_ns << ",\n"
            << "      \"p99_ns\": " << r.latency.p99_ns << ",\n"
            << "      \"p999_ns\": " << r.latency.p999_ns << ",\n"
            << "      \"hot_path_allocations\": " << r.hot_path_allocations << ",\n"
            << "      \"total_trades\": " << r.total_trades << ",\n"
            << "      \"state_hash\": " << r.state_hash << "\n"
            << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
}

void write_csv(const std::string& path, const std::vector<BenchmarkResult>& results) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "engine,scenario,orders,throughput_ops_sec,p50_ns,p95_ns,p99_ns,p999_ns,allocations,trades,state_hash\n";
    for (const auto& r : results) {
        out << r.engine_type << ","
            << r.scenario << ","
            << r.order_count << ","
            << r.throughput_ops_sec << ","
            << r.latency.p50_ns << ","
            << r.latency.p95_ns << ","
            << r.latency.p99_ns << ","
            << r.latency.p999_ns << ","
            << r.hot_path_allocations << ","
            << r.total_trades << ","
            << r.state_hash << "\n";
    }
}

int main(int argc, char** argv) {
    std::size_t num_orders = 1'000'000;
    std::string scenario = "all";
    std::string json_path = "benchmark_results.json";
    std::string csv_path = "benchmark_results.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--orders" && i + 1 < argc) {
            num_orders = std::stoull(argv[++i]);
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--json" && i + 1 < argc) {
            json_path = argv[++i];
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        }
    }

    std::cout << "[NexusMatch Benchmarks] CPU: AMD Ryzen 9 8940HX (Zen 4, 16C/32T)" << std::endl;
    std::cout << "[NexusMatch Benchmarks] Workload: Synthetic deterministic stream of " << num_orders << " orders" << std::endl;

    std::vector<BenchmarkResult> results;
    HardwareRdtscTimer::calibrate();

    auto run_bench = [&](const std::string& name, const std::vector<OrderCommand>& cmds, bool run_reference) {
        // 1. Optimized NexusMatch Engine
        {
            EngineConfig cfg{
                .price_tick_count = 100'000,
                .max_live_orders = static_cast<std::uint32_t>(cmds.size() + 10'000),
                .reject_tick_zero = true
            };

            MatchingEngine engine(cfg);
            CountEventSink sink;

            // Warm-up run with 10,000 commands
            for (std::size_t i = 0; i < std::min<std::size_t>(cmds.size(), 10'000); ++i) {
                engine.process(cmds[i], sink);
            }
            sink.reset();

            // Re-initialize for measured run
            MatchingEngine measured_engine(cfg);
            std::vector<double> latencies;
            latencies.reserve(cmds.size());

            SteadyTimer timer;
            std::size_t allocs_detected = 0;

            {
                ScopedAllocationTracker alloc_tracker;
                timer.start();

                for (const auto& cmd : cmds) {
                    auto t0 = std::chrono::steady_clock::now();
                    measured_engine.process(cmd, sink);
                    auto t1 = std::chrono::steady_clock::now();
                    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
                    latencies.push_back(ns);
                }

                timer.stop();
                allocs_detected = alloc_tracker.allocation_count();
            }

            BenchmarkResult res{
                .scenario = name,
                .engine_type = "NexusMatch",
                .order_count = cmds.size(),
                .duration_seconds = timer.elapsed_seconds(),
                .throughput_ops_sec = static_cast<double>(cmds.size()) / timer.elapsed_seconds(),
                .latency = LatencyDistribution::compute(latencies),
                .hot_path_allocations = allocs_detected,
                .total_trades = measured_engine.stats().total_trades,
                .state_hash = measured_engine.state_hash()
            };
            results.push_back(res);
        }

        // 2. Reference Baseline Engine (std::map + std::deque)
        if (run_reference) {
            EngineConfig cfg{
                .price_tick_count = 100'000,
                .max_live_orders = static_cast<std::uint32_t>(cmds.size() + 10'000),
                .reject_tick_zero = true
            };

            test::ReferenceEngine ref_engine(cfg);
            CountEventSink ref_sink;
            std::vector<double> latencies;
            std::size_t sample_size = std::min<std::size_t>(cmds.size(), 200'000); // Sample up to 200k for baseline
            latencies.reserve(sample_size);

            SteadyTimer timer;
            std::size_t allocs_detected = 0;

            {
                ScopedAllocationTracker alloc_tracker;
                timer.start();

                for (std::size_t i = 0; i < sample_size; ++i) {
                    auto t0 = std::chrono::steady_clock::now();
                    ref_engine.process(cmds[i], ref_sink);
                    auto t1 = std::chrono::steady_clock::now();
                    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
                    latencies.push_back(ns);
                }

                timer.stop();
                allocs_detected = alloc_tracker.allocation_count();
            }

            BenchmarkResult res{
                .scenario = name,
                .engine_type = "Baseline(Map)",
                .order_count = sample_size,
                .duration_seconds = timer.elapsed_seconds(),
                .throughput_ops_sec = static_cast<double>(sample_size) / timer.elapsed_seconds(),
                .latency = LatencyDistribution::compute(latencies),
                .hot_path_allocations = allocs_detected,
                .total_trades = ref_engine.stats().total_trades,
                .state_hash = 0
            };
            results.push_back(res);
        }
    };

    if (scenario == "all" || scenario == "mixed") {
        std::cout << "[Workload] Generating Balanced Mixed Workload (60% Limit, 30% Cancel, 10% Market)..." << std::endl;
        WorkloadConfig cfg{
            .num_orders = num_orders,
            .seed = 42,
            .price_tick_min = 100,
            .price_tick_max = 500,
            .limit_ratio = 0.60,
            .cancel_ratio = 0.30,
            .market_ratio = 0.10
        };
        auto cmds = WorkloadGenerator::generate_mixed(cfg);
        run_bench("Balanced-Mixed", cmds, true);
    }

    if (scenario == "all" || scenario == "cancel-heavy") {
        std::cout << "[Workload] Generating Cancel-Heavy Workload (50% Limit, 50% Cancel)..." << std::endl;
        auto cmds = WorkloadGenerator::generate_cancel_heavy(num_orders);
        run_bench("Cancel-Heavy", cmds, false);
    }

    if (scenario == "all" || scenario == "trade-heavy") {
        std::cout << "[Workload] Generating Trade-Heavy Cross Workload (100% Executions)..." << std::endl;
        auto cmds = WorkloadGenerator::generate_trade_heavy(num_orders);
        run_bench("Trade-Heavy", cmds, false);
    }

    print_result_table(results);
    write_json(json_path, results);
    write_csv(csv_path, results);
    std::cout << "[NexusMatch Benchmarks] Results written to " << json_path << " and " << csv_path << std::endl;

    return 0;
}
