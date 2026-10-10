#include "engine/InstrumentEngine.h"

#include "core/PitchMath.h"
#include "engine/KaleidoscopeEngine.h"

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
    {
        voice.prepare (sampleRate, config.adsr, interpolator.get(), &liveShaping);
        voice.setModulation (&modRuntime);
    }
    outputGain = static_cast<float> (dbToGain (config.outputGainDb));
    bufferSize = std::max (16, maximumBlockSize);
    for (std::size_t l = 0; l < slots.size(); ++l)
    {
        auto& slot = slots[l];
        for (auto& b : slot.buffer)
            b.assign (static_cast<std::size_t> (bufferSize), 0.0f);
        slot.rootRatio = 1.0;
        slot.tuneRatio = semitonesToRatio (config.layer[l].tuneSemitones);
        slot.pitchRatio = slot.rootRatio * slot.tuneRatio;
        slot.primed = false;
        refreshLiveGranular (l);
    }
    blendNow = config.blend;
    mixXNow = config.mixX;
    mixYNow = config.mixY;
    pedalDown = false;
    noteCounter = 0;
    clearHeldKeys();
    lastMonoNote = -1;
    config.glideSeconds = std::clamp (config.glideSeconds, 0.0, 10.0);
    pitchRatio = 1.0;
    channelBendRatio.fill (1.0);
    channelPressure.fill (0.0f);
    channelTimbre.fill (0.0f);
    // Targets first: prepare() resets the smoothed values to the targets, so the post
    // stage never depends on what was played before (bit-identical recall and bounces).
    post.setMacros (config.macros);
    post.setShaping (config.shaping);
    if (config.reimaginedRouting == ReimaginedRouting::perLayer)
        post.setReimagined (0.0);
    post.prepare (outputSampleRate, maximumBlockSize, config.seed);
    for (std::size_t l = 0; l < slots.size(); ++l)   // each layer's formants wander their own way
        slots[l].reimagined.prepare (outputSampleRate, Prng::deriveSeed (config.seed, 0x7265696dull, l));
    for (auto& slot : slots)
        slot.eq.prepare (outputSampleRate);
    resetPerformance();
}

void InstrumentEngine::resetLayerStages() noexcept
{
    const bool perLayer = config.reimaginedRouting == ReimaginedRouting::perLayer;
    for (std::size_t l = 0; l < slots.size(); ++l)
    {
        auto& slot = slots[l];
        slot.reimagined.setAmount (perLayer ? kaleidoscopeAmount (l) : 0.0);
        const auto& k = config.layer[l].reimaginedSettings.kaleidoscope;
        slot.reimagined.setShape (perLayer ? k.focus : 0.5, perLayer ? k.spread : 0.5);
        slot.reimagined.setModel (slot.model);
        slot.reimagined.reset();
        slot.reimaginedCountdown = 0;
        refreshReimagined (l);
    }
}

void InstrumentEngine::refreshReimagined (std::size_t layer) noexcept
{
    auto& live = slots[layer].reimaginedLive;
    live.amount = std::clamp (layerReimagined (static_cast<int> (layer)), 0.0, 1.0);
    live.settings = config.layer[layer].reimaginedSettings;
}

eq::Settings InstrumentEngine::effectiveEq (std::size_t layer) const noexcept
{
    // The stored settings, plus the global LFOs' routes (EQ is a stage of the whole layer:
    // a per-voice source has no single value here). A route moves a band, never switches it.
    auto s = config.layer[layer].eq;
    const auto& o = modEqOffset[layer];
    if (o[0] != 0.0 || o[1] != 0.0 || o[2] != 0.0 || o[3] != 0.0)
    {
        auto& bell = s.bands[static_cast<std::size_t> (eq::Band::bell)];
        const auto range = eq::frequencyRange (eq::Band::bell);
        bell.frequencyHz = std::clamp (bell.frequencyHz * std::exp2 (o[0]), range.minHz, range.maxHz);
        bell.gainDb = std::clamp (bell.gainDb + o[1], -eq::maxGainDb, eq::maxGainDb);
        auto& low = s.bands[static_cast<std::size_t> (eq::Band::lowShelf)];
        low.gainDb = std::clamp (low.gainDb + o[2], -eq::maxGainDb, eq::maxGainDb);
        auto& high = s.bands[static_cast<std::size_t> (eq::Band::highShelf)];
        high.gainDb = std::clamp (high.gainDb + o[3], -eq::maxGainDb, eq::maxGainDb);
    }
    return s;
}

void InstrumentEngine::runLayerStage (Slot& slot, float* left, float* right, int numSamples, bool mono) noexcept
{
    auto& stage = slot.reimagined;
    for (int i = 0; i < numSamples; ++i)
    {
        if (slot.reimaginedCountdown-- <= 0)
        {
            slot.reimaginedCountdown = ReimaginedStage::controlInterval - 1;
            stage.update();
        }
        float l = left[i];
        float r = mono ? l : right[i];
        stage.process (l, r);
        if (mono)
            left[i] = 0.5f * (l + r);
        else
        {
            left[i] = l;
            right[i] = r;
        }
    }
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
    noteCounter = 0;
    sampleClock = 0;
    for (auto& slot : slots)
    {
        slot.take.fill (-1);
        slot.performance.reset (config.seed);
    }
    // Tape and drift randomness and reverb tails restart too: a bounce from the same
    // position repeats exactly.
    post.reset();
    resetLayerStages();
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

void InstrumentEngine::publishGrains() noexcept
{
    GranularSource::GrainView views[GranularSource::maxGrains];
    for (int layer = 0; layer < EngineSettings::layers; ++layer)
    {
        auto& snapshot = slots[static_cast<std::size_t> (layer)].grains;
        int n = 0;
        for (const auto& voice : voices)
        {
            if (n >= GrainSnapshot::capacity)
                break;
            if (! voice.isActive() || voice.layerIndex() != layer || ! voice.isGranular())
                continue;
            const int got = voice.collectGrains (views, std::min (GranularSource::maxGrains, GrainSnapshot::capacity - n));
            for (int i = 0; i < got; ++i, ++n)
            {
                snapshot.position[static_cast<std::size_t> (n)].store (views[i].position, std::memory_order_relaxed);
                snapshot.level[static_cast<std::size_t> (n)].store (views[i].level, std::memory_order_relaxed);
                snapshot.lane[static_cast<std::size_t> (n)].store (views[i].lane, std::memory_order_relaxed);
            }
        }
        snapshot.count.store (n, std::memory_order_release);

        int heads = 0;
        for (const auto& voice : voices)
        {
            float where = 0.0f, level = 0.0f;
            if (heads >= GrainSnapshot::playheadCapacity)
                break;
            if (voice.layerIndex() != layer || ! voice.playhead (where, level))
                continue;
            snapshot.playheadPosition[static_cast<std::size_t> (heads)].store (where, std::memory_order_relaxed);
            snapshot.playheadLevel[static_cast<std::size_t> (heads)].store (level, std::memory_order_relaxed);
            ++heads;
        }
        snapshot.playheads.store (heads, std::memory_order_release);
    }
}

void InstrumentEngine::granularLife (NoteShape& shape, int note, std::uint64_t eventIndex) const noexcept
{
    // LIFE in Granular mode: every note takes its grains from its own place in the
    // recording, with its own grain size, density, spread and a small pitch offset, so
    // repeated notes sound like different clouds of the same sound. 50 % is clearly
    // audible but related; 100 % twice as far. The popup's PITCH sets the pitch offset,
    // TONE the spread and density variation, ATTACK the size variation; LOOSE varies
    // more, FRAY now and then jumps far across the recording.
    const double life = std::clamp (config.macros.life + modLifeOffset, 0.0, 1.0);
    if (life <= 0.0)
        return;
    const auto& s = config.shaping;
    const double mode = s.lifeMode == LifeMode::loose ? 1.5 : (s.lifeMode == LifeMode::fray ? 1.2 : 1.0);
    const double scale = life / 0.5 * mode;
    const double tone = std::max (0.0, s.lifeTone) / 0.3, attack = std::max (0.0, s.lifeAttack) / 0.25;
    Prng rng (Prng::deriveSeed (config.seed, eventIndex, static_cast<std::uint64_t> (note) + 0x67726c66ull));
    auto g = [&rng] { return std::clamp (rng.gaussian(), -2.5, 2.5); };
    double position = 0.06 * scale * g();
    if (s.lifeMode == LifeMode::fray && rng.nextDouble() < std::min (0.5, 0.25 * scale))
        position += rng.bipolar() * 0.3;   // a frayed note: grains from somewhere else entirely
    shape.grainPositionOffset = static_cast<float> (position);
    shape.grainSizeRatio = static_cast<float> (std::exp2 (0.45 * scale * attack * g()));
    shape.grainDensityRatio = static_cast<float> (std::exp2 (0.35 * scale * tone * g()));
    shape.grainSpreadOffset = static_cast<float> (0.08 * scale * tone * g());
    shape.grainTuneCents = static_cast<float> (1.5 * std::max (0.0, s.lifePitchCents) * scale * g());
}

void InstrumentEngine::setLayerPitchOffsetSemitones (int layer, double semitones) noexcept
{
    auto& slot = slots[layerIndex (layer)];
    slot.rootRatio = semitonesToRatio (semitones);
    slot.pitchRatio = slot.rootRatio * slot.tuneRatio;
}

void InstrumentEngine::setLayerSettings (int layer, const LayerSettings& settings) noexcept
{
    const auto l = layerIndex (layer);
    config.layer[l] = settings;
    auto& slot = slots[l];
    slot.tuneRatio = semitonesToRatio (std::clamp (settings.tuneSemitones, -48.0, 48.0));
    slot.pitchRatio = slot.rootRatio * slot.tuneRatio;
    refreshLiveGranular (l);
}

void InstrumentEngine::refreshLiveGranular (std::size_t l) noexcept
{
    // Granular reads START as an offset of POS, and the layer's REVERSE and FOLLOW.
    auto g = config.granular[l];
    const auto& layer = config.layer[l];
    g.position = std::clamp (g.position + layer.start, 0.0, 1.0);
    g.reverse = layer.reverse;
    g.follow = layer.follow;
    slots[l].liveGranular = g;
}

std::array<double, 3> InstrumentEngine::triangleShares (double x, double y) noexcept
{
    // Corners: A (0, 0), B (0.5, 1), C (1, 0). Outside the triangle the nearest shares are
    // used (negative ones clipped, the rest renormalised).
    x = std::clamp (x, 0.0, 1.0);
    y = std::clamp (y, 0.0, 1.0);
    std::array<double, 3> share { 1.0 - x - 0.5 * y, y, x - 0.5 * y };
    double total = 0.0;
    for (auto& v : share)
    {
        v = std::max (0.0, v);
        total += v;
    }
    if (total <= 1.0e-12)
        return { 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 };
    for (auto& v : share)
        v /= total;
    return share;
}

LayerMixWeights InstrumentEngine::mixWeights (const std::array<bool, 3>& occupied, double blend, double x, double y) noexcept
{
    LayerMixWeights w;
    int count = 0;
    std::array<int, 3> which {};
    for (int l = 0; l < 3; ++l)
        if (occupied[static_cast<std::size_t> (l)])
            which[static_cast<std::size_t> (count++)] = l;
    if (count == 1)
        w.gain[static_cast<std::size_t> (which[0])] = 1.0;
    else if (count == 2)
    {
        // The A/B blend (equal power): exactly the two-layer engine's crossfade.
        const double b = std::clamp (blend, 0.0, 1.0);
        w.gain[static_cast<std::size_t> (which[0])] = std::cos (0.5 * std::numbers::pi * b);
        w.gain[static_cast<std::size_t> (which[1])] = std::sin (0.5 * std::numbers::pi * b);
    }
    else if (count == 3)
    {
        // Constant power: the shares are powers, so all three at the centre add up like one.
        const auto share = triangleShares (x, y);
        for (std::size_t l = 0; l < 3; ++l)
            w.gain[l] = std::sqrt (share[l]);
    }
    return w;
}

std::array<bool, 3> InstrumentEngine::occupiedLayers() const noexcept
{
    std::array<bool, 3> occupied {};
    for (int l = 0; l < EngineSettings::layers; ++l)
        occupied[static_cast<std::size_t> (l)] = isLayerOccupied (l);
    return occupied;
}

int InstrumentEngine::occupiedLayerCount() const noexcept
{
    int count = 0;
    for (int l = 0; l < EngineSettings::layers; ++l)
        count += isLayerOccupied (l) ? 1 : 0;
    return count;
}

int InstrumentEngine::musicalVoiceCount() const noexcept
{
    // The voices of one note-on share their start order, whichever layers they play.
    std::array<std::uint64_t, totalSlots> seen;
    int count = 0;
    for (const auto& voice : voices)
    {
        if (! voice.isActive() || voice.isFading())
            continue;
        bool known = false;
        for (int i = 0; i < count && ! known; ++i)
            known = seen[static_cast<std::size_t> (i)] == voice.startOrder();
        if (! known)
            seen[static_cast<std::size_t> (count++)] = voice.startOrder();
    }
    return count;
}

int InstrumentEngine::memberFor (int note, int velocity, std::uint64_t eventIndex, int layerNumber) const noexcept
{
    const auto which = layerIndex (layerNumber);
    const auto* set = slots[which].set;
    if (set == nullptr || set->groups.empty())
        return -1;
    // Nearest pitch anchor.
    const PitchGroup* group = &set->groups.front();
    int groupIndex = 0;
    for (int g = 0; g < static_cast<int> (set->groups.size()); ++g)
        if (std::abs (set->groups[static_cast<std::size_t> (g)].rootMidi - note) < std::abs (group->rootMidi - note))
        {
            group = &set->groups[static_cast<std::size_t> (g)];
            groupIndex = g;
        }
    // Velocity layer, then a round-robin take that is never the previous one.
    const int layer = std::clamp (static_cast<int> (std::clamp (velocity, 1, 127) / 128.0 * group->layers), 0, group->layers - 1);
    // The requested layer, or the nearest layer that has playable takes.
    int chosenLayer = -1;
    for (int id : group->members)
    {
        const auto& m = set->members[static_cast<std::size_t> (id)];
        if (m.role != SampleRole::articulation && (chosenLayer < 0 || std::abs (m.layer - layer) < std::abs (chosenLayer - layer)))
            chosenLayer = m.layer;
    }
    int candidates[64];
    int count = 0;
    for (int id : group->members)
    {
        const auto& m = set->members[static_cast<std::size_t> (id)];
        if (m.role != SampleRole::articulation && m.layer == chosenLayer && count < 64)
            candidates[count++] = id;
    }
    if (count == 0)
        return group->members.front();
    if (count == 1)
        return candidates[0];
    const auto key = static_cast<std::size_t> ((groupIndex % 64) * 8 + std::min (chosenLayer, 7));
    const int previous = slots[which].take[key];
    Prng rng (Prng::deriveSeed (config.seed, eventIndex, static_cast<std::uint64_t> (note) + 0x7272));
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const int pick = candidates[rng.nextBelow (static_cast<std::uint64_t> (count))];
        if (set->members[static_cast<std::size_t> (pick)].take != previous)
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
        const auto* set = slots[context].set;
        if (layered && set != nullptr && set->hasDynamicsModel && set->layerStepDb > 0.5)
        {
            // Multi-velocity learning (spec §35): one layer step is this far apart in
            // applyDynamics' intensity, and should change timbre as much as the real
            // recordings do, so a layer played softer approaches the layer below it.
            const double stepIntensity = set->layerStepDb * 127.0 / std::max (levelRangeDb(), 6.0) / 80.0;
            // About 2 dB of shelf per semitone of centroid; full mode applies 0.8 x brightnessDb.
            dynamics.brightnessDb = std::clamp (2.0 * set->layerStepBrightnessSt / (0.8 * stepIntensity), -6.0, 18.0);
            dynamics.attackSoftenMs = std::clamp (-set->layerStepAttackMs / stepIntensity, 0.0, 80.0);
        }
        // How much velocity reshapes the sound depends on the source (lab, dynamics-1):
        // sustained bowed/blown sources preferred velocity as level only, plucks the full
        // model at twice the calibrated strength. Real velocity layers (a set's learned
        // dynamics) speak for themselves and are not scaled.
        const double sourceScale = layered && set != nullptr && set->hasDynamicsModel
                                       ? 1.0
                                       : std::clamp (0.25 + 3.2 * model->character.transientTonal, 0.25, 2.0);
        applyDynamics (shape, velocity, dynamics, model->character, config.macros.dynamics * sourceScale, config.dynamicsMode, referenceVelocity);
        slots[context].performance.perform (shape, note, velocity, static_cast<double> (sampleClock) / sampleRate, eventIndex,
                             model->performance, model->character, std::clamp (config.macros.life + modLifeOffset, 0.0, 1.0), config.shaping);
    }
    const auto* modelForMotion = model;

    // KALEIDOSCOPE's per-voice part (the other modes play through their own engines).
    const double r = kaleidoscopeAmount (context);
    const auto& kaleidoscopeParams = config.layer[context].reimaginedSettings.kaleidoscope;
    const double motion = std::clamp (config.macros.motion, 0.0, 1.0);
    // (As before the adaptive layers, a sound played once - LOOP off, or Sustain "Recording" -
    // keeps its own pitch and tone: no per-voice wander.)
    if (config.continuation == ContinuationStrategy::multiLoopMovement && config.layer[context].loop && modelForMotion != nullptr)
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
        shape.driftCents = static_cast<float> ((m * shaping::driftPitchCents (sh.driftPitch) + 12.0 * rr) * (1.0 + 2.0 * std::max (0.0, r - 0.6)));
        shape.driftToneOctaves = static_cast<float> (m * shaping::driftToneOctaves (sh.driftTone) + 0.6 * rr);
        shape.driftLevelDb = static_cast<float> (1.5 * m + 1.0 * rr);
        shape.driftPan = static_cast<float> (0.3 * m + 0.2 * rr * static_cast<double> (kaleidoscope::width (kaleidoscopeParams.spread)));
        shape.driftRateHz = static_cast<float> (sh.movementMode == MovementMode::drift ? shaping::driftSpeedHz (sh.driftSpeed) : 0.15);
    }
    // Original <-> Reimagined (spec §12), KALEIDOSCOPE: shorter, more varied continuation,
    // saturation, doubling, granular continuation. (Resonance and width: ReimaginedStage.)
    kaleidoscope::shapeNote (shape, r, motion, kaleidoscopeParams);
    return shape;
}

void InstrumentEngine::noteOn (int note, int velocity, int channel) noexcept
{
    if (velocity <= 0)
    {
        noteOff (note, channel);
        return;
    }
    modRuntime.noteStarted (note, channel);
    // DYNAMICS curve: SOFT reaches expressive levels easily, HARD needs a firm touch.
    velocity = shaping::curvedVelocity (config.shaping.velocityCurve, velocity);
    if (occupiedLayerCount() == 0)
        return;
    // One event for every layer (same note order and performance memory); layers B and C
    // draw their own randomness from a salted index.
    if (config.mono)
        monoNoteOn (note, velocity, channel);
    else
        startNote (note, velocity, channel);
}

void InstrumentEngine::startNote (int note, int velocity, int channel) noexcept
{
    const std::uint64_t eventIndex = noteCounter++;
    post.noteStarted();   // without a running transport SHAPER starts its pattern here
    static constexpr std::array<std::uint64_t, 3> salt { 0, 0x4c61796572420000ull, 0x4c61796572430000ull };
    for (int layer = 0; layer < EngineSettings::layers; ++layer)
        noteOnLayer (layer, note, velocity, channel, eventIndex ^ salt[static_cast<std::size_t> (layer)]);
}

void InstrumentEngine::monoNoteOn (int note, int velocity, int channel) noexcept
{
    // The key goes on top of the held stack (once).
    int kept = 0;
    for (int i = 0; i < heldCount; ++i)
        if (heldKeys[static_cast<std::size_t> (i)] != note)
            heldKeys[static_cast<std::size_t> (kept++)] = heldKeys[static_cast<std::size_t> (i)];
    heldCount = kept;
    if (heldCount == maxHeldKeys)
    {
        std::move (heldKeys.begin() + 1, heldKeys.end(), heldKeys.begin());
        --heldCount;
    }
    heldKeys[static_cast<std::size_t> (heldCount++)] = note;

    // Legato: a note is held (or kept by the pedal) - it changes pitch, nothing restarts.
    bool legato = false;
    if (monoNote >= 0)
        for (auto& voice : voices)
            if (voice.isActive() && ! voice.isReleased() && ! voice.isFading() && voice.note() == monoNote)
            {
                voice.glideTo (note, config.glideSeconds);
                legato = true;
            }
    if (! legato)
    {
        // A fresh note: whatever still sounds (a release tail) gets out of the way quickly.
        const auto fade = std::max (1, static_cast<int> (config.stealFadeSeconds * sampleRate));
        for (auto& voice : voices)
            if (voice.isActive() && ! voice.isFading())
                voice.beginFastFade (fade);
        startNote (note, velocity, channel);
        if (lastMonoNote >= 0 && lastMonoNote != note)
            for (auto& voice : voices)
                if (voice.isActive() && ! voice.isFading() && voice.startOrder() == noteCounter - 1)
                    voice.glideFrom (lastMonoNote, config.glideSeconds);
    }
    monoNote = lastMonoNote = note;
}

void InstrumentEngine::monoNoteOff (int note, int channel) noexcept
{
    int kept = 0;
    for (int i = 0; i < heldCount; ++i)
        if (heldKeys[static_cast<std::size_t> (i)] != note)
            heldKeys[static_cast<std::size_t> (kept++)] = heldKeys[static_cast<std::size_t> (i)];
    heldCount = kept;
    if (note != monoNote)
    {
        releaseNote (note, channel);   // e.g. a note started before switching to mono
        return;
    }
    if (heldCount > 0)
    {
        // Back to the newest key still held, legato.
        const int previous = heldKeys[static_cast<std::size_t> (heldCount - 1)];
        for (auto& voice : voices)
            if (voice.isActive() && ! voice.isReleased() && ! voice.isFading() && voice.note() == monoNote)
                voice.glideTo (previous, config.glideSeconds);
        monoNote = lastMonoNote = previous;
        return;
    }
    releaseNote (note, channel);
    monoNote = -1;
}

void InstrumentEngine::noteOnLayer (int layerNumber, int note, int velocity, int channel, std::uint64_t eventIndex) noexcept
{
    context = layerIndex (layerNumber);
    const InstrumentModel* model = slots[context].model;
    const auto* set = slots[context].set;
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
    if (set != nullptr && set->members.size() > 1)
    {
        const int index = memberFor (note, velocity, eventIndex, layerNumber);
        if (index >= 0)
        {
            const auto& member = set->members[static_cast<std::size_t> (index)];
            const auto& group = set->groups[static_cast<std::size_t> (member.pitchGroup)];
            model = member.model.get();
            const auto key = static_cast<std::size_t> ((member.pitchGroup % 64) * 8 + std::min (member.layer, 7));
            slots[context].take[key] = static_cast<std::int8_t> (member.take);
            // Loudness-anchored: the loudest layer belongs at velocity 127, a softer one as
            // much lower as the velocity range says its level is (round-robin takes keep
            // their own differences around their layer).
            setMember = true;
            layered = group.layers > 1;
            referenceVelocity = std::clamp (127.0 - (set->loudestDb - member.layerLoudnessDb) * 127.0 / std::max (levelRangeDb(), 6.0),
                                            1.0, 127.0);
            if (set->hasRegisterModel)
                registerDb = std::clamp (1.5 * set->brightnessSlope * (note - group.rootMidi), -8.0, 8.0);
        }
    }
    InstrumentVoiceStart params;
    params.model = model;
    params.shape = shapeFor (model, note, velocity, eventIndex, referenceVelocity, registerDb, setMember, layered);
    params.layer = &model->layerFor (static_cast<double> (note), config.pitchCharacter);
    // REIMAGINED modes other than KALEIDOSCOPE make their own pitch from the recording
    // itself (they are their own pitch character).
    refreshReimagined (context);
    params.reimagined = &slots[context].reimaginedLive;
    if (const auto mode = config.layer[context].reimaginedSettings.mode; mode != ReimaginedMode::kaleidoscope && params.reimagined->amount > 0.0)
    {
        params.layer = &model->original;
        params.reimaginedMode = mode;
    }
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
    const auto& layerSettings = config.layer[context];
    // LOOP off: the layer plays its recording once (Sustain in Advanced still applies to all).
    params.strategy = layerSettings.loop ? config.continuation : ContinuationStrategy::off;
    params.releaseGraft = config.releaseGraft;
    params.layerIndex = layerNumber;
    params.sourceMode = config.sourceMode[context];
    params.granular = &slots[context].liveGranular;
    params.startFraction = layerSettings.start;
    params.reverse = layerSettings.reverse;
    params.follow = layerSettings.follow;
    if (params.sourceMode == SourceMode::granular)
        granularLife (params.shape, note, eventIndex);
    slot->start (params);
}

void InstrumentEngine::noteOff (int note, int channel) noexcept
{
    modRuntime.noteEnded (note, channel);
    if (config.mono)
        monoNoteOff (note, channel);
    else
        releaseNote (note, channel);
}

void InstrumentEngine::releaseNote (int note, int channel) noexcept
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
    modRuntime.setPedal (down);
    if (! down)
        for (auto& voice : voices)
            if (voice.isActive() && voice.isHeldByPedal())
                voice.release();
}

void InstrumentEngine::allNotesOff() noexcept
{
    modRuntime.allNotesOff();
    pedalDown = false;
    clearHeldKeys();
    for (auto& voice : voices)
        voice.release();
}

void InstrumentEngine::reset() noexcept
{
    pedalDown = false;
    clearHeldKeys();
    lastMonoNote = -1;
    for (auto& voice : voices)
        voice.kill();
    for (auto& slot : slots)
    {
        slot.primed = false;
        slot.eq.reset();
    }
    post.reset();
    resetLayerStages();
}

void InstrumentEngine::setModulation (const mod::Settings& settings) noexcept
{
    modRuntime.setSettings (settings);
    if (! modRuntime.compiled.anyGlobalDest && modGlobalApplied)
    {
        // The last route to a shared stage went: the stages go back to the stored values.
        modReimaginedOffset.fill (0.0);
        modLifeOffset = 0.0;
        for (auto& o : modEqOffset)
            o.fill (0.0);
        modGlobalApplied = false;
        setMacros (config.macros);
        for (std::size_t l = 0; l < slots.size(); ++l)
            refreshReimagined (l);
    }
}

void InstrumentEngine::applyGlobalModulation (int samples) noexcept
{
    modRuntime.advanceGlobal (samples, sampleRate);
    if (! modRuntime.compiled.anyGlobalDest)
        return;
    // Effective values next to the stored ones (config.macros never changes here).
    // The shared stages hear the global envelopes too; REIMAGINED's layer amount (a voice
    // destination whose shared stage follows it) takes only the sources with one value here,
    // each voice adds its own envelopes and poly LFOs on top.
    const auto& c = modRuntime.compiled;
    const auto& shared = modRuntime.sharedValue;
    const auto& single = modRuntime.globalValue;
    auto offset = [&c, &shared] (mod::Dest d) { return c.has (d) ? mod::destInfo (d).span * c.sum (d, shared) : 0.0; };
    auto voiceOffset = [&c, &single] (mod::Dest d) { return c.has (d) ? mod::destInfo (d).span * c.sum (d, single) : 0.0; };
    Macros m = config.macros;
    modLifeOffset = offset (mod::Dest::life);
    m.drive = std::clamp (m.drive + offset (mod::Dest::drive), 0.0, 1.0);
    m.character = std::clamp (m.character + offset (mod::Dest::character), 0.0, 1.0);
    m.motion = std::clamp (m.motion + offset (mod::Dest::movement), 0.0, 1.0);
    m.space = std::clamp (m.space + offset (mod::Dest::space), 0.0, 1.0);
    m.echo = std::clamp (m.echo + offset (mod::Dest::echo), 0.0, 1.0);
    liveShaping.character = m.character;
    liveShaping.movement = m.motion;
    post.setMacros (m);
    if (config.reimaginedRouting == ReimaginedRouting::perLayer)
        post.setReimagined (0.0);
    for (std::size_t l = 0; l < modReimaginedOffset.size(); ++l)
    {
        auto layerDest = [l] (mod::Dest a) { return static_cast<mod::Dest> (static_cast<int> (a) + static_cast<int> (l)); };
        modReimaginedOffset[l] = voiceOffset (layerDest (mod::Dest::reimaginedA));
        refreshReimagined (l);
        modEqOffset[l] = { offset (layerDest (mod::Dest::eqBellFrequencyA)), offset (layerDest (mod::Dest::eqBellGainA)),
                           offset (layerDest (mod::Dest::eqLowShelfGainA)), offset (layerDest (mod::Dest::eqHighShelfGainA)) };
    }
    modGlobalApplied = true;
}

void InstrumentEngine::publishModulation() noexcept
{
    // The global LFOs, and the newest sounding voice's own sources.
    const InstrumentVoice* newest = nullptr;
    for (const auto& voice : voices)
        if (voice.isActive() && voice.modulationRunning() && (newest == nullptr || voice.startOrder() > newest->startOrder()))
            newest = &voice;
    for (std::size_t i = 0; i < 2; ++i)
    {
        const bool poly = modRuntime.settings.lfo[i].scope == mod::Scope::poly;
        const auto& lfo = poly && newest != nullptr ? newest->modulationState().lfo[i] : modRuntime.globalLfo[i];
        modViewPhase[i].store (static_cast<float> (lfo.phase), std::memory_order_relaxed);
        modViewValue[i].store (static_cast<float> (lfo.value), std::memory_order_relaxed);
        const auto* env = newest != nullptr ? &newest->modulationState().env[i] : nullptr;
        modViewValue[2 + i].store (env != nullptr ? static_cast<float> (env->level) : 0.0f, std::memory_order_relaxed);
        // A one-shot curve runs on its own clock (time since the note), an ADSR per stage.
        const double envTime = env == nullptr ? 0.0 : (modRuntime.settings.env[i].oneShotCurve ? env->elapsed : env->t);
        modViewEnvTime[i].store (static_cast<float> (envTime), std::memory_order_relaxed);
        modViewEnvStage[i].store (env != nullptr ? static_cast<int> (env->stage) : 0, std::memory_order_relaxed);
    }
    modViewValue[4].store (modRuntime.globalValue[4], std::memory_order_relaxed);   // the wheel
    for (std::size_t i = 0; i < 2; ++i)
        modViewGlobalEnv[i].store (static_cast<float> (modRuntime.globalEnv[i].level), std::memory_order_relaxed);
    modViewVoice.store (newest != nullptr, std::memory_order_release);
}

InstrumentEngine::ModView InstrumentEngine::modulationView() const noexcept
{
    ModView view;
    view.voice = modViewVoice.load (std::memory_order_acquire);
    for (std::size_t i = 0; i < 2; ++i)
    {
        view.lfoPhase[i] = modViewPhase[i].load (std::memory_order_relaxed);
        view.envTime[i] = modViewEnvTime[i].load (std::memory_order_relaxed);
        view.envStage[i] = modViewEnvStage[i].load (std::memory_order_relaxed);
    }
    for (std::size_t i = 0; i < view.value.size(); ++i)
        view.value[i] = modViewValue[i].load (std::memory_order_relaxed);
    for (std::size_t i = 0; i < 2; ++i)
        view.globalEnv[i] = modViewGlobalEnv[i].load (std::memory_order_relaxed);
    return view;
}

void InstrumentEngine::render (float* const* output, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;
    if (! modRuntime.compiled.any)
    {
        // No route: the global LFOs only keep time (for the display); the sound is untouched.
        modRuntime.advanceGlobal (numSamples, sampleRate);
        renderBlock (output, numChannels, numSamples);
        publishModulation();
        return;
    }
    // Modulated: the shared stages and the layers' amounts follow at control rate (32
    // samples), so a rhythmic LFO keeps its edges whatever block size the host uses.
    std::array<float*, 16> chunk {};
    const int channels = std::min (numChannels, static_cast<int> (chunk.size()));
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (32, numSamples - done);
        for (int ch = 0; ch < channels; ++ch)
            chunk[static_cast<std::size_t> (ch)] = output[ch] + done;
        applyGlobalModulation (n);
        renderBlock (chunk.data(), channels, n);
        done += n;
    }
    publishModulation();
}

void InstrumentEngine::renderBlock (float* const* output, int numChannels, int numSamples) noexcept
{
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

    // Each layer renders apart, then the mix: its weight (one layer alone, two by the
    // equal-power blend, three by the triangle; the controls smoothed over about 20 ms)
    // times LEVEL and PAN. Gains glide from block to block; a sudden change (a layer loaded
    // or emptied, a LEVEL jump) takes at least 10 ms.
    const double blendStep = 1.0 / (0.02 * sampleRate);
    const auto maxGainStep = static_cast<float> (1.0 / (0.01 * sampleRate));
    const bool mono = left == right;
    const auto occupied = occupiedLayers();
    // The mix law counts kept slots too (an empty one gets a share but plays nothing).
    auto mixLayers = occupied;
    for (int l = 0; l < std::min (config.mixSlots, EngineSettings::layers); ++l)
        mixLayers[static_cast<std::size_t> (l)] = true;
    auto towards = [] (double now, double target, double step) {
        return target > now ? std::min (target, now + step) : std::max (target, now - step);
    };
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (bufferSize, numSamples - done);
        blendNow = towards (blendNow, config.blend, blendStep * n);
        mixXNow = towards (mixXNow, config.mixX, blendStep * n);
        mixYNow = towards (mixYNow, config.mixY, blendStep * n);
        const auto weights = mixWeights (mixLayers, blendNow, mixXNow, mixYNow);
        const bool perLayer = config.reimaginedRouting == ReimaginedRouting::perLayer;
        if (perLayer)
        {
            post.setReimagined (0.0);   // each layer's own stage, below
        }
        else
        {
            // Legacy routing: the shared resonance stage follows the layers' amounts,
            // weighted by how much of each is heard (power). Untouched when every layer
            // follows the instrument's amount.
            double sum = 0.0, weighted = 0.0;
            bool own = false;
            for (std::size_t l = 0; l < weights.gain.size(); ++l)
            {
                own = own || config.layer[l].reimagined >= 0.0;
                const double levelDb = config.layer[l].levelDb;
                const double level = levelDb <= LayerSettings::minLevelDb || ! config.layer[l].audible ? 0.0 : dbToGain (std::min (levelDb, 12.0));
                const double power = occupied[l] ? weights.gain[l] * weights.gain[l] * level * level : 0.0;   // a kept, empty slot is not heard
                sum += power;
                weighted += power * kaleidoscopeAmount (l);   // another mode adds nothing to the shared stage
            }
            if (own && sum > 1.0e-12)
                post.setReimagined (weighted / sum);
        }
        for (int layer = 0; layer < EngineSettings::layers; ++layer)
        {
            auto& slot = slots[static_cast<std::size_t> (layer)];
            const auto& settings = config.layer[static_cast<std::size_t> (layer)];
            const double level = settings.levelDb <= LayerSettings::minLevelDb || ! settings.audible ? 0.0 : dbToGain (std::min (settings.levelDb, 12.0));
            const double pan = std::clamp (settings.pan, -1.0, 1.0);
            const double weight = weights.gain[static_cast<std::size_t> (layer)] * level;
            const float targetLeft = static_cast<float> (weight * (pan > 0.0 ? 1.0 - pan : 1.0));
            const float targetRight = static_cast<float> (weight * (pan < 0.0 ? 1.0 + pan : 1.0));

            // Per-layer routing: this layer's own Reimagined stage, on its signal before
            // LEVEL, PAN and the mix. While its amount is above 0 it runs every sample (also
            // between notes, so the resonators ring out and its state never depends on how
            // the host splits blocks); at 0 it is skipped.
            bool layerStage = false;
            refreshReimagined (static_cast<std::size_t> (layer));
            if (perLayer)
            {
                // KALEIDOSCOPE's stage; another mode fades it out (its tail rings down).
                const auto& k = settings.reimaginedSettings.kaleidoscope;
                slot.reimagined.setModel (slot.model);
                slot.reimagined.setAmount (kaleidoscopeAmount (static_cast<std::size_t> (layer)));
                slot.reimagined.setShape (k.focus, k.spread);
                layerStage = slot.reimagined.active();
            }

            bool sounding = false;
            for (const auto& voice : voices)
                if (voice.isActive() && voice.layerIndex() == layer)
                {
                    sounding = true;
                    break;
                }
            // The layer's EQ: engaged, it filters the layer (and rings out after its last note).
            slot.eq.setTarget (effectiveEq (static_cast<std::size_t> (layer)));
            const bool eqOn = slot.eq.active();
            const bool eqTail = eqOn && ! sounding && slot.eq.ringing();
            if ((! sounding && ! layerStage && ! eqTail) || ! slot.primed)
            {
                // Nothing to click: the gains may jump.
                slot.gainLeft = targetLeft;
                slot.gainRight = targetRight;
                slot.primed = true;
                if (! sounding && ! layerStage && ! eqTail)
                    continue;
            }
            const float l0 = slot.gainLeft, r0 = slot.gainRight;
            const float limit = maxGainStep * static_cast<float> (n);
            const float l1 = std::clamp (targetLeft, l0 - limit, l0 + limit);
            const float r1 = std::clamp (targetRight, r0 - limit, r0 + limit);
            slot.gainLeft = l1;
            slot.gainRight = r1;

            if (l0 == 0.0f && l1 == 0.0f && r0 == 0.0f && r1 == 0.0f)
            {
                // A silent layer is not rendered: held notes wait for it to come back,
                // released ones end, and an emptied layer's notes all end.
                for (auto& voice : voices)
                    if (voice.isActive() && voice.layerIndex() == layer && (voice.isReleased() || ! occupied[static_cast<std::size_t> (layer)]))
                        voice.kill();
                if (layerStage)
                {
                    // Unheard, but its stage keeps time (and rings down) on silence.
                    std::fill_n (slot.buffer[0].begin(), n, 0.0f);
                    std::fill_n (slot.buffer[1].begin(), n, 0.0f);
                    runLayerStage (slot, slot.buffer[0].data(), slot.buffer[1].data(), n, mono);
                }
                if (eqOn && slot.eq.ringing())
                    slot.eq.reset();   // unheard: no stale tail when it comes back (it fades in)
                continue;
            }

            auto& bl = slot.buffer[0];
            auto& br = slot.buffer[1];
            std::fill_n (bl.begin(), n, 0.0f);
            std::fill_n (br.begin(), n, 0.0f);
            for (auto& voice : voices)
            {
                if (! voice.isActive() || voice.layerIndex() != layer)
                    continue;
                const auto ch = static_cast<std::size_t> (mpe ? std::clamp (voice.channel(), 1, 16) : 1);
                voice.render (bl.data(), mono ? bl.data() : br.data(), n, pitchRatio * slot.pitchRatio * (mpe ? channelBendRatio[ch] : 1.0),
                              sampleClock + done);
            }
            if (layerStage)
                runLayerStage (slot, bl.data(), br.data(), n, mono);
            if (eqOn)
                slot.eq.process (bl.data(), br.data(), n, mono);
            for (int i = 0; i < n; ++i)
            {
                const float gl = l0 == l1 ? l0 : l0 + (l1 - l0) * static_cast<float> (i + 1) / static_cast<float> (n);
                left[done + i] += bl[static_cast<std::size_t> (i)] * gl;
                if (! mono)
                {
                    const float gr = r0 == r1 ? r0 : r0 + (r1 - r0) * static_cast<float> (i + 1) / static_cast<float> (n);
                    right[done + i] += br[static_cast<std::size_t> (i)] * gr;
                }
            }
        }
        done += n;
    }
    sampleClock += numSamples;
    // The shared post stage's resonances follow the loudest layer (the first of equals).
    {
        const auto weights = mixWeights (mixLayers, config.blend, config.mixX, config.mixY);
        int main = -1;
        for (int l = 0; l < EngineSettings::layers; ++l)
            if (occupied[static_cast<std::size_t> (l)] && (main < 0 || weights.gain[static_cast<std::size_t> (l)] > weights.gain[static_cast<std::size_t> (main)] + 1.0e-9))
                main = l;
        post.setModel (main >= 0 ? slots[static_cast<std::size_t> (main)].model : nullptr);
    }
    post.setVoicesActive (activeVoiceCount() > 0);
    post.process (left, right, numSamples);
    publishGrains();
    for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
        for (int i = 0; i < numSamples; ++i)
            output[ch][i] *= outputGain;
    for (int ch = 2; ch < numChannels; ++ch)
        std::memcpy (output[ch], output[ch % 2], sizeof (float) * static_cast<std::size_t> (numSamples));
}

} // namespace osp
