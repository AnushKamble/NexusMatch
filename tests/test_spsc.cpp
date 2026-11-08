#include "nexusmatch/spsc_queue.hpp"
#include "tests/test_utils.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>

using namespace nexusmatch;

void test_spsc_single_thread_basics() {
    SpscQueue<std::uint64_t> queue(4); // capacity rounded to 4
    TEST_ASSERT(queue.capacity() == 4);
    TEST_ASSERT(queue.is_empty());

    std::uint64_t val = 0;
    TEST_ASSERT(!queue.try_pop(val));

    TEST_ASSERT(queue.try_push(10));
    TEST_ASSERT(queue.try_push(20));
    TEST_ASSERT(queue.try_push(30));
    TEST_ASSERT(queue.try_push(40));
    // Full
    TEST_ASSERT(!queue.try_push(50));

    TEST_ASSERT(queue.try_pop(val) && val == 10);
    TEST_ASSERT(queue.try_pop(val) && val == 20);

    TEST_ASSERT(queue.try_push(50));
    TEST_ASSERT(queue.try_push(60));
    TEST_ASSERT(!queue.try_push(70));

    TEST_ASSERT(queue.try_pop(val) && val == 30);
    TEST_ASSERT(queue.try_pop(val) && val == 40);
    TEST_ASSERT(queue.try_pop(val) && val == 50);
    TEST_ASSERT(queue.try_pop(val) && val == 60);
    TEST_ASSERT(!queue.try_pop(val));
}

void test_spsc_multithreaded_stress() {
    constexpr std::uint64_t total_items = 2'000'000;
    SpscQueue<std::uint64_t> queue(1024);

    std::atomic<bool> producer_done{false};
    std::vector<std::uint64_t> received;
    received.reserve(total_items);

    // Producer thread
    std::thread producer([&]() {
        for (std::uint64_t i = 1; i <= total_items; ++i) {
            while (!queue.try_push(i)) {
                std::this_thread::yield();
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // Consumer thread (main thread)
    std::uint64_t current = 0;
    while (!producer_done.load(std::memory_order_acquire) || !queue.is_empty()) {
        std::uint64_t val = 0;
        if (queue.try_pop(val)) {
            TEST_ASSERT(val == current + 1); // Strictly monotonic sequence
            current = val;
        } else {
            std::this_thread::yield();
        }
    }

    producer.join();
    TEST_ASSERT(current == total_items);
    std::cout << "SPSC two-thread stress test passed for " << total_items << " messages." << std::endl;
}

int main() {
    std::cout << "[NexusMatch SPSC Queue Tests] Running..." << std::endl;
    test_spsc_single_thread_basics();
    test_spsc_multithreaded_stress();
    std::cout << "[NexusMatch SPSC Queue Tests] All passed successfully!" << std::endl;
    return 0;
}
