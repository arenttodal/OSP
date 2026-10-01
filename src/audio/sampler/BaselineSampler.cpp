#include "audio/sampler/BaselineSampler.h"

#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace osp
{

BaselineSampler::BaselineSampler() = default;

void BaselineSampler::prepare (double outputSampleRate, int /*maximumBlockSize*/, const SamplerSettings& settings)
{
    config = settings;
    config.polyphony = std::clamp (config.polyphony, 1, SamplerSettings::maxPolyphony);
    sampleRate = outputSampleRate;

    if (! interpolator || interpolator->zeroCrossings() != config.interpolationZeroCrossings)
        interpolator = std::make_unique<SincInterpolator> (config.interpolationZeroCrossings);

    for (auto& voice : voices)
        voice.prepare (sampleRate, config.adsr, interpolator.get());

    outputGain = static_cast<float> (dbToGain (config.outputGainDb));
    pedalDown = false;
    noteCounter = 0;
    pitchRatio = 1.0;
}

void BaselineSampler::setEnvelope (const AdsrSettings& adsr) noexcept
{
    config.adsr = adsr;
    for (auto& voice : voices)
        voice.setEnvelopeSettings (adsr);
}

void BaselineSampler::setOutputGainDb (double db) noexcept
{
    config.outputGainDb = db;
    outputGain = static_cast<float> (dbToGain (db));
}

void BaselineSampler::setPitchOffsetSemitones (double semitones) noexcept
{
    pitchRatio = semitonesToRatio (semitones);
}

bool BaselineSampler::isSourceInUse (const PlaybackSource* source) const noexcept
{
    for (const auto& voice : voices)
        if (voice.isActive() && voice.source() == source)
            return true;
    return false;
}

void BaselineSampler::killVoicesUsing (const PlaybackSource* source) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.source() == source)
            voice.kill();
}

double BaselineSampler::incrementFor (double note, double rootMidi, double sourceRate, double outputRate) noexcept
{
    return semitonesToRatio (note - rootMidi) * (sourceRate / outputRate);
}

int BaselineSampler::countSoundingVoices() const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive() && ! voice.isFading())
            ++count;
    return count;
}

int BaselineSampler::activeVoiceCount() const noexcept
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;
    return count;
}

bool BaselineSampler::isNoteActive (int note) const noexcept
{
    for (const auto& voice : voices)
        if (voice.isActive() && ! voice.isFading() && voice.note() == note)
            return true;
    return false;
}

SamplerVoice* BaselineSampler::findFreeSlot() noexcept
{
    for (auto& voice : voices)
        if (! voice.isActive())
            return &voice;
    return nullptr;
}

SamplerVoice* BaselineSampler::chooseVictim() noexcept
{
    // Preference: released voices (quietest first), then held voices (quietest, then oldest).
    SamplerVoice* best = nullptr;
    auto score = [] (const SamplerVoice& v) {
        // Lower is a better victim. Released voices sort before held ones.
        return (v.isReleased() ? 0.0 : 10.0) + static_cast<double> (v.currentLevel());
    };

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

void BaselineSampler::noteOn (int note, int velocity) noexcept
{
    if (velocity <= 0)
    {
        noteOff (note);
        return;
    }

    const PlaybackSource* source = currentSource;
    if (source == nullptr || ! source->isValid())
        return;

    const auto stealFade = std::max (1, static_cast<int> (config.stealFadeSeconds * sampleRate));

    // Enforce polyphony: fade out the best victim if all sounding slots are taken.
    if (countSoundingVoices() >= config.polyphony)
        if (auto* victim = chooseVictim())
            victim->beginFastFade (stealFade);

    SamplerVoice* slot = findFreeSlot();
    if (slot == nullptr)
    {
        // Every slot (including tail slots) is busy: hard-kill the oldest fading voice.
        for (auto& voice : voices)
            if (voice.isFading() && (slot == nullptr || voice.startOrder() < slot->startOrder()))
                slot = &voice;
        if (slot == nullptr)
            return;
        slot->kill();
    }

    const std::uint64_t eventIndex = noteCounter++;
    const double velocityDb = -config.velocityRangeDb * (1.0 - std::clamp (velocity, 1, 127) / 127.0);

    double gainDb = velocityDb;
    double detuneCents = 0.0;
    double startOffsetSeconds = 0.0;

    if (config.randomization.enabled)
    {
        Prng rng (Prng::deriveSeed (config.seed, eventIndex, static_cast<std::uint64_t> (note)));
        gainDb += config.randomization.gainDb * rng.bipolar();
        detuneCents = config.randomization.detuneCents * rng.bipolar();
        startOffsetSeconds = 0.001 * config.randomization.startOffsetMs * rng.nextDouble();
    }

    VoiceStartParams params;
    params.source = source;
    params.note = note;
    params.velocity = velocity;
    params.increment = incrementFor (note + detuneCents / 100.0, source->rootMidi(), source->sampleRate(), sampleRate);
    params.gain = static_cast<float> (dbToGain (gainDb));
    params.startPosition = startOffsetSeconds * source->sampleRate();
    params.startOrder = eventIndex;
    slot->start (params);
}

void BaselineSampler::noteOff (int note) noexcept
{
    for (auto& voice : voices)
    {
        if (voice.isActive() && ! voice.isReleased() && voice.note() == note)
        {
            if (pedalDown)
                voice.setHeldByPedal (true);
            else
                voice.release();
        }
    }
}

void BaselineSampler::setSustainPedal (bool down) noexcept
{
    pedalDown = down;
    if (! down)
        for (auto& voice : voices)
            if (voice.isActive() && voice.isHeldByPedal())
                voice.release();
}

void BaselineSampler::allNotesOff() noexcept
{
    pedalDown = false;
    for (auto& voice : voices)
        voice.release();
}

void BaselineSampler::reset() noexcept
{
    pedalDown = false;
    for (auto& voice : voices)
        voice.kill();
}

void BaselineSampler::render (float* const* output, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;

    for (int ch = 0; ch < numChannels; ++ch)
        std::memset (output[ch], 0, sizeof (float) * static_cast<std::size_t> (numSamples));

    float* left = output[0];
    float* right = numChannels > 1 ? output[1] : output[0];

    for (auto& voice : voices)
        voice.render (left, right, numSamples, pitchRatio);

    for (int ch = 0; ch < std::min (numChannels, 2); ++ch)
        for (int i = 0; i < numSamples; ++i)
            output[ch][i] *= outputGain;

    // Extra output channels (if any) mirror the stereo pair rather than staying silent.
    for (int ch = 2; ch < numChannels; ++ch)
        std::memcpy (output[ch], output[ch % 2], sizeof (float) * static_cast<std::size_t> (numSamples));
}

} // namespace osp
