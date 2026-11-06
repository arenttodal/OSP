#pragma once

#include "core/AudioData.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace osp::capture
{

/** Live capture's limits: a take lasts at most this long (RECORD stops itself there). */
constexpr double maxTakeSeconds = 60.0;
/** The ring keeps a little more than a take, so a take being copied out is never overwritten
    while the input keeps arriving. */
constexpr double ringMarginSeconds = 4.0;

/**
    The live input, always kept: the last `maxTakeSeconds` (and a margin) of what arrived, in a
    ring allocated once. The audio thread only writes it (write: a copy per channel and one
    atomic store, no allocation, no lock). Any other thread copies a stretch out by absolute
    position (copy): RECORD remembers where it started and stopped, GRAB takes the last N
    seconds. A stretch the writer may already have overwritten is refused, never returned torn.

    Positions are frames counted since prepare (int64: no wrap in practice).
*/
class CaptureBuffer
{
public:
    /** Allocates (not the audio thread): two channels of `seconds` + the margin. */
    void prepare (double sampleRate, double seconds = maxTakeSeconds);
    /** Frees the ring (not while the audio thread may write). */
    void release();
    bool isPrepared() const noexcept { return capacity > 0; }
    double sampleRate() const noexcept { return rate; }

    // -- Audio thread --------------------------------------------------------------------------
    /** Appends a block (one channel is kept as both; channels past the second are ignored). */
    void write (const float* const* input, int numChannels, int numSamples) noexcept;

    // -- Any thread ----------------------------------------------------------------------------
    /** Frames written since prepare. */
    std::int64_t position() const noexcept { return written.load (std::memory_order_acquire); }
    /** The oldest frame that can still be copied out safely. */
    std::int64_t oldestSafe() const noexcept;
    /** The loudest sample since the last call (the input meter), 0..; resets it. */
    float takePeak() noexcept { return peak.exchange (0.0f, std::memory_order_relaxed); }

    // -- Not the audio thread ------------------------------------------------------------------
    /** Frames [from, to) as audio: two channels, or one when both are the same (a mono input).
        False when the stretch is not (or no longer) in the ring: `out` is then left empty. */
    bool copy (std::int64_t from, std::int64_t to, AudioData& out) const;

private:
    std::vector<std::vector<float>> ring;   ///< two channels
    std::int64_t capacity = 0;              ///< frames per channel
    std::int64_t margin = 0;                ///< frames the writer may be ahead of `written`
    double rate = 0.0;
    std::atomic<std::int64_t> written { 0 };
    std::atomic<float> peak { 0.0f };
};

/** A take ready to keep: the silence before and after the sound cut (a short lead-in and a
    longer tail kept, so attacks and releases stay whole). Empty when nothing reached
    `floorDb` (dBFS, peak over 10 ms windows). */
AudioData trimTake (const AudioData& take, double floorDb = -50.0, double leadSeconds = 0.05, double tailSeconds = 0.25);

} // namespace osp::capture
