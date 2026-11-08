# ADR 0002: Fixed-Capacity Preallocated Storage

## Context
Standard dynamically allocating containers (`std::map`, `std::unordered_map`, `std::list`, `std::vector::push_back`) invoke general-purpose heap allocators (`malloc`, `free`, `new`). Heap allocations cause non-deterministic tail latency spikes (from lock contention in the memory allocator, searching free lists, and triggering OS kernel page faults via `mmap`/`brk`).

## Decision
NexusMatch pre-allocates all matching data structures (`OrderArena`, `OrderIdIndex`, `PriceLadder`, `PriceBitmap`) at initialization:
1. `OrderArena`: Flat contiguous array with an integer index free-list stack.
2. `OrderIdIndex`: Fixed-capacity power-of-two Robin Hood open-addressing table with backward-shift deletion (load factor $\le 0.70$).
3. `PriceLadder`: Dense array of price levels indexed directly by tick.
4. `PriceBitmap`: Hierarchical bit-array for $O(1)$ BBO discovery.

## Consequences
- **Positive:** Exactly **0** dynamic heap allocations occur during continuous order matching. Eliminates tail latency jitter.
- **Negative:** Engine capacity is bounded at construction. An incoming order attempting to rest when capacity is exhausted will cancel residual quantity with an explicit `CapacityExhausted` event rather than expanding memory indefinitely.
