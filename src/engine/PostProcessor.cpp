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
    echo.prepare (rate);
    drive.prepare (rate);
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
    appliedSpace = SpaceReverb::Settings::from (shaping);
    sizeSettled = 0;
    activeReverb = 0;
    reverbFade = 0;
    reverbAsleep = false;
    quietRun = 0;
    for (auto& r : reverbs)
    {
        r.configure (appliedSpace);
        r.reset();
    }
    spaceIdle = space < 1.0e-5;
    echoLevel = echoTarget;
    echo.setSettings (EchoDelay::Settings::from (shaping));
    echo.reset();
    echoIdle = echoLevel < 1.0e-5;
    drive.reset();
    echoAsleep = false;
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
    // ECHO is a send too: 50 % sits the repeats just under the dry sound, 100 % level with it.
    echoTarget = std::pow (std::clamp (macros.echo, 0.0, 1.0), 1.4);
    movement.setTargets (shaping, motionTarget);
    driveAmount = std::clamp (macros.drive, 0.0, 1.0);
    drive.setSettings (DriveProcessor::Settings::from (shaping, driveAmount));
}

void PostProcessor::setShaping (const Shaping& newShaping) noexcept
{
    shaping = newShaping;
    movement.setTargets (shaping, motionTarget);
    echo.setSettings (EchoDelay::Settings::from (shaping));
    drive.setSettings (DriveProcessor::Settings::from (shaping, driveAmount));
}

void PostProcessor::setTiming (const HostTiming& timing) noexcept
{
    movement.setTiming (timing);
    if (timing.bpm > 1.0)
        echo.setTempo (timing.bpm);   // ECHO follows the tempo even when the host gives no position
}

void PostProcessor::updateCoefficients() noexcept
{
    reimaginedStage.update();

    // SPACE: a new type or size fades in on the idle reverb (a size being dragged waits
    // until it rests for ~60 ms); everything else retunes the playing one in place.
    const auto wanted = SpaceReverb::Settings::from (shaping);
    sizeSettled = std::abs (wanted.size - lastWantedSize) > 1.0e-6 ? 0 : sizeSettled + 1;
    lastWantedSize = wanted.size;
    const bool structural = wanted.structurallyDifferent (appliedSpace);
    if (structural && reverbFade == 0 && (wanted.type != appliedSpace.type || sizeSettled >= 90))
    {
        appliedSpace = wanted;
        activeReverb = 1 - activeReverb;
        auto& next = reverbs[static_cast<std::size_t> (activeReverb)];
        next.configure (appliedSpace);
        next.reset();
        reverbFade = reverbFadeLength;
    }
    else
    {
        auto tuned = appliedSpace;
        // DECAY moves gradually: at most 3 % per control tick, so the tail never jumps.
        if (std::abs (wanted.decaySeconds - tuned.decaySeconds) > 1.0e-3)
            tuned.decaySeconds += std::clamp (wanted.decaySeconds - tuned.decaySeconds, -0.03 * tuned.decaySeconds, 0.03 * tuned.decaySeconds);
        tuned.preDelayMs = wanted.preDelayMs;
        tuned.damping = wanted.damping;
        tuned.modulation = wanted.modulation;
        tuned.width = wanted.width;
        tuned.lowCutHz = wanted.lowCutHz;
        tuned.highCutHz = wanted.highCutHz;
        const bool moved = tuned.decaySeconds != appliedSpace.decaySeconds || tuned.preDelayMs != appliedSpace.preDelayMs
                           || tuned.damping != appliedSpace.damping || tuned.modulation != appliedSpace.modulation
                           || tuned.width != appliedSpace.width || tuned.lowCutHz != appliedSpace.lowCutHz
                           || tuned.highCutHz != appliedSpace.highCutHz;
        if (moved)
        {
            appliedSpace = tuned;
            reverbs[static_cast<std::size_t> (activeReverb)].tune (appliedSpace);
        }
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

        drive.process (l, r);   // returns at once (untouched) while DRIVE is 0

        movement.process (l, r);

        // ECHO as a send, in parallel with SPACE (each hears the dry sound only).
        echoLevel += (echoTarget - echoLevel) * spaceCoef;
        float el = 0.0f, er = 0.0f;
        if (echoLevel > 1.0e-5)
        {
            if (echoIdle)
            {
                echoIdle = false;   // old repeats never come back after the send was off
                echo.reset();
            }
            if (echoAsleep && (l != 0.0f || r != 0.0f))
                echoAsleep = false;
            if (echoAsleep)
                echo.skip();
            else
            {
                echo.process (l, r, el, er);
                echoAsleep = echo.silent();
            }
        }
        else
            echoIdle = true;
        const auto echoGain = static_cast<float> (echoLevel);

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
            }
            else
            {
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
        }
        else
            spaceIdle = true;

        l += echoGain * el;
        r += echoGain * er;

        left[i] = l;
        right[i] = r;
    }
}

} // namespace osp
