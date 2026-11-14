#include "engine/InstrumentVoice.h"

#include "core/Prng.h"

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "engine/FastMath.h"
#include "engine/KeyDna.h"
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
    synthVoice.prepare (outputSampleRate);
    imprintShaper.prepare (outputSampleRate);
    charFilter.prepare (outputSampleRate);
    if (shaping == nullptr)
        charFilter.setParameters (FilterType::off, 1000.0, 0.0, 0.0, 0.0); // a bare voice: no CHARACTER stage
    // CHARACTER follows the knob within ~10 ms (control rate one-pole).
    charSmoothing = 1.0 - std::exp (-static_cast<double> (controlInterval) / (0.010 * outputSampleRate));
    keyDnaSmoothing = 1.0 - std::exp (-static_cast<double> (controlInterval) / (0.030 * outputSampleRate));
    satRearCoef = static_cast<float> (1.0 - std::exp (-2.0 * 3.14159265358979323846 * 2200.0 / outputSampleRate));   // (ORBIT's rear: darker near 2 kHz)
    adsrSettings = adsr;
    envelope.prepare (outputSampleRate, adsr);
    tapeFrameEngine.prepare (outputSampleRate);
    toyboxEngine.prepare (outputSampleRate);
    mosaicEngine.prepare (outputSampleRate);
    mirageEngine.prepare (outputSampleRate);
    kill();
}

void InstrumentVoice::setEnvelopeSettings (const AdsrSettings& adsr) noexcept
{
    adsrSettings = adsr;
    envelope.prepare (sampleRate, adsr);
}

void InstrumentVoice::start (const InstrumentVoiceStart& params) noexcept
{
    outputPeak = 0.0f;
    quietSamples = 0;
    currentModel = params.model;
    layer = params.layer;
    active = currentModel != nullptr && layer != nullptr && layer->source != nullptr && layer->source->isValid() && sinc != nullptr;
    if (! active)
        return;

    cont = &layer->continuation;
    currentNote = params.note;
    // VEL and NOTE for this note's life: the sounding note, the raw velocity it was played at.
    modState.note = params.note;
    modState.velocity = modRuntime != nullptr ? modRuntime->noteVelocity : params.velocity;
    midiChannel = params.channel;
    order = params.startOrder;
    expressionGainDb = expressionBrightDb = 0.0f;
    expressionGain = 1.0;
    expressionBright = 0.0;
    shape = params.shape;
    voiceLayer = params.layerIndex;
    granularMode = params.sourceMode == SourceMode::granular && params.granular != nullptr;
    granularLive = params.granular;
    keyDnaAmountLive = params.keyDnaAmount;
    // SYNTHESIZE: armed when the layer's amount is above 0 (now or later in the note).
    synthAmountLive = params.synthesize;
    synthBlend = params.synthBlend;
    synthEnvelope = params.synthEnvelope;
    synthSettingsLive = params.synthSettings;
    synthTrackRatio = 1.0;
    synthSeed = params.shape.seed;
    synthVelocity = params.velocity;
    synthOn = false;
    sourceEnded = false;
    synthAmountNow = 0.0;
    synthPitchHz = 0.0;
    synthSampleGain = 1.0f;
    synthGain = synthSampleStep = synthGainStep = 0.0f;
    synthVoice.stop();
    // Imprint: this note's model's descriptor (an Atlas member's own); kept for the note's life.
    imprintProfile = currentModel->imprint != nullptr && currentModel->imprint->valid ? currentModel->imprint.get() : nullptr;
    imprintClock = 0;
    {
        // The oscillator at 100 % as loud as this recording's typical level (an Atlas member by
        // its own): halfway in dB between its loudest 20 ms and its body's median, at most 6 dB
        // under the loudest. The loudest alone made dynamic material (a bow, a decaying pluck)
        // jump by 8-10 dB at 100 % in the listening matrix (docs/research/synthesize/s1-results.md).
        // As read: the playback levelling is in baseGain and applies to both alike.
        const auto& envelope = currentModel->analysis.envelope;
        const double typicalDb = envelope.maxRmsDbfs + 0.5 * std::clamp (envelope.sustainLevelDb, -12.0, 0.0);
        synthLevel = static_cast<float> (std::clamp (std::pow (10.0, typicalDb / 20.0), 1.0e-4, 1.0) / synth::Voice::referenceRms);   // (quiet recordings too: -40 dBFS is ordinary before levelling)
    }
    keyDnaAmountApplied = keyDnaAmountLive != nullptr ? std::clamp (*keyDnaAmountLive, 0.0, 1.0) : 1.0;
    // Granular voices never read the recording through: no continuation jumps or grafts.
    strategy = cont->canSustain && ! granularMode ? params.strategy : ContinuationStrategy::off;
    releaseGraftEnabled = params.releaseGraft;
    // REVERSE reads from the end of the sound back to its start. The continuation walk
    // takes the same jumps mirrored (scheduleNextJump); the release graft does not apply
    // (the recording's ending is where a reversed note begins).
    direction = params.reverse && ! granularMode ? -1.0 : 1.0;
    stretchSpeed = std::clamp (params.speed, 0.3, 2.0);
    stretching = false;
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
    // START modulation is read once, as the note begins (the sources as they are at this
    // instant: a free-running LFO picks a different place for each note); a sounding note
    // never jumps. Granular voices carry it as an offset of their grain position.
    modStartOffset = 0.0;
    if (modRuntime != nullptr)
    {
        const auto startDest = static_cast<mod::Dest> (static_cast<int> (mod::Dest::startA) + std::clamp (voiceLayer, 0, 2));
        if (modRuntime->compiled.has (startDest))
        {
            mod::VoiceState probe;
            probe.note = modState.note;
            probe.velocity = modState.velocity;
            probe.start (*modRuntime);
            modStartOffset = probe.offset (*modRuntime, startDest);
        }
    }
    const double startFraction = std::clamp (params.startFraction + (granularMode ? 0.0 : modStartOffset), 0.0, 1.0);
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
    pedalMode = PedalLoopMode::normal;   // (eligibility at the end of start)
    pedalState = PedalState::off;
    pedalWanted = false;
    pedalRegion = -1;
    ppDir = xDir = pendingXDir = 1.0;
    pendingIsPedal = xIsPedal = xEndsLoop = false;
    loopGain = xInGain = pendingInGain = 1.0f;
    trendOut = trendIn = 1.0f;
    lateResume = -1.0;
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
    panNow = p;
    satellite = params.satellite;
    satNoteX = params.satelliteNoteX;
    satRearL = satRearR = 0.0f;

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
        const double phase = 2.0 * std::numbers::pi * dRng.nextDouble();
        dSway.reset (phase, 2.0 * std::numbers::pi * (0.25 + 0.2 * dRng.nextDouble()) / sampleRate);
        dRamp = 0.0f;
        dRampStep = 1.0f / static_cast<float> (0.08 * sampleRate);
        dDelaySamples = static_cast<int> (0.03 * sampleRate);
        dSide = dRng.nextDouble() < 0.5 ? -1.0f : 1.0f;
        // SPREAD: the quieter side of the doubling (0.6 is the original width).
        dNear = shape.spread == 1.0f ? 0.6f : std::clamp (1.0f - 0.4f * shape.spread, 0.0f, 1.0f);
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
    grainDoubling = false;
    gScatter = 0.0f;
    modeEngine = nullptr;   // the previous note's engine must not steer this note's first grains
    if (granularMode)
    {
        // The read-through extras need a read head; on grains KALEIDOSCOPE doubles with a
        // second grain stream and scatters (set up below, once the mode engine is known).
        gScatter = gAmount;
        gAmount = 0.0f;
        dGrainBehind = dAmount > 0.0f ? dBase / std::max (1.0, static_cast<double> (src.numFrames())) : 0.0;
    }
    if (granularMode)
    {
        setGrainCap (params.grainCap);
        granularSource.setGrainGate (params.grainGate);
        doubleSource.setGrainGate (params.grainGate);
        granularSource.start (*layer->source, notesGranular(), sampleRate, shape.seed, &currentModel->analysis.envelope,
                              currentModel->analysis.source.durationSeconds);
    }
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
        tapEnvPeak = 0.0;
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

    clearModulation();
    // DAMPEN: the sounding key (the arpeggiator's octaves included, not pitch bend).
    dampenLive = params.dampen;
    dampenNote = params.note;
    dampenFilter.reset();
    dampenApplied = -1.0;
    updateDampen (true);
    modPanApplied = false;
    noteAdsr = adsrSettings;
    if (modRuntime != nullptr && modRuntime->compiled.anyVoiceDest)
    {
        // The amp envelope's times are taken as the note starts (a running stage is never
        // re-timed); SUSTAIN follows while held, RELEASE is taken at note-off.
        updateModulation (false);
        const auto& rt = *modRuntime;
        noteAdsr.attackSeconds = mod::modulatedSeconds (mod::Dest::ampAttack, adsrSettings.attackSeconds, modState.offset (rt, mod::Dest::ampAttack));
        noteAdsr.decaySeconds = mod::modulatedSeconds (mod::Dest::ampDecay, adsrSettings.decaySeconds, modState.offset (rt, mod::Dest::ampDecay));
        noteAdsr.sustainLevel = std::clamp (adsrSettings.sustainLevel + modSustain, 0.0, 1.0);
        appliedSustain = noteAdsr.sustainLevel;
    }
    envelope.prepare (sampleRate, noteAdsr);
    envelope.reset();
    envelope.noteOn();

    scheduleNextJump();
    startModeEngine (params);
    startStretch (params.note);   // (after the mode engine: a REIMAGINED mode keeps its own read)
    // PEDAL LOOP: One Shot notes read forward through the voice's own path, on a recording with
    // a loop region. FORWARD where the layer's LOOP already holds the note changes nothing (the
    // continuation walk is the better forward loop); Ping-Pong under SPEED plays as Forward.
    pedalMode = params.pedalLoop;
    if (granularMode || modeEngine != nullptr || direction < 0.0 || ! cont->pedal.valid)
        pedalMode = PedalLoopMode::normal;
    if (pedalMode == PedalLoopMode::pingPong && stretching)
        pedalMode = PedalLoopMode::forward;
    if (pedalMode == PedalLoopMode::forward && strategy != ContinuationStrategy::off)
        pedalMode = PedalLoopMode::normal;
    pedalTurnFrames = std::max (0.0, params.pedalTurnSeconds) * src.sampleRate();
    pedalTurnCorrelation = std::clamp (params.pedalTurnCorrelation, 0.0f, 1.0f);
    if (granularMode)
    {
        if (modeEngine != nullptr)
        {
            // Another mode transforms the grains: KALEIDOSCOPE's own extras stay out.
            dAmount = 0.0f;
            gScatter = 0.0f;
        }
        grainDoubling = dAmount > 0.0f;
        if (grainDoubling)
            doubleSource.start (*layer->source, doubledGranular(), sampleRate, Prng::deriveSeed (shape.seed, 0x6772646dull, 0),
                                &currentModel->analysis.envelope, currentModel->analysis.source.durationSeconds);
    }
}

void InstrumentVoice::release() noexcept
{
    if (! active || released)
        return;
    released = true;
    heldByPedal = false;
    if (synthOn)
        synthVoice.release();
    if (modStarted)
    {
        modState.release (*modRuntime);
        if (modRuntime != nullptr && modRuntime->compiled.has (mod::Dest::ampRelease))
        {
            auto a = noteAdsr;
            a.sustainLevel = appliedSustain >= 0.0 ? appliedSustain : a.sustainLevel;
            a.releaseSeconds = mod::modulatedSeconds (mod::Dest::ampRelease, adsrSettings.releaseSeconds, modState.offset (*modRuntime, mod::Dest::ampRelease));
            envelope.prepare (sampleRate, a);
        }
    }
    if (modeEngine != nullptr)
        modeEngine->release();

    if (granularMode)
    {
        // No new grains; the playing ones finish under the release envelope.
        granularSource.stopSpawning();
        doubleSource.stopSpawning();
        envelope.noteOff();
        return;
    }

    // PEDAL LOOP: the loop stops (forward on into the recording) and the envelope releases;
    // no graft (the note ends where it is, not in another part of the recording).
    if (pedalState != PedalState::off)
    {
        pedalWanted = false;
        if (! crossfading)
            pedalDisarm();
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
    pedalState = PedalState::off;
    pedalWanted = false;
    envelope.reset();
}

void InstrumentVoice::scheduleNextJump() noexcept
{
    hasPending = false;
    pendingIndex = -1;
    pendingIsGraft = false;
    // (A jump of the walk or a graft is never the pedal loop's: schedulePedal sets these again.)
    pendingIsPedal = false;
    pendingXDir = 1.0;
    pendingInGain = 1.0f;
    if (graftPending)
    {
        scheduleGraft();
        return;
    }
    if (pedalState != PedalState::off && schedulePedal())
        return;
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
    xIsPedal = pendingIsPedal;
    if (xIsPedal)
    {
        // The incoming stream may read the other way (a Ping-Pong turnaround): it starts as far
        // past its landing point, in its own direction, as the read is past the leaving point.
        xDir = pendingXDir;
        xInGain = pendingInGain;
        xPosition = pending.toFrame + direction * xDir * std::abs (position - pending.fromFrame);
        if (! xEndsLoop)
            pedalState = PedalState::looping;
    }
    else
    {
        xPosition = pending.toFrame + (position - pending.fromFrame);
        xDir = 1.0;
        xInGain = 1.0f;
    }
    pendingIsPedal = false;
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

//==============================================================================
// PEDAL LOOP (Pedal Loop 1.0, docs/research/pedal-loop)
//
// One state per voice: off (as without the pedal loop), armed (the read goes on to the loop
// point ahead), looping. Every move between them is a crossfade on the continuation's own
// machinery (pending / beginCrossfade), so the read never jumps unblended:
//   - Forward: the region's jump (end - crossfade -> start), scheduled after every pass.
//   - Ping-Pong: at each end, a short mirror crossfade: the read carries on to the turning point
//     while the incoming stream leaves it the other way, both reading the same audio (at the
//     crossfade's middle, the same sample); or, with no turnaround time, a plain reflection.
//   - A late pedal (the read past every loop point): a bounded crossfade from here into the
//     last region, its level matched to where the note was.
//   - Leaving (pedal up): Forward reads on into the recording (no splice); Ping-Pong reading back
//     turns forward where it is; a late loop crossfades back to where it found the recording.
// The latest pedal state wins: a change during a crossfade is taken when it ends.

void InstrumentVoice::setPedal (bool down) noexcept
{
    if (! active || released || pedalMode == PedalLoopMode::normal)
        return;
    pedalWanted = down;
    if (! crossfading)
        pedalUpdate();
}

void InstrumentVoice::pedalUpdate() noexcept
{
    if (crossfading || pedalMode == PedalLoopMode::normal)
        return;
    if (pedalWanted && ! released && pedalState == PedalState::off)
        pedalArm();
    else if (! pedalWanted && pedalState != PedalState::off)
        pedalDisarm();
}

void InstrumentVoice::pedalArm() noexcept
{
    const auto& model = cont->pedal;
    const auto& regions = model.regions;
    const int count = static_cast<int> (regions.size());
    if (count == 0)
        return;
    loopGain = 1.0f;
    lateResume = -1.0;
    // The first region whose loop point is still ahead: the note plays on to it.
    for (int k = 0; k < count; ++k)
    {
        const auto& r = regions[static_cast<std::size_t> (k)];
        const double leave = pedalMode == PedalLoopMode::forward ? r.endFrame - r.crossfadeFrames : r.endFrame - pedalTurnFor (r);
        if (position < leave)
        {
            pedalRegion = k;
            pedalState = PedalState::armed;
            scheduleNextJump();
            return;
        }
    }
    // Late: the read is past every loop point. A bounded crossfade into the last region, at the
    // level the note has here; too close to the end of the recording, nothing (a note that has
    // ended is never brought back).
    const auto& r = regions.back();
    const auto& src = *layer->source;
    const double rate = src.sampleRate();
    const double room = static_cast<double> (src.numFrames()) - position - static_cast<double> (sinc->maxReach()) - 2.0;
    double len = std::min (0.05 * rate, room);
    len = std::min (len, 0.5 * (r.endFrame - r.startFrame - r.crossfadeFrames));
    if (len < 0.002 * rate)
        return;
    pedalRegion = count - 1;
    pedalState = PedalState::armed;
    const float gain = std::clamp (dbToLinear (static_cast<double> (model.levelAt (position) - r.levelDb)), 0.02f, 1.0f);
    lateResume = position + len;
    if (pedalMode == PedalLoopMode::forward)
        startPedalCrossfade (position, r.startFrame, len, 0.0f, 1.0, gain, false);
    else
        startPedalCrossfade (position, r.endFrame, len, 0.0f, -1.0, gain, false);
}

void InstrumentVoice::pedalDisarm() noexcept
{
    const auto& src = *layer->source;
    const double rate = src.sampleRate();
    const double reach = static_cast<double> (sinc->maxReach()) + 2.0;
    if (pedalState == PedalState::looping && lateResume >= 0.0)
    {
        // Back to the recording where the late pedal found it, at its own level.
        const double len = std::min (0.03 * rate, static_cast<double> (src.numFrames()) - lateResume - reach);
        if (len >= 0.002 * rate)
        {
            startPedalCrossfade (position, lateResume, len, 0.0f, 1.0, 1.0f, true);
            return;
        }
    }
    if (pedalState == PedalState::looping && ppDir < 0.0)
    {
        // Reading back: turn forward where the read is (both streams leave the same sample).
        const double len = std::min (0.015 * rate, std::max (1.0, position - reach));
        startPedalCrossfade (position, position, len, pedalTurnCorrelation, 1.0, loopGain, true);
        return;
    }
    // Forward (or not looping yet): the read simply goes on into the recording.
    pedalState = PedalState::off;
    pedalRegion = -1;
    lateResume = -1.0;
    hasPending = false;
    scheduleNextJump();
}

bool InstrumentVoice::schedulePedal() noexcept
{
    const auto& regions = cont->pedal.regions;
    if (pedalRegion < 0 || pedalRegion >= static_cast<int> (regions.size()))
    {
        pedalState = PedalState::off;
        return false;
    }
    const auto& r = regions[static_cast<std::size_t> (pedalRegion)];
    hasPending = false;
    pendingIndex = -1;
    pendingIsGraft = false;
    pendingInGain = loopGain;
    xEndsLoop = false;
    if (pedalMode == PedalLoopMode::forward)
    {
        pending = r.jump();
        pendingXDir = 1.0;
        pendingIsPedal = true;
        hasPending = true;
        return true;
    }
    const double turn = pedalTurnFor (r);
    if (turn < 1.0)
        return true;   // a plain reflection (render)
    pending.crossfadeFrames = turn;
    pending.correlation = pedalTurnCorrelation;
    pending.score = 0.0f;
    if (ppDir > 0.0)
    {
        pending.fromFrame = r.endFrame - turn;
        pending.toFrame = r.endFrame;
        pendingXDir = -1.0;
    }
    else
    {
        pending.fromFrame = r.startFrame + turn;
        pending.toFrame = r.startFrame;
        pendingXDir = 1.0;
    }
    pendingIsPedal = true;
    hasPending = true;
    return true;
}

void InstrumentVoice::startPedalCrossfade (double from, double to, double frames, float correlation, double incomingDir, float inGain, bool endsLoop) noexcept
{
    pending.fromFrame = from;
    pending.toFrame = to;
    pending.crossfadeFrames = std::max (1.0, frames);
    pending.correlation = correlation;
    pending.score = 0.0f;
    pendingIndex = -1;
    pendingIsGraft = false;
    pendingIsPedal = true;
    pendingXDir = incomingDir;
    pendingInGain = inGain;
    xEndsLoop = endsLoop;
    hasPending = true;
    beginCrossfade();
}

void InstrumentVoice::finishPedalCrossfade() noexcept
{
    position = xPosition;
    ppDir = xDir;
    loopGain = xInGain;
    trendOut = trendIn;   // (the read is where the incoming stream was)
    if (xEndsLoop)
    {
        trendOut = trendIn = 1.0f;
        xEndsLoop = false;
        pedalState = PedalState::off;
        pedalRegion = -1;
        lateResume = -1.0;
        ppDir = 1.0;
    }
}

float InstrumentVoice::trendGainAt (double pos) const noexcept
{
    const auto& regions = cont->pedal.regions;
    if (pedalRegion < 0 || pedalRegion >= static_cast<int> (regions.size()))
        return 1.0f;
    const auto& r = regions[static_cast<std::size_t> (pedalRegion)];
    // The region's trend taken out: the level it has where the jump leaves, everywhere in it
    // (0 dB there, so the loop starts without a step).
    const double length = std::max (1.0, r.endFrame - r.startFrame);
    const double frac = std::clamp ((pos - r.startFrame) / length, 0.0, 1.0);
    const double leave = std::clamp ((r.endFrame - r.crossfadeFrames - r.startFrame) / length, 0.0, 1.0);
    return dbToLinear (static_cast<double> (r.trendDb) * (leave - frac));
}

void InstrumentVoice::reflectPedal() noexcept
{
    const auto& regions = cont->pedal.regions;
    if (pedalRegion < 0 || pedalRegion >= static_cast<int> (regions.size()))
        return;
    const auto& r = regions[static_cast<std::size_t> (pedalRegion)];
    const double lo = r.startFrame, hi = r.endFrame;
    // Overshoot folds back (twice at most for any sane step; then the middle, never outside).
    for (int n = 0; n < 4; ++n)
    {
        if (position > hi)
        {
            position = 2.0 * hi - position;
            ppDir = -1.0;
            pedalState = PedalState::looping;
        }
        else if (ppDir < 0.0 && position < lo)
        {
            position = 2.0 * lo - position;
            ppDir = 1.0;
        }
        else
            return;
    }
    position = 0.5 * (lo + hi);
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
    // The range ends rarely move; their log2 is taken only when they do.
    if (s.filterMinHz != rangeMinHz || s.filterMaxHz != rangeMaxHz)
    {
        rangeMinHz = s.filterMinHz;
        rangeMaxHz = s.filterMaxHz;
        rangeLoOctaves = shaping::rangeOctaves (rangeMinHz);
        rangeHiOctaves = shaping::rangeOctaves (rangeMaxHz);
    }
    const double target = shaping::cutoffOctaves (rangeLoOctaves, rangeHiOctaves, shapingState->character);
    charOctaves = immediate ? target : charOctaves + (target - charOctaves) * charSmoothing;
    const double env = shaping::envelopeOctaves (s.envAmount) * filterEnv * envelopeScale;
    // A CUTOFF route moves along the filter's travel (1 = min to max), never past its ends.
    const double travel = modCutoff == 0.0 ? charOctaves
                                           : std::clamp (charOctaves + modCutoff * (rangeHiOctaves - rangeLoOctaves), std::min (rangeLoOctaves, rangeHiOctaves),
                                                         std::max (rangeLoOctaves, rangeHiOctaves));
    const double octaves = travel + env + velocityOctaves + driftTone;
    // Telemetry only (CharacterTap): nothing below reads it.
    tapBaseOctaves = travel + velocityOctaves + driftTone;
    tapDepthOctaves = shaping::envelopeOctaves (s.envAmount) * envelopeScale;
    tapCutoffOctaves = octaves;
    tapEnvPeak = std::max (tapEnvPeak, filterEnv);
    // TILT: the knob rotates dark <-> bright (+-12 dB), the envelope and velocity lean it.
    const double tiltDb = (std::clamp (shapingState->character, 0.0, 1.0) - 0.5) * 24.0 + 3.0 * (env + velocityOctaves + driftTone);
    const double resonance = modResonance != 0.0 ? std::clamp (s.resonance + modResonance, 0.0, 1.0) : s.resonance;
    charFilter.setParameters (s.filterType, std::exp2 (octaves), resonance, s.drive, tiltDb);
}

void InstrumentVoice::prepareSatellite() noexcept
{
    // SATELLITE: this render's place gains (the layer's motion glides across the chunk; BOUNCE
    // with NOTE: this note's own side). Inactive: the original output, untouched.
    satOn = satellite != nullptr && satellite->active;
    if (! satOn)
        return;
    const auto& s = *satellite;
    const float x0 = s.bounceNote ? s.amount * satNoteX : s.x0;
    const float x1 = s.bounceNote ? s.amount * satNoteX : s.x1;
    osp::satellite::gains (x0, satL0, satR0);
    osp::satellite::gains (x1, satL1, satR1);
    satRear0 = s.rear0;
    satRear1 = s.rear1;
}

void InstrumentVoice::satelliteOut (float lg, float rg, float* left, float* right, int at, float t) noexcept
{
    // The voice's own pan (WIDTH scales it towards the layer's travelling place), times the
    // place's equal-power gains; ORBIT's far half a little quieter and darker.
    float pL = panLeft, pR = panRight;
    if (satellite->width < 1.0f)
    {
        const float p = panNow * satellite->width;
        pL = std::min (1.0f, 1.0f - p);
        pR = std::min (1.0f, 1.0f + p);
    }
    const float rear = satRear0 + t * (satRear1 - satRear0);
    satRearL += satRearCoef * (lg - satRearL);
    satRearR += satRearCoef * (rg - satRearR);
    if (rear > 1.0e-4f)
    {
        lg = (1.0f - 0.3f * rear) * (lg + 0.65f * rear * (satRearL - lg));
        rg = (1.0f - 0.3f * rear) * (rg + 0.65f * rear * (satRearR - rg));
    }
    left[at] += lg * pL * (satL0 + t * (satL1 - satL0));
    right[at] += rg * pR * (satR0 + t * (satR1 - satR0));
}

void InstrumentVoice::updateDampen (bool immediate) noexcept
{
    // amount = curve[note] x AMOUNT (plus its routes), glided over ~10 ms; switched off it
    // glides to nothing, and the filter is skipped once there.
    const auto* s = dampenLive;
    double target = 0.0;
    double shelf = 3000.0, cutoff = 2000.0;
    auto mode = dampen::Mode::softShelf;
    if (s != nullptr)
    {
        mode = s->mode;
        shelf = std::clamp (s->shelfHz * (modDampenShelf != 0.0 ? std::exp2 (modDampenShelf) : 1.0), dampen::minShelfHz, dampen::maxShelfHz);
        cutoff = std::clamp (s->minCutoffHz * (modDampenCutoff != 0.0 ? std::exp2 (modDampenCutoff) : 1.0), dampen::minCutoffLowHz, dampen::minCutoffHighHz);
        if (s->enabled)
            target = dampen::noteDamping (*s, dampenNote) * std::clamp (s->amount + modDampenAmount, 0.0, 1.0);
    }
    dampenNow = immediate ? target : dampenNow + (target - dampenNow) * charSmoothing;
    if (target == 0.0 && dampenNow < 1.0e-3)
        dampenNow = 0.0;
    const bool moved = std::abs (dampenNow - dampenApplied) > 1.0e-3 || (dampenNow == 0.0 && dampenApplied != 0.0) || mode != dampenModeApplied
                       || std::abs (shelf - dampenShelfApplied) > 0.002 * shelf || std::abs (cutoff - dampenCutoffApplied) > 0.002 * cutoff;
    if (! moved)
        return;
    dampenApplied = dampenNow;
    dampenModeApplied = mode;
    dampenShelfApplied = shelf;
    dampenCutoffApplied = cutoff;
    dampenFilter.setup (mode, sampleRate, dampenNow, shelf, cutoff);
}

GranularParams InstrumentVoice::doubledGranular() const noexcept
{
    GranularParams p = notesGranular();
    // Behind = what was just played: earlier in the recording, or later when reversed.
    p.position = std::clamp (p.position + (p.reverse ? dGrainBehind : -dGrainBehind), 0.0, 1.0);
    return p;
}

GranularParams InstrumentVoice::notesGranular() const noexcept
{
    // The order (docs/research/unified-andor/phase-1-key-dna.md): the layer's knobs (START
    // included) -> KEY DNA (this key, fixed at note-on) -> modulation -> REIMAGINED scatter ->
    // LIFE (this trigger) -> the final clamp; the grains add their own randomness at spawn.
    GranularParams p = *granularLive;
    keydna::apply (shape.keyDna, keyDnaAmountApplied, p);
    double offset = static_cast<double> (shape.grainPositionOffset) + modStartOffset;
    if (modeEngine != nullptr)
        offset += modeEngine->grainPositionOffset();   // TAPE FRAME's tape, TOYBOX's heads
    if (modStarted)
    {
        offset += modGrainPosition;
        p.spread += modGrainSpread;
        // Routes take SIZE and DENSITY to their knobs' ends, never past.
        if (modGrainSize != 0.0)
            p.sizeSeconds = std::clamp (p.sizeSeconds * std::exp2 (modGrainSize), 0.02, 0.4);
        if (modGrainDensity != 0.0)
            p.density = std::clamp (p.density * std::exp2 (modGrainDensity), 4.0, 40.0);
    }
    if (gScatter > 0.0f)
    {
        // KALEIDOSCOPE's far end: grains from further around POS, a denser cloud.
        p.spread += 0.45 * static_cast<double> (gScatter);
        p.density *= 1.0 + 0.5 * static_cast<double> (gScatter);
    }
    p.position = std::clamp (p.position + offset, 0.0, 1.0);
    p.sizeSeconds = std::clamp (p.sizeSeconds * static_cast<double> (shape.grainSizeRatio), 0.01, 0.6);
    p.density = std::clamp (p.density * static_cast<double> (shape.grainDensityRatio), 2.0, 60.0);
    p.spread = std::clamp (p.spread + static_cast<double> (shape.grainSpreadOffset), 0.0, 1.0);
    p.tuneSemitones += static_cast<double> (shape.grainTuneCents) / 100.0;
    return p;
}

void InstrumentVoice::clearModulation() noexcept
{
    modStarted = false;
    modCents = modLevelDb = modPan = modReimagined = 0.0f;
    modCutoff = modResonance = modSustain = 0.0;
    modDampenAmount = modDampenShelf = modDampenCutoff = 0.0;
    modGrainPosition = modGrainSpread = modGrainSize = modGrainDensity = 0.0;
    modKeyDnaAmount = modSynthAmount = 0.0;
    appliedSustain = -1.0;
}

void InstrumentVoice::updateModulation (bool advance) noexcept
{
    if (modRuntime == nullptr || ! modRuntime->compiled.anyVoiceDest)
    {
        if (modStarted)
        {
            // The last route to a voice destination went: back to the plain settings.
            const bool sustainMoved = appliedSustain >= 0.0;
            clearModulation();
            if (sustainMoved)
                envelope.prepare (sampleRate, noteAdsr);
        }
        return;
    }
    const auto& rt = *modRuntime;
    if (! modStarted)
    {
        modState.start (rt);   // routes added while the note sounds: its sources start now
        if (released)
            modState.release (rt);
        modStarted = true;
    }
    else if (advance)
        modState.advance (rt, static_cast<double> (controlInterval) / sampleRate);

    const int layerOffset = std::clamp (voiceLayer, 0, 2);
    auto layerDest = [layerOffset] (mod::Dest a) { return static_cast<mod::Dest> (static_cast<int> (a) + layerOffset); };
    {
        // TUNE and LEVEL: a route takes the knob to its ends, never past; at LEVEL's bottom
        // the layer is silent.
        const auto l = static_cast<std::size_t> (layerOffset);
        const double cents = modState.offset (rt, layerDest (mod::Dest::fineTuneA));
        const double tune = rt.layerTuneSemitones[l];
        modCents = cents == 0.0 ? 0.0f
                                : static_cast<float> (100.0 * (std::clamp (tune + cents / 100.0, -mod::Runtime::maxTuneSemitones, mod::Runtime::maxTuneSemitones) - tune));
        modLevelDb = 0.0f;
        if (rt.compiled.has (layerDest (mod::Dest::levelA)))
        {
            // The layer's mix holds it at the bottom (-48 dB) while a route can lift it.
            const double base = std::max (rt.layerLevelDb[l], mod::Runtime::silentLevelDb);
            const double total = base + modState.offset (rt, layerDest (mod::Dest::levelA));
            modLevelDb = total <= mod::Runtime::silentLevelDb ? -400.0f : static_cast<float> (std::min (total, mod::Runtime::maxLevelDb) - base);
        }
    }
    modPan = static_cast<float> (modState.offset (rt, layerDest (mod::Dest::panA)));
    modCutoff = modState.offset (rt, mod::Dest::cutoff);
    modResonance = modState.offset (rt, mod::Dest::resonance);
    modGrainPosition = modState.offset (rt, layerDest (mod::Dest::grainPositionA));
    modGrainSpread = modState.offset (rt, layerDest (mod::Dest::grainSpreadA));
    modKeyDnaAmount = modState.offset (rt, layerDest (mod::Dest::keyDnaAmountA));
    modSynthAmount = modState.offset (rt, layerDest (mod::Dest::synthAmountA));
    modGrainSize = modState.offset (rt, layerDest (mod::Dest::grainSizeA));
    modGrainDensity = modState.offset (rt, layerDest (mod::Dest::grainDensityA));
    modSustain = modState.offset (rt, mod::Dest::ampSustain);
    modDampenAmount = modState.offset (rt, layerDest (mod::Dest::dampenAmountA));
    modDampenShelf = modState.offset (rt, layerDest (mod::Dest::dampenShelfA));
    modDampenCutoff = modState.offset (rt, layerDest (mod::Dest::dampenCutoffA));
    {
        // REIMAGINED: the engine already adds the global LFOs to the layer's amount (its
        // shared stage hears them too); a voice adds its own sources on top.
        const auto d = layerDest (mod::Dest::reimaginedA);
        const auto i = static_cast<std::size_t> (d);
        double own = 0.0;
        for (int k = 0; k < rt.compiled.termCount[i]; ++k)
        {
            const auto& term = rt.compiled.terms[i][static_cast<std::size_t> (k)];
            if (term.metaCount > 0)
            {
                // Its depth moved by other routes: this voice's whole term, less the part the
                // engine already added from the single-valued sources.
                own += rt.compiled.depthOf (term, modState.values) * modState.values[term.source]
                       - rt.compiled.depthOf (term, rt.globalValue) * rt.globalValue[term.source];
            }
            else if (mod::isPolySource (rt.settings, static_cast<mod::Source> (term.source + 1)))
                own += static_cast<double> (term.depth) * modState.values[term.source];
        }
        modReimagined = static_cast<float> (mod::destInfo (d).span * own);
    }
    if (rt.compiled.has (mod::Dest::ampSustain))
    {
        const double sustain = std::clamp (adsrSettings.sustainLevel + modSustain, 0.0, 1.0);
        if (std::abs (sustain - appliedSustain) > 1.0e-4)
        {
            auto a = noteAdsr;
            a.sustainLevel = sustain;
            envelope.prepare (sampleRate, a);   // the envelope glides to a new sustain (no step)
            appliedSustain = sustain;
        }
    }
}

void InstrumentVoice::updateControl() noexcept
{
    updateModulation (true);
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
            panNow = p;
        }
    }

    if (modPan != 0.0f || modPanApplied)
    {
        // PAN modulation on top of the note's own place (and its drift).
        const float drifted = shape.driftPan > 0.0f ? static_cast<float> (driftPanValue) : 0.0f;
        const float p = std::clamp (shape.pan + drifted + modPan, -1.0f, 1.0f);
        panLeft = std::min (1.0f, 1.0f - p);
        panRight = std::min (1.0f, 1.0f + p);
        panNow = p;
        modPanApplied = modPan != 0.0f;
    }
    if (shapingState != nullptr)
        updateCharacter (false);
    if (dampenLive != nullptr)
        updateDampen (false);
    if (granularMode)
    {
        if (shape.keyDna.active)
        {
            // KEY DNA AMOUNT, live on this sounding note (its profile stays its key's).
            const double target = std::clamp ((keyDnaAmountLive != nullptr ? *keyDnaAmountLive : 1.0) + modKeyDnaAmount, 0.0, 1.0);
            keyDnaAmountApplied += (target - keyDnaAmountApplied) * keyDnaSmoothing;
            if (std::abs (target - keyDnaAmountApplied) < 1.0e-5)
                keyDnaAmountApplied = target;
        }
        granularSource.setParams (notesGranular());
        if (grainDoubling)
            doubleSource.setParams (doubledGranular());
    }

    settleCents *= settleCoef;
    double shared = 0.0;
    if (shapingState != nullptr && shapingState->shaping.movementMode == MovementMode::drift && shapingState->movement > 0.0)
    {
        // DRIFT's shared part: the whole instrument wanders a little together.
        const auto& s = shapingState->shaping;
        shared = 0.3 * shapingState->movement * shaping::driftPitchCents (s.driftPitch)
                 * shaping::sharedWander (shapingState->seed, static_cast<double> (clock) / sampleRate, shaping::driftSpeedHz (s.driftSpeed));
    }
    const double cents = shape.pitchCents + settleCents + driftCentsValue + shared + static_cast<double> (modCents);
    pitchMod = cents != 0.0 ? std::exp2 (cents / 1200.0) : 1.0;

    // Expression follows quickly but smoothly (about 10 ms).
    expressionGain += (std::pow (10.0, expressionGainDb / 20.0) - expressionGain) * 0.15;
    expressionBright += (expressionBrightDb - expressionBright) * 0.15;
    float gainTarget = (drifting ? dbToLinear (driftLevel) : 1.0f) * static_cast<float> (expressionGain);
    if (modLevelDb != 0.0f)
        gainTarget *= dbToLinear (static_cast<double> (modLevelDb));
    controlGainStep = (gainTarget - controlGain) / controlInterval;

    if (! followContour && ! granularMode)
    {
        // FOLLOW off: the contour's lift where the read is now, glided over this control period.
        const double frames = std::max (1.0, static_cast<double> (layer->source->numFrames()));
        const auto target = static_cast<float> (std::pow (10.0, levelContour::boostDb (currentModel->analysis.envelope,
                                                                                        currentModel->analysis.source.durationSeconds,
                                                                                        readPosition() / frames) / 20.0));
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
    sinc->readFast (pos, step, kernelLevel, kernel, src.channelData (0), src.numChannels() > 1 ? src.channelData (1) : nullptr, l, r);
}

float InstrumentVoice::readTransient (const PlaybackSource& src, double pos, double step, int channel) noexcept
{
    // The transient buffers are short and silent at their end; past it they read as zero.
    if (pos < 0.0 || pos >= static_cast<double> (src.numFrames()))
        return 0.0f;
    sinc->computeKernelFast (pos, step, kernel, kernelLevel);
    return SincInterpolator::apply (kernel, src.channelData (channel));
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
    const float pan = std::clamp (static_cast<float> (0.7 * g * static_cast<double> (shape.spread) * grainRng.bipolar()), -1.0f, 1.0f);
    slot->left = std::min (1.0f, 1.0f - pan);
    slot->right = std::min (1.0f, 1.0f + pan);
    slot->active = true;
}

void InstrumentVoice::render (float* left, float* right, int numSamples, double pitchRatio, std::int64_t clockAtStart, double rootCorrection) noexcept
{
    clock = clockAtStart;
    if (! active)
        return;
    synthRootCorrection = rootCorrection;
    prepareSatellite();
    if (modeEngine != nullptr)
    {
        renderMode (left, right, numSamples, pitchRatio);
        return;
    }
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
            if (synthAmountLive != nullptr)
                synthControl (controlInterval);
            if (pedalMode == PedalLoopMode::forward && pedalState == PedalState::looping)
            {
                trendOut = trendGainAt (position);
                trendIn = crossfading && xIsPedal ? trendGainAt (xPosition) : trendOut;
            }
        }

        if (sourceEnded)
            ;   // the recording has ended; the oscillator holds the note
        else if (granularMode)
        {
            if (granularSource.isFinished())
            {
                if (synthHoldsVoice())
                    sourceEnded = true;
                else
                {
                    kill();
                    return;
                }
            }
        }
        else
        {
            if (! crossfading && hasPending && (direction * ppDir > 0.0 ? position >= pending.fromFrame : position <= pending.fromFrame))
                beginCrossfade();

            if (tailEndPosition > 0.0 && position >= tailEndPosition && fadeRemaining == 0)
                beginFastFade (static_cast<int> (0.05 * sampleRate));
            if (direction > 0.0 ? position >= endPosition : position <= 0.0)
            {
                if (synthHoldsVoice())
                    sourceEnded = true;
                else
                {
                    kill();
                    return;
                }
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
        if (sourceEnded)
            l = r = 0.0f;
        else if (granularMode)
        {
            granularSource.render (l, r, currentStep);
            if (grainDoubling)
            {
                // The doubling cloud: a few cents of slow chorus, faded in after the attack,
                // a little to one side (as on a read-through note).
                if (dDelaySamples > 0)
                    --dDelaySamples;
                else if (dRamp < 1.0f)
                    dRamp = std::min (1.0f, dRamp + dRampStep);
                float l2 = 0.0f, r2 = 0.0f;
                doubleSource.render (l2, r2, currentStep * (1.0 + 0.004 * dSway.sin()));
                const float g = dAmount * dRamp;
                const float norm = 1.0f / std::sqrt (1.0f + g * g);
                l = (l + g * (dSide < 0.0f ? 1.0f : dNear) * l2) * norm;
                r = (r + g * (dSide < 0.0f ? dNear : 1.0f) * r2) * norm;
                dSway.advance();
            }
        }
        else
        {
            const double bodyPosition = position; // what this sample's main read used
            if (stretching)
            {
                // SPEED: the grains carry the sound; the timeline (and a crossfade's other
                // read) moves at speed x the pitch step.
                stretchRead (l, r);
                if (crossfading)
                {
                    xPosition += direction * xDir * currentStep * stretchSpeed;
                    xProgress += currentStep * stretchSpeed;
                }
            }
            else
            {
                readFrame (position, currentStep, l, r);
                if (crossfading)
                {
                    float l2, r2, gOut, gIn;
                    readFrame (xPosition, currentStep, l2, r2);
                    crossfadeGains (static_cast<float> (xProgress / xLength), xCorrelation, gOut, gIn);
                    if (xIsPedal)
                    {
                        gOut *= loopGain * trendOut;
                        gIn *= xInGain * trendIn;
                    }
                    l = l * gOut + l2 * gIn;
                    r = r * gOut + r2 * gIn;
                    xPosition += direction * xDir * currentStep;
                    xProgress += currentStep;
                }
                else if (loopGain != 1.0f || trendOut != 1.0f)
                {
                    // A late pedal's loop, matched to where the note was; Forward's trend.
                    l *= loopGain * trendOut;
                    r *= loopGain * trendOut;
                }
            }
            if (dAmount > 0.0f)
            {
                if (dDelaySamples > 0)
                    --dDelaySamples;
                else if (dRamp < 1.0f)
                    dRamp = std::min (1.0f, dRamp + dRampStep);
                if (dRamp > 0.0f)
                {
                    const double behind = dBase + dDepth * dSway.sin();
                    float l2, r2;
                    // Behind = what was just played: earlier in the file, or later when reversed.
                    const double doubled = direction > 0.0 ? std::max (0.0, bodyPosition - behind * currentStep)
                                                           : std::min (dEnd, bodyPosition + behind * currentStep);
                    readFrame (doubled, currentStep, l2, r2);
                    const float g = dAmount * dRamp;
                    const float norm = 1.0f / std::sqrt (1.0f + g * g);
                    // Placed a little to one side, so the doubling widens instead of thickening.
                    l = (l + g * (dSide < 0.0f ? 1.0f : dNear) * l2) * norm;
                    r = (r + g * (dSide < 0.0f ? dNear : 1.0f) * r2) * norm;
                }
                dSway.advance();
            }
            if (stretching)
                position += direction * currentStep * stretchSpeed;
            else
                position += direction * ppDir * currentStep;
            if (pedalMode == PedalLoopMode::pingPong && pedalState != PedalState::off && ! crossfading && ! hasPending)
                reflectPedal();   // (no turnaround crossfade scheduled: a plain reflection)
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
                // Cheap 4-point reads (the grains' bus is darkened, so no band-limiting is
                // needed): each grain's Hermite taps, weighted by its window and pan, go into
                // four lanes per channel, summed once for the sample.
                using simd::Float4;
                const auto& src = *layer->source;
                const float* dataL = src.channelData (0);
                const float* dataR = src.numChannels() > 1 ? src.channelData (1) : dataL;
                const auto last = static_cast<double> (std::max<std::int64_t> (2, src.numFrames() - 3));
                auto accL = Float4::zero(), accR = Float4::zero();
                for (auto& grain : grains)
                {
                    if (! grain.active)
                        continue;
                    const auto x = static_cast<float> (grain.phase);
                    const float w = 16.0f * x * x * (1.0f - x) * (1.0f - x);
                    const double at = std::clamp (grain.position, 1.0, last);
                    const auto index = static_cast<std::int64_t> (at);
                    const auto taps = hermiteWeights (static_cast<float> (at - static_cast<double> (index)));
                    accL = accL + taps * Float4::broadcast (w * grain.left) * Float4::load (dataL + index - 1);
                    accR = accR + taps * Float4::broadcast (w * grain.right) * Float4::load (dataR + index - 1);
                    grain.position += grain.step;
                    grain.phase += grain.phaseStep;
                    if (grain.phase >= 1.0)
                        grain.active = false;
                }
                const float gl = accL.sum(), gr = accR.sum();
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
                if (xIsPedal)
                    finishPedalCrossfade();
                else
                    position = xPosition;
                // PEDAL LOOP: the pedal may have moved during the crossfade (the latest wins).
                if (pedalMode != PedalLoopMode::normal)
                    pedalUpdate();
                if (! crossfading)
                    scheduleNextJump();
            }
        }

        if (synthOn)
        {
            mixSynth (l, r);
            if (sourceEnded && (synthVoice.isSilent() || (synthGain <= 0.0f && synthGainStep <= 0.0f)))
            {
                kill();
                return;
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
            l = fastTanh (saturationDrive * l) * saturationNorm;
            r = fastTanh (saturationDrive * r) * saturationNorm;
        }
        charFilter.process (l, r);
        dampenFilter.process (l, r);

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

        // (l * g) * pan is what l * g * pan computes: the same samples, and the peak for free.
        const float lg = l * g, rg = r * g;
        outputPeak = std::max (outputPeak, std::abs (lg) + std::abs (rg));
        if (stereoOutput)
        {
            if (satOn)
                satelliteOut (lg, rg, left, right, i, static_cast<float> (i + 1) / static_cast<float> (numSamples));
            else
            {
                left[i] += lg * panLeft;
                right[i] += rg * panRight;
            }
        }
        else
        {
            left[i] += 0.5f * (l + r) * g;
        }
    }
}

void InstrumentVoice::startModeEngine (const InstrumentVoiceStart& params) noexcept
{
    modeEngine = nullptr;
    modeLive = params.reimagined;
    playingMode = params.reimaginedMode;
    dryEnded = false;
    modeCountdown = 0;
    // A note started at 0 % plays the recording exactly (no engine at all).
    if (playingMode == ReimaginedMode::kaleidoscope || modeLive == nullptr || ! (modeLive->amount > 0.0))
        return;
    ReimaginedVoiceEngine* engine = nullptr;
    switch (playingMode)
    {
        case ReimaginedMode::tapeFrame: engine = &tapeFrameEngine; break;
        case ReimaginedMode::toybox:    engine = &toyboxEngine; break;
        case ReimaginedMode::mosaic:    engine = &mosaicEngine; break;
        case ReimaginedMode::mirage:    engine = &mirageEngine; break;
        case ReimaginedMode::kaleidoscope: return;
    }
    ReimaginedNote note;
    note.model = currentModel;
    note.source = layer->source.get();
    note.analysis = currentModel->reimagined.get();
    note.sinc = sinc;
    note.kernel = &kernel;
    note.outputRate = sampleRate;
    note.startFrame = position;
    note.rootStep = layer->source->sampleRate() / sampleRate * std::exp2 ((layer->source->rootMidi() - currentModel->rootMidi) / 12.0);
    note.reverse = direction < 0.0;
    note.loop = params.strategy != ContinuationStrategy::off;   // the layer's LOOP (and Sustain)
    note.granular = granularMode;
    note.velocity = std::clamp (params.velocity, 1, 127) / 127.0;
    note.note = params.note;
    note.seed = shape.seed;
    ReimaginedControl control;
    control.amount = modReimagined != 0.0f ? std::clamp (modeLive->amount + static_cast<double> (modReimagined), 0.0, 1.0) : modeLive->amount;
    control.settings = &modeLive->settings;
    control.step = currentStep;
    if (! engine->start (note, control))
        return;   // its analysis is not ready: the recording plays as it is
    modeEngine = engine;
    if (! granularMode && ! engine->wantsDryRead())
    {
        // The engine reads the recording itself: no continuation walk, no graft.
        strategy = ContinuationStrategy::off;
        hasPending = false;
        graftPending = false;
        releaseGraftEnabled = false;
        tRemaining = 0;
    }
}

void InstrumentVoice::renderMode (float* left, float* right, int numSamples, double pitchRatio) noexcept
{
    const bool stereoOutput = right != left;
    for (int done = 0; done < numSamples;)
    {
        if (modeCountdown <= 0)
        {
            modeCountdown = controlInterval;
            updateControl();
            currentStep = baseIncrement * pitchRatio * pitchMod;
            if (gliding)
            {
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
            ReimaginedControl control;
            control.amount = modReimagined != 0.0f ? std::clamp (modeLive->amount + static_cast<double> (modReimagined), 0.0, 1.0) : modeLive->amount;
            control.settings = &modeLive->settings;
            control.step = currentStep;
            modeEngine->control (control);
            modeStep = currentStep * modeEngine->stepFactor();
            if (synthAmountLive != nullptr)
                synthControl (controlInterval);
        }
        const int n = std::min (modeCountdown, numSamples - done);
        const bool dry = ! granularMode && modeEngine->wantsDryRead();

        // The engine's input: the grains, the plain read of the recording, or nothing.
        if (granularMode)
        {
            for (int k = 0; k < n; ++k)
            {
                if (granularSource.isFinished())
                {
                    kill();
                    return;
                }
                granularSource.render (dryL[static_cast<std::size_t> (k)], dryR[static_cast<std::size_t> (k)], modeStep);
            }
        }
        else if (dry)
        {
            for (int k = 0; k < n; ++k)
            {
                float l = 0.0f, r = 0.0f;
                if (! dryEnded)
                {
                    if (! crossfading && hasPending && (direction > 0.0 ? position >= pending.fromFrame : position <= pending.fromFrame))
                        beginCrossfade();
                    if ((tailEndPosition > 0.0 && position >= tailEndPosition) || (direction > 0.0 ? position >= endPosition : position <= 0.0))
                        dryEnded = true;
                }
                if (! dryEnded)
                {
                    readFrame (position, modeStep, l, r);
                    if (crossfading)
                    {
                        float l2, r2, gOut, gIn;
                        readFrame (xPosition, modeStep, l2, r2);
                        crossfadeGains (static_cast<float> (xProgress / xLength), xCorrelation, gOut, gIn);
                        l = l * gOut + l2 * gIn;
                        r = r * gOut + r2 * gIn;
                        xPosition += direction * modeStep;
                        xProgress += modeStep;
                    }
                    position += direction * modeStep;
                    if (crossfading && xProgress >= xLength)
                    {
                        crossfading = false;
                        position = xPosition;
                        scheduleNextJump();
                    }
                }
                dryL[static_cast<std::size_t> (k)] = l;
                dryR[static_cast<std::size_t> (k)] = r;
            }
        }
        else
        {
            std::fill_n (dryL.begin(), n, 0.0f);
            std::fill_n (dryR.begin(), n, 0.0f);
        }
        modeEngine->render (dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);

        // Everything around the engine, as on every note: envelope, shelves, CHARACTER, level, pan.
        for (int k = 0; k < n; ++k)
        {
            ++clock;
            const float env = envelope.next();
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
            float l = wetL[static_cast<std::size_t> (k)], r = wetR[static_cast<std::size_t> (k)];
            if (synthOn)
                mixSynth (l, r);
            if (filtersActive)
            {
                l = lowL.process (highL.process (l));
                r = lowR.process (highR.process (r));
            }
            charFilter.process (l, r);
            dampenFilter.process (l, r);
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
            const int at = done + k;
            const float lg = l * g, rg = r * g;
            outputPeak = std::max (outputPeak, std::abs (lg) + std::abs (rg));
            if (stereoOutput)
            {
                if (satOn)
                    satelliteOut (lg, rg, left, right, at, static_cast<float> (at + 1) / static_cast<float> (numSamples));
                else
                {
                    left[at] += lg * panLeft;
                    right[at] += rg * panRight;
                }
            }
            else
                left[at] += 0.5f * (l + r) * g;
        }
        modeCountdown -= n;
        done += n;
        if (modeEngine->finished() && (! dry || dryEnded))
        {
            kill();
            return;
        }
    }
}

//==============================================================================
// SYNTHESIZE

void InstrumentVoice::synthControl (int samplesToNext) noexcept
{
    // The layer's amount and its routes (an LFO, an envelope, velocity ...), per voice.
    const double target = std::clamp (*synthAmountLive + modSynthAmount, 0.0, 1.0);
    if (! synthOn)
    {
        if (! (target > 0.0))
            return;   // never on: nothing of the oscillator runs
        synthOn = true;
        // SHAPE: the layer's, unless the development audition chose Follow for every source.
        const auto shape = synthEnvelope == synth::Envelope::follow || synthSettingsLive == nullptr ? synthEnvelope : synthSettingsLive->shape;
        synthVoice.start (synthSeed, std::clamp (synthVelocity, 1, 127) / 127.0, shape);
        imprintShaper.reset();
        imprintClock = 0;
        if (released)
            synthVoice.release();
        synthAmountNow = 0.0;
        synthSampleGain = 1.0f;
        synthGain = 0.0f;
    }
    // About 20 ms to a new amount (automation and turning the control stay click-free).
    const double k = 1.0 - std::exp (-static_cast<double> (samplesToNext) / (0.02 * sampleRate));
    synthAmountNow += (target - synthAmountNow) * k;
    if (std::abs (target - synthAmountNow) < 1.0e-4)
        synthAmountNow = target;
    const auto gains = synth::blendGains (synthAmountNow, synthBlend);
    const auto n = static_cast<float> (std::max (1, samplesToNext));
    synthSampleStep = (gains.sample - synthSampleGain) / n;
    synthGainStep = (gains.synth - synthGain) / n;

    // The played pitch: the key, with everything intentional on the voice (bend, fine tune,
    // the layer's TUNE, LIFE / DRIFT / routed cents, glide), never the recording's own root or
    // a root correction; Granular's own TUNE too (not its scatter or KEY DNA's offsets).
    double ratio = currentStep / std::max (1.0e-12, baseIncrement) / std::max (1.0e-12, synthRootCorrection);
    if (granularMode && granularLive != nullptr && granularLive->tuneSemitones != 0.0)
        ratio *= std::exp2 (granularLive->tuneSemitones / 12.0);
    synthPitchHz = 440.0 * std::exp2 ((static_cast<double> (currentNote) - 69.0) / 12.0) * ratio;
    if (synthSettingsLive == nullptr)
    {
        synthVoice.setPitch (synthPitchHz);
        return;
    }
    if (synthSettingsLive->pitch == synth::PitchSource::recording && ! granularMode)
    {
        // PITCH Recording: the pitch the recording is heard at here (its tuning, its vibrato),
        // the root correction put back. Unvoiced moments hold the last; ~5 ms smoothing (the
        // track's frames are already averaged over their window).
        if (const double r = recordedPitchRatio(); r > 0.0)
        {
            const double c = 1.0 - std::exp (-static_cast<double> (samplesToNext) / (0.005 * sampleRate));
            synthTrackRatio += (r - synthTrackRatio) * c;
        }
        synthPitchHz *= synthTrackRatio * synthRootCorrection;
    }
    synthVoice.setPitch (synthPitchHz, *synthSettingsLive);
    imprintControl (samplesToNext);
}

void InstrumentVoice::imprintControl (int samplesToNext) noexcept
{
    // IMPRINT: Analog (or IMPRINT 0) never touches the shaper, so S1's arithmetic stays exact; a
    // change to either fades the colour in or out (~25 ms) and the shaper steps aside at 0.
    const auto& s = *synthSettingsLive;
    // The note's own time (MOTION's frames), kept while Analog plays too: Imprint switched on
    // mid-note joins the recording where the note is, not at its attack.
    const auto clock = imprintClock;
    imprintClock += samplesToNext;
    if (imprintProfile == nullptr || (! s.imprinting() && ! imprintShaper.active()))
        return;
    const double amount = s.imprinting() ? std::clamp (s.imprint, 0.0, 1.0) : 0.0;
    // MOTION: the recording's own frames, the attack in order, then back and forth through its
    // sustain at the recording's pace (no seam; its release is never replayed while held).
    double framePosition = -1.0;
    const auto& p = *imprintProfile;
    if (s.motion > 0.0 && p.frameCount() > 0)
    {
        const double f = static_cast<double> (clock) / sampleRate / p.frameSeconds;
        if (f < p.sustainBegin)
            framePosition = f;
        else
        {
            const double span = std::max (1, p.sustainEnd - p.sustainBegin - 1);
            const double u = std::fmod (f - p.sustainBegin, 2.0 * span);
            framePosition = p.sustainBegin + (u < span ? u : 2.0 * span - u);
        }
    }
    imprintShaper.setTarget (p, amount, framePosition, s.motion, synthPitchHz, imprint::defaultFormantFollow, samplesToNext);
}

double InstrumentVoice::recordedPitchRatio() const noexcept
{
    const auto& track = currentModel->analysis.pitch.trackHz;
    const auto& src = *layer->source;
    if (track.values.empty() || ! (track.hopSeconds > 0.0) || ! (src.sampleRate() > 0.0))
        return 0.0;
    const double index = position / src.sampleRate() / track.hopSeconds;
    if (index < 0.0 || index > static_cast<double> (track.values.size() - 1))
        return 0.0;
    const auto i = static_cast<std::size_t> (index);
    const auto j = std::min (i + 1, track.values.size() - 1);
    const double a = track.values[i], b = track.values[j];
    if (! (a > 0.0) || ! (b > 0.0))
        return a > 0.0 ? a / (440.0 * std::exp2 ((src.rootMidi() - 69.0) / 12.0)) : 0.0;   // at a voicing edge: the voiced side
    const double hz = a + (b - a) * (index - static_cast<double> (i));
    const double ratio = hz / (440.0 * std::exp2 ((src.rootMidi() - 69.0) / 12.0));
    // Within a semitone of the root: an octave error or the next note is not followed.
    return ratio > 0.94387 && ratio < 1.05946 ? ratio : 0.0;
}

void InstrumentVoice::mixSynth (float& l, float& r) noexcept
{
    synthSampleGain += synthSampleStep;
    synthGain += synthGainStep;
    float osc = synthVoice.next (0.5f * (std::abs (l) + std::abs (r)));
    if (imprintShaper.active())
        osc = imprintShaper.process (osc);
    const float y = osc * std::max (0.0f, synthGain) * synthLevel;
    l = l * synthSampleGain + y;
    r = r * synthSampleGain + y;
}

bool InstrumentVoice::synthHoldsVoice() noexcept
{
    // A note whose recording ran out keeps sounding through its oscillator while the key is
    // held (a pluck turning into a held tone), then releases with it.
    // Follow ends with the recording ("when the sound stops"); Held keeps it to the ADSR.
    if (! synthOn || synthVoice.isFollowing())
        return false;
    return synthAmountNow > 0.0 || (synthAmountLive != nullptr && *synthAmountLive + modSynthAmount > 0.0);
}

//==============================================================================
// SPEED

void InstrumentVoice::startStretch (int note) noexcept
{
    stretching = ! granularMode && modeEngine == nullptr && std::abs (stretchSpeed - 1.0) > 1.0e-6;
    if (! stretching)
        return;
    // Grains of about four periods of the played note (40..120 ms): long enough to hold the
    // pitch, short enough to follow the recording.
    const double hz = 440.0 * std::exp2 ((static_cast<double> (note) - 69.0) / 12.0);
    const double seconds = std::clamp (4.0 / std::max (20.0, hz), 0.04, 0.12);
    stretchLength = 2 * std::max (32, static_cast<int> (0.5 * seconds * sampleRate));
    stretchHop = stretchLength / 2;
    // The recording's own period, in its (layer) frames: the note's analysed pitch.
    const auto& src = *layer->source;
    const double rootHz = 440.0 * std::exp2 ((src.rootMidi() - 69.0) / 12.0);
    stretchPeriod = rootHz > 0.0 ? src.sampleRate() / rootHz : 0.0;
    // The grains read ahead of the timeline (a whole grain's length): near the note's ends
    // they are kept inside its audio (spawnStretchGrain), and a read outside it is silence.
    stretchLo = 0.0;
    stretchHi = std::max (0.0, static_cast<double> (src.numFrames() - 1));
    // The first grain starts at its full level (the attack is heard as recorded); the next one
    // rises from the first sample, so the two always sum to one.
    stretchRotCos = std::cos (2.0 * std::numbers::pi / static_cast<double> (stretchLength));
    stretchRotSin = std::sin (2.0 * std::numbers::pi / static_cast<double> (stretchLength));
    stretchGrains = {};
    auto& first = stretchGrains[0];
    first.active = true;
    first.pos = position;
    first.age = stretchHop;
    first.cosine = -1.0;   // halfway: the window's peak
    first.sine = 0.0;
    stretchCountdown = 1;
}

void InstrumentVoice::spawnStretchGrain() noexcept
{
    StretchGrain* slot = nullptr;
    const StretchGrain* previous = nullptr;
    for (auto& g : stretchGrains)
    {
        if (! g.active && slot == nullptr)
            slot = &g;
        else if (g.active)
            previous = &g;
    }
    if (slot == nullptr)
        return;
    StretchGrain g;
    g.active = true;
    g.pos = position;
    if (crossfading)
    {
        // Inside a loop's crossfade: both reads, at their gains now.
        crossfadeGains (static_cast<float> (xProgress / xLength), xCorrelation, g.gainA, g.gainB);
        g.posB = xPosition;
    }
    else if (previous != nullptr && stretchPeriod > 0.0)
    {
        // In phase with the grain it overlaps: where that one reads now, plus whole periods
        // towards the timeline.
        const double k = std::round ((position - previous->pos) / stretchPeriod);
        g.pos = previous->pos + k * stretchPeriod;
    }
    // The whole grain inside the note's audio: one that would run past its end (or, reversed,
    // its start) begins earlier by whole periods, so it stays in phase. (Its read moves by
    // up to the grain's length at the current step; a margin covers bends and glides.)
    const double span = 1.25 * static_cast<double> (stretchLength) * currentStep;
    auto fit = [this, span] (double pos) {
        const double over = direction > 0.0 ? pos + span - stretchHi : stretchLo - (pos - span);
        if (over > 0.0)
            pos -= direction * (stretchPeriod > 0.0 ? std::ceil (over / stretchPeriod) * stretchPeriod : over);
        return std::clamp (pos, stretchLo, stretchHi);
    };
    g.pos = fit (g.pos);
    if (g.gainB > 0.0f)
        g.posB = fit (g.posB);
    *slot = g;
}

void InstrumentVoice::stretchRead (float& l, float& r) noexcept
{
    if (--stretchCountdown <= 0)
    {
        spawnStretchGrain();
        stretchCountdown = stretchHop;
    }
    l = r = 0.0f;
    for (auto& g : stretchGrains)
    {
        if (! g.active)
            continue;
        // sin^2 (pi age / length) = (1 - cos (2 pi age / length)) / 2: the angle turned by a
        // rotation each sample (no sin per sample; exact enough over a grain's few thousand).
        const auto window = static_cast<float> (0.5 * (1.0 - g.cosine));
        const double c = g.cosine * stretchRotCos - g.sine * stretchRotSin;
        g.sine = g.sine * stretchRotCos + g.cosine * stretchRotSin;
        g.cosine = c;
        // (Never outside the note's audio: past its ends a grain reads silence.)
        float a = 0.0f, b = 0.0f;
        if (g.pos >= stretchLo && g.pos <= stretchHi)
            readFrame (g.pos, currentStep, a, b);
        float left = a * g.gainA, right = b * g.gainA;
        if (g.gainB > 0.0f)
        {
            a = b = 0.0f;
            if (g.posB >= stretchLo && g.posB <= stretchHi)
                readFrame (g.posB, currentStep, a, b);
            left += a * g.gainB;
            right += b * g.gainB;
            g.posB += direction * currentStep;
        }
        l += window * left;
        r += window * right;
        g.pos += direction * currentStep;
        if (++g.age >= stretchLength)
            g.active = false;
    }
}

} // namespace osp
