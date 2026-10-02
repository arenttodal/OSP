#include "engine/InstrumentEngine.h"

#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace osp
{

InstrumentEngine::InstrumentEngine() = default;

void InstrumentEngine::prepare (double outputSampleRate, int maximumBlockSize, const EngineSettings& settings)
{
    config = settings;
    config.polyphony = std::clamp (config.polyphony, 1, EngineSettings::maxPolyphony);
    sampleRate = outputSampleRate;
    if (! interpolator || interpolator->zeroCrossings() != config.interpolationZeroCrossings)
        interpolator = std::make_unique<SincInterpolator> (config.interpolationZeroCrossings);
    for (auto& voice : voices)
        voice.prepare (sampleRate, config.adsr, interpolator.get());
    outputGain = static_cast<float> (dbToGain (config.outputGainDb));
    pedalDown = false;
    noteCounter = 0;
    pitchRatio = 1.0;
    post.prepare (outputSampleRate, maximumBlockSize);
    post.setMacros (config.macros);
    resetPerformance();
}

void InstrumentEngine::resetPerformance() noexcept
{
    noteCounter = 0;
    sampleClock = 0;
    performance.reset (config.seed);
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

int InstrumentEngine::countSoundingVoices() const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive() && ! voice.isFading())
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

InstrumentVoice* InstrumentEngine::chooseVictim() noexcept
{
    // Released voices first (quietest), then held voices (quietest, then oldest).
    InstrumentVoice* best = nullptr;
    auto score = [] (const InstrumentVoice& v) { return (v.isReleased() ? 0.0 : 10.0) + static_cast<double> (v.currentLevel()); };
    for (auto& voice : voices)
    {
        if (! voice.isActive() || voice.isFading())
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
                                      double dynamicsMacro, DynamicsMode mode) noexcept
{
    if (mode == DynamicsMode::gainOnly)
        return;
    // Intensity relative to the recording: velocity 100 plays it as recorded. Softer is
    // a wide, reliable range (we can always take bite away); harder is extrapolation and
    // stays narrow.
    const double v = std::clamp (velocity, 1, 127);
    const double intensity = std::clamp ((v - 100.0) / 80.0, -1.25, 0.35);
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

NoteShape InstrumentEngine::shapeFor (int note, int velocity, std::uint64_t eventIndex) noexcept
{
    NoteShape shape;
    shape.seed = Prng::deriveSeed (config.seed, eventIndex, static_cast<std::uint64_t> (note));
    const double velocityDb = -config.velocityRangeDb * (1.0 - std::clamp (velocity, 1, 127) / 127.0);
    shape.gain = static_cast<float> (dbToGain (velocityDb));

    if (currentModel != nullptr)
    {
        applyDynamics (shape, velocity, currentModel->dynamics, currentModel->character, config.macros.dynamics, config.dynamicsMode);
        performance.perform (shape, note, velocity, static_cast<double> (sampleClock) / sampleRate, eventIndex,
                             currentModel->performance, currentModel->character, config.macros.life);
    }

    const double r = std::clamp (config.macros.reimagined, 0.0, 1.0);
    const double motion = std::clamp (config.macros.motion, 0.0, 1.0);
    if (config.continuation == ContinuationStrategy::multiLoopMovement && currentModel != nullptr)
    {
        // MOTION: evolution after the onset (spec §11) - mostly for sustained sources.
        const auto& c = currentModel->original.continuation;
        const double sustained = 1.0 - 0.8 * currentModel->character.transientTonal;
        const double m = motion / 0.35 * (1.0 + 1.5 * r) * sustained; // 1 at the default MOTION
        shape.driftLevelDb = static_cast<float> (m * std::clamp (0.3 + 0.4 * c.levelFluctuationDb, 0.3, 1.0));
        shape.driftCents = static_cast<float> (m * std::clamp (1.5 + 0.3 * c.pitchFluctuationCents, 1.5, 5.0) * (1.0 + 2.0 * std::max (0.0, r - 0.6)));
        shape.driftBrightnessDb = static_cast<float> (m * 0.8);
        shape.driftPan = static_cast<float> (0.15 * motion * sustained);
        shape.driftRateHz = static_cast<float> (0.1 + 0.15 * motion);
    }
    // Original <-> Reimagined (spec §12): shorter, more varied continuation; harmonic
    // saturation towards the far end. (Resonance and width live in PostProcessor.)
    shape.segmentScale = static_cast<float> ((1.0 - 0.7 * r * r) * (1.3 - 0.6 * motion));
    shape.saturation = static_cast<float> (0.7 * std::clamp ((r - 0.5) / 0.5, 0.0, 1.0));
    return shape;
}

void InstrumentEngine::noteOn (int note, int velocity) noexcept
{
    if (velocity <= 0)
    {
        noteOff (note);
        return;
    }
    const InstrumentModel* model = currentModel;
    if (model == nullptr || ! model->isValid())
        return;

    const auto stealFade = std::max (1, static_cast<int> (config.stealFadeSeconds * sampleRate));
    if (countSoundingVoices() >= config.polyphony)
        if (auto* victim = chooseVictim())
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

    const std::uint64_t eventIndex = noteCounter++;
    InstrumentVoiceStart params;
    params.model = model;
    params.shape = shapeFor (note, velocity, eventIndex);
    params.layer = &model->layerFor (static_cast<double> (note), config.pitchCharacter);
    params.note = note;
    params.velocity = velocity;
    const auto& src = *params.layer->source;
    params.increment = semitonesToRatio (static_cast<double> (note) - src.rootMidi()) * (src.sampleRate() / sampleRate);
    params.startOrder = eventIndex;
    params.strategy = config.continuation;
    params.releaseGraft = config.releaseGraft;
    slot->start (params);
}

void InstrumentEngine::noteOff (int note) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && ! voice.isReleased() && voice.note() == note)
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
    for (auto& voice : voices)
        voice.render (left, right, numSamples, pitchRatio);
    sampleClock += numSamples;
    post.setModel (currentModel);
    post.process (left, right, numSamples);
    for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
        for (int i = 0; i < numSamples; ++i)
            output[ch][i] *= outputGain;
    for (int ch = 2; ch < numChannels; ++ch)
        std::memcpy (output[ch], output[ch % 2], sizeof (float) * static_cast<std::size_t> (numSamples));
}

} // namespace osp
