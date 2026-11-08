#include "nexusmatch/spsc_queue.hpp"
#include "nexusmatch/types.hpp"

namespace nexusmatch {

// Explicit instantiation for OrderCommand
template class SpscQueue<OrderCommand>;

} // namespace nexusmatch
