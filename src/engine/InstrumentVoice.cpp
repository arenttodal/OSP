#include "engine/InstrumentVoice.h"

#include "analysis/continuation/ContinuationAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace osp
{

namespace
{
    float dbToLinear (double db) noexcept { return static_cast<float> (std::pow (10.0, db / 20.0)); }

}

void InstrumentVoice::prepare (double outputSampleRate, const AdsrSettings& adsr, const SincInterpolator* interpolator) noexcept
{
    sampleRate = outputSampleRate;
    sinc = interpolator;
    adsrSettings = adsr;
    envelope.prepare (outputSampleRate, adsr);
    kill();
}

void InstrumentVoice::setEnvelopeSettings (const AdsrSettings& adsr) noexcept
{
    adsrSettings = adsr;
    envelope.prepare (sampleRate, adsr);
}

void InstrumentVoice::start (const InstrumentVoiceStart& params) noexcept
{
    currentModel = params.model;
    layer = params.layer;
    active = currentModel != nullptr && layer != nullptr && layer->source != nullptr && layer->source->isValid() && sinc != nullptr;
    if (! active)
        return;

    cont = &layer->continuation;
    currentNote = params.note;
    midiChannel = params.channel;
    order = params.startOrder;
    expressionGainDb = expressionBrightDb = 0.0f;
    expressionGain = 1.0;
    expressionBright = 0.0;
    shape = params.shape;
    strategy = cont->canSustain ? params.strategy : ContinuationStrategy::off;
    releaseGraftEnabled = params.releaseGraft;

    const auto& src = *layer->source;
    baseIncrement = params.increment;
    currentStep = baseIncrement;
    position = src.startFrame() + std::max (0.0, shape.startOffsetSeconds) * src.sampleRate();
    endPosition = static_cast<double> (src.numFrames()) + static_cast<double> (sinc->maxReach());

    released = false;
    heldByPedal = false;
    fadeRemaining = 0;
    fadeGain = 1.0f;
    fadeStep = 0.0f;
    hasPending = false;
    crossfading = false;
    graftPending = false;
    grafted = false;
    holdForTail = false;
    jumpsTaken = 0;
    recent.fill (-1);
    recentWrite = 0;
    rng.reseed (shape.seed);

    baseGain = shape.gain * static_cast<float> (src.playbackGain());
    transientExtra = dbToLinear (shape.transientDb) - 1.0f;
    transientCoef = static_cast<float> (std::exp (-1.0 / (std::max (0.002f, shape.transientSeconds) * sampleRate)));
    attackBright = shape.attackBrightnessDb;
    attackBrightCoef = std::exp (-static_cast<double> (controlInterval) / (std::max (0.002f, 1.5f * shape.transientSeconds) * sampleRate));
    dampingGain = 1.0f;
    // Only extra damping: "less damping than recorded" would mean unbounded growth.
    dampingCoef = dbToLinear (-std::clamp (static_cast<double> (shape.dampingDbPerSecond), 0.0, 60.0) / sampleRate);
    if (shape.attackSoftenSeconds > 0.0f)
    {
        attackRamp = 0.0f;
        attackRampStep = 1.0f / static_cast<float> (shape.attackSoftenSeconds * sampleRate);
    }
    else
    {
        attackRamp = 1.0f;
        attackRampStep = 0.0f;
    }
    settleCents = shape.pitchSettleCents;
    settleCoef = std::exp (-static_cast<double> (controlInterval) / (std::max (0.005, shape.pitchSettleSeconds) * sampleRate));
    const float p = std::clamp (shape.pan, -1.0f, 1.0f);
    panLeft = std::min (1.0f, 1.0f - p);
    panRight = std::min (1.0f, 1.0f + p);

    driftLevel = driftLevelTarget = 0.0;
    driftCentsValue = driftCentsTarget = 0.0;
    driftBright = driftBrightTarget = 0.0;
    driftPanValue = driftPanTarget = 0.0;
    saturationDrive = 1.0f + 4.0f * std::clamp (shape.saturation, 0.0f, 1.0f);
    saturationNorm = 1.0f / saturationDrive;
    driftCoef = 1.0 - std::exp (-static_cast<double> (controlInterval) * std::max (0.01, static_cast<double> (shape.driftRateHz)) * 2.0 / sampleRate);
    driftCountdown = 0;
    controlCountdown = 0;
    controlGain = 1.0f;
    controlGainStep = 0.0f;
    pitchMod = 1.0;

    highL.reset();
    highR.reset();
    lowL.reset();
    lowR.reset();
    appliedBright = appliedBody = 1.0e9f; // force coefficient update
    filtersActive = false;

    envelope.prepare (sampleRate, adsrSettings);
    envelope.reset();
    envelope.noteOn();

    scheduleNextJump();
}

void InstrumentVoice::release() noexcept
{
    if (! active || released)
        return;
    released = true;
    heldByPedal = false;

    const bool canGraft = releaseGraftEnabled && strategy != ContinuationStrategy::off && cont->hasRelease
                          && ! cont->graftExits.empty() && position < cont->releaseFrame;
    if (canGraft)
    {
        graftPending = true;
        // The recording's own ending replaces the envelope release unless the release
        // was deliberately set very short (staccato).
        holdForTail = adsrSettings.releaseSeconds >= 0.2 || adsrSettings.releaseSeconds >= 0.5 * cont->tailSeconds;
        if (! crossfading)
            scheduleGraft();
        if (! holdForTail)
            envelope.noteOff();
        return;
    }
    envelope.noteOff();
}

void InstrumentVoice::beginFastFade (int fadeSamples) noexcept
{
    if (! active)
        return;
    fadeRemaining = std::max (1, fadeSamples);
    fadeStep = fadeGain / static_cast<float> (fadeRemaining);
    released = true;
    heldByPedal = false;
}

void InstrumentVoice::kill() noexcept
{
    active = false;
    released = false;
    heldByPedal = false;
    currentNote = -1;
    fadeRemaining = 0;
    fadeGain = 1.0f;
    crossfading = false;
    hasPending = false;
    envelope.reset();
}

void InstrumentVoice::scheduleNextJump() noexcept
{
    hasPending = false;
    pendingIndex = -1;
    pendingIsGraft = false;
    if (graftPending)
    {
        scheduleGraft();
        return;
    }
    if (strategy == ContinuationStrategy::off || grafted)
        return;

    switch (strategy)
    {
        case ContinuationStrategy::naiveLoop:
            if (position < cont->naiveLoop.fromFrame)
            {
                pending = cont->naiveLoop;
                hasPending = true;
            }
            return;
        case ContinuationStrategy::bestLoop:
            if (cont->bestLoop >= 0)
            {
                const auto& j = cont->jumps[static_cast<std::size_t> (cont->bestLoop)];
                if (position < j.fromFrame)
                {
                    pending = j;
                    pendingIndex = cont->bestLoop;
                    hasPending = true;
                }
            }
            return;
        case ContinuationStrategy::multiLoop:
        case ContinuationStrategy::multiLoopMovement:
            break;
        case ContinuationStrategy::off:
            return;
    }

    // Random walk over compatible jumps: prefer good matches, avoid the last few jumps,
    // and vary how long each stretch plays before the next jump.
    const double sr = layer->source->sampleRate();
    // Reimagined / MOTION shorten the stretches between jumps (towards granular continuation).
    const double scale = std::clamp (static_cast<double> (shape.segmentScale), 0.2, 2.0);
    const double minFrom = position + cont->minSegmentFrames * std::min (1.0, scale);
    const double lookahead = minFrom + rng.uniform (0.4, 2.5) * sr * scale;
    auto weightOf = [&] (int index) -> double
    {
        const auto& j = cont->jumps[static_cast<std::size_t> (index)];
        if (j.fromFrame < minFrom)
            return 0.0;
        double w = 0.05 + static_cast<double> (j.score);
        for (int r : recent)
            if (r == index)
                w *= 0.1;
        if (j.fromFrame > lookahead)
            w *= 0.15;
        return w;
    };
    double total = 0.0;
    const int count = static_cast<int> (cont->jumps.size());
    for (int i = 0; i < count; ++i)
        total += weightOf (i);

    int chosen = -1;
    if (total > 0.0)
    {
        double pick = rng.nextDouble() * total;
        for (int i = 0; i < count; ++i)
        {
            pick -= weightOf (i);
            if (pick <= 0.0)
            {
                chosen = i;
                break;
            }
        }
        if (chosen < 0)
            chosen = count - 1;
    }
    else if (cont->backstop >= 0 && cont->jumps[static_cast<std::size_t> (cont->backstop)].fromFrame > position)
    {
        chosen = cont->backstop;
    }
    if (chosen >= 0)
    {
        pending = cont->jumps[static_cast<std::size_t> (chosen)];
        pendingIndex = chosen;
        hasPending = true;
    }
}

void InstrumentVoice::scheduleGraft() noexcept
{
    hasPending = false;
    pendingIsGraft = false;
    // First exit ahead of the read position; none left means we are already near the
    // original ending, which then simply plays.
    for (const auto& exit : cont->graftExits)
        if (exit.fromFrame >= position + 1.0)
        {
            pending = exit;
            pendingIndex = -1;
            pendingIsGraft = true;
            hasPending = true;
            return;
        }
    graftPending = false;
    grafted = true;
}

void InstrumentVoice::beginCrossfade() noexcept
{
    crossfading = true;
    xIsGraft = pendingIsGraft;
    xPosition = pending.toFrame + (position - pending.fromFrame);
    xProgress = 0.0;
    xLength = std::max (1.0, pending.crossfadeFrames);
    xCorrelation = pending.correlation;
    hasPending = false;
    if (pendingIndex >= 0)
    {
        recent[static_cast<std::size_t> (recentWrite)] = pendingIndex;
        recentWrite = (recentWrite + 1) % recentSize;
    }
    if (xIsGraft)
    {
        graftPending = false;
        grafted = true;
    }
    ++jumpsTaken;
}

void InstrumentVoice::updateControl() noexcept
{
    // Drift: slowly wandering targets, smoothed (strategy D / MOTION).
    const bool drifting = shape.driftLevelDb > 0.0f || shape.driftCents > 0.0f || shape.driftBrightnessDb > 0.0f || shape.driftPan > 0.0f;
    if (drifting)
    {
        if (--driftCountdown <= 0)
        {
            driftLevelTarget = shape.driftLevelDb * rng.bipolar();
            driftCentsTarget = shape.driftCents * rng.bipolar();
            driftBrightTarget = shape.driftBrightnessDb * rng.bipolar();
            driftPanTarget = shape.driftPan * rng.bipolar();
            const double seconds = rng.uniform (0.5, 1.5) / std::max (0.01, static_cast<double> (shape.driftRateHz));
            driftCountdown = std::max (1, static_cast<int> (seconds * sampleRate / controlInterval));
        }
        driftLevel += (driftLevelTarget - driftLevel) * driftCoef;
        driftCentsValue += (driftCentsTarget - driftCentsValue) * driftCoef;
        driftBright += (driftBrightTarget - driftBright) * driftCoef;
        driftPanValue += (driftPanTarget - driftPanValue) * driftCoef;
        if (shape.driftPan > 0.0f)
        {
            const float p = std::clamp (shape.pan + static_cast<float> (driftPanValue), -1.0f, 1.0f);
            panLeft = std::min (1.0f, 1.0f - p);
            panRight = std::min (1.0f, 1.0f + p);
        }
    }

    settleCents *= settleCoef;
    const double cents = shape.pitchCents + settleCents + driftCentsValue;
    pitchMod = cents != 0.0 ? std::exp2 (cents / 1200.0) : 1.0;

    // Expression follows quickly but smoothly (about 10 ms).
    expressionGain += (std::pow (10.0, expressionGainDb / 20.0) - expressionGain) * 0.15;
    expressionBright += (expressionBrightDb - expressionBright) * 0.15;
    const float target = (drifting ? dbToLinear (driftLevel) : 1.0f) * static_cast<float> (expressionGain);
    controlGainStep = (target - controlGain) / controlInterval;

    attackBright *= attackBrightCoef;
    const float bright = shape.brightnessDb + static_cast<float> (driftBright + attackBright + expressionBright);
    const float body = shape.bodyDb;
    if (std::abs (bright - appliedBright) > 0.02f || std::abs (body - appliedBody) > 0.02f)
    {
        appliedBright = bright;
        appliedBody = body;
        filtersActive = std::abs (bright) > 0.01f || std::abs (body) > 0.01f;
        if (filtersActive)
        {
            // Shelves follow the note: the source's spectrum moves with transposition.
            const double ratio = baseIncrement * sampleRate / layer->source->sampleRate();
            const double brightHz = currentModel->brightnessShelfHz * ratio;
            const double bodyHz = currentModel->bodyShelfHz * ratio;
            highL.setup (ShelfFilter::Type::high, sampleRate, brightHz, bright);
            highR.setup (ShelfFilter::Type::high, sampleRate, brightHz, bright);
            lowL.setup (ShelfFilter::Type::low, sampleRate, bodyHz, body);
            lowR.setup (ShelfFilter::Type::low, sampleRate, bodyHz, body);
        }
    }
}

void InstrumentVoice::readFrame (double pos, double step, float& l, float& r) noexcept
{
    const auto& src = *layer->source;
    sinc->computeKernel (pos, step, kernel);
    l = SincInterpolator::apply (kernel, src.channelData (0));
    r = src.numChannels() > 1 ? SincInterpolator::apply (kernel, src.channelData (1)) : l;
}

void InstrumentVoice::render (float* left, float* right, int numSamples, double pitchRatio) noexcept
{
    if (! active)
        return;
    const bool stereoOutput = right != left;

    for (int i = 0; i < numSamples; ++i)
    {
        if (controlCountdown-- <= 0)
        {
            controlCountdown = controlInterval - 1;
            updateControl();
            currentStep = baseIncrement * pitchRatio * pitchMod;
        }

        if (! crossfading && hasPending && position >= pending.fromFrame)
            beginCrossfade();

        if (position >= endPosition)
        {
            kill();
            return;
        }

        float env = envelope.next();
        if (! envelope.isActive())
        {
            kill();
            return;
        }
        if (fadeRemaining > 0)
        {
            fadeGain -= fadeStep;
            if (--fadeRemaining == 0)
            {
                kill();
                return;
            }
        }

        float l, r;
        readFrame (position, currentStep, l, r);
        if (crossfading)
        {
            float l2, r2, gOut, gIn;
            readFrame (xPosition, currentStep, l2, r2);
            crossfadeGains (static_cast<float> (xProgress / xLength), xCorrelation, gOut, gIn);
            l = l * gOut + l2 * gIn;
            r = r * gOut + r2 * gIn;
            xPosition += currentStep;
            xProgress += currentStep;
        }
        position += currentStep;
        if (crossfading && xProgress >= xLength)
        {
            crossfading = false;
            position = xPosition;
            scheduleNextJump();
        }

        if (filtersActive)
        {
            l = lowL.process (highL.process (l));
            r = lowR.process (highR.process (r));
        }
        if (saturationDrive > 1.0f)
        {
            // Gentle harmonic generation (Reimagined): unity gain for small signals.
            l = std::tanh (saturationDrive * l) * saturationNorm;
            r = std::tanh (saturationDrive * r) * saturationNorm;
        }

        controlGain += controlGainStep;
        float g = env * baseGain * std::max (fadeGain, 0.0f) * controlGain * dampingGain * (1.0f + transientExtra) * attackRamp;
        transientExtra *= transientCoef;
        dampingGain *= dampingCoef;
        if (attackRamp < 1.0f)
            attackRamp = std::min (1.0f, attackRamp + attackRampStep);

        if (stereoOutput)
        {
            left[i] += l * g * panLeft;
            right[i] += r * g * panRight;
        }
        else
        {
            left[i] += 0.5f * (l + r) * g;
        }
    }
}

} // namespace osp
