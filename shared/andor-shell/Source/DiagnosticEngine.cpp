#include "DiagnosticEngine.h"

#include "InstrumentHost.h"

#include "engine/InstrumentEngine.h"

#include <cmath>

namespace andor::shell
{

using osp::plugin::InstrumentHost;

namespace
{
    float read (const std::atomic<float>* p, float fallback) noexcept
    {
        return p != nullptr ? p->load (std::memory_order_relaxed) : fallback;
    }

    constexpr float testToneLevel = 0.12f;   ///< about -18 dBFS per note: a check, not a sound
}

DiagnosticEngine::DiagnosticEngine() = default;

Engine::Description DiagnosticEngine::describe() const
{
    return { "Diagnostic engine", true,
             juce::String::fromUTF8 ("DIAGNOSTIC ENGINE \xc2\xb7 plain resampler (baseline A) / test tone \xc2\xb7 not a production instrument") };
}

juce::StringArray DiagnosticEngine::boundParameters() const
{
    juce::StringArray ids { "attack", "decay", "sustainLevel", "release", "gain", "velocityRange", "fineTune", "bendRange", "midi.pitchWheel" };
    for (int l = 0; l < layers; ++l)
        for (const char* name : { "level", "pan", "tune", "mute", "solo" })
            ids.add (InstrumentHost::layerParameterId (l, name));
    return ids;
}

void DiagnosticEngine::prepare (double rate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters)
{
    sampleRate = rate;
    auto raw = [&parameters] (const juce::String& id) { return parameters.getRawParameterValue (id); };
    attack = raw ("attack");
    decay = raw ("decay");
    sustainLevel = raw ("sustainLevel");
    release = raw ("release");
    gain = raw ("gain");
    velocityRange = raw ("velocityRange");
    fineTune = raw ("fineTune");
    bendRange = raw ("bendRange");
    for (int l = 0; l < layers; ++l)
    {
        auto& p = layerParams[static_cast<std::size_t> (l)];
        p.level = raw (InstrumentHost::layerParameterId (l, "level"));
        p.pan = raw (InstrumentHost::layerParameterId (l, "pan"));
        p.tune = raw (InstrumentHost::layerParameterId (l, "tune"));
        p.mute = raw (InstrumentHost::layerParameterId (l, "mute"));
        p.solo = raw (InstrumentHost::layerParameterId (l, "solo"));
    }
    osp::SamplerSettings settings;   // baseline A as it is: 24 voices, sinc interpolation, no randomisation
    for (auto& s : samplers)
        s.prepare (rate, maximumBlockSize, settings);
    sources = {};
    scratch.setSize (2, std::max (1, maximumBlockSize));
    for (auto& t : tones)
        t = {};
    toneAttackStep = toneReleaseStep = 1.0f;
}

void DiagnosticEngine::setSource (int layer, const osp::plugin::LoadedInstrument* source) noexcept
{
    if (layer < 0 || layer >= layers)
        return;
    auto& sampler = samplers[static_cast<std::size_t> (layer)];
    const osp::PlaybackSource* next = nullptr;
    // Fail beautifully: a source the interpolator cannot read safely is not played.
    if (source != nullptr && source->model != nullptr && source->model->original.source != nullptr
        && source->model->original.source->padding() >= sampler.requiredSourcePadding())
        next = source->model->original.source.get();
    if (const auto* previous = sources[static_cast<std::size_t> (layer)]; previous != nullptr && previous != next)
        sampler.killVoicesUsing (previous);   // nothing reads the old source after this returns
    sampler.setSource (next);
    sources[static_cast<std::size_t> (layer)] = next;
}

void DiagnosticEngine::updateSettings (const Performance& performance) noexcept
{
    osp::AdsrSettings adsr;
    adsr.attackSeconds = 0.001 * read (attack, 2.0f);
    adsr.decaySeconds = 0.001 * read (decay, 600.0f);
    adsr.sustainLevel = 0.01 * read (sustainLevel, 100.0f);
    adsr.releaseSeconds = 0.001 * read (release, 700.0f);
    const double master = read (gain, 0.0f);
    outputGain = static_cast<float> (std::pow (10.0, master / 20.0));
    const double bend = performance.pitchWheel * read (bendRange, 2.0f);
    const double fine = 0.01 * read (fineTune, 0.0f);

    bool anySolo = false;
    for (const auto& p : layerParams)
        anySolo = anySolo || read (p.solo, 0.0f) >= 0.5f;
    for (std::size_t l = 0; l < samplers.size(); ++l)
    {
        auto& s = samplers[l];
        const auto& p = layerParams[l];
        s.setEnvelope (adsr);
        s.setVelocityRangeDb (read (velocityRange, 30.0f));
        s.setOutputGainDb (osp::SamplerSettings().outputGainDb + master);
        s.setPitchOffsetSemitones (read (p.tune, 0.0f) + fine + bend);
        const bool heard = anySolo ? read (p.solo, 0.0f) >= 0.5f : read (p.mute, 0.0f) < 0.5f;
        const double levelDb = read (p.level, 0.0f);
        const float level = ! heard || levelDb <= osp::LayerSettings::minLevelDb + 0.01 ? 0.0f : static_cast<float> (std::pow (10.0, levelDb / 20.0));
        const float pan = juce::jlimit (-1.0f, 1.0f, 0.01f * read (p.pan, 0.0f));
        gainL[l] = level * (pan <= 0.0f ? 1.0f : 1.0f - pan);
        gainR[l] = level * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    }
    // The test tone follows the AMP envelope's attack and release (linear, per sample).
    toneAttackStep = static_cast<float> (1.0 / std::max (1.0, adsr.attackSeconds * sampleRate));
    toneReleaseStep = static_cast<float> (1.0 / std::max (1.0, adsr.releaseSeconds * sampleRate / 6.9));
}

void DiagnosticEngine::handle (const juce::MidiMessage& m) noexcept
{
    const bool anySource = sources[0] != nullptr || sources[1] != nullptr || sources[2] != nullptr;
    if (m.isNoteOn())
    {
        for (std::size_t l = 0; l < samplers.size(); ++l)
            if (sources[l] != nullptr)
                samplers[l].noteOn (m.getNoteNumber(), m.getVelocity());
        if (! anySource)
        {
            // The quietest free tone (or the oldest held one) takes the key.
            auto* slot = &tones[0];
            for (auto& t : tones)
                if (t.note < 0 || t.target <= 0.0f)
                {
                    slot = &t;
                    if (t.note < 0)
                        break;
                }
            slot->note = m.getNoteNumber();
            slot->increment = 2.0 * juce::MathConstants<double>::pi * 440.0 * std::pow (2.0, (m.getNoteNumber() - 69) / 12.0) / sampleRate;
            slot->velocity = static_cast<float> (m.getVelocity()) / 127.0f;
            slot->target = 1.0f;
        }
    }
    else if (m.isNoteOff())
    {
        for (auto& s : samplers)
            s.noteOff (m.getNoteNumber());
        if (! pedalDown)
            for (auto& t : tones)
                if (t.note == m.getNoteNumber())
                    t.target = 0.0f;
    }
    else if (m.isSustainPedalOn() || m.isSustainPedalOff())
    {
        pedalDown = m.isSustainPedalOn();
        for (auto& s : samplers)
            s.setSustainPedal (pedalDown);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& s : samplers)
            s.allNotesOff();
        for (auto& t : tones)
            t.target = 0.0f;
    }
}

void DiagnosticEngine::renderSegment (juce::AudioBuffer<float>& out, int start, int count) noexcept
{
    if (count <= 0)
        return;
    const int channels = out.getNumChannels();
    for (std::size_t l = 0; l < samplers.size(); ++l)
    {
        if (sources[l] == nullptr || samplers[l].activeVoiceCount() == 0)
            continue;
        // In pieces of the prepared size (a host may send a larger block than it announced).
        for (int done = 0; done < count;)
        {
            const int piece = std::min (count - done, scratch.getNumSamples());
            float* scratchChannels[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
            samplers[l].render (scratchChannels, 2, piece);
            out.addFrom (0, start + done, scratch, 0, 0, piece, gainL[l]);
            if (channels > 1)
                out.addFrom (1, start + done, scratch, 1, 0, piece, gainR[l]);
            done += piece;
        }
    }
    for (auto& t : tones)
    {
        if (t.note < 0)
            continue;
        for (int i = 0; i < count; ++i)
        {
            t.level = t.target > t.level ? std::min (t.target, t.level + toneAttackStep) : std::max (t.target, t.level - toneReleaseStep);
            const float v = testToneLevel * outputGain * t.velocity * t.level * static_cast<float> (std::sin (t.phase));
            t.phase += t.increment;
            if (t.phase > 2.0 * juce::MathConstants<double>::pi)
                t.phase -= 2.0 * juce::MathConstants<double>::pi;
            for (int ch = 0; ch < channels; ++ch)
                out.addSample (ch, start + i, v);
        }
        if (t.target <= 0.0f && t.level <= 0.0f)
            t.note = -1;
    }
}

void DiagnosticEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept
{
    updateSettings (performance);
    const int n = buffer.getNumSamples();
    int position = 0;
    for (const auto metadata : midi)
    {
        const int at = juce::jlimit (0, n, metadata.samplePosition);
        renderSegment (buffer, position, at - position);
        position = at;
        handle (metadata.getMessage());
    }
    renderSegment (buffer, position, n - position);
    int count = 0;
    for (const auto& s : samplers)
        count = std::max (count, s.activeVoiceCount());
    for (const auto& t : tones)
        count += t.note >= 0 ? 1 : 0;
    sounding.store (count, std::memory_order_relaxed);
}

} // namespace andor::shell
