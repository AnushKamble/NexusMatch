#pragma once

#include <cstddef>
#include <new>
#include <atomic>
#include <cstdlib>

namespace nexusmatch::bench {

struct AllocationStats {
    std::size_t count{0};
    std::size_t dealloc_count{0};
    std::size_t bytes_allocated{0};
};

class ScopedAllocationTracker {
public:
    static inline thread_local bool active{false};
    static inline thread_local AllocationStats stats{};

    ScopedAllocationTracker() noexcept {
        reset();
        active = true;
    }

    ~ScopedAllocationTracker() noexcept {
        active = false;
    }

    void reset() noexcept {
        stats.count = 0;
        stats.dealloc_count = 0;
        stats.bytes_allocated = 0;
    }

    [[nodiscard]] std::size_t allocation_count() const noexcept {
        return stats.count;
    }

    [[nodiscard]] std::size_t deallocation_count() const noexcept {
        return stats.dealloc_count;
    }

    [[nodiscard]] std::size_t bytes_allocated() const noexcept {
        return stats.bytes_allocated;
    }
};

} // namespace nexusmatch::bench

#ifdef NEXUSMATCH_ENABLE_ALLOCATION_HOOKS
// Global operator new / delete interception
void* operator new(std::size_t size) {
    if (nexusmatch::bench::ScopedAllocationTracker::active) {
        ++nexusmatch::bench::ScopedAllocationTracker::stats.count;
        nexusmatch::bench::ScopedAllocationTracker::stats.bytes_allocated += size;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept {
    if (nexusmatch::bench::ScopedAllocationTracker::active) {
        ++nexusmatch::bench::ScopedAllocationTracker::stats.dealloc_count;
    }
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept {
    if (nexusmatch::bench::ScopedAllocationTracker::active) {
        ++nexusmatch::bench::ScopedAllocationTracker::stats.dealloc_count;
    }
    std::free(ptr);
}

void* operator new[](std::size_t size) {
    if (nexusmatch::bench::ScopedAllocationTracker::active) {
        ++nexusmatch::bench::ScopedAllocationTracker::stats.count;
        nexusmatch::bench::ScopedAllocationTracker::stats.bytes_allocated += size;
    }
    void* ptr = std::malloc(size);
    if (!ptr) throw std::bad_alloc();
    return ptr;
}

void operator delete[](void* ptr) noexcept {
    if (nexusmatch::bench::ScopedAllocationTracker::active) {
        ++nexusmatch::bench::ScopedAllocationTracker::stats.dealloc_count;
    }
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
    if (nexusmatch::bench::ScopedAllocationTracker::active) {
        ++nexusmatch::bench::ScopedAllocationTracker::stats.dealloc_count;
    }
    std::free(ptr);
}
#endif
