#include "support/AllocationCounter.h"

#include <cstdlib>
#include <new>

namespace
{
    std::atomic<bool> counting { false };
    std::atomic<int> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace osp::test
{

AllocationScope::AllocationScope() noexcept
{
    allocations = 0;
    counting = true;
}

AllocationScope::~AllocationScope()
{
    counting = false;
}

int AllocationScope::count() const noexcept
{
    return allocations.load();
}

} // namespace osp::test
