#pragma once

#include "InstrumentLoader.h"
#include "LoadedInstrument.h"

#include "engine/InstrumentEngine.h"
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
    The OSP instrument: InstrumentEngine ("engine C") playing a staged, immutable
    InstrumentModel, with the five macros, Original <-> Reimagined, Pitch Character and
    Sustain as host parameters.

    Threads:
      - audio:   processBlock() — MIDI, engine, instrument hand-over; no allocation/locks/I/O.
      - message: parameters, editor, state, publishing loaded instruments, garbage collection.
      - loader:  a single background thread that imports, hashes and analyses samples, then
                 builds the model stages (playable -> sustain -> register anchors).
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
    /** Several files -> one multi-sample instrument (Phase 7). One file falls back to loadFile. */
    void loadFiles (const juce::Array<juce::File>& files);
    /** Samples inspector correction: pin one file's role/layer and rebuild the set. */
    void reassignSample (const std::string& filename, SampleRole role, int layer, std::optional<double> rootMidi = std::nullopt);
    void loadExample();

    enum class LoadState { empty, loading, ready, failed };
    LoadState loadState() const noexcept { return state.load(); }
    juce::String statusMessage() const;
    /** "Ready", or what is still being prepared in the background ("Building sustain…"). */
    juce::String stageMessage() const;

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

    /** 2: engine C parameters (macros, pitch character, sustain, seed). v1 sessions migrate to neutral settings. */
    static constexpr int stateVersion = 2;

private:
    void timerCallback() override;
    void enqueueLoad (LoadRequest request);
    void enqueueRefine (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const AudioData> audio);
    void enqueueSetLoad (SetLoadRequest request);
    void pushResult (LoadResult result);
    void applyParameters (bool force) noexcept;
    void handleMidi (const juce::MidiMessage& message) noexcept;
    void swapInstrumentIfPending() noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    InstrumentEngine engine;
    EngineSettings engineSettings;
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
    std::atomic<std::uint64_t> latestLoadId { 0 };   // newest load request; older refinements are skipped
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
    std::atomic<float>* lifeParam = nullptr;
    std::atomic<float>* dynamicsParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* motionParam = nullptr;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* reimaginedParam = nullptr;
    std::atomic<float>* pitchCharacterParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* seedParam = nullptr;
    float lastAttack = -1.0f, lastRelease = -1.0f, lastGain = -1000.0f, lastVelocityRange = -1.0f;
    double pitchBendSemitones = 0.0;
    bool hostWasPlaying = false;     // audio thread: transport start resets performance memory

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessor)
};

} // namespace osp::plugin
