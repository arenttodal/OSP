#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "audio/voices/SamplerVoice.h"
#include "core/Prng.h"
#include "model/PlaybackSource.h"

#include <array>
#include <cstdint>
#include <memory>

namespace osp
{

/**
    Per-note independent randomisation: the "simple randomized sampler" baseline (B).
    Deliberately naive (independent uniform draws) so the future correlated
    Performance Engine has an honest comparison point.
*/
struct RandomizationSettings
{
    bool enabled = false;
    double gainDb = 1.5;          ///< +/- range
    double detuneCents = 8.0;     ///< +/- range
    double startOffsetMs = 4.0;   ///< 0..range, skipped into the source
};

struct SamplerSettings
{
    static constexpr int maxPolyphony = 64;

    int polyphony = 24;
    AdsrSettings adsr {};
    double velocityRangeDb = 30.0;      ///< velocity 1 is this many dB below velocity 127
    double outputGainDb = -9.0;         ///< fixed headroom for polyphony; the source is never normalised
    int interpolationZeroCrossings = 16;
    double stealFadeSeconds = 0.005;
    std::uint64_t seed = 1;
    RandomizationSettings randomization {};
};

/**
    Baseline A (and, with randomisation, baseline B): the simplest scientifically
    useful polyphonic sampler.

    - chromatic playback of one PlaybackSource by bandlimited resampling
    - velocity -> gain only
    - ADSR, sustain pedal, voice stealing (released -> quietest -> oldest) with a
      short fade into spare "tail" slots so stealing does not click

    Threading: prepare() and setSettings() allocate and must be called off the audio
    thread. setSource(), the note functions and render() are real-time safe.
*/
class BaselineSampler
{
public:
    BaselineSampler();

    void prepare (double outputSampleRate, int maximumBlockSize, const SamplerSettings& settings);

    /** The source must stay alive while any voice may read it. Pass nullptr to stop new notes. */
    void setSource (const PlaybackSource* source) noexcept { currentSource = source; }

    /**
        Zero padding a PlaybackSource needs for this sampler's interpolator: a voice keeps
        reading until its kernel has fully left the source, so reads extend up to two
        kernel reaches past either end.
    */
    int requiredSourcePadding() const noexcept { return interpolator ? 2 * interpolator->maxReach() + 4 : 0; }

    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;
    void reset() noexcept;

    /** Overwrites `numSamples` of output (1 or 2 channels). */
    void render (float* const* output, int numChannels, int numSamples) noexcept;

    int activeVoiceCount() const noexcept;

    /** True if a voice for this note is sounding and not being stolen (UI keyboard, tests). */
    bool isNoteActive (int note) const noexcept;
    const SamplerSettings& settings() const noexcept { return config; }
    std::uint64_t noteOnCount() const noexcept { return noteCounter; }

    /** Pure function used by tests and analysis tools: source increment for a note. */
    static double incrementFor (double note, double rootMidi, double sourceRate, double outputRate) noexcept;

private:
    static constexpr int tailSlots = 16;
    static constexpr int totalSlots = SamplerSettings::maxPolyphony + tailSlots;

    SamplerVoice* findFreeSlot() noexcept;
    SamplerVoice* chooseVictim() noexcept;
    int countSoundingVoices() const noexcept;

    SamplerSettings config;
    double sampleRate = 48000.0;
    std::unique_ptr<SincInterpolator> interpolator;
    std::array<SamplerVoice, totalSlots> voices;
    const PlaybackSource* currentSource = nullptr;
    bool pedalDown = false;
    std::uint64_t noteCounter = 0;
    float outputGain = 1.0f;
};

} // namespace osp
