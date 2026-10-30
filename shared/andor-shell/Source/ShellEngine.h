#pragma once

#include "LoadedInstrument.h"

#include "engine/InstrumentEngine.h"
#include "engine/Modulation.h"
#include "engine/RhythmicShaper.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

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
    /** The output's delay in samples after prepare() (oversampling filters): the shell
        reports it to the host. */
    virtual int latencySamples() const { return 0; }

    /** An engine that makes its own sound (a synthesizer's oscillators) and plays no
        recordings returns the name its card shows ("OSCILLATORS"): the instrument then shows
        one card (layer A) for it with sourcePicture(), never invites a file and loads none.
        Empty (the default): the engine plays the recordings it is given. */
    virtual juce::String generatorName() const { return {}; }

    /** A generator engine's card picture: one period of what the layer sounds like with
        `parameters` as they are, -1..1, in `cycle` (any length). Message thread (before
        prepare() too); false: none. */
    virtual bool sourcePicture (int layer, const juce::AudioProcessorValueTreeState& parameters, std::vector<float>& cycle) const
    {
        juce::ignoreUnused (layer, parameters, cycle);
        return false;
    }

    /** Message thread: the shell has just published `source` for `layer` (null: cleared);
        it reaches the audio thread through setSource() soon after. An engine that derives
        data of its own from a recording (an analysis on its own worker) starts here and may
        keep the pointer as long as it needs. */
    virtual void sourcePublished (int layer, std::shared_ptr<const osp::plugin::LoadedInstrument> source)
    {
        juce::ignoreUnused (layer, source);
    }
    /** Audio thread: layer `layer` plays `source` from now on (null: silent). When this
        returns the engine must no longer read the layer's previous source. */
    virtual void setSource (int layer, const osp::plugin::LoadedInstrument* source) noexcept = 0;

    /** The wheels as the instrument shows them: the host's MIDI or the on-screen wheels,
        whichever moved last. */
    struct Performance
    {
        float pitchWheel = 0.0f;   ///< -1..1 (centre 0)
        float modWheel = 0.0f;     ///< 0..1
        /** The host's transport at the block's first sample (hasPosition: the host gave a
            musical position; otherwise 120 BPM, stopped). */
        bool hasPosition = false;
        bool playing = false;
        double bpm = 120.0;
        double ppq = 0.0;          ///< quarter notes since the song start
        int numerator = 4, denominator = 4;
    };
    /** Audio thread: renders one block (the buffer arrives cleared) from the block's MIDI
        (the on-screen keyboard is already merged in). The wheels in `performance` are the
        block's last positions; an engine that wants them sample-accurate reads the MIDI
        (the on-screen wheels arrive only here, at the block's start). */
    virtual void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept = 0;

    /** Audio thread, before process(): MOVEMENT SHAPER's CUSTOM pattern changed (the
        musician's 16 steps; given once after prepare() and on every change). */
    virtual void setShaperPattern (const osp::ShaperPattern& pattern) noexcept { juce::ignoreUnused (pattern); }
    /** Audio thread, before process(): an editable modulation curve changed
        (InstrumentHost::ModCurves' index: 0, 1 LFO 1/2 CUSTOM, ...; given once after
        prepare() and on every change). */
    virtual void setModulationCurve (int index, const osp::mod::Curve& curve) noexcept { juce::ignoreUnused (index, curve); }
    /** Audio thread, before process(): the instrument's modulation as the musician set it
        (ANDOR OSP's mod::Settings: LFO 1 and 2, ENV 1 and 2, the 16 routes with their depth
        links, the curves compiled, APPLY TO AMP, the seed) and the MIDI controllers CC 1-4
        follow. Given after prepare() and whenever any of it changes, to an engine that binds
        the routes ("mod.*"). A route may name ANDOR OSP's destinations or the product's own
        (ProductInfo::modulationDestinations: mod::productDest (k) is the k-th). */
    virtual void setModulation (const osp::mod::Settings& settings, const std::array<int, osp::mod::ccSlots>& controllers) noexcept
    {
        juce::ignoreUnused (settings, controllers);
    }
    /** The arpeggiator's destinations now (ARP RATE in octaves of speed, OCTAVES in octaves,
        GATE, SWING and VELOCITY in their own fractions), read by the shell before it runs the
        arpeggiator. False: no route reaches it. Audio thread. */
    virtual bool arpModulation (osp::mod::Dest dest, double& offset) const noexcept
    {
        juce::ignoreUnused (dest, offset);
        return false;
    }
    /** Audio thread: while the arpeggiator plays, the keys the musician pressed (VEL and NOTE
        on shared stages follow them, not the generated notes); `arpeggiating` false: the
        notes the engine is given are the musician's. Called before process(). */
    virtual void setPlayedKeys (bool arpeggiating, const juce::MidiBuffer& played) noexcept
    {
        juce::ignoreUnused (arpeggiating, played);
    }
    /** The modulation sources' live values (the panel's meters and moving dots). Any thread. */
    virtual osp::InstrumentEngine::ModView modulationView() const noexcept { return {}; }

    /** MOVEMENT SHAPER's playhead, 0..1 of its bar, for its picture (0: none). Any thread. */
    virtual float shaperPhase() const noexcept { return 0.0f; }

    /** A REIMAGINED mode (osp::ReimaginedMode's index) the engine cannot play on `layer`,
        in the words its popover shows (InstrumentHost::reimaginedModeUnavailable). Empty:
        it plays (or the engine does not read REIMAGINED at all). Message thread. */
    virtual juce::String reimaginedModeUnavailable (int layer, int mode) const
    {
        juce::ignoreUnused (layer, mode);
        return {};
    }

    /** The grains a generator layer plays now, for its card (the POS line, the SPREAD pane
        and a mote per grain, as over a recording): the engine fills it on the audio thread
        (positions 0..1 on the POS scale, levels 0..1, lanes 0..1), the card reads it on the
        message thread. Null (the default): no grains shown. Any thread; the object lives
        as long as the engine. */
    virtual const osp::InstrumentEngine::GrainSnapshot* grainSnapshot (int layer) const noexcept
    {
        juce::ignoreUnused (layer);
        return nullptr;
    }

    /** Sounding notes (the status line). Any thread. */
    virtual int activeNotes() const noexcept { return 0; }

    /** The engine plays KEY DNA on its Granular layers (ANDOR OSP's per-key profile): the
        Granular strip then shows its KEY DNA key. Off by default (no key). */
    virtual bool hasKeyDna() const noexcept { return false; }
};

} // namespace andor::shell
