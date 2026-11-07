#pragma once

#include "nexusmatch/types.hpp"
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <cassert>

namespace nexusmatch {

enum class IndexInsertResult : std::uint8_t {
    Success = 0,
    DuplicateId,
    TableFull
};

struct RobinHoodSlot {
    std::uint64_t order_id{0};
    std::uint32_t node_index{kInvalidIndex};
    std::uint32_t probe_distance{0};

    [[nodiscard]] bool is_empty() const noexcept {
        return node_index == kInvalidIndex;
    }

    void reset() noexcept {
        order_id = 0;
        node_index = kInvalidIndex;
        probe_distance = 0;
    }
};

class OrderIdIndex {
public:
    explicit OrderIdIndex(std::uint32_t max_live_orders);
    ~OrderIdIndex() = default;

    OrderIdIndex(const OrderIdIndex&) = delete;
    OrderIdIndex& operator=(const OrderIdIndex&) = delete;
    OrderIdIndex(OrderIdIndex&&) noexcept = default;
    OrderIdIndex& operator=(OrderIdIndex&&) noexcept = default;

    [[nodiscard]] IndexInsertResult insert(std::uint64_t order_id, std::uint32_t node_index) noexcept;
    [[nodiscard]] std::uint32_t find(std::uint64_t order_id) const noexcept;
    [[nodiscard]] bool erase(std::uint64_t order_id) noexcept;

    [[nodiscard]] std::uint32_t size() const noexcept { return size_; }
    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool is_empty() const noexcept { return size_ == 0; }
    [[nodiscard]] float load_factor() const noexcept {
        return static_cast<float>(size_) / static_cast<float>(capacity_);
    }

    // Diagnostics
    [[nodiscard]] std::uint32_t max_probe_distance() const noexcept;
    [[nodiscard]] double average_probe_distance() const noexcept;
    [[nodiscard]] bool validate_invariants() const noexcept;

private:
    [[nodiscard]] static std::uint32_t compute_capacity(std::uint32_t max_live_orders);
    [[nodiscard]] static std::uint64_t hash_key(std::uint64_t key) noexcept {
        // Fast 64-bit mixer (SplitMix64)
        key ^= key >> 30;
        key *= 0xbf58476d1ce4e5b9ULL;
        key ^= key >> 27;
        key *= 0x94d049bb133111ebULL;
        key ^= key >> 31;
        return key;
    }

    std::uint32_t capacity_;
    std::uint32_t mask_;
    std::uint32_t size_{0};
    std::unique_ptr<RobinHoodSlot[]> slots_;
};

} // namespace nexusmatch
