#include "engine/InstrumentVoice.h"

#include "core/Prng.h"

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "engine/LevelContour.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    float dbToLinear (double db) noexcept { return static_cast<float> (std::pow (10.0, db / 20.0)); }

}

void InstrumentVoice::prepare (double outputSampleRate, const AdsrSettings& adsr, const SincInterpolator* interpolator,
                               const ShapingState* shaping) noexcept
{
    sampleRate = outputSampleRate;
    sinc = interpolator;
    shapingState = shaping;
    charFilter.prepare (outputSampleRate);
    if (shaping == nullptr)
        charFilter.setParameters (FilterType::off, 1000.0, 0.0, 0.0, 0.0); // a bare voice: no CHARACTER stage
    // CHARACTER follows the knob within ~10 ms (control rate one-pole).
    charSmoothing = 1.0 - std::exp (-static_cast<double> (controlInterval) / (0.010 * outputSampleRate));
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
    voiceLayer = params.layerIndex;
    granularMode = params.sourceMode == SourceMode::granular && params.granular != nullptr;
    granularLive = params.granular;
    // Granular voices never read the recording through: no continuation jumps or grafts.
    strategy = cont->canSustain && ! granularMode ? params.strategy : ContinuationStrategy::off;
    releaseGraftEnabled = params.releaseGraft;
    // REVERSE reads from the end of the sound back to its start. The continuation walk
    // takes the same jumps mirrored (scheduleNextJump); the release graft does not apply
    // (the recording's ending is where a reversed note begins).
    direction = params.reverse && ! granularMode ? -1.0 : 1.0;
    if (direction < 0.0)
        releaseGraftEnabled = false;
    followContour = params.follow;
    followGain = 1.0f;
    followGainStep = 0.0f;

    const auto& src = *layer->source;
    baseIncrement = params.increment;
    currentStep = baseIncrement;
    glideOctaves = glideStep = 0.0;
    gliding = false;
    const double frames = static_cast<double> (src.numFrames());
    const double startFraction = std::clamp (params.startFraction, 0.0, 1.0);
    if (direction > 0.0)
    {
        // START moves through what follows the onset (0 = the analysed start).
        position = src.startFrame() + startFraction * std::max (0.0, frames - src.startFrame())
                   + std::max (0.0, shape.startOffsetSeconds) * src.sampleRate();
    }
    else
    {
        // Backwards from where the sound ends (trailing silence skipped).
        const double trailing = std::max (0.0, currentModel->analysis.envelope.trailingSilenceSeconds) * src.sampleRate();
        const double soundEnd = std::clamp (frames - trailing, std::min (frames - 1.0, src.startFrame() + 1.0), frames - 1.0);
        position = soundEnd - startFraction * std::max (0.0, soundEnd - src.startFrame());
    }
    endPosition = frames + static_cast<double> (sinc->maxReach());

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
    tailEndPosition = 0.0;
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
    // A note that starts inside the sound (START, REVERSE) fades in over a few ms: no click.
    const float softenSeconds = std::max (shape.attackSoftenSeconds, startFraction > 1.0e-6 || direction < 0.0 ? 0.003f : 0.0f);
    if (softenSeconds > 0.0f)
    {
        attackRamp = 0.0f;
        attackRampStep = 1.0f / static_cast<float> (softenSeconds * sampleRate);
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
    // The doubling head trails by 12-20 ms and moves slowly (a few cents of chorus); it
    // fades in after the attack so a pluck never flams.
    dAmount = std::clamp (shape.doubling, 0.0f, 1.0f);
    if (dAmount > 0.0f)
    {
        Prng dRng (Prng::deriveSeed (shape.seed, 0x646f75626c65ull, 0));
        const double sr = src.sampleRate();
        dBase = (0.012 + 0.008 * dRng.nextDouble()) * sr;
        dDepth = 0.0018 * sr;
        dPhase = 2.0 * std::numbers::pi * dRng.nextDouble();
        dOmega = 2.0 * std::numbers::pi * (0.25 + 0.2 * dRng.nextDouble()) / sampleRate;
        dRamp = 0.0f;
        dRampStep = 1.0f / static_cast<float> (0.08 * sampleRate);
        dDelaySamples = static_cast<int> (0.03 * sampleRate);
        dSide = dRng.nextDouble() < 0.5 ? -1.0f : 1.0f;
        dEnd = static_cast<double> (src.numFrames() - 1);
    }
    // Granular continuation (Reimagined far end): grains start once there is history to
    // draw from and fade in after the attack, which stays the recording's own.
    gAmount = std::clamp (shape.granular, 0.0f, 1.0f);
    for (auto& grain : grains)
        grain.active = false;
    if (gAmount > 0.0f)
    {
        grainRng.reseed (Prng::deriveSeed (shape.seed, 0x6772616eull, 0));
        gFloor = position;
        gDensity = 14.0 + 26.0 * gAmount;                               // grains per second
        const double overlap = gDensity * 0.095;                         // mean grain length 95 ms
        gNorm = static_cast<float> (1.0 / (0.637 * std::sqrt (std::max (1.0, overlap))));
        gDelay = static_cast<int> (0.12 * sampleRate);
        gCountdown = 0;
        gFade = 0.0f;
        gFadeStep = static_cast<float> (1.0 / (0.35 * sampleRate));
        gLowCoef = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * 6500.0 / sampleRate));
        gLowL = gLowR = 0.0f;
    }
    if (granularMode)
    {
        // The read-through extras (doubling head, Reimagined grains) follow a read head.
        dAmount = 0.0f;
        gAmount = 0.0f;
    }
    if (granularMode)
        granularSource.start (*layer->source, notesGranular(), sampleRate, shape.seed, &currentModel->analysis.envelope,
                              currentModel->analysis.source.durationSeconds);
    saturationDrive = 1.0f + 4.0f * std::clamp (shape.saturation, 0.0f, 1.0f);
    saturationNorm = 1.0f / saturationDrive;
    // Drift has its own random stream (so it never changes the continuation's choices)
    // and starts part-way through a glide: like a free-running oscillator, a note does
    // not begin at the centre of the wander.
    driftRng.reseed (Prng::deriveSeed (shape.seed, 0x6472696674ull, 0));
    {
        auto away = [this] (float depth) {
            const double magnitude = driftRng.uniform (0.35, 1.0);
            return static_cast<double> (depth) * (driftRng.nextDouble() < 0.5 ? -magnitude : magnitude);
        };
        driftFrom = { away (shape.driftLevelDb), away (shape.driftCents), away (shape.driftBrightnessDb), away (shape.driftPan), away (shape.driftToneOctaves) };
        // Begin at 0.5x the start offset and glide on from there.
        for (auto& v : driftFrom)
            v *= 0.5;
    }
    driftLevel = driftFrom[0];
    driftCentsValue = driftFrom[1];
    driftBright = driftFrom[2];
    driftPanValue = driftFrom[3];
    driftTone = driftFrom[4];
    driftCountdown = 0;
    driftSegment = 1;
    controlCountdown = 0;
    controlGain = 1.0f;
    controlGainStep = 0.0f;
    pitchMod = 1.0;

    // Transient/body separation (spec §19): the attack's broadband part is played at its
    // own speed while the body is transposed. The transposed copy of that part is
    // subtracted from the main read, so it is swapped, not doubled.
    tAmount = std::clamp (shape.transientPreserve, 0.0f, 1.0f);
    tMix = dbToLinear (std::clamp (shape.transientMixDb, -24.0f, 12.0f)) - 1.0f;
    tRemaining = 0;
    if (direction < 0.0)
        tAmount = tMix = 0.0f;   // the transient is at the other end
    if (! granularMode && direction > 0.0 && (tAmount > 0.0f || std::abs (tMix) > 1.0e-4f) && layer->transient != nullptr
        && currentModel->original.transient != nullptr)
    {
        const auto& orig = *currentModel->original.transient;
        tStep = orig.sampleRate() / sampleRate;
        // Both reads reach the transient's peak at the same moment, so the swapped
        // transient lands exactly on the body's onset instead of before or after it.
        const double peak = currentModel->original.transientPeakSeconds * orig.sampleRate();
        const double bodyStep = std::max (1.0e-3, baseIncrement);
        tPosition = peak - (peak - position) * tStep / bodyStep;
        tRemaining = std::max (1, static_cast<int> (shape.transientPreserveSeconds * sampleRate));
    }

    // CHARACTER: velocity coupling (DYNAMICS x TONE) and the filter envelope start here.
    driftToneTarget = 0.0;   // driftTone starts part-way through its glide (above)
    if (shapingState != nullptr)
    {
        const auto& s = shapingState->shaping;
        velocityOctaves = shape.filterVelocityOctaves;
        envelopeScale = shape.filterEnvelopeScale;
        filterEnv = 0.0;
        filterEnvAttacking = true;
        filterEnvStep = static_cast<double> (controlInterval) / (std::max (0.0005, s.envAttackSeconds) * sampleRate);
        // Back to the knob's position by the end of DECAY (exponential, ~1 % left).
        filterEnvDecay = std::exp (-4.6 * static_cast<double> (controlInterval) / (std::max (0.02, s.envDecaySeconds) * sampleRate));
        charFilter.reset();
        updateCharacter (true);
    }

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

    if (granularMode)
    {
        // No new grains; the playing ones finish under the release envelope.
        granularSource.stopSpawning();
        envelope.noteOff();
        return;
    }

    bool canGraft = releaseGraftEnabled && strategy != ContinuationStrategy::off && cont->hasRelease
                    && ! cont->graftExits.empty() && position < cont->releaseFrame;
    if (canGraft)
    {
        // Only when an exit into the ending is close: released early in a long recording
        // (before its stable region, or far from any exit) the note would otherwise keep
        // sounding at full level until it got there - seconds, or the rest of the file.
        const double reach = 0.3 * sampleRate * std::max (currentStep, 1.0e-3);
        bool nearby = false;
        for (const auto& exit : cont->graftExits)
            if (exit.fromFrame >= position + 1.0)
            {
                nearby = exit.fromFrame - position <= reach;
                break;
            }
        canGraft = nearby;
    }
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

void InstrumentVoice::glideTo (int newNote, double seconds) noexcept
{
    if (! active || newNote == currentNote)
        return;
    const double octaves = static_cast<double> (newNote - currentNote) / 12.0;
    baseIncrement *= std::exp2 (octaves);
    glideOctaves -= octaves;   // the pitch stays where it is, then slides
    currentNote = newNote;
    setGlideTime (seconds);
    controlCountdown = 0;   // the new pitch takes effect on the next sample
}

void InstrumentVoice::glideFrom (int fromNote, double seconds) noexcept
{
    if (! active || fromNote == currentNote)
        return;
    glideOctaves = static_cast<double> (fromNote - currentNote) / 12.0;
    setGlideTime (seconds);
}

void InstrumentVoice::setGlideTime (double seconds) noexcept
{
    const double periods = seconds * sampleRate / controlInterval;
    if (periods < 1.0)
    {
        glideOctaves = 0.0;
        gliding = false;
        return;
    }
    glideStep = std::abs (glideOctaves) / periods;
    gliding = glideStep > 0.0;
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

    // REVERSE takes every jump mirrored: a jump matches the windows [from, from + crossfade)
    // and [to, to + crossfade), so read backwards it leaves at the end of the `to` window and
    // lands at the end of the `from` window, crossfading over the same matched audio.
    const bool forwards = direction > 0.0;
    auto oriented = [forwards] (const ContinuationJump& j) {
        if (forwards)
            return j;
        auto m = j;
        m.fromFrame = j.toFrame + j.crossfadeFrames;
        m.toFrame = j.fromFrame + j.crossfadeFrames;
        return m;
    };
    // Still ahead of the read head (in the direction of play)?
    auto ahead = [this, forwards] (double frame) { return forwards ? position < frame : position > frame; };

    switch (strategy)
    {
        case ContinuationStrategy::naiveLoop:
        {
            const auto j = oriented (cont->naiveLoop);
            if (ahead (j.fromFrame))
            {
                pending = j;
                hasPending = true;
            }
            return;
        }
        case ContinuationStrategy::bestLoop:
            if (cont->bestLoop >= 0)
            {
                const auto j = oriented (cont->jumps[static_cast<std::size_t> (cont->bestLoop)]);
                if (ahead (j.fromFrame))
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
    const double minFrom = forwards ? position + cont->minSegmentFrames * std::min (1.0, scale)
                                    : position - cont->minSegmentFrames * std::min (1.0, scale);
    const double reach = rng.uniform (0.4, 2.5) * sr * scale;
    const double lookahead = forwards ? minFrom + reach : minFrom - reach;
    auto weightOf = [&] (int index) -> double
    {
        const double from = oriented (cont->jumps[static_cast<std::size_t> (index)]).fromFrame;
        if (forwards ? from < minFrom : from > minFrom)
            return 0.0;
        double w = 0.05 + static_cast<double> (cont->jumps[static_cast<std::size_t> (index)].score);
        for (int r : recent)
            if (r == index)
                w *= 0.1;
        if (forwards ? from > lookahead : from < lookahead)
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
    else if (cont->backstop >= 0 && ahead (oriented (cont->jumps[static_cast<std::size_t> (cont->backstop)]).fromFrame))
    {
        chosen = cont->backstop;
    }
    if (chosen >= 0)
    {
        pending = oriented (cont->jumps[static_cast<std::size_t> (chosen)]);
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
        // The ending is over when its sound is, not at the end of the file (long
        // recordings can carry seconds of near-silence after it).
        const double soundEnd = cont->releaseFrame + (cont->tailSeconds + 0.25) * layer->source->sampleRate();
        tailEndPosition = soundEnd;
    }
    ++jumpsTaken;
}

void InstrumentVoice::updateCharacter (bool immediate) noexcept
{
    const auto& s = shapingState->shaping;
    // AD envelope: up over ATTACK, back over DECAY, then it rests at the knob's position.
    if (! immediate)
    {
        if (filterEnvAttacking)
        {
            filterEnv += filterEnvStep;
            if (filterEnv >= 1.0)
            {
                filterEnv = 1.0;
                filterEnvAttacking = false;
            }
        }
        else
            filterEnv *= filterEnvDecay;
    }
    const double target = shaping::cutoffOctaves (s, shapingState->character);
    charOctaves = immediate ? target : charOctaves + (target - charOctaves) * charSmoothing;
    const double env = shaping::envelopeOctaves (s.envAmount) * filterEnv * envelopeScale;
    const double octaves = charOctaves + env + velocityOctaves + driftTone;
    // TILT: the knob rotates dark <-> bright (+-12 dB), the envelope and velocity lean it.
    const double tiltDb = (std::clamp (shapingState->character, 0.0, 1.0) - 0.5) * 24.0 + 3.0 * (env + velocityOctaves + driftTone);
    charFilter.setParameters (s.filterType, std::exp2 (octaves), s.resonance, s.drive, tiltDb);
}

GranularParams InstrumentVoice::notesGranular() const noexcept
{
    GranularParams p = *granularLive;
    p.position = std::clamp (p.position + static_cast<double> (shape.grainPositionOffset), 0.0, 1.0);
    p.sizeSeconds = std::clamp (p.sizeSeconds * static_cast<double> (shape.grainSizeRatio), 0.01, 0.6);
    p.density = std::clamp (p.density * static_cast<double> (shape.grainDensityRatio), 2.0, 60.0);
    p.spread = std::clamp (p.spread + static_cast<double> (shape.grainSpreadOffset), 0.0, 1.0);
    p.tuneSemitones += static_cast<double> (shape.grainTuneCents) / 100.0;
    return p;
}

void InstrumentVoice::updateControl() noexcept
{
    // Drift: slowly wandering targets, smoothed (strategy D / MOTION).
    const bool drifting = shape.driftLevelDb > 0.0f || shape.driftCents > 0.0f || shape.driftBrightnessDb > 0.0f || shape.driftPan > 0.0f
                          || shape.driftToneOctaves > 0.0f;
    if (drifting)
    {
        if (--driftCountdown <= 0)
        {
            // A new glide from wherever the drift is now to fresh random values: smooth
            // value noise at about the drift rate, like a free-running analog wander.
            // Targets keep away from the centre (35-100 % of the depth, either side), so a
            // glide always goes somewhere audible.
            driftFrom = { driftLevel, driftCentsValue, driftBright, driftPanValue, driftTone };
            auto away = [this] (float depth) {
                const double magnitude = driftRng.uniform (0.35, 1.0);
                return static_cast<double> (depth) * (driftRng.nextDouble() < 0.5 ? -magnitude : magnitude);
            };
            driftLevelTarget = away (shape.driftLevelDb);
            driftCentsTarget = away (shape.driftCents);
            driftBrightTarget = away (shape.driftBrightnessDb);
            driftPanTarget = away (shape.driftPan);
            driftToneTarget = away (shape.driftToneOctaves);
            const double seconds = driftRng.uniform (0.6, 1.4) / std::max (0.01, static_cast<double> (shape.driftRateHz));
            driftCountdown = driftSegment = std::max (1, static_cast<int> (seconds * sampleRate / controlInterval));
        }
        const double f = 1.0 - static_cast<double> (driftCountdown) / static_cast<double> (driftSegment);
        const double glide = f * f * (3.0 - 2.0 * f);
        driftLevel = driftFrom[0] + (driftLevelTarget - driftFrom[0]) * glide;
        driftCentsValue = driftFrom[1] + (driftCentsTarget - driftFrom[1]) * glide;
        driftBright = driftFrom[2] + (driftBrightTarget - driftFrom[2]) * glide;
        driftPanValue = driftFrom[3] + (driftPanTarget - driftFrom[3]) * glide;
        driftTone = driftFrom[4] + (driftToneTarget - driftFrom[4]) * glide;
        if (shape.driftPan > 0.0f)
        {
            const float p = std::clamp (shape.pan + static_cast<float> (driftPanValue), -1.0f, 1.0f);
            panLeft = std::min (1.0f, 1.0f - p);
            panRight = std::min (1.0f, 1.0f + p);
        }
    }

    if (shapingState != nullptr)
        updateCharacter (false);
    if (granularMode)
        granularSource.setParams (notesGranular());

    settleCents *= settleCoef;
    double shared = 0.0;
    if (shapingState != nullptr && shapingState->shaping.movementMode == MovementMode::drift && shapingState->movement > 0.0)
    {
        // DRIFT's shared part: the whole instrument wanders a little together.
        const auto& s = shapingState->shaping;
        shared = 0.3 * shapingState->movement * shaping::driftPitchCents (s.driftPitch)
                 * shaping::sharedWander (shapingState->seed, static_cast<double> (clock) / sampleRate, shaping::driftSpeedHz (s.driftSpeed));
    }
    const double cents = shape.pitchCents + settleCents + driftCentsValue + shared;
    pitchMod = cents != 0.0 ? std::exp2 (cents / 1200.0) : 1.0;

    // Expression follows quickly but smoothly (about 10 ms).
    expressionGain += (std::pow (10.0, expressionGainDb / 20.0) - expressionGain) * 0.15;
    expressionBright += (expressionBrightDb - expressionBright) * 0.15;
    const float target = (drifting ? dbToLinear (driftLevel) : 1.0f) * static_cast<float> (expressionGain);
    controlGainStep = (target - controlGain) / controlInterval;

    if (! followContour && ! granularMode)
    {
        // FOLLOW off: the contour's lift where the read is now, glided over this control period.
        const double frames = std::max (1.0, static_cast<double> (layer->source->numFrames()));
        const auto target = static_cast<float> (std::pow (10.0, levelContour::boostDb (currentModel->analysis.envelope,
                                                                                        currentModel->analysis.source.durationSeconds,
                                                                                        position / frames) / 20.0));
        followGainStep = (target - followGain) / controlInterval;
    }

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
    sinc->computeKernelFast (pos, step, kernel);
    l = SincInterpolator::apply (kernel, src.channelData (0));
    r = src.numChannels() > 1 ? SincInterpolator::apply (kernel, src.channelData (1)) : l;
}

float InstrumentVoice::readTransient (const PlaybackSource& src, double pos, double step, int channel) noexcept
{
    // The transient buffers are short and silent at their end; past it they read as zero.
    if (pos < 0.0 || pos >= static_cast<double> (src.numFrames()))
        return 0.0f;
    sinc->computeKernelFast (pos, step, kernel);
    return SincInterpolator::apply (kernel, src.channelData (channel));
}

float InstrumentVoice::readHermite (int channel, double pos) const noexcept
{
    // Cheap 4-point read for grains (their bus is darkened, so no band-limiting needed).
    const auto& src = *layer->source;
    const auto last = static_cast<double> (std::max<std::int64_t> (2, src.numFrames() - 3));
    pos = std::clamp (pos, 1.0, last);
    const auto i = static_cast<std::int64_t> (pos);
    const float t = static_cast<float> (pos - static_cast<double> (i));
    const float* x = src.channelData (channel) + i;
    const float c0 = x[0], c1 = 0.5f * (x[1] - x[-1]);
    const float c2 = x[-1] - 2.5f * x[0] + 2.0f * x[1] - 0.5f * x[2];
    const float c3 = 0.5f * (x[2] - x[-1]) + 1.5f * (x[0] - x[1]);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

void InstrumentVoice::spawnGrain() noexcept
{
    Grain* slot = nullptr;
    for (auto& grain : grains)
        if (! grain.active)
        {
            slot = &grain;
            break;
        }
    if (slot == nullptr)
        return;

    const double g = gAmount;
    // Harmonic remapping: some grains an octave up, a fifth up or an octave down.
    const double pick = grainRng.nextDouble();
    double ratio = 1.0;
    if (pick < 0.22 * g)
        ratio = 2.0;
    else if (pick < 0.38 * g)
        ratio = 1.5;
    else if (pick < 0.48 * g)
        ratio = 0.5;
    // Controlled instability: every grain slightly off, more towards the far end.
    const double cents = (6.0 + 22.0 * g) * grainRng.bipolar();
    const double step = currentStep * ratio * std::exp2 (cents / 1200.0);
    const double seconds = grainRng.uniform (0.05, 0.14);
    const double outSamples = seconds * sampleRate;
    const double span = outSamples * step;
    // Read from what the note has already played: end at or before the current position
    // (REVERSE: what it has played lies after it in the file, and grains play backwards too).
    const double srcRate = layer->source->sampleRate();
    const bool forwards = direction > 0.0;
    const double history = forwards ? position - gFloor - span : gFloor - position - span;
    if (history < 0.01 * srcRate)
        return;
    const double back = grainRng.nextDouble() * std::min (history, (0.08 + 0.7 * g) * srcRate);
    slot->position = forwards ? position - span - back : position + span + back;
    slot->step = forwards ? step : -step;
    slot->phase = 0.0;
    slot->phaseStep = 1.0 / outSamples;
    const float pan = static_cast<float> (0.7 * g * grainRng.bipolar());
    slot->left = std::min (1.0f, 1.0f - pan);
    slot->right = std::min (1.0f, 1.0f + pan);
    slot->active = true;
}

void InstrumentVoice::render (float* left, float* right, int numSamples, double pitchRatio, std::int64_t clockAtStart) noexcept
{
    clock = clockAtStart;
    if (! active)
        return;
    const bool stereoOutput = right != left;

    for (int i = 0; i < numSamples; ++i)
    {
        ++clock;
        if (controlCountdown-- <= 0)
        {
            controlCountdown = controlInterval - 1;
            updateControl();
            currentStep = baseIncrement * pitchRatio * pitchMod;
            if (gliding)
            {
                // Linear in pitch, constant time: every interval takes GLIDE to cross.
                if (std::abs (glideOctaves) <= glideStep)
                {
                    glideOctaves = 0.0;
                    gliding = false;
                }
                else
                {
                    glideOctaves -= std::copysign (glideStep, glideOctaves);
                    currentStep *= std::exp2 (glideOctaves);
                }
            }
        }

        if (granularMode)
        {
            if (granularSource.isFinished())
            {
                kill();
                return;
            }
        }
        else
        {
            if (! crossfading && hasPending && (direction > 0.0 ? position >= pending.fromFrame : position <= pending.fromFrame))
                beginCrossfade();

            if (tailEndPosition > 0.0 && position >= tailEndPosition && fadeRemaining == 0)
                beginFastFade (static_cast<int> (0.05 * sampleRate));
            if (direction > 0.0 ? position >= endPosition : position <= 0.0)
            {
                kill();
                return;
            }
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
        if (granularMode)
        {
            granularSource.render (l, r, currentStep);
        }
        else
        {
            const double bodyPosition = position; // what this sample's main read used
            readFrame (position, currentStep, l, r);
            if (crossfading)
            {
                float l2, r2, gOut, gIn;
                readFrame (xPosition, currentStep, l2, r2);
                crossfadeGains (static_cast<float> (xProgress / xLength), xCorrelation, gOut, gIn);
                l = l * gOut + l2 * gIn;
                r = r * gOut + r2 * gIn;
                xPosition += direction * currentStep;
                xProgress += currentStep;
            }
            if (dAmount > 0.0f)
            {
                if (dDelaySamples > 0)
                    --dDelaySamples;
                else if (dRamp < 1.0f)
                    dRamp = std::min (1.0f, dRamp + dRampStep);
                if (dRamp > 0.0f)
                {
                    const double behind = dBase + dDepth * std::sin (dPhase);
                    float l2, r2;
                    // Behind = what was just played: earlier in the file, or later when reversed.
                    const double doubled = direction > 0.0 ? std::max (0.0, bodyPosition - behind * currentStep)
                                                           : std::min (dEnd, bodyPosition + behind * currentStep);
                    readFrame (doubled, currentStep, l2, r2);
                    const float g = dAmount * dRamp;
                    const float norm = 1.0f / std::sqrt (1.0f + g * g);
                    // Placed a little to one side, so the doubling widens instead of thickening.
                    l = (l + g * (dSide < 0.0f ? 1.0f : 0.6f) * l2) * norm;
                    r = (r + g * (dSide < 0.0f ? 0.6f : 1.0f) * r2) * norm;
                }
                dPhase += dOmega;
            }
            position += direction * currentStep;
            if (tRemaining > 0)
            {
                const auto& native = *currentModel->original.transient;
                const auto& shifted = *layer->transient;
                const bool stereo = native.numChannels() > 1 || shifted.numChannels() > 1;
                const bool swap = tAmount > 0.0f;
                const float nl = swap ? readTransient (native, tPosition, tStep, 0) : 0.0f;
                const float sl = readTransient (shifted, bodyPosition, currentStep, 0);
                const float nr = swap && stereo ? readTransient (native, tPosition, tStep, 1) : nl;
                const float sr = stereo ? readTransient (shifted, bodyPosition, currentStep, 1) : sl;
                // Swap (preservation) and level change (mixing) of the transient now playing.
                l += tAmount * (nl - sl) + tMix * (tAmount * nl + (1.0f - tAmount) * sl);
                r += tAmount * (nr - sr) + tMix * (tAmount * nr + (1.0f - tAmount) * sr);
                tPosition += tStep;
                --tRemaining;
            }
            if (gAmount > 0.0f)
            {
                if (gDelay > 0)
                    --gDelay;
                else
                {
                    if (--gCountdown <= 0)
                    {
                        spawnGrain();
                        gCountdown = std::max (1, static_cast<int> (sampleRate / gDensity * grainRng.uniform (0.5, 1.5)));
                    }
                    if (gFade < 1.0f)
                        gFade = std::min (1.0f, gFade + gFadeStep);
                }
                float gl = 0.0f, gr = 0.0f;
                const bool stereoSource = layer->source->numChannels() > 1;
                for (auto& grain : grains)
                {
                    if (! grain.active)
                        continue;
                    const auto x = static_cast<float> (grain.phase);
                    const float w = 16.0f * x * x * (1.0f - x) * (1.0f - x);
                    const float sl = readHermite (0, grain.position);
                    const float sr = stereoSource ? readHermite (1, grain.position) : sl;
                    gl += w * sl * grain.left;
                    gr += w * sr * grain.right;
                    grain.position += grain.step;
                    grain.phase += grain.phaseStep;
                    if (grain.phase >= 1.0)
                        grain.active = false;
                }
                gLowL += (gl - gLowL) * gLowCoef;
                gLowR += (gr - gLowR) * gLowCoef;
                // Alternate synthetic sustain: towards the far end the grains carry the sustain.
                const float mix = gAmount * gFade;
                l = l * (1.0f - 0.55f * mix) + 0.95f * mix * gNorm * gLowL;
                r = r * (1.0f - 0.55f * mix) + 0.95f * mix * gNorm * gLowR;
            }
            if (crossfading && xProgress >= xLength)
            {
                crossfading = false;
                position = xPosition;
                scheduleNextJump();
            }
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
        charFilter.process (l, r);

        controlGain += controlGainStep;
        float g = env * baseGain * std::max (fadeGain, 0.0f) * controlGain * dampingGain * (1.0f + transientExtra) * attackRamp;
        if (! followContour)
        {
            followGain += followGainStep;
            g *= followGain;
        }
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
