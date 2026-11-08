# NexusMatch Memory Layout & Data Structures

## 1. Structure Alignments and Footprints

| Structure | Size (Bytes) | Alignment | Cache Line Utilization (64B) |
|---|---:|---:|---|
| `OrderNode` | **32** | 8 | Exactly **2** orders per cache line |
| `PriceLevel` | **16** | 8 | Exactly **4** levels per cache line |
| `RobinHoodSlot` | **16** | 8 | Exactly **4** slots per cache line |
| `OrderCommand` | **24** | 8 | Packed input command without implicit padding |
| `EngineEvent` | **48** | 8 | Tagged union with zero dynamic memory |

---

## 2. OrderNode Layout Details

```cpp
struct alignas(8) OrderNode {
    std::uint64_t order_id;       // 8 bytes: Unique external 64-bit ID
    std::uint32_t price_tick;     // 4 bytes: Integer price tick
    std::uint32_t remaining_qty;  // 4 bytes: Remaining active quantity
    std::uint32_t prev_index;     // 4 bytes: Arena index of previous order in FIFO queue
    std::uint32_t next_index;     // 4 bytes: Arena index of next order in FIFO queue
    std::uint32_t owner_id;       // 4 bytes: Owner/account for self-trade prevention
    std::uint8_t  side;           // 1 byte:  0 = Buy, 1 = Sell
    std::uint8_t  state;          // 1 byte:  0 = Free, 1 = Active
    std::uint16_t reserved;       // 2 bytes: Explicit zero-padding
}; // Total: 32 bytes (static_assert verified)
```

### Why 32-Byte Compact Layout Over 64-Byte Padded Layout
1. **L1 Data Cache Capacity:** A modern Zen 4 core has a 32 KB L1 data cache (512 cache lines). A 64-byte padded order structure limits L1 capacity to only 512 active orders. The 32-byte layout doubles L1 capacity to 1,024 active orders.
2. **Memory Bandwidth:** Sweeping a deep price level fetches twice as many orders per DDR5 burst or L3 transfer.
3. **Index-Based Linking:** Using 32-bit arena indices (`prev_index`, `next_index`) instead of 64-bit pointers saves 8 bytes per node and prevents pointer invalidation upon memory relocation.

---

## 3. Order Arena & Free List Design

```text
Node Array:   [ Node 0 | Node 1 | Node 2 | ... | Node N-1 ]  (Contiguous 32B elements)
Free Stack:   [   0    |    1   |    2   | ... |    N-1   ]  (Indices, top pointer bumped)
```

- Allocation pops the index from `free_list_[--free_top_]`.
- Deallocation pushes the index to `free_list_[free_top_++]`.
- Time complexity: strictly $O(1)$. No heap syscalls (`brk`/`mmap`) or lock acquisition.

---

## 4. Fixed-Capacity Robin Hood Hash Table

- Power-of-two capacity chosen to ensure load factor $\le 0.70$.
- Hash function: SplitMix64 64-bit bit-mixer.
- Collision resolution: Open-addressing Robin Hood hashing with probe distance tracking.
- Deletion: **Backward-Shift Deletion**. When a key is removed, subsequent keys with probe distance $> 0$ are shifted back one slot. This prevents the accumulation of tombstone markers, ensuring consistent bounded lookups under sustained churn.

---

## 5. Hierarchical Occupancy Bitmap

For a price tick domain of $N = 100,000$:
- **Level 0:** 1,563 64-bit words ($100,000 / 64$).
- **Level 1:** 25 64-bit words ($1,563 / 64$).
- **Level 2:** 1 64-bit word ($25 / 64$). Fits entirely within one register!

```text
Level 2: [ 1 word (summary of summary) ]  <-- Top level (1 CPU register)
             |
Level 1: [ 25 words (summary of level 0) ]
             |
Level 0: [ 1,563 words (1 bit per tick) ]
```

- Finding minimum ask: Reads top word (`std::countr_zero`), descends to level 1 word, descends to level 0 word. Bounded to exactly 3 memory accesses and 3 CPU instructions.
