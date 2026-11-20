#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <cstdint>

namespace osp
{

/**
    What the DSP actually plays, for the pictures (read-only telemetry; universal
    modulation-aware visual feedback). One slot per parameter a product chooses to show: the
    value the sound engine uses after modulation, macros, mapping and clamping, in the
    parameter's own plain units - so a picture drawing it is the picture of the same knob set
    by hand to that value. Never written back to the parameter (no automation, no undo).

    The audio thread publishes (fixed storage, no allocation, no locks); any other thread
    reads. Each slot is a small sequence lock: a reading is one block's value, never half of
    two. The range a slot covered over the last one to two windows (~33 ms each, a UI
    frame) comes with it, so a modulation faster than the UI's refresh shows as the span it
    sweeps, not as one aliased point.

    A slot is active while the engine has a value of its own for it (something moves it, or
    a voice sounds for a per-voice one); inactive, a picture draws the stored value.
*/
class EffectiveValues
{
public:
    static constexpr int capacity = 512;

    struct Reading
    {
        float value = 0.0f, low = 0.0f, high = 0.0f;
    };

    /** Audio thread, once per block before publishing: advances the range window. */
    void beginBlock (int numSamples, double sampleRate) noexcept
    {
        windowSamples += numSamples;
        if (static_cast<double> (windowSamples) >= sampleRate / 30.0)
        {
            windowSamples = 0;
            generation = generation + 1;
        }
    }

    /** Audio thread: this block's value for `slot`. */
    void publish (int slot, float value) noexcept
    {
        if (slot < 0 || slot >= capacity)
            return;
        auto& s = slots[static_cast<std::size_t> (slot)];
        const auto seq = s.seq.load (std::memory_order_relaxed);
        s.seq.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        if (s.writerGeneration != generation || ! s.writerActive)
        {
            // A new window: the last one's span is kept for the reader, this one starts here.
            s.previousLow.store (s.writerActive ? s.low.load (std::memory_order_relaxed) : value, std::memory_order_relaxed);
            s.previousHigh.store (s.writerActive ? s.high.load (std::memory_order_relaxed) : value, std::memory_order_relaxed);
            s.low.store (value, std::memory_order_relaxed);
            s.high.store (value, std::memory_order_relaxed);
            s.writerGeneration = generation;
        }
        else
        {
            s.low.store (std::min (s.low.load (std::memory_order_relaxed), value), std::memory_order_relaxed);
            s.high.store (std::max (s.high.load (std::memory_order_relaxed), value), std::memory_order_relaxed);
        }
        s.value.store (value, std::memory_order_relaxed);
        s.active.store (true, std::memory_order_relaxed);
        s.writerActive = true;
        s.seq.store (seq + 2, std::memory_order_release);
    }

    /** Audio thread: `slot` has no value of its own now (the picture draws the stored one). */
    void clear (int slot) noexcept
    {
        if (slot < 0 || slot >= capacity)
            return;
        auto& s = slots[static_cast<std::size_t> (slot)];
        if (! s.writerActive)
            return;
        const auto seq = s.seq.load (std::memory_order_relaxed);
        s.seq.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        s.active.store (false, std::memory_order_relaxed);
        s.writerActive = false;
        s.seq.store (seq + 2, std::memory_order_release);
    }

    /** Any thread but the audio thread: false while the slot is inactive (or unknown). */
    bool read (int slot, Reading& out) const noexcept
    {
        if (slot < 0 || slot >= capacity)
            return false;
        const auto& s = slots[static_cast<std::size_t> (slot)];
        for (int attempt = 0; attempt < 256; ++attempt)
        {
            const auto before = s.seq.load (std::memory_order_acquire);
            if ((before & 1u) != 0)
            {
                // Being written: try again; after a few spins let the writer finish (a writer
                // pre-empted mid-publish would otherwise make the picture fall back to the
                // stored value for a frame).
                if (attempt >= 8)
                    std::this_thread::yield();
                continue;
            }
            const bool active = s.active.load (std::memory_order_relaxed);
            Reading r;
            r.value = s.value.load (std::memory_order_relaxed);
            r.low = std::min (s.low.load (std::memory_order_relaxed), s.previousLow.load (std::memory_order_relaxed));
            r.high = std::max (s.high.load (std::memory_order_relaxed), s.previousHigh.load (std::memory_order_relaxed));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (s.seq.load (std::memory_order_relaxed) != before)
            {
                if (attempt >= 8)
                    std::this_thread::yield();
                continue;
            }
            if (! active)
                return false;
            out = r;
            return true;
        }
        return false;   // (a writer that never lets go: draw the stored value this frame)
    }

private:
    struct Slot
    {
        std::atomic<std::uint32_t> seq { 0 };
        std::atomic<float> value { 0.0f }, low { 0.0f }, high { 0.0f }, previousLow { 0.0f }, previousHigh { 0.0f };
        std::atomic<bool> active { false };
        // (the audio thread's own)
        std::uint32_t writerGeneration = 0;
        bool writerActive = false;
    };
    std::array<Slot, capacity> slots {};
    std::uint32_t generation = 1;
    int windowSamples = 0;
};

} // namespace osp
