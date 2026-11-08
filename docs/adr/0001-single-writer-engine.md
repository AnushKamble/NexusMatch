# ADR 0001: Single-Writer Matching Engine Core

## Context
Electronic trading engines require sub-microsecond determinism. Multi-threaded matching cores using mutexes, read-write locks, or fine-grained per-level locks suffer from severe latency degradation due to thread contention, kernel context switches, cache line bouncing, and non-deterministic event sequencing.

## Decision
Design the NexusMatch core matching engine as a strictly **single-writer thread-confined state machine**. Ingress concurrency is decoupled via a bounded Single-Producer Single-Consumer (SPSC) lock-free ring buffer.

## Consequences
- **Positive:** Zero locking overhead, deterministic execution sequence, no cache-line invalidation across CPU cores on the hot matching path, and predictable nanosecond latencies.
- **Negative:** Instrument throughput is bound to a single CPU core's single-thread performance (achieving ~3.5M–4.7M ops/sec on AMD Zen 4). Scaling to multiple trading instruments is achieved via independent engine instances pinned to separate CPU cores.
