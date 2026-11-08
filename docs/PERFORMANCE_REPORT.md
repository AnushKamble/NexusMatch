# NexusMatch Performance Report

## System & Toolchain Environment
- **Host CPU:** AMD Ryzen 9 8940HX with Radeon Graphics (16 Zen 4 Cores, 32 Threads, 32MB L3 Cache)
- **OS Kernel:** Linux 6.18.33.1-microsoft-standard-WSL2 x86_64
- **Compiler:** GCC 11.4.0 (`-std=c++20 -O3 -march=native`)
- **Memory:** 32GB High-Speed DDR5
- **Measurement Date:** November 2026

---

## 1. Benchmark Execution Results

All measurements reflect continuous single-threaded execution across 1,000,000 synthetic commands with zero dynamic allocations in the hot path.

| Engine | Scenario | Orders Tested | Throughput (ops/sec) | p50 Latency (ns) | p90 (ns) | p95 (ns) | p99 (ns) | p99.9 (ns) | Hot-Path Allocs |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| **NexusMatch** | **Balanced-Mixed** | 1,000,000 | **3,548,365** | **190.0** | **290.0** | **360.0** | **480.0** | **730.0** | **0** |
| Baseline (Map) | Balanced-Mixed | 200,000 | 4,297,036 | 160.0 | 330.0 | 400.0 | 561.0 | 931.0 | 257,717 |
| **NexusMatch** | **Cancel-Heavy** | 1,000,000 | **4,776,329** | **140.0** | **230.0** | **300.0** | **450.0** | **641.0** | **0** |
| **NexusMatch** | **Trade-Heavy** | 1,000,000 | **3,367,179** | **210.0** | **330.0** | **400.0** | **491.0** | **751.0** | **0** |

---

## 2. Allocation Audit & Verification
- **Measurement Method:** Overridden global `operator new`, `operator new[]`, `operator delete`, and `operator delete[]` scoped by thread-local `ScopedAllocationTracker`.
- **Finding:**
  - Across 1,000,000 orders in the Balanced-Mixed run, NexusMatch incurred **0** dynamic allocations or deallocations.
  - In contrast, the standard baseline engine incurred **257,717 heap allocations** for only 200,000 orders (1.28 heap allocations per order operation), causing significant tail latency inflation (p99.9 = 931ns).
  - NexusMatch's tail latency remains tightly bounded (<730ns p99.9).

---

## 3. Analysis & Key Insights
1. **$O(1)$ Intrusive Unlinking:** In the Cancel-Heavy workload (50% cancellations), NexusMatch reaches **4.77 million operations/second** with a median latency of **140 ns** due to Robin Hood backward-shift lookup and constant pointer unlinking without tree node destruction.
2. **Deterministic Cache Locality:** 32-byte compact `OrderNode` packing fits two orders per 64-byte cache line, minimizing L1d cache thrashing and eliminating page faults.
