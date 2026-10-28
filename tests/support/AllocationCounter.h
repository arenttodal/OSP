#pragma once

#include <atomic>

namespace osp::test
{

/**
    Counts heap allocations (the test binary replaces the global operator new) made on the
    scope's own thread while it is alive: real-time code must make none. One scope at a time.
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
