#include "nexusmatch/order_id_index.hpp"
#include <algorithm>
#include <bit>

namespace nexusmatch {

std::uint32_t OrderIdIndex::compute_capacity(std::uint32_t max_live_orders) {
    if (max_live_orders == 0) {
        throw std::invalid_argument("max_live_orders must be > 0");
    }

    // Target load factor <= 0.70 => capacity >= max_live_orders / 0.70 = max_live_orders * 10 / 7
    std::uint64_t needed = (static_cast<std::uint64_t>(max_live_orders) * 10ULL + 6ULL) / 7ULL;
    if (needed < 16) {
        needed = 16;
    }

    // Next power of 2
    if (needed > (1ULL << 30)) {
        throw std::invalid_argument("Capacity exceeds maximum 30-bit power of 2");
    }

    std::uint32_t cap = std::bit_ceil(static_cast<std::uint32_t>(needed));
    return cap;
}

OrderIdIndex::OrderIdIndex(std::uint32_t max_live_orders)
    : capacity_(compute_capacity(max_live_orders)),
      mask_(capacity_ - 1),
      size_(0),
      slots_(std::make_unique<RobinHoodSlot[]>(capacity_))
{
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        slots_[i].reset();
    }
}

IndexInsertResult OrderIdIndex::insert(std::uint64_t order_id, std::uint32_t node_index) noexcept {
    assert(order_id != 0);
    assert(node_index != kInvalidIndex);

    if (size_ >= capacity_) {
        return IndexInsertResult::TableFull;
    }

    RobinHoodSlot candidate{
        .order_id = order_id,
        .node_index = node_index,
        .probe_distance = 0
    };

    std::uint32_t bucket = static_cast<std::uint32_t>(hash_key(order_id)) & mask_;

    while (true) {
        RobinHoodSlot& resident = slots_[bucket];

        if (resident.is_empty()) {
            resident = candidate;
            ++size_;
            return IndexInsertResult::Success;
        }

        if (resident.order_id == candidate.order_id) {
            return IndexInsertResult::DuplicateId;
        }

        // Robin Hood: if candidate has traveled further than resident, steal the slot
        if (resident.probe_distance < candidate.probe_distance) {
            std::swap(resident, candidate);
        }

        bucket = (bucket + 1) & mask_;
        ++candidate.probe_distance;

        if (candidate.probe_distance >= capacity_) {
            return IndexInsertResult::TableFull;
        }
    }
}

std::uint32_t OrderIdIndex::find(std::uint64_t order_id) const noexcept {
    if (size_ == 0 || order_id == 0) {
        return kInvalidIndex;
    }

    std::uint32_t bucket = static_cast<std::uint32_t>(hash_key(order_id)) & mask_;
    std::uint32_t probe_dist = 0;

    while (true) {
        const RobinHoodSlot& slot = slots_[bucket];

        if (slot.is_empty() || slot.probe_distance < probe_dist) {
            return kInvalidIndex; // Early termination due to Robin Hood ordering
        }

        if (slot.order_id == order_id) {
            return slot.node_index;
        }

        bucket = (bucket + 1) & mask_;
        ++probe_dist;

        if (probe_dist >= capacity_) {
            return kInvalidIndex;
        }
    }
}

bool OrderIdIndex::erase(std::uint64_t order_id) noexcept {
    if (size_ == 0 || order_id == 0) {
        return false;
    }

    std::uint32_t bucket = static_cast<std::uint32_t>(hash_key(order_id)) & mask_;
    std::uint32_t probe_dist = 0;

    // Locate the slot
    while (true) {
        const RobinHoodSlot& slot = slots_[bucket];
        if (slot.is_empty() || slot.probe_distance < probe_dist) {
            return false;
        }
        if (slot.order_id == order_id) {
            break; // Found at bucket
        }
        bucket = (bucket + 1) & mask_;
        ++probe_dist;
        if (probe_dist >= capacity_) {
            return false;
        }
    }

    // Backward-shift deletion
    std::uint32_t curr = bucket;
    while (true) {
        std::uint32_t next = (curr + 1) & mask_;
        RobinHoodSlot& next_slot = slots_[next];

        if (next_slot.is_empty() || next_slot.probe_distance == 0) {
            slots_[curr].reset();
            break;
        }

        slots_[curr] = next_slot;
        --slots_[curr].probe_distance;
        curr = next;
    }

    --size_;
    return true;
}

std::uint32_t OrderIdIndex::max_probe_distance() const noexcept {
    std::uint32_t max_p = 0;
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (!slots_[i].is_empty()) {
            max_p = std::max(max_p, slots_[i].probe_distance);
        }
    }
    return max_p;
}

double OrderIdIndex::average_probe_distance() const noexcept {
    if (size_ == 0) return 0.0;
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (!slots_[i].is_empty()) {
            total += slots_[i].probe_distance;
        }
    }
    return static_cast<double>(total) / static_cast<double>(size_);
}

bool OrderIdIndex::validate_invariants() const noexcept {
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < capacity_; ++i) {
        if (!slots_[i].is_empty()) {
            ++count;
            std::uint32_t expected_home = static_cast<std::uint32_t>(hash_key(slots_[i].order_id)) & mask_;
            std::uint32_t actual_dist = (i >= expected_home) ? (i - expected_home) : (capacity_ - expected_home + i);
            if (slots_[i].probe_distance != actual_dist) {
                return false;
            }
        }
    }
    return count == size_;
}

} // namespace nexusmatch
