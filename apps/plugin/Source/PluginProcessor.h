#pragma once

#include "InstrumentLoader.h"
#include "LoadedInstrument.h"

#include "audio/sampler/BaselineSampler.h"
#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <deque>
#include <mutex>

namespace osp::plugin
{

/**
    Phase 1 instrument: the baseline sampler (A) inside a plugin.

    Threads:
      - audio:   processBlock() — MIDI, sampler, instrument hand-over; no allocation/locks/I/O.
      - message: parameters, editor, state, publishing loaded instruments, garbage collection.
      - loader:  a single background thread that imports, hashes and analyses samples.
*/
class OspAudioProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    OspAudioProcessor();
    ~OspAudioProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Instrument loading (message thread)
    void loadFile (const juce::File& file);
    void loadExample();

    enum class LoadState { empty, loading, ready, failed };
    LoadState loadState() const noexcept { return state.load(); }
    juce::String statusMessage() const;

    /** Most recently loaded instrument (any non-audio thread). */
    std::shared_ptr<const LoadedInstrument> currentInstrument() const;

    /** Manual root (fractional MIDI) or nullopt for the analysed root. Message thread. */
    void setRootOverride (std::optional<double> midi);
    std::optional<double> rootOverride() const;
    double effectiveRootMidi() const;

    /** Blocks until pending loads have finished (tests, offline hosts). */
    bool waitForLoads (int timeoutMs);
    /** Message thread: applies finished loads immediately (normally done by a timer). */
    void pollLoads() { timerCallback(); }

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> activeVoices { 0 };
    juce::UndoManager undoManager;

    static constexpr int stateVersion = 1;

private:
    void timerCallback() override;
    void enqueueLoad (LoadRequest request);
    void applyParameters (bool force) noexcept;
    void handleMidi (const juce::MidiMessage& message) noexcept;
    void swapInstrumentIfPending() noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    BaselineSampler sampler;
    SamplerSettings samplerSettings;
    ModelExchange<LoadedInstrument> exchange;

    // Audio-thread view of instruments
    const LoadedInstrument* playing = nullptr;
    std::array<const LoadedInstrument*, 8> retired {};

    // Loader
    SampleStore store;
    juce::ThreadPool loaderPool { 1 };
    std::mutex resultsMutex;               // loader <-> message thread only, never audio
    std::deque<LoadResult> finishedLoads;
    std::atomic<int> pendingLoads { 0 };
    std::atomic<std::uint64_t> nextGeneration { 1 };
    std::atomic<LoadState> state { LoadState::empty };
    bool lastLoadFailed = false;           // message thread
    juce::String lastMessage;
    mutable std::mutex messageMutex;       // lastMessage
    mutable std::mutex modelMutex;         // message-side use of `exchange` (hosts may save state off the message thread)

    // Root override (message -> audio)
    std::atomic<double> rootShiftSemitones { 0.0 };
    std::atomic<bool> hasRootOverride { false };
    std::atomic<double> rootOverrideMidi { 60.0 };

    // Cached parameter values (audio thread)
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* velocityRangeParam = nullptr;
    std::atomic<float>* fineTuneParam = nullptr;
    std::atomic<float>* bendRangeParam = nullptr;
    float lastAttack = -1.0f, lastRelease = -1.0f, lastGain = -1000.0f, lastVelocityRange = -1.0f;
    double pitchBendSemitones = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessor)
};

} // namespace osp::plugin
