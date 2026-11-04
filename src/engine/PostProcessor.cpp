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
    movementB.prepare (rate, seed + 0x9e3779b97f4a7c15ull);   // (its own randomness)
    laneCoef = 1.0 - std::exp (-1.0 / (0.006 * rate));          // a lane on / off: in or out within ~30 ms
    guardCoef = 1.0 - std::exp (-1.0 / (0.3 * rate));
    guardSmooth = 1.0 - std::exp (-1.0 / (0.05 * rate));
    for (auto& r : reverbs)
        r.prepare (rate);
    for (auto& a : abysses)
        a.prepare (rate);
    for (auto& v : vintages)
        v.prepare (rate);
    for (auto& b : blooms)
        b.prepare (rate);
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
    applyMovementTargets();
    movement.reset();
    movementB.reset();
    laneA.mix = laneA.on ? 1.0 : 0.0;
    laneB.mix = laneB.on ? 1.0 : 0.0;
    guardIn = guardOut = 0.0;
    guardGain = 1.0;
    appliedSpace = SpaceReverb::Settings::from (shaping);
    appliedAbyss = AbyssReverb::Settings::from (shaping);
    appliedVintage = VintageReverb::Settings::from (shaping);
    appliedBloom = BloomReverb::Settings::from (shaping);
    appliedEngine = shaping.spaceEngine;
    sizeSettled = 0;
    activeReverb = 0;
    reverbFade = 0;
    reverbAsleep = false;
    quietRun = 0;
    for (int slot = 0; slot < 2; ++slot)
        startSlot (slot);
    spaceIdle = space < 1.0e-5;
    echoLevel = echoTarget;
    echo.setSettings (EchoDelay::Settings::from (shaping));
    echo.reset();
    echoIdle = echoLevel < 1.0e-5;
    drive.reset();
    echoAsleep = false;
}

void PostProcessor::startSlot (int slot) noexcept
{
    // A slot starts the engine SPACE plays now, from silence, at the applied settings.
    const auto s = static_cast<std::size_t> (slot);
    slotEngine[s] = appliedEngine;
    switch (appliedEngine)
    {
        case SpaceEngine::abyss:
            abysses[s].configure (appliedAbyss);
            abysses[s].reset();
            break;
        case SpaceEngine::vintage:
            vintages[s].configure (appliedVintage);
            vintages[s].reset();
            break;
        case SpaceEngine::bloom:
            blooms[s].configure (appliedBloom);
            blooms[s].reset();
            break;
        case SpaceEngine::rooms:
            reverbs[s].configure (appliedSpace);
            reverbs[s].reset();
            break;
    }
}

void PostProcessor::updateAbyss() noexcept
{
    // As the rooms below: SIZE (once it rests) fades in on the idle slot; DECAY glides; the
    // rest (GRAVITY, FREEZE, RESONANCE among them) retunes the playing ABYSS in place.
    const auto wanted = AbyssReverb::Settings::from (shaping);
    sizeSettled = std::abs (wanted.size - lastWantedSize) > 1.0e-6 ? 0 : sizeSettled + 1;
    lastWantedSize = wanted.size;
    // (A new variant fades in at once; a SIZE being dragged waits until it rests.)
    if (wanted.structurallyDifferent (appliedAbyss) && reverbFade == 0 && (wanted.variant != appliedAbyss.variant || sizeSettled >= 90))
    {
        appliedAbyss = wanted;
        activeReverb = 1 - activeReverb;
        startSlot (activeReverb);
        reverbFade = reverbFadeLength;
        return;
    }
    auto tuned = appliedAbyss;
    if (std::abs (wanted.decaySeconds - tuned.decaySeconds) > 1.0e-3)
        tuned.decaySeconds += std::clamp (wanted.decaySeconds - tuned.decaySeconds, -0.03 * tuned.decaySeconds, 0.03 * tuned.decaySeconds);
    tuned.preDelayMs = wanted.preDelayMs;
    tuned.damping = wanted.damping;
    tuned.modulation = wanted.modulation;
    tuned.width = wanted.width;
    tuned.lowCutHz = wanted.lowCutHz;
    tuned.highCutHz = wanted.highCutHz;
    tuned.gravity = wanted.gravity;
    tuned.freeze = wanted.freeze;
    tuned.resonance = wanted.resonance;
    const bool moved = tuned.decaySeconds != appliedAbyss.decaySeconds || tuned.preDelayMs != appliedAbyss.preDelayMs
                       || tuned.damping != appliedAbyss.damping || tuned.modulation != appliedAbyss.modulation
                       || tuned.width != appliedAbyss.width || tuned.lowCutHz != appliedAbyss.lowCutHz
                       || tuned.highCutHz != appliedAbyss.highCutHz || tuned.gravity != appliedAbyss.gravity
                       || tuned.freeze != appliedAbyss.freeze || tuned.resonance != appliedAbyss.resonance;
    if (moved)
    {
        appliedAbyss = tuned;
        abysses[static_cast<std::size_t> (activeReverb)].tune (appliedAbyss);
    }
}

void PostProcessor::updateVintage() noexcept
{
    // VARIANT, ERA and SIZE (once it rests) fade in on the idle slot; DECAY glides; the rest
    // retunes in place.
    const auto wanted = VintageReverb::Settings::from (shaping);
    sizeSettled = std::abs (wanted.size - lastWantedSize) > 1.0e-6 ? 0 : sizeSettled + 1;
    lastWantedSize = wanted.size;
    const bool rebuilt = wanted.variant != appliedVintage.variant || wanted.era != appliedVintage.era;
    if (wanted.structurallyDifferent (appliedVintage) && reverbFade == 0 && (rebuilt || sizeSettled >= 90))
    {
        appliedVintage = wanted;
        activeReverb = 1 - activeReverb;
        startSlot (activeReverb);
        reverbFade = reverbFadeLength;
        return;
    }
    auto tuned = appliedVintage;
    if (std::abs (wanted.decaySeconds - tuned.decaySeconds) > 1.0e-3)
        tuned.decaySeconds += std::clamp (wanted.decaySeconds - tuned.decaySeconds, -0.03 * tuned.decaySeconds, 0.03 * tuned.decaySeconds);
    tuned.preDelayMs = wanted.preDelayMs;
    tuned.damping = wanted.damping;
    tuned.modulation = wanted.modulation;
    tuned.width = wanted.width;
    tuned.lowCutHz = wanted.lowCutHz;
    tuned.highCutHz = wanted.highCutHz;
    tuned.diffusion = wanted.diffusion;
    const bool moved = tuned.decaySeconds != appliedVintage.decaySeconds || tuned.preDelayMs != appliedVintage.preDelayMs
                       || tuned.damping != appliedVintage.damping || tuned.modulation != appliedVintage.modulation
                       || tuned.width != appliedVintage.width || tuned.lowCutHz != appliedVintage.lowCutHz
                       || tuned.highCutHz != appliedVintage.highCutHz || tuned.diffusion != appliedVintage.diffusion;
    if (moved)
    {
        appliedVintage = tuned;
        vintages[static_cast<std::size_t> (activeReverb)].tune (appliedVintage);
    }
}

void PostProcessor::updateBloom() noexcept
{
    // Everything plays in place (its TIME glides): only DECAY is eased, as the others'.
    const auto wanted = BloomReverb::Settings::from (shaping);
    auto tuned = wanted;
    tuned.decaySeconds = appliedBloom.decaySeconds;
    if (std::abs (wanted.decaySeconds - tuned.decaySeconds) > 1.0e-3)
        tuned.decaySeconds += std::clamp (wanted.decaySeconds - tuned.decaySeconds, -0.03 * tuned.decaySeconds, 0.03 * tuned.decaySeconds);
    const auto& a = appliedBloom;
    const bool moved = tuned.decaySeconds != a.decaySeconds || tuned.preDelayMs != a.preDelayMs || tuned.size != a.size || tuned.damping != a.damping
                       || tuned.modulation != a.modulation || tuned.width != a.width || tuned.lowCutHz != a.lowCutHz || tuned.highCutHz != a.highCutHz
                       || tuned.variant != a.variant || tuned.sync != a.sync || tuned.division != a.division || tuned.timeMs != a.timeMs
                       || tuned.diffusion != a.diffusion;
    if (moved)
    {
        appliedBloom = tuned;
        blooms[static_cast<std::size_t> (activeReverb)].tune (appliedBloom);
    }
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
    applyMovementTargets();
    driveAmount = std::clamp (macros.drive, 0.0, 1.0);
    drive.setSettings (DriveProcessor::Settings::from (shaping, driveAmount));
}

void PostProcessor::setShaping (const Shaping& newShaping) noexcept
{
    shaping = newShaping;
    applyMovementTargets();
    echo.setSettings (EchoDelay::Settings::from (shaping));
    drive.setSettings (DriveProcessor::Settings::from (shaping, driveAmount));
}

void PostProcessor::applyMovementTargets() noexcept
{
    if (! lanes)
    {
        movement.setTargets (shaping, motionTarget);   // one lane: as always
        return;
    }
    movement.setTargets (shaping, motionTarget * std::clamp (laneA.level, 0.0, 1.0));
    movementB.setTargets (laneBSettings, motionTarget * std::clamp (laneB.level, 0.0, 1.0));
}

void PostProcessor::setMovementLanes (bool aOn, double aLevel, bool bOn, double bLevel, const Shaping& bSettings) noexcept
{
    lanes = true;
    laneA.on = aOn;
    laneA.level = aLevel;
    if (bOn && ! laneB.on && laneB.mix <= 0.0)
        movementB.reset();   // (comes in clean: no old delay line, its clock from the start)
    laneB.on = bOn;
    laneB.level = bLevel;
    laneBSettings = bSettings;
    applyMovementTargets();
}

void PostProcessor::processLanes (float& l, float& r) noexcept
{
    auto glide = [this] (Lane& lane) {
        const double target = lane.on ? 1.0 : 0.0;
        lane.mix += (target - lane.mix) * laneCoef;
        if (std::abs (target - lane.mix) < 1.0e-5)
            lane.mix = target;
    };
    glide (laneA);
    glide (laneB);
    if (laneA.mix > 0.0)
    {
        float al = l, ar = r;
        movement.process (al, ar);
        if (laneA.mix >= 1.0)
        {
            l = al;
            r = ar;
        }
        else
        {
            const auto w = static_cast<float> (laneA.mix);
            l += w * (al - l);
            r += w * (ar - r);
        }
    }
    if (laneB.mix > 0.0)
    {
        float bl = l, br = r;
        movementB.process (bl, br);
        // The guard: TAPE and CHORUS after lane A never raise the level (slow, at most -6 dB).
        double target = 1.0;
        if (laneBSettings.movementMode == MovementMode::tape || laneBSettings.movementMode == MovementMode::chorus)
        {
            guardIn += (0.5 * (static_cast<double> (l) * l + static_cast<double> (r) * r) - guardIn) * guardCoef;
            guardOut += (0.5 * (static_cast<double> (bl) * bl + static_cast<double> (br) * br) - guardOut) * guardCoef;
            if (guardOut > guardIn && guardOut > 1.0e-12)
                target = std::max (0.5, std::sqrt (guardIn / guardOut));
        }
        guardGain += (target - guardGain) * guardSmooth;
        bl *= static_cast<float> (guardGain);
        br *= static_cast<float> (guardGain);
        const auto w = static_cast<float> (laneB.mix);
        l += w * (bl - l);
        r += w * (br - r);
    }
}

void PostProcessor::setTiming (const HostTiming& timing) noexcept
{
    movement.setTiming (timing);
    movementB.setTiming (timing);
    if (timing.bpm > 1.0)
    {
        echo.setTempo (timing.bpm);   // ECHO follows the tempo even when the host gives no position
        for (auto& b : blooms)
            b.setTempo (timing.bpm);  // (and BLOOM)
    }
}

void PostProcessor::updateCoefficients() noexcept
{
    reimaginedStage.update();

    // SPACE's engine changed: the new one fades in on the idle slot (as a new type does).
    if (shaping.spaceEngine != appliedEngine)
    {
        if (reverbFade == 0)
        {
            appliedEngine = shaping.spaceEngine;
            appliedSpace = SpaceReverb::Settings::from (shaping);
            appliedAbyss = AbyssReverb::Settings::from (shaping);
            appliedVintage = VintageReverb::Settings::from (shaping);
            appliedBloom = BloomReverb::Settings::from (shaping);
            lastWantedSize = shaping.spaceSize;
            activeReverb = 1 - activeReverb;
            startSlot (activeReverb);
            reverbFade = reverbFadeLength;
        }
        return;
    }
    if (appliedEngine == SpaceEngine::abyss)
    {
        updateAbyss();
        return;
    }
    if (appliedEngine == SpaceEngine::vintage)
    {
        updateVintage();
        return;
    }
    if (appliedEngine == SpaceEngine::bloom)
    {
        updateBloom();
        return;
    }

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

        if (lanes)
            processLanes (l, r);
        else
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
                for (int slot = 0; slot < 2; ++slot)
                    startSlot (slot);
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
                if (slotEngine[static_cast<std::size_t> (activeReverb)] == SpaceEngine::abyss)
                    abysses[static_cast<std::size_t> (activeReverb)].skip();
                else if (slotEngine[static_cast<std::size_t> (activeReverb)] == SpaceEngine::rooms)
                    reverbs[static_cast<std::size_t> (activeReverb)].skip();   // (VINTAGE and BLOOM keep no clocks asleep)
            }
            else
            {
                float wl, wr;
                processSlot (activeReverb, l, r, wl, wr);
                if (silentIn && reverbFade == 0 && std::abs (wl) < 1.0e-6f && std::abs (wr) < 1.0e-6f)
                    reverbAsleep = ++quietRun >= sleepAfter;
                else
                    quietRun = 0;
                if (reverbFade > 0)
                {
                    float ol, orr;
                    processSlot (1 - activeReverb, l, r, ol, orr);
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
