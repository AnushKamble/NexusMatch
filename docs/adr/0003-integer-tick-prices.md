# ADR 0003: Fixed-Point Integer Tick Pricing

## Context
Floating-point representations (`double`, `float`) are inherently imprecise due to IEEE 754 binary fraction approximations (e.g., $0.1 + 0.2 \ne 0.3$). In matching engines, equality comparisons and sorted orderings using floating-point types can introduce rounding discrepancies, non-deterministic book state, and subtle priority inversion bugs. Furthermore, floating-point comparisons require floating-point execution units (FPUs) rather than standard integer ALUs.

## Decision
Represent all prices internally as unsigned 32-bit fixed-point integer ticks (`std::uint32_t`). Prices are mapped to integer multiples of the configured instrument tick size.

## Consequences
- **Positive:** Exact, deterministic equality and relational comparisons on standard integer CPU ALUs. Enables direct array indexing into the dense price ladder and bitwise operations in the occupancy bitmap.
- **Negative:** Instrument price ranges must be configured within a bounded tick domain (e.g. 100,000 ticks). External human-readable decimal prices must be converted at the network ingress boundary.
