#pragma once

#include "nexusmatch/event.hpp"
#include <span>
#include <vector>
#include <cstddef>

namespace nexusmatch {

// Non-allocating sink concept / base
class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void emit(const EngineEvent& event) noexcept = 0;
};

// Fixed count sink for benchmarking without storage
class CountEventSink final : public IEventSink {
public:
    std::size_t count{0};
    std::size_t trade_count{0};
    std::uint64_t total_traded_qty{0};

    void emit(const EngineEvent& event) noexcept override {
        ++count;
        if (event.type == EventType::Trade) {
            ++trade_count;
            total_traded_qty += event.trade.quantity;
        }
    }

    void reset() noexcept {
        count = 0;
        trade_count = 0;
        total_traded_qty = 0;
    }
};

// Span-backed sink for bounded preallocated capture
class SpanEventSink final : public IEventSink {
public:
    explicit SpanEventSink(std::span<EngineEvent> buffer) noexcept
        : buffer_(buffer), index_(0), overflowed_(false) {}

    void emit(const EngineEvent& event) noexcept override {
        if (index_ < buffer_.size()) {
            buffer_[index_++] = event;
        } else {
            overflowed_ = true;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return index_; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    void reset() noexcept {
        index_ = 0;
        overflowed_ = false;
    }

private:
    std::span<EngineEvent> buffer_;
    std::size_t index_;
    bool overflowed_;
};

// Vector-backed sink for test suites and differential validation (allocations allowed outside hot path)
class VectorEventSink final : public IEventSink {
public:
    std::vector<EngineEvent> events;

    void emit(const EngineEvent& event) noexcept override {
        events.push_back(event);
    }

    void clear() noexcept {
        events.clear();
    }
};

} // namespace nexusmatch
