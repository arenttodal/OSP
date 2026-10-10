#pragma once

#include "core/AudioData.h"

#include <cstdint>
#include <vector>

namespace osp::library
{

/**
    Finding the sounds in a recording (Stage 8, ported from Sample Forge's `slicer.js`, the
    user's own code, decision D-04): onsets are rises of the log-energy envelope against the
    recording's own noise floor, each moved back to where its attack begins; a slice ends when
    its sound has died into the floor, at the next onset, or at the longest slice allowed
    (longer than Sample Forge's 1.5 s: sustained notes are valid one-shots).

    Pure, deterministic, positions in frames. Background threads only.
*/
struct Slice
{
    std::int64_t start = 0, end = 0;   ///< frames, end exclusive
};

struct SliceOptions
{
    double sensitivity = 0.3;          ///< 0..1: higher finds softer hits (Sample Forge's SENS / 100)
    double maxSliceSeconds = 20.0;
    double minSliceSeconds = 0.02;
};

/** The mono mix-down every analysis here uses (the average of the channels). */
std::vector<float> monoOf (const AudioData& audio);

/** Frame energies in dB: RMS of 512 samples every 128. */
std::vector<float> energyEnvelope (const std::vector<float>& mono);

/** The recording's noise floor: the 10th percentile of its frame energies (dB). */
float noiseFloorDb (const std::vector<float>& envelope);

/** Onset frames (Sample Forge `detectOnsets`). */
std::vector<std::int64_t> detectOnsets (const std::vector<float>& mono, double sampleRate, double sensitivity = 0.3);

/** Where a sound starting at `start` has died away: three 10 ms windows below max(floor + 4 dB,
    its peak - 50 dB); or `limit`; or the longest slice (Sample Forge `sliceEnd`). */
std::int64_t sliceEnd (const std::vector<float>& mono, std::int64_t start, std::int64_t limit, double sampleRate, float floorDb,
                       double maxSliceSeconds);

/** Every sound in the recording (Sample Forge `sliceHits`). */
std::vector<Slice> sliceRecording (const AudioData& audio, const SliceOptions& options = {});

/**
    A conservative trim (8.3): from just before the first sound to just after the last sound
    has faded into the floor. Breath, bow noise and soft attacks are kept: the start is where
    the signal first rises 60 dB below its peak (and 6 dB over the floor), minus 10 ms; the
    end where it last is, plus 50 ms. Nothing found: the whole recording.
*/
Slice suggestTrim (const AudioData& audio);

struct RenderOptions
{
    double fadeInSeconds = 0.0005;     ///< Sample Forge's export values: 0.5 ms in, 5 ms out
    double fadeOutSeconds = 0.005;
    bool snapToZeroCrossing = false;   ///< move the start back to a zero crossing within 1 ms
    double gainDb = 0.0;               ///< optional gain (never automatic)
};

/** The region as a new recording (stereo kept), with anti-click fades. The source is never
    changed. */
AudioData renderSlice (const AudioData& audio, Slice region, const RenderOptions& options = {});

/** The gain that brings the region's peak to `targetDbfs` (e.g. -1), capped at +24 dB. */
double peakGainDb (const AudioData& audio, Slice region, double targetDbfs);

} // namespace osp::library
