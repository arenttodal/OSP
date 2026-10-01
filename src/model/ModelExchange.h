#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace osp
{

/**
    Lock-free hand-over of immutable models (e.g. a loaded instrument) from the
    message/worker side to the audio thread, with deferred destruction.

    T must expose a `std::uint64_t generation` that increases with every published model.

    - Message thread: publish() a new model; call collectGarbage() periodically.
    - Audio thread:   takePending() at the start of a block; when it returns a model,
                      start using it. After deciding which older models voices still
                      read from, report the oldest generation in use with
                      publishOldestInUse().

    Models are only destroyed on the message thread, and only once the audio thread has
    reported that nothing older than them is in use. The audio thread never allocates,
    frees or locks. A model that was published but superseded before the audio thread
    saw it is freed once the newer one is in use.
*/
template <typename T>
class ModelExchange
{
public:
    /** Message thread. */
    void publish (std::shared_ptr<const T> model)
    {
        if (model == nullptr)
            return;
        latest = model;
        owned.push_back (std::move (model));
        pending.store (latest.get(), std::memory_order_release);
    }

    /** Message thread. Frees every model older than the oldest one still in use. */
    void collectGarbage()
    {
        const auto oldest = oldestInUse.load (std::memory_order_acquire);
        owned.erase (std::remove_if (owned.begin(), owned.end(),
                                     [&] (const auto& m) { return m != latest && m->generation < oldest; }),
                     owned.end());
    }

    /** Message thread: the most recently published model (for UI display). */
    std::shared_ptr<const T> latestModel() const { return latest; }

    /** Message thread: number of models still alive (tests, diagnostics). */
    std::size_t ownedCount() const noexcept { return owned.size(); }

    /** Audio thread. Returns a newly published model once, else nullptr. */
    const T* takePending() noexcept { return pending.exchange (nullptr, std::memory_order_acq_rel); }

    /** Audio thread. */
    void publishOldestInUse (std::uint64_t generation) noexcept
    {
        oldestInUse.store (generation, std::memory_order_release);
    }

private:
    std::vector<std::shared_ptr<const T>> owned;
    std::shared_ptr<const T> latest;
    std::atomic<const T*> pending { nullptr };
    std::atomic<std::uint64_t> oldestInUse { 0 };
};

} // namespace osp
