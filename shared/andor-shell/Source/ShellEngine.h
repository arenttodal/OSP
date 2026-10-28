#pragma once

#include "LoadedInstrument.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace andor::shell
{

/**
    The DSP integration boundary of the ANDOR Instrument Shell: a product's sound engine.

    The shell owns everything around it (the interface, the parameters, the sources and their
    loading, presets, MIDI from the keyboard); the engine only makes sound. Every product
    implements one; the shell's own DiagnosticEngine is a labelled stand-in, never a product.

    Threads:
      - message thread: describe(), boundParameters(), prepare(), release();
      - audio thread:   setSource(), process(): no allocation, locks, file I/O or blocking.
*/
class Engine
{
public:
    virtual ~Engine() = default;

    struct Description
    {
        juce::String name;           ///< e.g. "Diagnostic engine"
        /** A stand-in (test tones, a plain resampler): the instrument says so on screen. */
        bool diagnostic = false;
        /** Shown in the footer's status line while nothing else is (diagnostic engines must
            say what they are and what they are not). */
        juce::String statusLine;
    };
    virtual Description describe() const = 0;

    /** The parameters this engine reads, by ID (AndorParameters.h; layerParameterId (...) for
        a layer's). A trailing '*' matches a prefix ("arp.*"). Every control bound to another
        parameter is shown disabled: the interface never offers a control the sound ignores. */
    virtual juce::StringArray boundParameters() const = 0;

    /** Message thread, before playback (allocate here). `parameters` stays valid while the
        engine lives: cache getRawParameterValue() pointers now, read them in process(). */
    virtual void prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters) = 0;
    virtual void release() {}

    /** Audio thread: layer `layer` plays `source` from now on (null: silent). When this
        returns the engine must no longer read the layer's previous source. */
    virtual void setSource (int layer, const osp::plugin::LoadedInstrument* source) noexcept = 0;

    /** The wheels as the instrument shows them: the host's MIDI or the on-screen wheels,
        whichever moved last. */
    struct Performance
    {
        float pitchWheel = 0.0f;   ///< -1..1 (centre 0)
        float modWheel = 0.0f;     ///< 0..1
    };
    /** Audio thread: renders one block (the buffer arrives cleared) from the block's MIDI
        (the on-screen keyboard is already merged in). */
    virtual void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept = 0;

    /** Sounding notes (the status line). Any thread. */
    virtual int activeNotes() const noexcept { return 0; }
};

} // namespace andor::shell
