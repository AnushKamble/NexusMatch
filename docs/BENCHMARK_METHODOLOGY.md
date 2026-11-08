# NexusMatch Benchmark Methodology

This document outlines the benchmarking philosophy, timing apparatus, workload profiles, and measurement boundaries used to evaluate NexusMatch.

---

## 1. Benchmarking Principles

1. **Evidence, Not Vanity:** Benchmarking measures actual performance under reproducible conditions, not hand-picked outliers or fabricated speeds.
2. **Deterministic Inputs:** Workloads are pseudo-randomly generated using deterministic seeds prior to starting the measurement loop. No random generation, file I/O, or console logging occurs in the timed hot path.
3. **Mechanical Sympathy:** Measures the core matching engine's CPU service time directly, distinguishing it from thread scheduling and network transport residence.
4. **Allocation Scoping:** Dynamic allocations (`operator new`, `malloc`) are intercepted via a thread-local tracking scope around the execution loop.

---

## 2. Timing Instrumentation & Hardware Calibration

- **High-Resolution Clock:** Uses `std::chrono::steady_clock` (monotonic wall-clock) for total throughput and per-order sampling.
- **Hardware RDTSCP:** Features serializing assembly instructions (`LFENCE; RDTSC` / `RDTSCP; LFENCE`) to prevent out-of-order execution across the timestamp read boundaries on x86-64.
- **Overhead Calibration:** Calibrates timer invocation overhead prior to benchmark execution.

---

## 3. Workload Scenarios

1. **Balanced-Mixed:** 60% Limit Orders, 30% Cancellations, 10% Market Orders. Demonstrates sustained bidirectional queue maintenance, crosses, and unlinks.
2. **Cancel-Heavy:** 50% Limit Orders, 50% Cancellations. Stresses the Robin Hood ID index and intrusive unlinking under rapid queue churn.
3. **Trade-Heavy (100% Crosses):** Consecutive matching passes where aggressive takers immediately match against resting makers, testing maker-price trades, partial fills, and node returns.
4. **Baseline Reference Engine:** Semantically identical reference engine using `std::map<uint32_t, std::deque<Order>>` and heap allocation, measured under the exact same command stream.
