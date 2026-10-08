#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct SineSound final : juce::SynthesiserSound
    {
        bool appliesToNote (int) override { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    /** A sine with a short envelope: enough to hear that the plug-in loads and plays. */
    struct SineVoice final : juce::SynthesiserVoice
    {
        bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SineSound*> (s) != nullptr; }

        void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
        {
            phase = 0.0;
            step = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
            level = 0.25f * velocity;
            envelope.setSampleRate (getSampleRate());
            envelope.setParameters ({ 0.005f, 0.2f, 0.7f, 0.3f });
            envelope.noteOn();
        }

        void stopNote (float, bool allowTailOff) override
        {
            if (allowTailOff)
                envelope.noteOff();
            else
            {
                envelope.reset();
                clearCurrentNote();
            }
        }

        using SynthesiserVoice::renderNextBlock;   // (the double-precision one stays as is)

        void pitchWheelMoved (int) override {}
        void controllerMoved (int, int) override {}

        void renderNextBlock (juce::AudioBuffer<float>& out, int start, int count) override
        {
            if (! isVoiceActive())
                return;
            for (int i = start; i < start + count; ++i)
            {
                const auto value = static_cast<float> (std::sin (phase)) * level * envelope.getNextSample();
                phase += step;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.addSample (ch, i, value);
            }
            if (! envelope.isActive())
                clearCurrentNote();
        }

        double phase = 0.0, step = 0.0;
        float level = 0.0f;
        juce::ADSR envelope;
    };
}

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsSynth
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
#endif
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "STATE", createLayout())
{
    gainDb = parameters.getRawParameterValue ("gain");
    for (int i = 0; i < 8; ++i)
        synth.addVoice (new SineVoice());
    synth.addSound (new SineSound());
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain",
                                                             juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    return layout;
}

void PluginProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    gain.reset (sampleRate, 0.02);
    gain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
#if ! JucePlugin_IsSynth
    if (layouts.getMainInputChannelSet() != out)
        return false;
#endif
    return true;
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

#if JucePlugin_IsSynth
    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
#else
    juce::ignoreUnused (midi);
#endif

    gain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto g = gain.getNextValue();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
