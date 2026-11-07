#pragma once

#include "nexusmatch/order_node.hpp"
#include <vector>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <cassert>

namespace nexusmatch {

class OrderArena {
public:
    explicit OrderArena(std::uint32_t capacity);
    ~OrderArena() = default;

    OrderArena(const OrderArena&) = delete;
    OrderArena& operator=(const OrderArena&) = delete;
    OrderArena(OrderArena&&) noexcept = default;
    OrderArena& operator=(OrderArena&&) noexcept = default;

    [[nodiscard]] std::uint32_t allocate() noexcept;
    void release(std::uint32_t index) noexcept;

    [[nodiscard]] OrderNode& operator[](std::uint32_t index) noexcept {
        assert(index < capacity_);
        return nodes_[index];
    }

    [[nodiscard]] const OrderNode& operator[](std::uint32_t index) const noexcept {
        assert(index < capacity_);
        return nodes_[index];
    }

    [[nodiscard]] OrderNode* data() noexcept { return nodes_.get(); }
    [[nodiscard]] const OrderNode* data() const noexcept { return nodes_.get(); }

    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::uint32_t active_count() const noexcept { return active_count_; }
    [[nodiscard]] std::uint32_t free_count() const noexcept { return capacity_ - active_count_; }
    [[nodiscard]] bool is_full() const noexcept { return active_count_ >= capacity_; }
    [[nodiscard]] bool is_empty() const noexcept { return active_count_ == 0; }

    // Diagnostic invariant check
    [[nodiscard]] bool validate_invariants() const noexcept;

private:
    std::uint32_t capacity_;
    std::uint32_t active_count_{0};
    std::uint32_t free_top_{0};
    std::unique_ptr<OrderNode[]> nodes_;
    std::unique_ptr<std::uint32_t[]> free_list_;
};

} // namespace nexusmatch
