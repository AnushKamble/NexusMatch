#include "nexusmatch/intrusive_queue.hpp"
#include "nexusmatch/order_arena.hpp"
#include "test_utils.hpp"
#include <iostream>

using namespace nexusmatch;

void test_queue_append_and_unlink() {
    OrderArena arena(10);
    PriceLevel level;
    TEST_ASSERT(level.is_empty());
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    // Append 1
    std::uint32_t n1 = arena.allocate();
    arena[n1].order_id = 101;
    arena[n1].remaining_qty = 50;
    IntrusiveQueue::append(level, arena, n1);
    TEST_ASSERT(!level.is_empty());
    TEST_ASSERT(level.head_index == n1);
    TEST_ASSERT(level.tail_index == n1);
    TEST_ASSERT(level.total_quantity == 50);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    // Append 2
    std::uint32_t n2 = arena.allocate();
    arena[n2].order_id = 102;
    arena[n2].remaining_qty = 30;
    IntrusiveQueue::append(level, arena, n2);
    TEST_ASSERT(level.head_index == n1);
    TEST_ASSERT(level.tail_index == n2);
    TEST_ASSERT(level.total_quantity == 80);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    // Append 3
    std::uint32_t n3 = arena.allocate();
    arena[n3].order_id = 103;
    arena[n3].remaining_qty = 20;
    IntrusiveQueue::append(level, arena, n3);
    TEST_ASSERT(level.head_index == n1);
    TEST_ASSERT(level.tail_index == n3);
    TEST_ASSERT(level.total_quantity == 100);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    // Unlink middle (n2)
    IntrusiveQueue::unlink(level, arena, n2);
    TEST_ASSERT(level.head_index == n1);
    TEST_ASSERT(level.tail_index == n3);
    TEST_ASSERT(level.total_quantity == 70);
    TEST_ASSERT(arena[n1].next_index == n3);
    TEST_ASSERT(arena[n3].prev_index == n1);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    // Unlink head (n1)
    IntrusiveQueue::unlink(level, arena, n1);
    TEST_ASSERT(level.head_index == n3);
    TEST_ASSERT(level.tail_index == n3);
    TEST_ASSERT(level.total_quantity == 20);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    // Unlink sole remaining (n3)
    IntrusiveQueue::unlink(level, arena, n3);
    TEST_ASSERT(level.is_empty());
    TEST_ASSERT(level.total_quantity == 0);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));
}

void test_queue_pop_front() {
    OrderArena arena(10);
    PriceLevel level;

    std::uint32_t n1 = arena.allocate();
    arena[n1].remaining_qty = 10;
    IntrusiveQueue::append(level, arena, n1);

    std::uint32_t n2 = arena.allocate();
    arena[n2].remaining_qty = 20;
    IntrusiveQueue::append(level, arena, n2);

    TEST_ASSERT(IntrusiveQueue::pop_front(level, arena) == n1);
    TEST_ASSERT(level.head_index == n2);
    TEST_ASSERT(level.total_quantity == 20);
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    TEST_ASSERT(IntrusiveQueue::pop_front(level, arena) == n2);
    TEST_ASSERT(level.is_empty());
    TEST_ASSERT(IntrusiveQueue::validate_invariants(level, arena));

    TEST_ASSERT(IntrusiveQueue::pop_front(level, arena) == kInvalidIndex);
}

int main() {
    std::cout << "[NexusMatch IntrusiveQueue Tests] Running..." << std::endl;
    test_queue_append_and_unlink();
    test_queue_pop_front();
    std::cout << "[NexusMatch IntrusiveQueue Tests] All passed successfully!" << std::endl;
    return 0;
}
