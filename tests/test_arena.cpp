#include "nexusmatch/order_arena.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <vector>

using namespace nexusmatch;

void test_capacity_one() {
    OrderArena arena(1);
    TEST_ASSERT(arena.capacity() == 1);
    TEST_ASSERT(arena.active_count() == 0);
    TEST_ASSERT(arena.free_count() == 1);
    TEST_ASSERT(!arena.is_full());
    TEST_ASSERT(arena.is_empty());
    TEST_ASSERT(arena.validate_invariants());

    std::uint32_t idx = arena.allocate();
    TEST_ASSERT(idx == 0);
    TEST_ASSERT(arena.active_count() == 1);
    TEST_ASSERT(arena.free_count() == 0);
    TEST_ASSERT(arena.is_full());
    TEST_ASSERT(!arena.is_empty());
    TEST_ASSERT(arena.validate_invariants());

    // Exhaustion
    std::uint32_t fail_idx = arena.allocate();
    TEST_ASSERT(fail_idx == kInvalidIndex);

    // Release
    arena.release(idx);
    TEST_ASSERT(arena.active_count() == 0);
    TEST_ASSERT(arena.free_count() == 1);
    TEST_ASSERT(arena.validate_invariants());

    // Reuse
    std::uint32_t idx2 = arena.allocate();
    TEST_ASSERT(idx2 == 0);
    TEST_ASSERT(arena.validate_invariants());
}

void test_allocate_all_and_exhaust() {
    constexpr std::uint32_t N = 1000;
    OrderArena arena(N);

    std::vector<std::uint32_t> indices;
    indices.reserve(N);

    for (std::uint32_t i = 0; i < N; ++i) {
        std::uint32_t idx = arena.allocate();
        TEST_ASSERT(idx != kInvalidIndex);
        TEST_ASSERT(idx < N);
        indices.push_back(idx);
    }

    TEST_ASSERT(arena.is_full());
    TEST_ASSERT(arena.active_count() == N);
    TEST_ASSERT(arena.free_count() == 0);
    TEST_ASSERT(arena.allocate() == kInvalidIndex);
    TEST_ASSERT(arena.validate_invariants());

    // Release half
    for (std::size_t i = 0; i < N / 2; ++i) {
        arena.release(indices[i]);
    }
    TEST_ASSERT(arena.active_count() == N - (N / 2));
    TEST_ASSERT(arena.free_count() == N / 2);
    TEST_ASSERT(arena.validate_invariants());

    // Reallocate
    for (std::size_t i = 0; i < N / 2; ++i) {
        std::uint32_t idx = arena.allocate();
        TEST_ASSERT(idx != kInvalidIndex);
    }
    TEST_ASSERT(arena.is_full());
    TEST_ASSERT(arena.validate_invariants());
}

void test_churn() {
    constexpr std::uint32_t N = 100;
    OrderArena arena(N);
    test::DeterministicRng rng(42);

    std::vector<std::uint32_t> active;

    for (int iter = 0; iter < 10000; ++iter) {
        if (active.size() < N && (active.empty() || rng.next_double() < 0.6)) {
            std::uint32_t idx = arena.allocate();
            TEST_ASSERT(idx != kInvalidIndex);
            active.push_back(idx);
        } else {
            std::size_t pos = rng.uniform_u32(0, static_cast<std::uint32_t>(active.size() - 1));
            std::uint32_t idx = active[pos];
            active[pos] = active.back();
            active.pop_back();
            arena.release(idx);
        }
    }
    TEST_ASSERT(arena.validate_invariants());
}

int main() {
    std::cout << "[NexusMatch OrderArena Tests] Running..." << std::endl;
    test_capacity_one();
    test_allocate_all_and_exhaust();
    test_churn();
    std::cout << "[NexusMatch OrderArena Tests] All passed successfully!" << std::endl;
    return 0;
}
