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
#include <optional>
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
class OspAudioProcessor final : public juce::AudioProcessor, private juce::Timer, private juce::AudioProcessorParameter::Listener
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
    static constexpr int numLayers = EngineSettings::layers;   ///< A, B, C (the instrument uses 1-3 of them)
    int editLayer() const noexcept { return editLayerIndex.load(); }
    void setEditLayer (int layer) noexcept { editLayerIndex = std::clamp (layer, 0, numLayers - 1); }
    static juce::String layerName (int layer) { return layer == 2 ? "C" : (layer == 1 ? "B" : "A"); }
    /** The stable parameter ID of a layer setting, e.g. layerParameterId (1, "sourceMode") -> "layerB.sourceMode". */
    static juce::String layerParameterId (int layer, const juce::String& name) { return "layer" + layerName (layer) + "." + name; }
    /** A layer's Original <-> Reimagined: A's is the instrument's `reimagined` (the ID older
        sessions and automation know); B and C have their own. */
    static juce::String reimaginedParameterId (int layer) { return layer <= 0 ? juce::String ("reimagined") : layerParameterId (layer, "reimagined"); }
    /** REIMAGINED modes (version hint 11): a layer's mode and every mode's own settings,
        "layerA.reimagined.mode", "layerA.reimagined.tapeFrame.age", ... in this order. */
    static const juce::StringArray& reimaginedModeNames();
    static juce::String reimaginedModeParameterId (int layer, const juce::String& name) { return layerParameterId (layer, "reimagined." + name); }

    // Instrument loading (message thread)
    void loadFile (const juce::File& file, int layer = -1);
    /** Several files -> one multi-sample instrument (Phase 7). One file falls back to loadFile. */
    void loadFiles (const juce::Array<juce::File>& files, int layer = -1);
    /** Samples inspector correction: pin one file's role/layer and rebuild the set. */
    void reassignSample (const std::string& filename, SampleRole role, int velocityLayer, std::optional<double> rootMidi = std::nullopt);
    void loadExample (int layer = -1);
    /** Empties a layer (it then contributes silence). */
    void clearLayer (int layer = -1);

    // Adaptive layers (message thread). A layer is occupied while it holds or loads a sound;
    // the instrument shows one card per occupied layer, and they are kept contiguous
    // (A, A+B, A+B+C) by removeLayer.
    bool isLayerOccupied (int layer) const;
    int occupiedLayerCount() const;
    /** The first slot without a sound, or -1 when all three hold one. */
    int firstFreeLayer() const;
    /** Each file becomes a new layer (from `firstLayer` if it is free, then the free slots),
        with neutral layer controls, and is made audible in the mix. Files beyond the third
        layer are reported, not loaded. Returns how many were loaded. */
    int addLayers (const juce::Array<juce::File>& files, int firstLayer = -1);
    /** Removes a layer's sound; the layers above move down (complete state: sound, root,
        mode, granular and layer controls). Not while sounds are loading. Restorable once. */
    bool removeLayer (int layer);
    /** A new sound for an occupied layer: its root goes back to automatic, its controls stay
        (several files make one multi-sample sound). Voices already playing finish on the old one. */
    void replaceLayer (const juce::Array<juce::File>& files, int layer);
    /** Several files as ONE new multi-sample layer (a dropped folder). Returns the slot, -1 when full. */
    int addLayerSet (const juce::Array<juce::File>& files);
    bool canRestoreRemovedLayer() const;
    bool restoreRemovedLayer();
    /** How many cards the instrument shows: every layer up to the highest one with a sound,
        or the slots kept by clearAllSamples / a starting state, whichever is more. */
    int slotCount() const;
    /** True for a slot that is shown but holds no sound (waiting for a drop). */
    bool isKeptEmptySlot (int layer) const { return layer >= 0 && layer < slotCount() && ! isLayerOccupied (layer); }
    /** Removes every sound but keeps the A/B/C slots and every setting (layer controls, blends,
        macros, popups): new sounds dropped into the slots play with the same settings. */
    void clearAllSamples();
    /** The slots kept without sounds (stored with the session; 0 = none). */
    int keptSlots() const noexcept { return keptSlotCount.load(); }
    void setKeptSlots (int slots) noexcept { keptSlotCount = std::clamp (slots, 0, numLayers); }
    /** Neutral layer controls and granular settings, automatic root. */
    void resetLayerControls (int layer);
    void setParameterValue (const juce::String& id, float value);
    float parameterValue (const juce::String& id) const;

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
    /** Layer control names for layerParameterId: start, tune, pan, level, link, reverse, loop, follow. */
    static const juce::StringArray& layerControlNames();
    /** MUTE and SOLO ("layerA.mute", "layerA.solo"): kept apart from the controls (LINK never shares them). */
    static const juce::StringArray& layerStateNames();
    /** Heard in the mix: SOLO wins (only soloed layers are heard), else MUTE decides. */
    bool isLayerHeard (int layer) const;
    /** SOLO this layer alone (alt-click on S). */
    void soloOnly (int layer);

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

    /** SHAPER's pattern position (0..1) for the display, -1 when it is not running (any thread). */
    float shaperPhase() const noexcept { return engine.shaperPhase(); }
    /** SHAPER's large pattern editor was last open (the popover reopens that way). */
    bool shaperEditorLarge() const noexcept { return shaperLarge.load(); }
    void setShaperEditorLarge (bool large) noexcept { shaperLarge = large; }
    /** The host's tempo (120 until a host tells), for ECHO's synced times in the display. */
    float hostTempo() const noexcept { return hostBpm.load (std::memory_order_relaxed); }
    /** The grains a layer is playing now (display; lock-free, any thread). */
    const InstrumentEngine::GrainSnapshot& grainSnapshot (int layer) const noexcept { return engine.grainSnapshot (layer); }

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> activeVoices { 0 };     ///< played notes sounding (a note on three layers counts once)

    /** The last played velocities (newest at `velocityCount - 1`, a ring of 16) and how many
        notes have been played: written by the audio thread, read by the popups (lock-free). */
    static constexpr int velocityHistory = 16;
    std::array<std::atomic<int>, velocityHistory> recentVelocity {};
    std::atomic<int> velocityCount { 0 };

    /** The on-screen wheels (message thread -> audio thread): pitch -1..1 (springs back), mod 0..1. */
    void setScreenPitchWheel (float value) noexcept { screenPitch = std::clamp (value, -1.0f, 1.0f); }
    void setScreenModWheel (float value) noexcept { screenMod = std::clamp (value, 0.0f, 1.0f); }
    float screenModWheel() const noexcept { return screenMod.load(); }

    /**
        LINK: a user change of one layer's START, TUNE, PAN or LEVEL (`control`) by `delta`
        moves every other linked layer's same control by the same amount (clamped), so
        their relationship is kept. Nothing happens unless `layer` itself is linked.
        Message thread (a UI gesture; automation is never propagated).
    */
    void applyLinkedDelta (int layer, const juce::String& control, float delta);
    bool isLayerLinked (int layer) const { return parameterValue (layerParameterId (layer, "link")) >= 0.5f; }

    // Header preset navigation (spec: previous / name / favourite / next): the factory
    // starting states first, then the user's preset files.
    juce::String presetDisplayName() const;
    void stepPresetList (int delta);
    struct PresetEntry
    {
        juce::String name;
        int program = -1;         ///< a starting state, or -1
        juce::File file;          ///< a preset file, or a user starting state (startingState)
        bool startingState = false;
    };
    /** INIT: an empty patch - no sounds, no kept slots, every setting at its default. */
    void initPatch();
    /** Every setting to its default; the sounds stay (two or three meet in the middle of the mix). */
    void resetSettings();
    /** A user starting state: every setting and the slot count, no audio. Opening one applies
        the settings to the sounds already loaded (and keeps empty slots for the rest). */
    bool saveStartingState (const juce::File& file);
    bool loadStartingState (const juce::File& file);
    static juce::File startingStateFolder();
    static constexpr const char* startingStateExtension = ".ospstate";
    std::vector<PresetEntry> presetList() const;
    void openPresetEntry (const PresetEntry& entry);
    bool isFavourite() const;
    void toggleFavourite();
private:
    const juce::StringArray& favourites() const;
    mutable juce::StringArray favouriteNames;
    mutable bool favouritesLoaded = false;
public:
    juce::UndoManager undoManager;

    /** 2: engine C parameters (macros, pitch character, sustain, seed). v1 sessions migrate to neutral settings.
        3: shaping system v1.0 (popup settings; CHARACTER is a filter, so older sessions open it fully).
        4: A/B layers (layer B in an InstrumentB tree; older sessions are layer A only).
        5: MOVEMENT v2 (every mode keeps its own settings; the shared knobs migrate to the selected mode).
        6: adaptive 1-3 layers (InstrumentC tree, layer controls, three-layer mix, ADSR decay/sustain;
           the global Sustain becomes every layer's LOOP).
        7: per-layer Reimagined amounts (B and C start from A's).
        8: Reimagined routing (`reimaginedRouting`: "perLayer" or "legacyGlobal"); a state
           without it was made before per-layer routing and keeps the legacy shared stage. */
    /** 9: REIMAGINED modes (older sessions open as KALEIDOSCOPE).
        10: SPACE v2 (HALL in CHAMBER's place; older sessions open the new EQ fully, so the
            room is not filtered where it never was), ECHO (off in older sessions) and the
            SHAPER's CUSTOM pattern (`shaperCustom`). */
    static constexpr int stateVersion = 10;

    /** SHAPER CUSTOM: the musician's 16 steps (message thread). Kept in the session and
        presets; setting it is undoable (one gesture = one transaction, see UndoManager). */
    ShaperPattern shaperCustomPattern() const;
    void setShaperCustomPattern (const ShaperPattern& pattern, bool undoable = true);
    static juce::String encodeShaperPattern (const ShaperPattern& pattern);
    static std::optional<ShaperPattern> decodeShaperPattern (const juce::String& text);
    /** Saved SHAPER patterns (Documents/OSP/Shaper Patterns, *.ospshaper JSON, schemaVersion 1). */
    static constexpr const char* shaperPatternExtension = ".ospshaper";
    static juce::File shaperPatternFolder();
    static juce::Array<juce::File> savedShaperPatterns();
    static bool writeShaperPatternFile (const juce::File& file, const ShaperPattern& pattern);
    static std::optional<ShaperPattern> readShaperPatternFile (const juce::File& file);

    /**
        Reimagined routing (see osp::ReimaginedRouting). New patches (a fresh instance,
        INIT, Reset settings) are per-layer; sessions, presets and starting states saved
        before it, and the factory starting states, keep the legacy shared stage - they
        sound exactly as they did, and saving them again keeps it.
    */
    bool isReimaginedPerLayer() const noexcept { return perLayerReimagined.load(); }
    /**
        The one place a patch leaves the legacy routing: the musician edits a layer's
        REIMAGINED (a gesture on `reimagined`, `layerB.reimagined` or `layerC.reimagined`).
        Every layer keeps the amount it has (older sessions already gave every layer the
        one amount), so the knobs do not move; from then on each layer's amount shapes only
        that layer, and host automation of `reimagined` is A's REIMAGINED. Never called for
        opening, showing, inspecting or automating a patch. No-op when already per-layer.
    */
    void convertLegacyReimaginedToPerLayer();

private:
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
    std::atomic<bool> perLayerReimagined { true };
    /** Every layer's REIMAGINED amount, mode and mode setting: a gesture on any converts a legacy patch. */
    std::vector<juce::AudioProcessorParameter*> reimaginedParameters;

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
    /** `settingsOnly`: parameters (with migrations) and kept slots; sounds and UI state untouched. */
    void applyStateXml (const juce::XmlElement& xml, bool settingsOnly = false);
    juce::ValueTree instrumentTree (int layer);
    void recallInstrument (const juce::ValueTree& tree, int layer);
    void republish (std::shared_ptr<const LoadedInstrument> instrument, int layer);
    void suggestGranularPosition (int layer, const LoadedInstrument& instrument);
    friend class InstrumentChangeAction;
    friend class RootChangeAction;
    void pushResult (LoadResult result, int layer);
    /** Everything that makes a layer what it is (moves with it when layers are compacted). */
    struct LayerSnapshot
    {
        std::shared_ptr<const LoadedInstrument> instrument;
        std::optional<double> rootOverride;
        juce::NamedValueSet values;   ///< granular + layer control parameters by name
        std::map<std::uint64_t, std::shared_ptr<const LoadedInstrument>> latestByLoad;
        std::uint64_t lastPublishedLoad = 0;
    };
    LayerSnapshot captureLayer (int layer) const;
    void applyLayer (int layer, const LayerSnapshot& snapshot);
    void makeLayerAudible (int layer, bool keepMix = false);
    struct RemovedLayer
    {
        int index = 0;
        LayerSnapshot snapshot;
    };
    std::unique_ptr<RemovedLayer> removedLayer;
    std::atomic<int> keptSlotCount { 0 };   ///< message thread writes, the audio thread reads (engine mix slots)
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
    std::atomic<bool> closing { false };   // set by the destructor: no further load stages
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
    std::atomic<float>* echoParam = nullptr;
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
        std::array<std::atomic<float>*, 8> controls {};   ///< in layerControlNames() order
        std::array<float, 8> lastControls { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
        std::atomic<float>* reimagined = nullptr;   ///< B and C (A follows the instrument's)
        float lastReimagined = -1.0e9f;
        std::array<std::atomic<float>*, 15> modes {};   ///< in reimaginedModeNames() order
        std::array<float, 15> lastModes {};
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        bool lastAudible = true;
    };
    std::atomic<float>* mixXParam = nullptr;
    std::atomic<float>* mixYParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainLevelParam = nullptr;
    std::atomic<float>* voiceModeParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    float lastDecay = -1.0f, lastSustainLevel = -1.0f;
    std::array<LayerParams, numLayers> layerParams;
    // Shaping system v1.0 (the macro popups), in the order of shapingIds().
    static constexpr int numShapingParams = 53;
    std::array<std::atomic<float>*, numShapingParams> shapingParams {};
    std::array<float, numShapingParams> lastShaping {};
    Shaping shapingFromParameters() const noexcept;
    // MIDI-controlled macro values (CC 20-26; ECHO is 26), used until the host parameter moves again.
    std::array<float, 7> ccMacro { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 7> lastMacroParam { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    // SHAPER CUSTOM: the message thread writes the steps as atomics, then bumps the
    // generation; the audio thread re-reads them when the generation moves.
    std::array<std::atomic<float>, 48> customStepValues {};
    std::atomic<std::uint32_t> customGeneration { 1 };
    std::atomic<float> hostBpm { 120.0f };
    std::atomic<bool> shaperLarge { false };
    std::uint32_t appliedCustomGeneration = 0;
    ShaperPattern customPattern {};   ///< message thread's copy
    mutable juce::CriticalSection customLock;
    void publishCustomPattern() noexcept;
    float modWheel = 0.0f;
    std::atomic<float> screenPitch { 0.0f }, screenMod { 0.0f };
    float lastScreenPitch = 0.0f, lastScreenMod = 0.0f;
    bool presetIsProgram = true;   ///< the header shows the starting state until a preset file is opened
    float lastAttack = -1.0f, lastRelease = -1.0f, lastGain = -1000.0f, lastVelocityRange = -1.0f;
    double pitchBendSemitones = 0.0;
    bool hostWasPlaying = false;     // audio thread: transport start resets performance memory
    int currentProgram = 0;
    juce::File lastPresetFile;
    juce::File lastStartingStateFile;
    juce::String presetNameOverride;   ///< INIT, Reset or a user starting state while it is the last one opened
    std::atomic<float> uiScaleFactor { 1.0f };
    std::atomic<bool> advancedPanelOpen { false };
    std::set<std::uint64_t> userLoads;     // load ids started by the user (undoable), not by recall

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessor)
};

} // namespace osp::plugin
