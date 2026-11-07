# NexusMatch Normative Matching Semantics

This document defines the normative matching semantics, execution priority model, self-trade prevention rules, volume accounting, and error conditions implemented in NexusMatch.

---

## 1. Number Representation & Units

- **Prices:** Fixed-point integer ticks (`std::uint32_t`). Prices are never represented as `double` or `float` within the engine. Tick zero is invalid by default; valid prices lie in `[1, price_tick_count - 1]`.
- **Quantities:** Integer shares (`std::uint32_t` per order, `std::uint64_t` for price level aggregates and lifetime trade statistics). Arithmetic operations check for overflow.
- **Order Identifiers:** `std::uint64_t` (nonzero).
- **Owner Identifiers:** `std::uint32_t`.
- **Internal Arena Indices:** `std::uint32_t`, with `0xFFFFFFFF` (`UINT32_MAX`) reserved as the sentinel `kInvalidIndex`.

---

## 2. Order Commands

NexusMatch processes three discrete command types:

### 2.1 Limit Order
- **Fields:** `order_id`, `owner_id`, `side` (`Buy` or `Sell`), `price_tick`, `quantity`.
- **Semantics:** Aggressively crosses eligible opposing resting orders. Any residual quantity is appended to the tail of the FIFO queue at `price_tick` on its respective side. If the arena is full when resting is required, fills already executed are preserved and the residual is cancelled with an explicit `CapacityExhausted` event.

### 2.2 Market Order
- **Fields:** `order_id`, `owner_id`, `side`, `quantity`.
- **Semantics:** Sweeps opposing liquidity beginning at the best opposing price (lowest ask for buy, highest bid for sell) in price-time priority. Any unfilled remainder is immediately cancelled (`ResidualCancelled`). Market orders **never rest** in the book.

### 2.3 Cancel Order
- **Fields:** `order_id`.
- **Semantics:** Locates the resting order in the fixed-capacity Robin Hood ID index in expected $O(1)$ time and unlinks it from its price-level intrusive queue in worst-case $O(1)$ time. If the order ID is not live, emits deterministic `CancelRejected` (`OrderNotFound`).

---

## 3. Priority and Execution Rules

1. **Price Priority:** Better prices execute before worse prices.
   - For Buy aggressors: lowest ask first.
   - For Sell aggressors: highest bid first.
2. **Time Priority (FIFO):** Orders at the exact same price tick execute in engine arrival order. Client-side timestamps are ignored for queue placement.
3. **Execution Price:** Trades always execute at the **maker price** (the resting order's price), not the taker's limit price.
4. **Partial Fills:** Decrements both the aggressor's remaining quantity and the maker's remaining quantity by `min(taker_remaining, maker_remaining)`. The level aggregate is decremented synchronously. When a maker's remaining quantity reaches zero, its arena slot is returned to the free list and its ID is erased from the index.

---

## 4. Self-Trade Prevention (STP): Cancel Aggressor Remainder

When an incoming aggressor order encounters a resting maker order belonging to the same `owner_id`:
- Matching against that maker is **prevented**.
- The engine does **not** skip the maker to execute against subsequent makers (which would violate FIFO priority).
- The remaining unfilled quantity of the aggressor order is **immediately cancelled**.
- Any fills already completed against previous makers at better prices remain valid.
- The resting maker order remains completely untouched at the head of its queue.
- Events emitted: `SelfTradePrevented` followed by `ResidualCancelled`.

---

## 5. Event Sequence & Determinism

For an identical command sequence and initial engine configuration:
- Emitted events, trade sequences, timestamps, and order states are bit-for-bit identical across independent engine instances.
- Events emitted per command:
  - `Trade`: Maker ID, Taker ID, Maker Price Tick, Execution Quantity, Trade Sequence, Maker Side.
  - `OrderRested`: Order ID, Owner ID, Price Tick, Remaining Quantity, Side.
  - `OrderCancelled`: Order ID, Price Tick, Cancelled Quantity, Side.
  - `CancelRejected`: Order ID, Reject Reason.
  - `OrderRejected`: Order ID, Reject Reason.
  - `ResidualCancelled`: Order ID, Cancelled Quantity, Side.
  - `SelfTradePrevented`: Taker ID, Maker ID, Cancelled Quantity, Side.
  - `CapacityExhausted`: Order ID, Unrested Quantity, Side.

---

## 6. Volume Conservation Invariants

For each side (`Buy` and `Sell`):
$$\text{accepted\_quantity} = \text{executed\_quantity} + \text{resting\_quantity} + \text{cancelled\_quantity}$$

Furthermore:
- Total Buy `executed_quantity` $\equiv$ Total Sell `executed_quantity`.
- Every trade decrements resting maker quantity and aggressor quantity by the identical fill amount.
- Rejected orders (invalid format, duplicate ID, capacity-rejected before any fill) increment `rejected_quantity` only and are excluded from `accepted_quantity`.
