#include "nexusmatch/order_id_index.hpp"
#include "test_utils.hpp"
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace nexusmatch;

void test_basic_ops() {
    OrderIdIndex index(100);
    TEST_ASSERT(index.is_empty());
    TEST_ASSERT(index.find(1) == kInvalidIndex);

    TEST_ASSERT(index.insert(1001, 10) == IndexInsertResult::Success);
    TEST_ASSERT(index.insert(1002, 20) == IndexInsertResult::Success);
    TEST_ASSERT(index.size() == 2);
    TEST_ASSERT(index.find(1001) == 10);
    TEST_ASSERT(index.find(1002) == 20);
    TEST_ASSERT(index.find(1003) == kInvalidIndex);

    // Duplicate ID rejection
    TEST_ASSERT(index.insert(1001, 99) == IndexInsertResult::DuplicateId);
    TEST_ASSERT(index.find(1001) == 10); // remains unchanged

    // Erase
    TEST_ASSERT(index.erase(1001));
    TEST_ASSERT(index.find(1001) == kInvalidIndex);
    TEST_ASSERT(index.find(1002) == 20);
    TEST_ASSERT(index.size() == 1);
    TEST_ASSERT(!index.erase(1001)); // already erased

    TEST_ASSERT(index.validate_invariants());
}

void test_collision_and_backward_shift() {
    // Small table to guarantee collisions
    OrderIdIndex index(10); // capacity will be 16
    TEST_ASSERT(index.capacity() == 16);

    std::vector<std::uint64_t> inserted_ids;
    for (std::uint64_t id = 1; id <= 10; ++id) {
        TEST_ASSERT(index.insert(id * 16, static_cast<std::uint32_t>(id)) == IndexInsertResult::Success);
        inserted_ids.push_back(id * 16);
    }
    TEST_ASSERT(index.validate_invariants());

    // Verify all can be found
    for (std::uint64_t id : inserted_ids) {
        TEST_ASSERT(index.find(id) != kInvalidIndex);
    }

    // Delete head of cluster
    TEST_ASSERT(index.erase(inserted_ids.front()));
    TEST_ASSERT(index.validate_invariants());
    for (std::size_t i = 1; i < inserted_ids.size(); ++i) {
        TEST_ASSERT(index.find(inserted_ids[i]) != kInvalidIndex);
    }

    // Delete middle of cluster
    std::size_t mid = inserted_ids.size() / 2;
    TEST_ASSERT(index.erase(inserted_ids[mid]));
    TEST_ASSERT(index.validate_invariants());
    for (std::size_t i = 1; i < inserted_ids.size(); ++i) {
        if (i == mid) {
            TEST_ASSERT(index.find(inserted_ids[i]) == kInvalidIndex);
        } else {
            TEST_ASSERT(index.find(inserted_ids[i]) != kInvalidIndex);
        }
    }

    // Delete tail of cluster
    TEST_ASSERT(index.erase(inserted_ids.back()));
    TEST_ASSERT(index.validate_invariants());
}

void test_randomized_oracle_comparison() {
    constexpr std::uint32_t max_orders = 1000;
    OrderIdIndex index(max_orders);
    std::unordered_map<std::uint64_t, std::uint32_t> oracle;
    test::DeterministicRng rng(12345);

    for (int iter = 0; iter < 50000; ++iter) {
        double op = rng.next_double();
        if (op < 0.50) {
            // Insert
            std::uint64_t id = rng.uniform_u32(1, 2000);
            std::uint32_t node = rng.uniform_u32(0, 5000);
            auto it = oracle.find(id);
            if (it != oracle.end()) {
                TEST_ASSERT(index.insert(id, node) == IndexInsertResult::DuplicateId);
            } else {
                if (oracle.size() < max_orders) {
                    TEST_ASSERT(index.insert(id, node) == IndexInsertResult::Success);
                    oracle[id] = node;
                }
            }
        } else if (op < 0.80) {
            // Find
            std::uint64_t id = rng.uniform_u32(1, 2000);
            auto it = oracle.find(id);
            if (it != oracle.end()) {
                TEST_ASSERT(index.find(id) == it->second);
            } else {
                TEST_ASSERT(index.find(id) == kInvalidIndex);
            }
        } else {
            // Erase
            std::uint64_t id = rng.uniform_u32(1, 2000);
            auto it = oracle.find(id);
            if (it != oracle.end()) {
                TEST_ASSERT(index.erase(id));
                oracle.erase(it);
            } else {
                TEST_ASSERT(!index.erase(id));
            }
        }
        TEST_ASSERT(index.size() == oracle.size());
    }

    TEST_ASSERT(index.validate_invariants());
}

int main() {
    std::cout << "[NexusMatch OrderIdIndex Tests] Running..." << std::endl;
    test_basic_ops();
    test_collision_and_backward_shift();
    test_randomized_oracle_comparison();
    std::cout << "[NexusMatch OrderIdIndex Tests] All passed successfully!" << std::endl;
    return 0;
}
