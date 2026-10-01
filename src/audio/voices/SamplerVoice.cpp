#include "audio/voices/SamplerVoice.h"

#include <algorithm>

namespace osp
{

void SamplerVoice::prepare (double outputSampleRate, const AdsrSettings& adsr, const SincInterpolator* interpolator) noexcept
{
    sampleRate = outputSampleRate;
    sinc = interpolator;
    envelope.prepare (outputSampleRate, adsr);
    kill();
}

void SamplerVoice::start (const VoiceStartParams& params) noexcept
{
    src = params.source;
    currentNote = params.note;
    order = params.startOrder;
    increment = params.increment;
    gain = params.gain;
    position = params.startPosition;

    active = src != nullptr && src->isValid() && sinc != nullptr;
    released = false;
    heldByPedal = false;
    fadeRemaining = 0;
    fadeGain = 1.0f;
    fadeStep = 0.0f;

    envelope.reset();
    if (active)
        envelope.noteOn();
}

void SamplerVoice::release() noexcept
{
    if (! active || released)
        return;
    released = true;
    heldByPedal = false;
    envelope.noteOff();
}

void SamplerVoice::beginFastFade (int fadeSamples) noexcept
{
    if (! active)
        return;
    fadeRemaining = std::max (1, fadeSamples);
    fadeStep = fadeGain / static_cast<float> (fadeRemaining);
    released = true;
    heldByPedal = false;
}

void SamplerVoice::kill() noexcept
{
    active = false;
    released = false;
    heldByPedal = false;
    currentNote = -1;
    fadeRemaining = 0;
    fadeGain = 1.0f;
    envelope.reset();
}

void SamplerVoice::render (float* left, float* right, int numSamples) noexcept
{
    if (! active)
        return;

    const float* dataL = src->channelData (0);
    const float* dataR = src->channelData (1);
    const bool stereoSource = src->numChannels() > 1;
    const bool stereoOutput = right != left;

    // Finished once every tap of the kernel lies beyond the last source sample.
    const double endPosition = static_cast<double> (src->numFrames()) + static_cast<double> (sinc->reachFor (increment));

    for (int i = 0; i < numSamples; ++i)
    {
        if (position >= endPosition)
        {
            kill();
            return;
        }

        float env = envelope.next() * gain;

        if (fadeRemaining > 0)
        {
            fadeGain -= fadeStep;
            env *= std::max (fadeGain, 0.0f);
            if (--fadeRemaining == 0)
            {
                kill();
                return;
            }
        }

        if (! envelope.isActive())
        {
            kill();
            return;
        }

        sinc->computeKernel (position, increment, kernel);
        const float l = SincInterpolator::apply (kernel, dataL);

        if (stereoOutput)
        {
            const float r = stereoSource ? SincInterpolator::apply (kernel, dataR) : l;
            left[i] += l * env;
            right[i] += r * env;
        }
        else
        {
            const float r = stereoSource ? SincInterpolator::apply (kernel, dataR) : l;
            left[i] += 0.5f * (l + r) * env;
        }

        position += increment;
    }
}

} // namespace osp
