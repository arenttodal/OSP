#pragma once

#include "InstrumentLoader.h"
#include "LoadedInstrument.h"

#include "engine/InstrumentEngine.h"
#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <set>

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

    // Factory starting states (spec §101): macro settings, the sample is kept.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Presets and portable instruments (spec §52, §54). Message thread.
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    /** One file with the sources, their analysis and the settings: opens on any computer. */
    bool exportInstrument (const juce::File& file, juce::String& error);
    bool importInstrument (const juce::File& file, juce::String& error);
    static constexpr const char* instrumentExtension = ".ospinstrument";
    static constexpr const char* presetExtension = ".osppreset";

    // Preset browser (spec §90). Presets live as plain files in Documents/OSP/Presets
    // (instruments in Documents/OSP/Instruments), so they can be shared, synced or
    // organised in sub-folders with the computer's own tools.
    static juce::File presetFolder();
    static juce::File instrumentFolder();
    /** Every file with this extension under `folder` (sub-folders included), sorted by path. */
    static juce::Array<juce::File> findFiles (const juce::File& folder, const juce::String& extension);
    /** The preset last loaded or saved (empty if none): the browser marks and steps from it. */
    juce::File currentPresetFile() const { return lastPresetFile; }
    /** Loads the previous (-1) or next (+1) preset under `root` (default: the preset
        folder; a preset opened from elsewhere steps through its own folder), wrapping around. */
    bool stepPreset (int delta, const juce::File& root = presetFolder());

    /** Editor zoom (80..200 %), stored with the session. */
    float uiScale() const noexcept { return uiScaleFactor.load(); }
    void setUiScale (float scale) noexcept { uiScaleFactor = std::clamp (scale, 0.8f, 2.0f); }
    /** Whether the Advanced panel is open (closed by default: spec §13), stored with the session. */
    bool advancedOpen() const noexcept { return advancedPanelOpen.load(); }
    void setAdvancedOpen (bool open) noexcept { advancedPanelOpen = open; }

    // A/B source layers. Everything below that takes a `layer` acts on the layer being
    // edited when it is -1 (the A/B tabs choose it).
    static constexpr int numLayers = 2;
    int editLayer() const noexcept { return editLayerIndex.load(); }
    void setEditLayer (int layer) noexcept { editLayerIndex = std::clamp (layer, 0, numLayers - 1); }
    static juce::String layerName (int layer) { return layer == 1 ? "B" : "A"; }
    /** The stable parameter ID of a layer setting, e.g. layerParameterId (1, "sourceMode") -> "layerB.sourceMode". */
    static juce::String layerParameterId (int layer, const juce::String& name) { return "layer" + layerName (layer) + "." + name; }

    // Instrument loading (message thread)
    void loadFile (const juce::File& file, int layer = -1);
    /** Several files -> one multi-sample instrument (Phase 7). One file falls back to loadFile. */
    void loadFiles (const juce::Array<juce::File>& files, int layer = -1);
    /** Samples inspector correction: pin one file's role/layer and rebuild the set. */
    void reassignSample (const std::string& filename, SampleRole role, int velocityLayer, std::optional<double> rootMidi = std::nullopt);
    void loadExample (int layer = -1);
    /** Empties a layer (it then contributes silence). */
    void clearLayer (int layer = -1);

    enum class LoadState { empty, loading, ready, failed };
    LoadState loadState (int layer = -1) const noexcept { return layers[resolve (layer)].state.load(); }
    juce::String statusMessage() const;
    void showMessage (const juce::String& message)
    {
        const std::lock_guard<std::mutex> lock (messageMutex);
        lastMessage = message;
    }
    /** "Ready", or what is still being prepared in the background ("Building sustain…"), for the edited layer. */
    juce::String stageMessage() const;
    /** Granular settings names for layerParameterId: the source mode, then POS, SIZE, DENS, TUNE, SPREAD. */
    static const juce::StringArray& granularNames();

    /** Most recently loaded instrument of a layer, or nullptr when it is empty (any non-audio thread). */
    std::shared_ptr<const LoadedInstrument> currentInstrument (int layer = -1) const;

    /** Manual root (fractional MIDI) or nullopt for the analysed root. Message thread. */
    void setRootOverride (std::optional<double> midi, int layer = -1);
    /** Same, as an undoable user action (the editor's root menu). */
    void changeRootOverride (std::optional<double> midi, int layer = -1);
    std::optional<double> rootOverride (int layer = -1) const;
    double effectiveRootMidi (int layer = -1) const;

    /** Blocks until pending loads have finished (tests, offline hosts). */
    bool waitForLoads (int timeoutMs);
    /** Message thread: applies finished loads immediately (normally done by a timer). */
    void pollLoads() { timerCallback(); }

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> activeVoices { 0 };
    juce::UndoManager undoManager;

    /** 2: engine C parameters (macros, pitch character, sustain, seed). v1 sessions migrate to neutral settings.
        3: shaping system v1.0 (popup settings; CHARACTER is a filter, so older sessions open it fully).
        4: A/B layers (layer B in an InstrumentB tree; older sessions are layer A only). */
    static constexpr int stateVersion = 4;

private:
    struct Layer
    {
        ModelExchange<LoadedInstrument> exchange;
        // Audio-thread view of instruments
        const LoadedInstrument* playing = nullptr;
        std::array<const LoadedInstrument*, 8> retired {};
        std::atomic<std::uint64_t> latestLoadId { 0 };   // newest load request; older refinements are skipped
        std::atomic<LoadState> state { LoadState::empty };
        bool lastLoadFailed = false;                     // message thread
        // Root override (message -> audio)
        std::atomic<double> rootShiftSemitones { 0.0 };
        std::atomic<bool> hasRootOverride { false };
        std::atomic<double> rootOverrideMidi { 60.0 };
        // Undo of sample loads: the latest instrument of every recent load. Message thread.
        std::map<std::uint64_t, std::shared_ptr<const LoadedInstrument>> latestByLoad;
        std::uint64_t lastPublishedLoad = 0;
        float autoPosition = -1.0f;                      // POS set from the analysis (granular), until the user moves it
        std::uint64_t autoPositionLoad = 0;
    };
    struct Finished
    {
        int layer = 0;
        LoadResult result;
    };
    std::size_t resolve (int layer) const noexcept { return static_cast<std::size_t> (layer < 0 ? editLayerIndex.load() : std::clamp (layer, 0, numLayers - 1)); }

    void timerCallback() override;
    void enqueueLoad (LoadRequest request, int layer);
    void enqueueRefine (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const AudioData> audio, int layer);
    void enqueueSetLoad (SetLoadRequest request, int layer);
    std::unique_ptr<juce::XmlElement> createStateXml();
    void applyStateXml (const juce::XmlElement& xml);
    juce::ValueTree instrumentTree (int layer);
    void recallInstrument (const juce::ValueTree& tree, int layer);
    void republish (std::shared_ptr<const LoadedInstrument> instrument, int layer);
    void suggestGranularPosition (int layer, const LoadedInstrument& instrument);
    friend class InstrumentChangeAction;
    friend class RootChangeAction;
    void pushResult (LoadResult result, int layer);
    void applyParameters (bool force) noexcept;
public:
    /** The popup parameter IDs (stable: never rename), in shapingParams order. */
    static const juce::StringArray& shapingIds();
private:
    void handleMidi (const juce::MidiMessage& message) noexcept;
    void swapInstrumentIfPending() noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    InstrumentEngine engine;
    EngineSettings engineSettings;
    std::array<Layer, numLayers> layers;
    std::atomic<int> editLayerIndex { 0 };

    // Loader
    SampleStore store;
    juce::ThreadPool loaderPool { 1 };
    std::mutex resultsMutex;               // loader <-> message thread only, never audio
    std::deque<Finished> finishedLoads;
    std::atomic<int> pendingLoads { 0 };
    std::atomic<std::uint64_t> nextGeneration { 1 };
    juce::String lastMessage;
    mutable std::mutex messageMutex;       // lastMessage
    mutable std::mutex modelMutex;         // message-side use of the layers' exchanges (hosts may save state off the message thread)

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
    std::atomic<float>* mpeParam = nullptr;
    // A/B layers: blend, then per layer source mode and POS, SIZE, DENS, TUNE, SPREAD.
    std::atomic<float>* blendParam = nullptr;
    struct LayerParams
    {
        std::atomic<float>* mode = nullptr;
        std::array<std::atomic<float>*, 5> granular {};
        std::array<float, 6> last { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
    };
    std::array<LayerParams, numLayers> layerParams;
    // Shaping system v1.0 (the macro popups), in the order of shapingIds().
    static constexpr int numShapingParams = 20;
    std::array<std::atomic<float>*, numShapingParams> shapingParams {};
    std::array<float, numShapingParams> lastShaping {};
    Shaping shapingFromParameters() const noexcept;
    // MIDI-controlled macro values (CC 20-25), used until the host parameter moves again.
    std::array<float, 6> ccMacro { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 6> lastMacroParam { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    float modWheel = 0.0f;
    float lastAttack = -1.0f, lastRelease = -1.0f, lastGain = -1000.0f, lastVelocityRange = -1.0f;
    double pitchBendSemitones = 0.0;
    bool hostWasPlaying = false;     // audio thread: transport start resets performance memory
    int currentProgram = 0;
    juce::File lastPresetFile;
    std::atomic<float> uiScaleFactor { 1.0f };
    std::atomic<bool> advancedPanelOpen { false };
    std::set<std::uint64_t> userLoads;     // load ids started by the user (undoable), not by recall

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessor)
};

} // namespace osp::plugin
