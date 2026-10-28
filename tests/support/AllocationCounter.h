#pragma once

#include <atomic>

namespace osp::test
{

/**
    Counts heap allocations (the test binary replaces the global operator new) while a scope
    is alive: real-time code must make none. One scope at a time, on one thread.
*/
class AllocationScope
{
public:
    AllocationScope() noexcept;
    ~AllocationScope();
    AllocationScope (const AllocationScope&) = delete;
    AllocationScope& operator= (const AllocationScope&) = delete;
    /** Allocations since the scope began. */
    int count() const noexcept;
};

} // namespace osp::test
