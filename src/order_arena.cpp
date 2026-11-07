#include "nexusmatch/order_arena.hpp"
#include <vector>

namespace nexusmatch {

OrderArena::OrderArena(std::uint32_t capacity)
    : capacity_(capacity),
      active_count_(0),
      free_top_(capacity),
      nodes_(std::make_unique<OrderNode[]>(capacity)),
      free_list_(std::make_unique<std::uint32_t[]>(capacity))
{
    if (capacity == 0) {
        throw std::invalid_argument("OrderArena capacity must be > 0");
    }
    if (capacity >= kInvalidIndex) {
        throw std::invalid_argument("OrderArena capacity cannot collide with kInvalidIndex sentinel");
    }

    for (std::uint32_t i = 0; i < capacity; ++i) {
        nodes_[i].reset();
        // free list pushed in reverse so index 0 is popped first
        free_list_[i] = (capacity - 1) - i;
    }
}

std::uint32_t OrderArena::allocate() noexcept {
    if (free_top_ == 0) {
        return kInvalidIndex; // Capacity exhausted
    }
    const std::uint32_t idx = free_list_[--free_top_];
    nodes_[idx].state = static_cast<std::uint8_t>(OrderNodeState::Active);
    ++active_count_;
    return idx;
}

void OrderArena::release(std::uint32_t index) noexcept {
    assert(index < capacity_);
    assert(nodes_[index].state == static_cast<std::uint8_t>(OrderNodeState::Active));
    assert(free_top_ < capacity_);

    nodes_[index].reset();
    free_list_[free_top_++] = index;
    --active_count_;
}

bool OrderArena::validate_invariants() const noexcept {
    if (active_count_ + free_top_ != capacity_) {
        return false;
    }

    // Check all free list entries are valid and in bounds
    std::vector<bool> seen(capacity_, false);
    for (std::uint32_t i = 0; i < free_top_; ++i) {
        std::uint32_t idx = free_list_[i];
        if (idx >= capacity_) return false;
        if (seen[idx]) return false; // Duplicate in free list
        seen[idx] = true;
        if (nodes_[idx].state != static_cast<std::uint8_t>(OrderNodeState::Free)) {
            return false;
        }
    }

    // Check that every node not in free list is Active
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (!seen[i]) {
            if (nodes_[i].state != static_cast<std::uint8_t>(OrderNodeState::Active)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace nexusmatch
