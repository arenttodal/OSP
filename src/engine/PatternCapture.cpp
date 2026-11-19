#include "engine/PatternCapture.h"

#include <algorithm>

namespace osp
{

void PatternCapture::start (double sampleRate) noexcept
{
    isArmed.store (false, std::memory_order_release);
    // (the audio thread pushes only while armed: the ring is ours until we arm it again)
    head.store (0, std::memory_order_relaxed);
    tail.store (0, std::memory_order_relaxed);
    overflow.store (false, std::memory_order_relaxed);
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    taken.clear();
    open.fill (0);
    origin = -1;
    lastSample = 0;
    firstBpm = 120.0;
    isFull = false;
    isArmed.store (true, std::memory_order_release);
}

void PatternCapture::stop()
{
    isArmed.store (false, std::memory_order_release);
    poll();
    // Notes still held end where the take ended.
    for (int n = 0; n < 128; ++n)
        if (const int i = open[static_cast<std::size_t> (n)]; i > 0)
        {
            auto& note = taken[static_cast<std::size_t> (i - 1)];
            note.duration = std::max (1.0 / 64.0, quartersAt (lastSample, firstBpm) - note.onset);
            open[static_cast<std::size_t> (n)] = 0;
        }
}

void PatternCapture::cancel() noexcept
{
    isArmed.store (false, std::memory_order_release);
    taken.clear();
    open.fill (0);
}

double PatternCapture::quartersAt (std::int64_t sample, double bpm) const noexcept
{
    return origin < 0 ? 0.0 : static_cast<double> (sample - origin) / rate * bpm / 60.0;
}

bool PatternCapture::poll()
{
    bool changed = false;
    int t = tail.load (std::memory_order_relaxed);
    const int h = head.load (std::memory_order_acquire);
    while (t != h)
    {
        const auto e = ring[static_cast<std::size_t> (t)];
        t = (t + 1) % ringCapacity;
        changed = true;
        lastSample = std::max (lastSample, e.sample);
        if (e.velocity > 0)
        {
            if (origin < 0)
            {
                origin = e.sample;   // the first note is the take's origin
                firstBpm = std::clamp (static_cast<double> (e.bpm), 5.0, 1000.0);
            }
            if (static_cast<int> (taken.size()) >= arppattern::maxCaptureNotes)
            {
                isFull = true;
                continue;
            }
            // The same key again while it sounds: the first ends here.
            if (const int i = open[e.note]; i > 0)
                taken[static_cast<std::size_t> (i - 1)].duration = std::max (1.0 / 64.0, quartersAt (e.sample, firstBpm) - taken[static_cast<std::size_t> (i - 1)].onset);
            taken.push_back ({ quartersAt (e.sample, firstBpm), 1.0 / 64.0, e.note, e.velocity });
            open[e.note] = static_cast<int> (taken.size());
        }
        else if (const int i = open[e.note]; i > 0)
        {
            auto& note = taken[static_cast<std::size_t> (i - 1)];
            note.duration = std::max (1.0 / 64.0, quartersAt (e.sample, firstBpm) - note.onset);
            open[e.note] = 0;
        }
    }
    tail.store (t, std::memory_order_release);
    if (overflow.load (std::memory_order_relaxed))
        isFull = true;
    return changed;
}

void PatternCapture::push (int offset, int note, int velocity, double bpm) noexcept
{
    if (! isArmed.load (std::memory_order_acquire) || note < 0 || note > 127)
        return;
    const int h = head.load (std::memory_order_relaxed);
    const int next = (h + 1) % ringCapacity;
    if (next == tail.load (std::memory_order_acquire))
    {
        overflow.store (true, std::memory_order_relaxed);
        return;
    }
    ring[static_cast<std::size_t> (h)] = { clock + std::max (0, offset), static_cast<float> (bpm), static_cast<std::uint8_t> (note),
                                           static_cast<std::uint8_t> (std::clamp (velocity, 0, 127)) };
    head.store (next, std::memory_order_release);
}

void PatternCapture::advance (int numSamples) noexcept
{
    clock += std::max (0, numSamples);
}

} // namespace osp
