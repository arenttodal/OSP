#include "engine/InstrumentEngine.h"

#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace osp
{

InstrumentEngine::InstrumentEngine() : voices (static_cast<std::size_t> (totalSlots)) {}

void InstrumentEngine::prepare (double outputSampleRate, int maximumBlockSize, const EngineSettings& settings)
{
    config = settings;
    config.polyphony = std::clamp (config.polyphony, 1, EngineSettings::maxPolyphony);
    sampleRate = outputSampleRate;
    liveShaping.shaping = config.shaping;
    liveShaping.character = config.macros.character;
    liveShaping.dynamics = config.macros.dynamics;
    liveShaping.movement = config.macros.motion;
    liveShaping.seed = config.seed;
    if (! interpolator || interpolator->zeroCrossings() != config.interpolationZeroCrossings)
    {
        interpolator = std::make_unique<SincInterpolator> (config.interpolationZeroCrossings);
        interpolator->prepareStretchTables();
    }
    for (auto& voice : voices)
        voice.prepare (sampleRate, config.adsr, interpolator.get(), &liveShaping);
    outputGain = static_cast<float> (dbToGain (config.outputGainDb));
    bufferSize = std::max (16, maximumBlockSize);
    for (auto& layerChannels : layerBuffer)
        for (auto& b : layerChannels)
            b.assign (static_cast<std::size_t> (bufferSize), 0.0f);
    liveGranular = config.granular;
    blendNow = config.blend;
    layerPitchRatio.fill (1.0);
    pedalDown = false;
    noteCounter = 0;
    pitchRatio = 1.0;
    channelBendRatio.fill (1.0);
    channelPressure.fill (0.0f);
    channelTimbre.fill (0.0f);
    // Targets first: prepare() resets the smoothed values to the targets, so the post
    // stage never depends on what was played before (bit-identical recall and bounces).
    post.setMacros (config.macros);
    post.setShaping (config.shaping);
    post.prepare (outputSampleRate, maximumBlockSize, config.seed);
    resetPerformance();
}

void InstrumentEngine::setChannelPitchBend (int channel, double semitones) noexcept
{
    if (channel >= 1 && channel <= 16)
        channelBendRatio[static_cast<std::size_t> (channel)] = semitonesToRatio (semitones);
}

void InstrumentEngine::setChannelPressure (int channel, double pressure01) noexcept
{
    if (channel >= 1 && channel <= 16)
        channelPressure[static_cast<std::size_t> (channel)] = static_cast<float> (std::clamp (pressure01, 0.0, 1.0));
}

void InstrumentEngine::setChannelTimbre (int channel, double timbre01) noexcept
{
    if (channel >= 1 && channel <= 16)
        channelTimbre[static_cast<std::size_t> (channel)] = static_cast<float> (std::clamp (timbre01, 0.0, 1.0)) - 0.5f;
}

void InstrumentEngine::resetPerformance() noexcept
{
    for (auto& takes : layerTake)
        takes.fill (-1);
    noteCounter = 0;
    sampleClock = 0;
    for (auto& p : layerPerformance)
        p.reset (config.seed);
    // Tape and drift randomness and reverb tails restart too: a bounce from the same
    // position repeats exactly.
    post.reset();
}

void InstrumentEngine::setEnvelope (const AdsrSettings& adsr) noexcept
{
    config.adsr = adsr;
    for (auto& voice : voices)
        voice.setEnvelopeSettings (adsr);
}

void InstrumentEngine::setOutputGainDb (double db) noexcept
{
    config.outputGainDb = db;
    outputGain = static_cast<float> (dbToGain (db));
}

void InstrumentEngine::setPitchOffsetSemitones (double semitones) noexcept
{
    pitchRatio = semitonesToRatio (semitones);
}

void InstrumentEngine::setLayerPitchOffsetSemitones (int layer, double semitones) noexcept
{
    layerPitchRatio[layerIndex (layer)] = semitonesToRatio (semitones);
}

int InstrumentEngine::memberFor (int note, int velocity, std::uint64_t eventIndex, int layerNumber) const noexcept
{
    const auto which = layerIndex (layerNumber);
    if (layerSet[which] == nullptr || layerSet[which]->groups.empty())
        return -1;
    // Nearest pitch anchor.
    const PitchGroup* group = &layerSet[which]->groups.front();
    int groupIndex = 0;
    for (int g = 0; g < static_cast<int> (layerSet[which]->groups.size()); ++g)
        if (std::abs (layerSet[which]->groups[static_cast<std::size_t> (g)].rootMidi - note) < std::abs (group->rootMidi - note))
        {
            group = &layerSet[which]->groups[static_cast<std::size_t> (g)];
            groupIndex = g;
        }
    // Velocity layer, then a round-robin take that is never the previous one.
    const int layer = std::clamp (static_cast<int> (std::clamp (velocity, 1, 127) / 128.0 * group->layers), 0, group->layers - 1);
    // The requested layer, or the nearest layer that has playable takes.
    int chosenLayer = -1;
    for (int id : group->members)
    {
        const auto& m = layerSet[which]->members[static_cast<std::size_t> (id)];
        if (m.role != SampleRole::articulation && (chosenLayer < 0 || std::abs (m.layer - layer) < std::abs (chosenLayer - layer)))
            chosenLayer = m.layer;
    }
    int candidates[64];
    int count = 0;
    for (int id : group->members)
    {
        const auto& m = layerSet[which]->members[static_cast<std::size_t> (id)];
        if (m.role != SampleRole::articulation && m.layer == chosenLayer && count < 64)
            candidates[count++] = id;
    }
    if (count == 0)
        return group->members.front();
    if (count == 1)
        return candidates[0];
    const auto key = static_cast<std::size_t> ((groupIndex % 64) * 8 + std::min (chosenLayer, 7));
    const int previous = layerTake[which][key];
    Prng rng (Prng::deriveSeed (config.seed, eventIndex, static_cast<std::uint64_t> (note) + 0x7272));
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const int pick = candidates[rng.nextBelow (static_cast<std::uint64_t> (count))];
        if (layerSet[which]->members[static_cast<std::size_t> (pick)].take != previous)
            return pick;
    }
    return candidates[0];
}

bool InstrumentEngine::isSetInUse (const InstrumentSet* set) const noexcept
{
    if (set == nullptr)
        return false;
    for (const auto& voice : voices)
        if (voice.isActive())
            for (const auto& m : set->members)
                if (voice.model() == m.model.get())
                    return true;
    return false;
}

void InstrumentEngine::killVoicesUsing (const InstrumentSet* set) noexcept
{
    if (set == nullptr)
        return;
    for (auto& voice : voices)
        if (voice.isActive())
            for (const auto& m : set->members)
                if (voice.model() == m.model.get())
                    voice.kill();
}

bool InstrumentEngine::isModelInUse (const InstrumentModel* model) const noexcept
{
    for (const auto& voice : voices)
        if (voice.isActive() && voice.model() == model)
            return true;
    return false;
}

void InstrumentEngine::killVoicesUsing (const InstrumentModel* model) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.model() == model)
            voice.kill();
}

int InstrumentEngine::countSoundingVoices (int layer) const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive() && ! voice.isFading() && voice.layerIndex() == layer)
            ++count;
    return count;
}

int InstrumentEngine::activeVoiceCount (int layer) const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive() && voice.layerIndex() == layer)
            ++count;
    return count;
}

int InstrumentEngine::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;
    return count;
}

bool InstrumentEngine::isNoteActive (int note) const noexcept
{
    for (const auto& voice : voices)
        if (voice.isActive() && ! voice.isFading() && voice.note() == note)
            return true;
    return false;
}

InstrumentVoice* InstrumentEngine::findFreeSlot() noexcept
{
    for (auto& voice : voices)
        if (! voice.isActive())
            return &voice;
    return nullptr;
}

InstrumentVoice* InstrumentEngine::chooseVictim (int layer) noexcept
{
    // Released voices first (quietest), then held voices (quietest, then oldest).
    InstrumentVoice* best = nullptr;
    auto score = [] (const InstrumentVoice& v) { return (v.isReleased() ? 0.0 : 10.0) + static_cast<double> (v.currentLevel()); };
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.isFading() || voice.layerIndex() != layer)
            continue;
        if (best == nullptr)
        {
            best = &voice;
            continue;
        }
        const double a = score (voice);
        const double b = score (*best);
        if (a < b - 1.0e-6 || (std::abs (a - b) <= 1.0e-6 && voice.startOrder() < best->startOrder()))
            best = &voice;
    }
    return best;
}

void InstrumentEngine::applyDynamics (NoteShape& shape, int velocity, const DynamicsProfile& p, const SourceCharacter& c,
                                      double dynamicsMacro, DynamicsMode mode, double referenceVelocity) noexcept
{
    if (mode == DynamicsMode::gainOnly)
        return;
    // Intensity relative to the recording: velocity 100 plays it as recorded. Softer is
    // a wide, reliable range (we can always take bite away); harder is extrapolation and
    // stays narrow.
    const double v = std::clamp (velocity, 1, 127);
    const double intensity = std::clamp ((v - referenceVelocity) / 80.0, -1.25, 0.35);
    const double k = std::clamp (dynamicsMacro, 0.0, 1.0) / 0.5; // 0.5 = calibrated, 1 = twice
    if (k <= 0.0)
        return;

    if (mode == DynamicsMode::gainFilter)
    {
        shape.brightnessDb += static_cast<float> (k * p.brightnessDb * intensity);
        return;
    }

    const double soft = std::max (0.0, -intensity);
    shape.brightnessDb += static_cast<float> (k * p.brightnessDb * 0.8 * intensity);
    shape.attackBrightnessDb += static_cast<float> (k * p.brightnessDb * 0.6 * intensity * (0.4 + 0.6 * c.transientTonal));
    shape.transientDb += static_cast<float> (k * p.transientDb * intensity);
    shape.bodyDb += static_cast<float> (k * p.bodyDb * 0.5 * intensity);
    shape.attackSoftenSeconds += static_cast<float> (k * 0.001 * p.attackSoftenMs * soft);
    shape.pitchSettleCents += k * p.pitchTransientCents * std::max (0.0, intensity + 0.3) / 0.65;
    shape.dampingDbPerSecond += static_cast<float> (k * p.dampingDbPerSecond * soft);
    shape.transientSeconds = static_cast<float> (0.02 + 0.03 * c.transientTonal);
}

NoteShape InstrumentEngine::shapeFor (const InstrumentModel* model, int note, int velocity, std::uint64_t eventIndex,
                                     double referenceVelocity, double registerBrightnessDb, bool setMember, bool layered) noexcept
{
    NoteShape shape;
    shape.seed = Prng::deriveSeed (config.seed, eventIndex, static_cast<std::uint64_t> (note));
    const double v = std::clamp (velocity, 1, 127);
    // Single recordings: velocity 127 plays at the recording's level. Set members: each
    // plays at its recorded level at its loudness-anchored velocity, i.e. the level
    // depends on velocity alone, whichever recording plays.
    const double range = levelRangeDb();
    const double velocityDb = setMember ? -range * (referenceVelocity - v) / 127.0 : -range * (1.0 - v / 127.0);
    shape.gain = static_cast<float> (dbToGain (velocityDb));
    shape.brightnessDb += static_cast<float> (registerBrightnessDb);

    if (model != nullptr)
    {
        auto dynamics = model->dynamics;
        if (layered && layerSet[context] != nullptr && layerSet[context]->hasDynamicsModel && layerSet[context]->layerStepDb > 0.5)
        {
            // Multi-velocity learning (spec §35): one layer step is this far apart in
            // applyDynamics' intensity, and should change timbre as much as the real
            // recordings do, so a layer played softer approaches the layer below it.
            const double stepIntensity = layerSet[context]->layerStepDb * 127.0 / std::max (levelRangeDb(), 6.0) / 80.0;
            // About 2 dB of shelf per semitone of centroid; full mode applies 0.8 x brightnessDb.
            dynamics.brightnessDb = std::clamp (2.0 * layerSet[context]->layerStepBrightnessSt / (0.8 * stepIntensity), -6.0, 18.0);
            dynamics.attackSoftenMs = std::clamp (-layerSet[context]->layerStepAttackMs / stepIntensity, 0.0, 80.0);
        }
        // How much velocity reshapes the sound depends on the source (lab, dynamics-1):
        // sustained bowed/blown sources preferred velocity as level only, plucks the full
        // model at twice the calibrated strength. Real velocity layers (a set's learned
        // dynamics) speak for themselves and are not scaled.
        const double sourceScale = layered && layerSet[context] != nullptr && layerSet[context]->hasDynamicsModel
                                       ? 1.0
                                       : std::clamp (0.25 + 3.2 * model->character.transientTonal, 0.25, 2.0);
        applyDynamics (shape, velocity, dynamics, model->character, config.macros.dynamics * sourceScale, config.dynamicsMode, referenceVelocity);
        layerPerformance[context].perform (shape, note, velocity, static_cast<double> (sampleClock) / sampleRate, eventIndex,
                             model->performance, model->character, config.macros.life, config.shaping);
    }
    const auto* modelForMotion = model;

    const double r = std::clamp (config.macros.reimagined, 0.0, 1.0);
    const double motion = std::clamp (config.macros.motion, 0.0, 1.0);
    if (config.continuation == ContinuationStrategy::multiLoopMovement && modelForMotion != nullptr)
    {
        // MOVEMENT (shaping system v1.0 §32-34). DRIFT lives in the voices: slow,
        // smoothed random wander of pitch, CHARACTER position, level and pan, at the
        // popup's SPEED, PITCH and TONE. TAPE, CHORUS and PULSE are bus effects
        // (PostProcessor); in those modes the voices only keep Reimagined's own drift.
        // Plucks move less (they are not sustained long enough to need it).
        const auto& sh = config.shaping;
        const double sustained = 1.0 - 0.5 * modelForMotion->character.transientTonal;
        // Depth grows a little faster than the knob at first, so low settings are audible.
        const double m = (sh.movementMode == MovementMode::drift ? std::pow (std::clamp (motion, 0.0, 1.0), 0.75) : 0.0) * sustained;
        const double rr = 0.5 * r * r * sustained; // Reimagined: instability of its own
        shape.driftCents = static_cast<float> ((m * shaping::driftPitchCents (sh.movementB) + 12.0 * rr) * (1.0 + 2.0 * std::max (0.0, r - 0.6)));
        shape.driftToneOctaves = static_cast<float> (m * shaping::driftToneOctaves (sh.movementC) + 0.6 * rr);
        shape.driftLevelDb = static_cast<float> (1.5 * m + 1.0 * rr);
        shape.driftPan = static_cast<float> (0.3 * m + 0.2 * rr);
        shape.driftRateHz = static_cast<float> (sh.movementMode == MovementMode::drift ? shaping::driftSpeedHz (sh.movementA) : 0.15);
    }
    // Original <-> Reimagined (spec §12): shorter, more varied continuation; harmonic
    // saturation towards the far end. (Resonance and width live in PostProcessor.)
    shape.segmentScale = static_cast<float> ((1.0 - 0.7 * r * r) * (1.3 - 0.6 * motion));
    shape.saturation = static_cast<float> (0.7 * std::clamp ((r - 0.5) / 0.5, 0.0, 1.0));
    shape.doubling = static_cast<float> (0.7 * std::clamp ((r - 0.35) / 0.65, 0.0, 1.0));
    // Far end: granular continuation with harmonic remapping takes over the sustain.
    shape.granular = static_cast<float> (std::pow (std::clamp ((r - 0.45) / 0.55, 0.0, 1.0), 1.2));
    return shape;
}

void InstrumentEngine::noteOn (int note, int velocity, int channel) noexcept
{
    if (velocity <= 0)
    {
        noteOff (note, channel);
        return;
    }
    // DYNAMICS curve: SOFT reaches expressive levels easily, HARD needs a firm touch.
    velocity = shaping::curvedVelocity (config.shaping.velocityCurve, velocity);
    bool any = false;
    for (const auto* m : layerModel)
        any = any || (m != nullptr && m->isValid());
    if (! any)
        return;
    // One event for both layers (same note order and performance memory); layer B
    // draws its own randomness from a salted index.
    const std::uint64_t eventIndex = noteCounter++;
    for (int layer = 0; layer < EngineSettings::layers; ++layer)
        noteOnLayer (layer, note, velocity, channel, layer == 0 ? eventIndex : (eventIndex ^ 0x4c61796572420000ull));
}

void InstrumentEngine::noteOnLayer (int layerNumber, int note, int velocity, int channel, std::uint64_t eventIndex) noexcept
{
    context = layerIndex (layerNumber);
    const InstrumentModel* model = layerModel[context];
    if (model == nullptr || ! model->isValid())
        return;

    const auto stealFade = std::max (1, static_cast<int> (config.stealFadeSeconds * sampleRate));
    if (countSoundingVoices (layerNumber) >= config.polyphony)
        if (auto* victim = chooseVictim (layerNumber))
            victim->beginFastFade (stealFade);

    InstrumentVoice* slot = findFreeSlot();
    if (slot == nullptr)
    {
        for (auto& voice : voices)
            if (voice.isFading() && (slot == nullptr || voice.startOrder() < slot->startOrder()))
                slot = &voice;
        if (slot == nullptr)
            return;
        slot->kill();
    }

    double referenceVelocity = 100.0;
    double registerDb = 0.0;
    bool setMember = false, layered = false;
    if (layerSet[context] != nullptr && layerSet[context]->members.size() > 1)
    {
        const int index = memberFor (note, velocity, eventIndex, layerNumber);
        if (index >= 0)
        {
            const auto& member = layerSet[context]->members[static_cast<std::size_t> (index)];
            const auto& group = layerSet[context]->groups[static_cast<std::size_t> (member.pitchGroup)];
            model = member.model.get();
            const auto key = static_cast<std::size_t> ((member.pitchGroup % 64) * 8 + std::min (member.layer, 7));
            layerTake[context][key] = static_cast<std::int8_t> (member.take);
            // Loudness-anchored: the loudest layer belongs at velocity 127, a softer one as
            // much lower as the velocity range says its level is (round-robin takes keep
            // their own differences around their layer).
            setMember = true;
            layered = group.layers > 1;
            referenceVelocity = std::clamp (127.0 - (layerSet[context]->loudestDb - member.layerLoudnessDb) * 127.0 / std::max (levelRangeDb(), 6.0),
                                            1.0, 127.0);
            if (layerSet[context]->hasRegisterModel)
                registerDb = std::clamp (1.5 * layerSet[context]->brightnessSlope * (note - group.rootMidi), -8.0, 8.0);
        }
    }
    InstrumentVoiceStart params;
    params.model = model;
    params.shape = shapeFor (model, note, velocity, eventIndex, referenceVelocity, registerDb, setMember, layered);
    params.layer = &model->layerFor (static_cast<double> (note), config.pitchCharacter);
    // CHARACTER follows touch (DYNAMICS x TONE): harder notes open the filter and get a
    // deeper filter envelope, softer ones stay darker. Off when velocity is level only.
    if (config.dynamicsMode != DynamicsMode::gainOnly)
    {
        const double coupling = std::clamp (config.macros.dynamics, 0.0, 1.0) * std::clamp (config.shaping.dynamicsTone, 0.0, 1.0);
        const double vn = std::clamp (velocity, 1, 127) / 127.0; // after the DYNAMICS curve
        params.shape.filterVelocityOctaves = static_cast<float> (4.0 * coupling * (vn - 0.75));
        params.shape.filterEnvelopeScale = static_cast<float> (std::clamp (1.0 + coupling * (vn / 0.75 - 1.0), 0.0, 1.6));
    }
    // Transient/body separation (spec §19): from about a fifth away, the attack's
    // transient keeps its own speed. The separated transient carries its own size, so
    // sources without one are left unchanged.
    if (model != nullptr && model->original.transient != nullptr)
    {
        const double shift = static_cast<double> (note) - model->rootMidi;
        if (config.transientPreservation)
            params.shape.transientPreserve = static_cast<float> (std::clamp ((std::abs (shift) - 2.0) / 5.0, 0.0, 1.0));
        // Transient/body mixing: where the recording has a real transient, the attack
        // emphasis from velocity and LIFE acts on it alone (a harder pick, not a louder
        // note); sources without one keep the gain-shaped attack.
        if (config.transientMixing && model->original.transientShare >= 0.02)
        {
            params.shape.transientMixDb = 2.0f * params.shape.transientDb;
            params.shape.transientDb = 0.0f;
        }
        // Long enough for the transposed copy of the transient (0.55 s of source) to end.
        const double ratio = std::pow (2.0, shift / 12.0);
        params.shape.transientPreserveSeconds = static_cast<float> (std::min (3.0, 0.55 / std::min (1.0, ratio)));
    }
    params.note = note;
    params.velocity = velocity;
    params.channel = std::clamp (channel, 1, 16);
    const auto& src = *params.layer->source;
    params.increment = semitonesToRatio (static_cast<double> (note) - src.rootMidi()) * (src.sampleRate() / sampleRate);
    params.startOrder = noteCounter - 1;   // both layers' voices of a note share their age
    params.strategy = config.continuation;
    params.releaseGraft = config.releaseGraft;
    params.layerIndex = layerNumber;
    params.sourceMode = config.sourceMode[context];
    params.granular = &liveGranular[context];
    slot->start (params);
}

void InstrumentEngine::noteOff (int note, int channel) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && ! voice.isReleased() && voice.note() == note && (channel == 0 || ! mpe || voice.channel() == channel))
        {
            if (pedalDown)
                voice.setHeldByPedal (true);
            else
                voice.release();
        }
}

void InstrumentEngine::setSustainPedal (bool down) noexcept
{
    pedalDown = down;
    if (! down)
        for (auto& voice : voices)
            if (voice.isActive() && voice.isHeldByPedal())
                voice.release();
}

void InstrumentEngine::allNotesOff() noexcept
{
    pedalDown = false;
    for (auto& voice : voices)
        voice.release();
}

void InstrumentEngine::reset() noexcept
{
    pedalDown = false;
    for (auto& voice : voices)
        voice.kill();
    post.reset();
}

void InstrumentEngine::render (float* const* output, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;
    for (int ch = 0; ch < numChannels; ++ch)
        std::memset (output[ch], 0, sizeof (float) * static_cast<std::size_t> (numSamples));
    float* left = output[0];
    float* right = numChannels > 1 ? output[1] : output[0];
    // Pressure -> intensity (level + brightness), timbre (CC74 / MPE slide) -> brightness.
    // Without MPE every voice follows channel 1's expression, which carries the global values.
    for (auto& voice : voices)
    {
        if (! voice.isActive())
            continue;
        const auto ch = static_cast<std::size_t> (mpe ? std::clamp (voice.channel(), 1, 16) : 1);
        const float pressure = channelPressure[ch];
        voice.setExpression (5.0f * pressure, 5.0f * pressure + 10.0f * channelTimbre[ch]);
    }

    // Each layer renders apart, then the equal-power A/B blend (smoothed, about 20 ms).
    const double blendStep = 1.0 / (0.02 * sampleRate);
    const bool mono = left == right;
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (bufferSize, numSamples - done);
        const double blendFrom = blendNow;
        const double blendTo = config.blend > blendNow ? std::min (config.blend, blendNow + blendStep * n)
                                                       : std::max (config.blend, blendNow - blendStep * n);
        blendNow = blendTo;
        for (int layer = 0; layer < EngineSettings::layers; ++layer)
        {
            auto& bl = layerBuffer[static_cast<std::size_t> (layer)][0];
            auto& br = layerBuffer[static_cast<std::size_t> (layer)][1];
            bool sounding = false;
            for (auto& voice : voices)
            {
                if (! voice.isActive() || voice.layerIndex() != layer)
                    continue;
                if (! sounding)
                {
                    std::fill_n (bl.begin(), n, 0.0f);
                    std::fill_n (br.begin(), n, 0.0f);
                    sounding = true;
                }
                const auto ch = static_cast<std::size_t> (mpe ? std::clamp (voice.channel(), 1, 16) : 1);
                voice.render (bl.data(), mono ? bl.data() : br.data(), n, pitchRatio * layerPitchRatio[static_cast<std::size_t> (layer)] * (mpe ? channelBendRatio[ch] : 1.0),
                              sampleClock + done);
            }
            if (! sounding)
                continue;
            auto gainAt = [layer] (double b) {
                return static_cast<float> (layer == 0 ? std::cos (0.5 * std::numbers::pi * b) : std::sin (0.5 * std::numbers::pi * b));
            };
            const float g0 = gainAt (blendFrom), g1 = gainAt (blendTo);
            for (int i = 0; i < n; ++i)
            {
                const float g = g0 == g1 ? g0 : g0 + (g1 - g0) * static_cast<float> (i + 1) / static_cast<float> (n);
                left[done + i] += bl[static_cast<std::size_t> (i)] * g;
                if (! mono)
                    right[done + i] += br[static_cast<std::size_t> (i)] * g;
            }
        }
        done += n;
    }
    sampleClock += numSamples;
    // The shared post stage's resonances follow the louder layer.
    const auto main = config.blend <= 0.5 ? (layerModel[0] != nullptr ? 0 : 1) : (layerModel[1] != nullptr ? 1 : 0);
    post.setModel (layerModel[static_cast<std::size_t> (main)]);
    post.process (left, right, numSamples);
    for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
        for (int i = 0; i < numSamples; ++i)
            output[ch][i] *= outputGain;
    for (int ch = 2; ch < numChannels; ++ch)
        std::memcpy (output[ch], output[ch % 2], sizeof (float) * static_cast<std::size_t> (numSamples));
}

} // namespace osp
