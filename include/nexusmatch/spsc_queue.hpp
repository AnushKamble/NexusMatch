#pragma once

#include <atomic>
#include <memory>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <bit>
#include <cassert>

namespace nexusmatch {

#if defined(__cpp_lib_hardware_interference_size)
using std::hardware_destructive_interference_size;
#else
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::uint32_t requested_capacity)
        : capacity_(compute_capacity(requested_capacity)),
          mask_(capacity_ - 1),
          buffer_(std::make_unique<T[]>(capacity_))
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        cached_head_ = 0;
        cached_tail_ = 0;
    }

    ~SpscQueue() = default;

    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    // Producer thread only
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const std::uint64_t current_tail = tail_.load(std::memory_order_relaxed);

        // Check if full using cached head to minimize cross-core atomic reads
        if (current_tail - cached_head_ >= capacity_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (current_tail - cached_head_ >= capacity_) {
                return false; // Queue is full
            }
        }

        buffer_[current_tail & mask_] = item;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only
    [[nodiscard]] bool try_pop(T& out_item) noexcept {
        const std::uint64_t current_head = head_.load(std::memory_order_relaxed);

        // Check if empty using cached tail
        if (current_head == cached_tail_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head == cached_tail_) {
                return false; // Queue is empty
            }
        }

        out_item = buffer_[current_head & mask_];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] bool is_empty() const noexcept {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        std::uint64_t h = head_.load(std::memory_order_relaxed);
        std::uint64_t t = tail_.load(std::memory_order_relaxed);
        return (t >= h) ? static_cast<std::size_t>(t - h) : 0;
    }

private:
    [[nodiscard]] static std::uint32_t compute_capacity(std::uint32_t requested) {
        if (requested == 0) throw std::invalid_argument("Capacity must be > 0");
        if (requested > (1U << 30)) throw std::invalid_argument("Capacity too large");
        return std::bit_ceil(requested);
    }

    const std::uint32_t capacity_;
    const std::uint32_t mask_;
    const std::unique_ptr<T[]> buffer_;

    // Separate producer and consumer indices onto separate cache lines to avoid false sharing
    alignas(hardware_destructive_interference_size) std::atomic<std::uint64_t> tail_{0};
    std::uint64_t cached_head_{0}; // Producer private copy

    alignas(hardware_destructive_interference_size) std::atomic<std::uint64_t> head_{0};
    std::uint64_t cached_tail_{0}; // Consumer private copy
};

} // namespace nexusmatch
