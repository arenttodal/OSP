#pragma once

#include "audio/envelopes/Adsr.h"
#include "audio/pitch/SincInterpolator.h"
#include "model/PlaybackSource.h"

#include <cstdint>

namespace osp
{

/** Everything a voice needs to start a note. Computed by the sampler. */
struct VoiceStartParams
{
    const PlaybackSource* source = nullptr;
    int note = 60;
    int velocity = 100;
    double increment = 1.0;       ///< source samples per output sample (pitch * sample-rate ratio)
    float gain = 1.0f;            ///< linear note gain (velocity, randomisation)
    double startPosition = 0.0;   ///< in source samples
    std::uint64_t startOrder = 0; ///< monotonically increasing note-on counter (for "oldest")
};

/**
    One baseline sampler voice: bandlimited one-shot playback of a PlaybackSource with
    an ADSR. No looping (continuation is a later engine), so a voice ends either when
    its envelope finishes or when the recording runs out.

    Real-time safe: all state is inline; render() never allocates.
*/
class SamplerVoice
{
public:
    void prepare (double outputSampleRate, const AdsrSettings& adsr, const SincInterpolator* interpolator) noexcept;

    void start (const VoiceStartParams& params) noexcept;
    void release() noexcept;

    /** Fades the voice out quickly (voice stealing). The voice becomes inactive after the fade. */
    void beginFastFade (int fadeSamples) noexcept;

    /** Immediately silences the voice. */
    void kill() noexcept;

    /** Adds output into left/right (right may equal left for mono output). */
    void render (float* left, float* right, int numSamples) noexcept;

    bool isActive() const noexcept { return active; }
    bool isReleased() const noexcept { return released; }
    bool isFading() const noexcept { return fadeRemaining > 0; }
    bool isHeldByPedal() const noexcept { return heldByPedal; }
    void setHeldByPedal (bool held) noexcept { heldByPedal = held; }
    int note() const noexcept { return currentNote; }
    std::uint64_t startOrder() const noexcept { return order; }
    const PlaybackSource* source() const noexcept { return src; }

    /** Approximate current output level (envelope * gain), used for "quietest" stealing. */
    float currentLevel() const noexcept { return envelope.level() * gain * fadeGain; }

private:
    const SincInterpolator* sinc = nullptr;
    const PlaybackSource* src = nullptr;
    Adsr envelope;
    double sampleRate = 48000.0;

    bool active = false;
    bool released = false;
    bool heldByPedal = false;
    int currentNote = -1;
    std::uint64_t order = 0;

    double position = 0.0;
    double increment = 1.0;
    float gain = 1.0f;

    int fadeRemaining = 0;
    float fadeGain = 1.0f;
    float fadeStep = 0.0f;

    SincInterpolator::Kernel kernel;
};

} // namespace osp
