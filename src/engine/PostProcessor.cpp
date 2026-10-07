#include "engine/PostProcessor.h"

#include "engine/InstrumentEngine.h"
#include "engine/Shaping.h"
#include "model/InstrumentModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

void PostProcessor::prepare (double rate, int /*maximumBlockSize*/, std::uint64_t seed)
{
    sampleRate = rate;
    reimaginedStage.prepare (rate, seed);
    movement.prepare (rate, seed);
    for (auto& r : reverbs)
        r.prepare (rate);
    reverbFadeLength = std::max (1, static_cast<int> (0.25 * rate));
    spaceCoef = 1.0 - std::exp (-1.0 / (0.05 * rate));
    sleepAfter = std::max (1, static_cast<int> (0.25 * rate));
    reset();
}

void PostProcessor::reset() noexcept
{
    reimaginedStage.reset();
    space = spaceTarget;
    countdown = 0;
    movement.setTargets (shaping, motionTarget);
    movement.reset();
    appliedType = shaping.spaceType;
    appliedDecay = shaping.spaceDecaySeconds;
    activeReverb = 0;
    reverbFade = 0;
    reverbAsleep = false;
    quietRun = 0;
    for (auto& r : reverbs)
    {
        r.configure (appliedType, appliedDecay);
        r.reset();
    }
    spaceIdle = space < 1.0e-5;
}

void PostProcessor::setModel (const InstrumentModel* newModel) noexcept
{
    reimaginedStage.setModel (newModel);
}

void PostProcessor::setMacros (const Macros& macros) noexcept
{
    reimaginedStage.setAmount (macros.reimagined);
    motionTarget = std::clamp (macros.motion, 0.0, 1.0);
    // SPACE is a send: perceptual wet level (10 % is a touch, 100 % is drenched).
    spaceTarget = 1.25 * std::pow (std::clamp (macros.space, 0.0, 1.0), 1.2);
    movement.setTargets (shaping, motionTarget);
}

void PostProcessor::setShaping (const Shaping& newShaping) noexcept
{
    shaping = newShaping;
    movement.setTargets (shaping, motionTarget);
}

void PostProcessor::updateCoefficients() noexcept
{
    reimaginedStage.update();

    // SPACE: a new type fades in on the idle reverb; a new decay retunes in place.
    if (shaping.spaceType != appliedType && reverbFade == 0)
    {
        appliedType = shaping.spaceType;
        appliedDecay = shaping.spaceDecaySeconds;
        activeReverb = 1 - activeReverb;
        auto& next = reverbs[static_cast<std::size_t> (activeReverb)];
        next.configure (appliedType, appliedDecay);
        next.reset();
        reverbFade = reverbFadeLength;
    }
    else if (std::abs (shaping.spaceDecaySeconds - appliedDecay) > 1.0e-3)
    {
        // Gradual: at most 3 % per control tick, so the tail never jumps.
        appliedDecay += std::clamp (shaping.spaceDecaySeconds - appliedDecay, -0.03 * appliedDecay, 0.03 * appliedDecay);
        reverbs[static_cast<std::size_t> (activeReverb)].configure (appliedType, appliedDecay);
    }
}

void PostProcessor::process (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (countdown-- <= 0)
        {
            countdown = controlInterval - 1;
            updateCoefficients();
        }
        float l = left[i];
        float r = right[i];

        reimaginedStage.process (l, r);

        movement.process (l, r);

        // SPACE as a send. Idle (no wet level for a while) skips the reverb entirely.
        space += (spaceTarget - space) * spaceCoef;
        if (space > 1.0e-5 || reverbFade > 0)
        {
            if (spaceIdle)
            {
                spaceIdle = false;
                for (auto& rv : reverbs)
                    rv.reset();
            }
            const bool silentIn = l == 0.0f && r == 0.0f;
            if (reverbAsleep && (! silentIn || reverbFade > 0))
            {
                reverbAsleep = false;
                quietRun = 0;
            }
            if (reverbAsleep)
            {
                // Silence in, a tail below -120 dBFS: the output is silence.
                reverbs[static_cast<std::size_t> (activeReverb)].skip();
                left[i] = l;
                right[i] = r;
                continue;
            }
            float wl, wr;
            reverbs[static_cast<std::size_t> (activeReverb)].process (l, r, wl, wr);
            if (silentIn && reverbFade == 0 && std::abs (wl) < 1.0e-6f && std::abs (wr) < 1.0e-6f)
                reverbAsleep = ++quietRun >= sleepAfter;
            else
                quietRun = 0;
            if (reverbFade > 0)
            {
                float ol, orr;
                reverbs[static_cast<std::size_t> (1 - activeReverb)].process (l, r, ol, orr);
                const float w = static_cast<float> (reverbFade) / static_cast<float> (reverbFadeLength);
                wl += w * (ol - wl);
                wr += w * (orr - wr);
                --reverbFade;
            }
            const auto wet = static_cast<float> (space);
            const auto dry = static_cast<float> (1.0 - 0.2 * std::min (1.0, space));
            l = dry * l + wet * wl;
            r = dry * r + wet * wr;
        }
        else
            spaceIdle = true;

        left[i] = l;
        right[i] = r;
    }
}

} // namespace osp
