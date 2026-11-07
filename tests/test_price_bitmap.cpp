#include "nexusmatch/price_bitmap.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <set>

using namespace nexusmatch;

void test_empty_bitmap() {
    PriceBitmap bitmap(1000);
    TEST_ASSERT(bitmap.is_empty());
    TEST_ASSERT(bitmap.find_min() == kInvalidIndex);
    TEST_ASSERT(bitmap.find_max() == kInvalidIndex);
    TEST_ASSERT(!bitmap.test(0));
    TEST_ASSERT(!bitmap.test(999));
    TEST_ASSERT(bitmap.validate_invariants());
}

void test_boundaries() {
    constexpr std::uint32_t N = 10000;
    PriceBitmap bitmap(N);

    std::vector<std::uint32_t> test_ticks = {
        0, 1, 63, 64, 65, 127, 128, 129,
        4095, 4096, 4097, // 64 * 64 level boundary!
        N - 2, N - 1
    };

    for (std::uint32_t tick : test_ticks) {
        TEST_ASSERT(!bitmap.test(tick));
        bitmap.set(tick);
        TEST_ASSERT(bitmap.test(tick));
        TEST_ASSERT(bitmap.find_min() == tick);
        TEST_ASSERT(bitmap.find_max() == tick);
        TEST_ASSERT(bitmap.validate_invariants());

        bitmap.clear(tick);
        TEST_ASSERT(!bitmap.test(tick));
        TEST_ASSERT(bitmap.is_empty());
        TEST_ASSERT(bitmap.validate_invariants());
    }
}

void test_min_max_and_removals() {
    PriceBitmap bitmap(5000);

    bitmap.set(100);
    bitmap.set(200);
    bitmap.set(300);

    TEST_ASSERT(bitmap.find_min() == 100);
    TEST_ASSERT(bitmap.find_max() == 300);
    TEST_ASSERT(bitmap.validate_invariants());

    // Remove non-best (200)
    bitmap.clear(200);
    TEST_ASSERT(bitmap.find_min() == 100);
    TEST_ASSERT(bitmap.find_max() == 300);
    TEST_ASSERT(!bitmap.test(200));
    TEST_ASSERT(bitmap.validate_invariants());

    // Remove best min (100)
    bitmap.clear(100);
    TEST_ASSERT(bitmap.find_min() == 300);
    TEST_ASSERT(bitmap.find_max() == 300);
    TEST_ASSERT(bitmap.validate_invariants());

    // Remove final (300)
    bitmap.clear(300);
    TEST_ASSERT(bitmap.is_empty());
    TEST_ASSERT(bitmap.find_min() == kInvalidIndex);
    TEST_ASSERT(bitmap.find_max() == kInvalidIndex);
    TEST_ASSERT(bitmap.validate_invariants());
}

void test_randomized_oracle_comparison() {
    constexpr std::uint32_t N = 10000;
    PriceBitmap bitmap(N);
    std::set<std::uint32_t> oracle;
    test::DeterministicRng rng(98765);

    for (int iter = 0; iter < 20000; ++iter) {
        std::uint32_t tick = rng.uniform_u32(0, N - 1);
        if (rng.next_double() < 0.55) {
            bitmap.set(tick);
            oracle.insert(tick);
        } else {
            bitmap.clear(tick);
            oracle.erase(tick);
        }

        if (oracle.empty()) {
            TEST_ASSERT(bitmap.is_empty());
            TEST_ASSERT(bitmap.find_min() == kInvalidIndex);
            TEST_ASSERT(bitmap.find_max() == kInvalidIndex);
        } else {
            TEST_ASSERT(!bitmap.is_empty());
            TEST_ASSERT(bitmap.find_min() == *oracle.begin());
            TEST_ASSERT(bitmap.find_max() == *oracle.rbegin());
        }
    }

    TEST_ASSERT(bitmap.validate_invariants());
}

int main() {
    std::cout << "[NexusMatch PriceBitmap Tests] Running..." << std::endl;
    test_empty_bitmap();
    test_boundaries();
    test_min_max_and_removals();
    test_randomized_oracle_comparison();
    std::cout << "[NexusMatch PriceBitmap Tests] All passed successfully!" << std::endl;
    return 0;
}
