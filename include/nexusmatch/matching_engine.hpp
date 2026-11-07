#pragma once

#include "nexusmatch/types.hpp"
#include "nexusmatch/config.hpp"
#include "nexusmatch/event.hpp"
#include "nexusmatch/event_sink.hpp"
#include "nexusmatch/order_book.hpp"
#include <cstdint>

namespace nexusmatch {

class MatchingEngine {
public:
    explicit MatchingEngine(const EngineConfig& config);
    ~MatchingEngine() = default;

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    MatchingEngine(MatchingEngine&&) noexcept = default;
    MatchingEngine& operator=(MatchingEngine&&) noexcept = default;

    // Hot-path entry point: allocation-free and non-throwing
    void process(const OrderCommand& cmd, IEventSink& sink) noexcept;

    [[nodiscard]] Bbo bbo() const noexcept { return book_.bbo(); }
    [[nodiscard]] BboEntry best_bid() const noexcept { return book_.best_bid(); }
    [[nodiscard]] BboEntry best_ask() const noexcept { return book_.best_ask(); }

    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::uint64_t state_hash() const noexcept {
        return book_.compute_state_hash(stats_);
    }

    [[nodiscard]] bool validate_invariants() const noexcept;

    [[nodiscard]] LimitOrderBook& book() noexcept { return book_; }
    [[nodiscard]] const LimitOrderBook& book() const noexcept { return book_; }
    [[nodiscard]] const EngineConfig& config() const noexcept { return config_; }

private:
    void process_limit(const OrderCommand& cmd, IEventSink& sink) noexcept;
    void process_market(const OrderCommand& cmd, IEventSink& sink) noexcept;
    void process_cancel(const OrderCommand& cmd, IEventSink& sink) noexcept;

    void record_execution(Side taker_side, Side maker_side, std::uint32_t qty) noexcept;
    void record_resting(Side side, std::uint32_t qty) noexcept;
    void record_cancelled(Side side, std::uint32_t qty) noexcept;
    void record_cancel_resting(Side side, std::uint32_t qty) noexcept;
    void record_rejected(Side side, std::uint32_t qty) noexcept;

    EngineConfig config_;
    LimitOrderBook book_;
    EngineStats stats_{};
    std::uint64_t trade_seq_{0};
};

} // namespace nexusmatch
