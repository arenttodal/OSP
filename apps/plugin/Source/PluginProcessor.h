#pragma once

#include "Audition.h"
#include "InstrumentLoader.h"
#include "InstrumentHost.h"
#include "LibraryService.h"
#include "LoadedInstrument.h"

#include "engine/Arpeggiator.h"
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
#include <vector>
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
class OspAudioProcessor final : public juce::AudioProcessor, public InstrumentHost, private juce::Timer, private juce::AudioProcessorParameter::Listener,
                                private juce::ValueTree::Listener
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
    /** The sounds a saved state (session, preset) needs: every layer's and set member's
        content hash ("sha256:<hex>"), sorted, without repeats. A template has none. */
    static std::vector<std::string> soundHashesInState (const juce::XmlElement& state);
    /** The Library catalog's door (loads and saves are recorded through it). */
    LibraryService& libraryService() noexcept { return *libraryHub; }
    /** Hearing Library sounds without changing the patch (the browser's preview, the audition
        tray), and loading them (Library Stage 4). */
    Audition& audition() noexcept { return *auditionHub; }
    /** While on, the keyboard (on screen and MIDI) plays the preview's slots in `slotMask`
        instead of the instrument (the instrument's notes stop when it turns on). */
    void setPreviewKeys (bool on, unsigned slotMask) noexcept
    {
        previewKeysMask.store (slotMask, std::memory_order_relaxed);
        previewKeysOn.store (on, std::memory_order_release);
    }
    bool previewKeys() const noexcept { return previewKeysOn.load (std::memory_order_relaxed); }
    /** Library loads: each (layer, file) into its layer - an occupied layer keeps its controls,
        an empty one is added to the mix - as one recoverable step ("Back to the patch before
        `label`"). Message thread. */
    bool loadLibrarySounds (const std::vector<Audition::LayerLoad>& sounds, const juce::String& label);
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    /** One file with the sources, their analysis and the settings: opens on any computer. */
    /** A portable instrument (.ospinstrument, manifest 2): the settings, every sound it plays
        with its checksum and rights, and Library metadata. For distribution, every sound's
        rights must be known: sounds of unknown origin are listed in `error` and refused until
        the user confirms them (`confirmUnknownRights`, recorded as "cleared"). */
    struct ExportOptions
    {
        bool forDistribution = false;
        bool confirmUnknownRights = false;
        juce::String name, category, notes, creator;
        juce::StringArray tags;
    };
    bool exportInstrument (const juce::File& file, juce::String& error, const ExportOptions& options);
    bool exportInstrument (const juce::File& file, juce::String& error) { return exportInstrument (file, error, {}); }
    bool importInstrument (const juce::File& file, juce::String& error);

    /** Loading a preset, template or instrument replaces the whole patch (and clears undo):
        the patch as it was is kept, and one call brings it back. */
    bool canRestorePreviousState() const noexcept override { return previousState != nullptr; }
    juce::String previousStateDescription() const override { return previousStateLabel; }
    bool restorePreviousState() override;

    /** Preset and template files in the Library: renamed (the record follows), duplicated,
        sent to the Library's trash (recoverable) and restored. Factory content is never a file
        and cannot be changed; editing it and saving makes a user copy. */
    std::optional<juce::File> renamePresetFile (const juce::File& file, const juce::String& newName, juce::String& error);
    std::optional<juce::File> duplicatePresetFile (const juce::File& file, juce::String& error);
    std::optional<juce::File> trashPresetFile (const juce::File& file, juce::String& error);
    std::optional<juce::File> restorePresetFile (const juce::File& trashed, juce::String& error);
    static juce::File libraryTrashFolder();
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
    /** Whether the arpeggiator's inline editor is shown: a view setting only (stored with the
        session like the window's zoom), independent of arp.enabled and never seen by audio. */
    bool arpEditorExpanded() const noexcept { return arpEditorOpen.load(); }
    void setArpEditorExpanded (bool open) noexcept { arpEditorOpen = open; }
    /** Which editor the envelope panel shows (0 AMP, 1 ENV 1, 2 ENV 2, 3 LFO 1, 4 LFO 2): a
        view setting only, stored with the session; every source runs whichever is shown. */
    int modPanelTab() const noexcept { return modTab.load(); }
    /** The modulation panel's tab: AMP, ENV, LFO 1, LFO 2, VEL, NOTE (0..5). */
    void setModPanelTab (int tab) noexcept { modTab = std::clamp (tab, 0, 5); }

    // MODULATION: parameter IDs, the destinations' names (registry order), the editable
    // curves (0, 1: LFO 1/2 CUSTOM; 2, 3: ENV 1/2 one-shot; 4 VEL; 5 NOTE), routes by slot, the display.
    mod::Curve modulationCurve (int index) const override;
    void setModulationCurve (int index, const mod::Curve& curve, bool undoable = true) override;
    /** State version < 11: scales every route depth from the old fixed spans to the
        full-range ones (same sound). */
    void migrateModulationDepths();
    // The routes themselves (assign, remove, rules, links): InstrumentHost.

    // EQ (version hint 18): each layer's five bands, `layerA.eq.*` (InstrumentHost::eqParameterId).
    // Off by default (older sessions open with it off and sound as they did).
    InstrumentEngine::ModView modulationView() const noexcept override { return engine.modulationView(); }
    /** The CC 1-4 sources' controller numbers (0..119; default 2, 11, 16, 17), saved with the
        session. Learn: the next controller that moves is taken for the slot (-1: none). */
    int modControllerNumber (int slot) const noexcept override;
    void setModControllerNumber (int slot, int controller) noexcept override;
    void learnModController (int slot) noexcept override { modCcLearn.store (slot >= 0 && slot < mod::ccSlots ? slot : -1, std::memory_order_release); }
    int learningModController() const noexcept override { return modCcLearn.load (std::memory_order_acquire); }

    /** What the arpeggiator's 16-step display shows (lock-free snapshot, any thread). */
    ArpView arpView() const noexcept override;

    // A/B source layers (numLayers, layerName, layerParameterId: InstrumentHost). Everything
    // below that takes a `layer` acts on the layer being edited when it is -1 (the A/B tabs choose it).
    int editLayer() const noexcept override { return editLayerIndex.load(); }
    void setEditLayer (int layer) noexcept { editLayerIndex = std::clamp (layer, 0, numLayers - 1); }
    /** REIMAGINED modes (version hint 11): a layer's mode and every mode's own settings,
        "layerA.reimagined.mode", "layerA.reimagined.tapeFrame.age", ... in this order. */
    static const juce::StringArray& reimaginedModeNames();

    // Instrument loading (message thread)
    void loadFile (const juce::File& file, int layer = -1);
    /** Several files -> one multi-sample instrument (Phase 7). One file falls back to loadFile. */
    void loadFiles (const juce::Array<juce::File>& files, int layer = -1);
    /** Samples inspector correction: pin one file's role/layer and rebuild the set. */
    void reassignSample (const std::string& filename, SampleRole role, int velocityLayer, std::optional<double> rootMidi = std::nullopt);
    void loadExample (int layer = -1);
    /** Empties a layer (it then contributes silence). */
    void clearLayer (int layer = -1);
    /**
        A Note Atlas on a layer (unified engine Phase 3): its notes become the layer's
        multi-sample instrument, built on the loader thread (InstrumentLoader's
        loadInstrumentAtlas) and published like any sound. Its audio is found by content hash
        in the sample store, then through the atlas's hints (`atlasFolder`: where a stand-alone
        .andoratlas was read from). Missing audio fails the load (missingSources).
    */
    void loadAtlas (const atlas::NoteAtlas& atlas, int layer = -1, const std::string& atlasFolder = {});
    /** The sources the layer's last Atlas load could not find (empty when it loaded). */
    std::vector<std::string> missingSources (int layer = -1) const;
    /**
        Unified engine Phase 4: a monophonic recording (sung, bowed, played phrase) becomes the
        layer's instrument. The file is imported into the sample store by hash, analysed into a
        Note Atlas on a background job (its stored analysis is reused when the same recording
        was analysed before with the same settings), and published in two stages: playable at
        once (Stage A), then with sustain loops and releases (Stage B, while notes may sound:
        a sounding note keeps what it started with). A recording that does not suit is loaded
        as one ordinary sample, with the reason as the status message. Nothing of this runs in
        processBlock.
    */
    void analyseRecording (const juce::File& file, int layer = -1, const phrase::AnalysisSettings& settings = {});
    /** Stops the layer's analysis or preparation (the instrument already published stays). */
    void cancelAnalysis (int layer = -1);

    // -- Unified engine Phase 5: MAP NOTES and the NOTE MAP (InstrumentHost) ------------------
    /** One recording (not a multi-sample set): MAP NOTES can analyse it. */
    bool canMapNotes (int layer) const override;
    /** MAP NOTES: the layer's recording (its copy in the sample store) analysed with
        `detection` (analyseRecording). On an analysed atlas: detected again with `detection`,
        the musician's corrections kept and conflicts reported (one undo step). */
    void mapNotes (int layer, phrase::Detection detection = phrase::Detection::normal) override;
    /** The notes a dropped (user-loaded) one-file recording holds, when it holds two or more
        and lasts a second or more (library::heldNotes over the load's own pitch track):
        the card's "Map notes?" prompt. 0 once answered, mapped or replaced. */
    int mapNotesPrompt (int layer) const override;
    void dismissMapNotesPrompt (int layer) override;
    /** An edit of the layer's atlas on the loader thread, published through Stage A / B; one
        undo step. Edits asked for while one is being prepared wait for it (each edits the
        result of the one before). */
    void editAtlas (int layer, const juce::String& name, AtlasEdit edit) override;
    AtlasActivity atlasActivity (int layer) const noexcept override;
    void auditionAtlasRegion (int layer, std::int64_t start, std::int64_t end) override;
    /** The last re-detection's report for the layer (kept corrections, conflicts), and the notes
        the musician corrected that the new analysis disagrees with. Message thread. */
    struct RedetectReport
    {
        int keptCorrected = 0, conflicts = 0;
        std::vector<std::string> conflictNoteIds;
        juce::String summary;
    };
    RedetectReport redetectReport (int layer) const { return layers[resolve (layer)].redetect; }
    juce::String atlasReport (int layer) const override { return layers[resolve (layer)].redetect.summary; }
    std::vector<std::string> atlasConflicts (int layer) const override { return layers[resolve (layer)].redetect.conflictNoteIds; }
    void dismissAtlasConflict (int layer, const std::string& noteId) override
    {
        auto& ids = layers[resolve (layer)].redetect.conflictNoteIds;
        ids.erase (std::remove (ids.begin(), ids.end(), noteId), ids.end());
    }
    /** The layer's atlas back to its recording as one ordinary sample (undoable). */
    void playAsOneSample (int layer);
    /** The layer's atlas status (stage, capacity, analysis verdict and doubts); null otherwise. */
    std::shared_ptr<const AtlasStatus> atlasStatus (int layer = -1) const;
    /** Capacity policy (Phase 4): region audio one layer's atlas may hold, and all layers
        together, for new loads. A session's recall keeps the per-layer limit only, so it
        reopens as it was saved. */
    static constexpr double atlasLayerSeconds = 300.0, atlasSessionSeconds = 600.0;
    /** What a new atlas on `layer` may hold now (the other layers' atlases counted). */
    double atlasCapacityFor (int layer) const;
    /** The folder of stored phrase analyses (inside the sample store). */
    juce::File phraseAnalysisFolder() const { return store.phraseAnalysisFolder(); }
    /** The grain budget's policy (state v14; new sessions bounded, older ones shared). */
    bool isGrainBudgetBounded() const noexcept { return boundedGrainBudget.load(); }
    void setGrainBudgetBounded (bool bounded) noexcept { boundedGrainBudget = bounded; }

    // Adaptive layers (message thread). A layer is occupied while it holds or loads a sound;
    // the instrument shows one card per occupied layer, and they are kept contiguous
    // (A, A+B, A+B+C) by removeLayer.
    bool isLayerOccupied (int layer) const override;
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
    // setParameterValue / parameterValue: InstrumentHost.

    LoadState loadState (int layer = -1) const noexcept override { return layers[resolve (layer)].state.load(); }
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
    /** MUTE and SOLO ("layerA.mute", "layerA.solo"): kept apart from the controls (LINK never shares them). */
    static const juce::StringArray& layerStateNames();
    // isLayerHeard / soloOnly (MUTE and SOLO): InstrumentHost.

    /** Most recently loaded instrument of a layer, or nullptr when it is empty (any non-audio thread). */
    std::shared_ptr<const LoadedInstrument> currentInstrument (int layer = -1) const override;

    /** Manual root (fractional MIDI) or nullopt for the analysed root. Message thread. */
    void setRootOverride (std::optional<double> midi, int layer = -1);
    /** Same, as an undoable user action (the editor's root menu). */
    void changeRootOverride (std::optional<double> midi, int layer = -1);
    std::optional<double> rootOverride (int layer = -1) const override;
    double effectiveRootMidi (int layer = -1) const;

    /** Blocks until pending loads have finished (tests, offline hosts). */
    bool waitForLoads (int timeoutMs);
    /** Message thread: applies finished loads immediately (normally done by a timer). */
    void pollLoads() { timerCallback(); }

    /** SHAPER's pattern position (0..1) for the display, -1 when it is not running (any thread). */
    float shaperPhase() const noexcept override { return engine.shaperPhase(); }
    /** SHAPER's large pattern editor was last open (the popover reopens that way). */
    bool shaperEditorLarge() const noexcept override { return shaperLarge.load(); }
    void setShaperEditorLarge (bool large) noexcept override { shaperLarge = large; }
    /** The host's tempo (120 until a host tells), for ECHO's synced times in the display. */
    float hostTempo() const noexcept override { return hostBpm.load (std::memory_order_relaxed); }
    /** The grains a layer is playing now (display; lock-free, any thread). */
    const InstrumentEngine::GrainSnapshot& grainSnapshot (int layer) const noexcept override { return engine.grainSnapshot (layer); }

    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;
    std::atomic<int> activeVoices { 0 };     ///< played notes sounding (a note on three layers counts once)

    /** The last played velocities (newest at `velocityCount - 1`, a ring of 16) and how many
        notes have been played: written by the audio thread, read by the popups (lock-free). */
    std::array<std::atomic<int>, velocityHistory> recentVelocity {};
    std::atomic<int> velocityCount { 0 };
    /** The last played note (the NOTE tab's marker; -1 before the first). */
    std::atomic<int> lastPlayedNote { -1 };
    int recentVelocityAt (int index) const noexcept override { return recentVelocity[static_cast<std::size_t> (index)].load (std::memory_order_relaxed); }
    int playedVelocityCount() const noexcept override { return velocityCount.load (std::memory_order_acquire); }
    int lastPlayedNoteNumber() const noexcept override { return lastPlayedNote.load (std::memory_order_relaxed); }

    /** APPLY TO AMP: the VEL curve sets the amplitude's velocity response in place of
        DYNAMICS' curve (one curve, never both). Saved with the session; on by default (older
        sessions get their DYNAMICS curve as the VEL curve: they sound as they did). */
    bool velocityAppliesToAmp() const noexcept override { return velToAmp.load (std::memory_order_relaxed); }
    void setVelocityAppliesToAmp (bool on) noexcept override;
    /** ARP RATE's modulation: Stepped keeps to musical ratios (1/2, 2/3, 1, 3/2, 2, ...) and
        changes at the next step; smooth (the default) follows the source. Saved with the
        session. */
    bool arpRateIsStepped() const noexcept override { return arpRateStepped.load (std::memory_order_relaxed); }
    void setArpRateStepped (bool stepped) noexcept override { arpRateStepped.store (stepped, std::memory_order_relaxed); }
    /** The ratio Stepped plays for a multiplier (the nearest of 1/4 .. 4, by octaves). */
    static double steppedArpRatio (double multiplier) noexcept;

    /** The on-screen wheels (message thread -> audio thread): pitch -1..1 (springs back), mod 0..1. */
    void setScreenPitchWheel (float value) noexcept { screenPitch = std::clamp (value, -1.0f, 1.0f); }
    void setScreenModWheel (float value) noexcept { screenMod = std::clamp (value, 0.0f, 1.0f); }
    float screenModWheel() const noexcept { return screenMod.load(); }
    /** Where the mod wheel is, from either the screen or MIDI CC 1 (what the wheel shows). */
    float modWheelPosition() const noexcept { return wheelShown.load (std::memory_order_relaxed); }

    // LINK (applyLinkedDelta, isLayerLinked): InstrumentHost.

    // Header preset navigation (spec: previous / name / favourite / next): the factory
    // starting states first, then the user's preset files.
    juce::String presetDisplayName() const override;
    void stepPresetList (int delta) override;
    /** INIT: an empty patch - no sounds, no kept slots, every setting at its default. */
    void initPatch() override;
    /** Every setting to its default; the sounds stay (two or three meet in the middle of the mix). */
    void resetSettings() override;
    /** A user starting state: every setting and the slot count, no audio. Opening one applies
        the settings to the sounds already loaded (and keeps empty slots for the rest). */
    bool saveStartingState (const juce::File& file);
    bool loadStartingState (const juce::File& file);
    static juce::File startingStateFolder();
    static constexpr const char* startingStateExtension = ".ospstate";
    std::vector<PresetEntry> presetList() const override;
    void openPresetEntry (const PresetEntry& entry) override;
    bool isFavourite() const override;
    void toggleFavourite() override;
    /** A preset or starting state's favourite mark by name (the header's heart and the Library's). */
    bool isFavouriteName (const juce::String& name) const;
    void setFavouriteName (const juce::String& name, bool favourite);
    /** Favourites.txt changed on disk (a restored backup): read again on next use. */
    void reloadFavourites() { favouritesLoaded = false; }
    /** The content hashes the layers play now (the Library never moves these away). */
    std::vector<std::string> soundsInUse() const;
    /** The built-in starting states' names (the Library lists them as factory templates). */
    static juce::StringArray factoryStartingStates();
private:
    const juce::StringArray& favourites() const;
    mutable juce::StringArray favouriteNames;
    mutable bool favouritesLoaded = false;
public:
    juce::UndoManager undoManager;

    // InstrumentHost: the shared interface's view of the parameters.
    juce::AudioProcessorValueTreeState& apvts() override { return parameters; }
    const juce::AudioProcessorValueTreeState& apvts() const override { return parameters; }
    juce::UndoManager& undo() override { return undoManager; }
    double hostSampleRate() const override { return getSampleRate(); }

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
    /** 11: full-range modulation (100 % depth covers a destination's whole knob; older
            sessions' depths are scaled so they sound the same: migrateModulationDepths).
        12: the final modulation system: the sources VEL, NOTE, CC 1-4 and PRESSURE (appended),
            the curves as "c3" (VEL, NOTE, DAMPEN A-C), APPLY TO AMP (`velToAmp`; absent: on,
            the session's DYNAMICS curve as the VEL curve), the CC slots' controllers
            (`modControllers`), the panel's six tabs (`modTabs`), ARP RATE Stepped
            (`arpRateStepped`), ARP VELOCITY and DAMPEN (parameters, hint 19; absent: 100 %,
            off). Every addition reads its default from an older session, so nothing there is
            migrated beyond the VEL curve and the panel's tab.
        13: the unified engine's Phase 2: each layer's KEY DNA (the `KeyDna` child, its own
            schemaVersion; absent: none), the grain options SCATTER / PAN / WINDOW and KEY DNA
            AMOUNT (parameters, hint 20; absent: neutral) and the grain budget (absent, i.e.
            any older session or preset: Unlimited, the original grains).
        14: the unified engine's Phase 3: a layer can hold a Note Atlas (its `Instrument` tree's
            `Atlas` child: the atlas JSON, own schemaVersion; the audio stays in the sample store,
            referenced by hash), and the grain budget's policy (`grainBudgetPolicy`; absent, i.e.
            any older session: `shared`, Phase 2's; new sessions `bounded`, never above the budget). */
    static constexpr int stateVersion = 14;

    /** KEY DNA (message thread): a layer's per-key identity. Its profile (all 128 keys) is
        computed here and handed to the audio thread through a ModelExchange; new notes take
        their key's values from it. AMOUNT is the host parameter layerX.keyDna.amount (live on
        sounding notes); `settings.amount` is ignored. Off by default; saved in the session. */
    void setKeyDnaSettings (int layer, const keydna::Settings& settings);
    keydna::Settings keyDnaSettings (int layer) const override;
    // Phase 5 (InstrumentHost): the KEY DNA editor's undoable edits and drawn curves.
    bool hasKeyDna() const noexcept override { return true; }
    void setKeyDnaSettings (int layer, const keydna::Settings& settings, const juce::String& name, bool undoable) override;
    mod::Curve keyDnaCurve (int layer, keydna::Param param) const override;
    void setKeyDnaCurve (int layer, keydna::Param param, const mod::Curve& curve, bool undoable) override;
    /** The drawn curves (points) of every layer, saved beside the compiled tables (`points`). */
    using KeyDnaCurves = std::array<std::array<std::optional<mod::Curve>, keydna::paramCount>, 3>;
    static juce::ValueTree keyDnaToTree (const std::array<keydna::Settings, 3>& layers, const KeyDnaCurves& curves);
    static KeyDnaCurves keyDnaCurvesFromTree (const juce::ValueTree& tree);
    /** The profile generation the audio thread plays now (0: none yet), for tests. */
    std::uint64_t keyDnaGenerationInUse (int layer) const noexcept { return keyDnaInUse[resolve (layer)].load(); }
    std::size_t keyDnaProfilesAlive (int layer) const;
    static juce::ValueTree keyDnaToTree (const std::array<keydna::Settings, 3>& layers);
    static std::array<keydna::Settings, 3> keyDnaFromTree (const juce::ValueTree& tree);

    /** SHAPER CUSTOM: the musician's 16 steps (message thread). Kept in the session and
        presets; setting it is undoable (one gesture = one transaction, see UndoManager). */
    ShaperPattern shaperCustomPattern() const override;
    void setShaperCustomPattern (const ShaperPattern& pattern, bool undoable = true) override;
    // Its encodings and the saved pattern files are InstrumentHost's (shared with every product).

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
    /** The grain budget's policy (state v14): bounded (never above the budget) for new
        sessions, shared (Phase 2's) for older ones. Message thread writes, audio thread reads. */
    std::atomic<bool> boundedGrainBudget { true };
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
        std::vector<std::string> missingSources;         // message thread: the last Atlas load's
        std::shared_ptr<std::atomic<bool>> cancelPreparation;   // message thread: the current analysis / atlas job's
        std::uint64_t messageLoad = 0;                   // message thread: the load the status message is about
        std::uint64_t promptLoad = 0;                    // message thread: the load "Map notes?" asks about (0: none)
        int promptNotes = 0;
        // Root override (message -> audio)
        std::atomic<double> rootShiftSemitones { 0.0 };
        std::atomic<bool> hasRootOverride { false };
        std::atomic<double> rootOverrideMidi { 60.0 };
        // Undo of sample loads: the latest instrument of every recent load. Message thread.
        std::map<std::uint64_t, std::shared_ptr<const LoadedInstrument>> latestByLoad;
        std::uint64_t lastPublishedLoad = 0;
        float autoPosition = -1.0f;                      // POS set from the analysis (granular), until the user moves it
        std::uint64_t autoPositionLoad = 0;
        // Phase 5 (message thread): the atlas's decoded sources (for its edits), the edits
        // waiting for the one being prepared, the analysis or edit running, the last report.
        std::vector<atlasset::SourceAudio> atlasAudio;
        struct QueuedEdit
        {
            juce::String name;
            AtlasEdit edit;
            std::shared_ptr<RedetectReport> report;
        };
        std::deque<QueuedEdit> editQueue;
        std::uint64_t editInFlight = 0, analysisInFlight = 0;
        RedetectReport redetect;
    };
    /** Phase 5: an edit load's undo record (message thread), by its load id. */
    struct EditRecord
    {
        int layer = 0;
        juce::String name;
        std::uint64_t beforeLoad = 0;
        std::shared_ptr<const atlas::NoteAtlas> before;
        std::shared_ptr<RedetectReport> report;   ///< a re-detection's (filled on the loader thread)
        bool restore = false;                      ///< an undo / redo rebuilding an evicted state: no new undo step
    };
    std::map<std::uint64_t, EditRecord> editLoads;
    void queueEdit (int layer, const juce::String& name, AtlasEdit edit, std::shared_ptr<RedetectReport> report);
    void startEdit (int layer, const juce::String& name, AtlasEdit edit, std::shared_ptr<RedetectReport> report = nullptr);
    /** Shows the atlas instrument of `loadId` (from the undo cache) or rebuilds it from `atlas`. */
    bool showAtlasState (int layer, std::uint64_t loadId, std::shared_ptr<const atlas::NoteAtlas> atlas);
    /** Prepares `atlas` again on the layer (no undo step): an evicted undo state, or Stage B lost. */
    void restoreAtlas (int layer, std::shared_ptr<const atlas::NoteAtlas> atlas);
    friend class AtlasEditAction;
    friend class KeyDnaChangeAction;
    KeyDnaCurves keyDnaCurves {};   ///< guarded by keyDnaMutex
    /** A KEY DNA gesture's starting state (a drag: undoable once, at its end). Message thread. */
    struct KeyDnaState
    {
        keydna::Settings settings;
        std::array<std::optional<mod::Curve>, keydna::paramCount> curves;
    };
    std::array<std::optional<KeyDnaState>, numLayers> keyDnaGestureStart {};
    void beginKeyDnaGesture (std::size_t layer);
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
    void enqueueAtlasLoad (AtlasLoadRequest request, int layer);
    /** Phase 4: Stage B of an atlas load (queued before Stage A is reported). */
    void enqueueAtlasEnhance (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const atlasset::PreparedAtlas> pending, int layer,
                              std::shared_ptr<std::atomic<bool>> cancel);
    std::shared_ptr<std::atomic<bool>> newPreparation (std::size_t layer);
    /** Publishes an instrument without a model (the layer plays nothing; sounding voices finish
        on the old one). Layer controls are untouched (clearLayer also resets those). */
    void publishEmpty (std::size_t index);
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
    void routePreviewKeys (juce::MidiBuffer& midi) noexcept;
    void swapInstrumentIfPending() noexcept;
    /** The arpeggiator stage: from the block's incoming MIDI to the notes the engine plays
        (the incoming buffer itself while it is off, otherwise its own preallocated one). */
    const juce::MidiBuffer& runArpeggiator (const juce::MidiBuffer& midi, int numSamples, const HostTiming& timing) noexcept;
    void publishArpDisplay() noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    InstrumentEngine engine;
    EngineSettings engineSettings;
    Arpeggiator arpeggiator;
    juce::MidiBuffer arpMidi;   ///< preallocated in prepareToPlay
    std::array<std::atomic<int>, Arpeggiator::displaySteps> arpDisplayLow {}, arpDisplayHigh {};
    std::atomic<int> arpDisplayCurrent { -1 };
    std::atomic<bool> arpDisplayActive { false };
    std::array<Layer, numLayers> layers;
    std::atomic<int> editLayerIndex { 0 };

    // Loader
    SampleStore store;
    std::unique_ptr<LibraryService> libraryHub;   ///< the catalog (background thread; never the audio thread)
    library::PreviewEngine preview;               ///< the Library's audition path (rendered after the instrument, dry)
    std::unique_ptr<Audition> auditionHub;        ///< feeds `preview` (background decoding)
    std::atomic<bool> previewKeysOn { false };
    std::atomic<unsigned> previewKeysMask { 0 };
    bool previewKeysActive = false;               ///< audio thread
    juce::MidiBuffer previewMidi;                 ///< preallocated in prepareToPlay
    std::unique_ptr<juce::XmlElement> previousState;   ///< the patch before the last preset / template / instrument load
    juce::String previousStateLabel;
    void rememberPreviousState (const juce::String& what);
    /** Phrase analyses (Phase 4): one at a time, at most ten minutes of audio waiting. Declared
        before the loader pool, so it outlives every loader job that waits on it. */
    phrase::AnalysisQueue analysisQueue { 1, 600.0 };
    io::PhraseAnalysisCache analysisCache { std::filesystem::path (store.phraseAnalysisFolder().getFullPathName().toStdString()) };
    static constexpr int atlasThreads = 2;
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
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* arpEnabledParam = nullptr;
    std::atomic<float>* arpPatternParam = nullptr;
    std::atomic<float>* arpRateParam = nullptr;
    std::atomic<float>* arpGateParam = nullptr;
    std::atomic<float>* arpOctavesParam = nullptr;
    std::atomic<float>* arpVelocityParam = nullptr;
    std::atomic<float>* arpSwingParam = nullptr;
    // MODULATION's parameters, its curves (message thread, customLock) and their tables
    // for the audio thread (atomics + generation, as SHAPER CUSTOM's steps).
    std::array<std::array<std::atomic<float>*, 8>, 2> modLfoParams {};
    std::array<std::array<std::atomic<float>*, 7>, 2> modEnvParams {};
    std::array<std::array<std::atomic<float>*, 4>, mod::maxRoutes> modRouteParams {};
    static constexpr int numModValues = 2 * 8 + 2 * 7 + mod::maxRoutes * 5;
    // META-MODULATION's links: each route's stable ID and a depth route's target ID live in
    // the state's ModLinks child (saved, undoable); the audio thread reads them resolved to
    // slots (message thread writes on every change of the links or the state).
    std::array<std::atomic<int>, mod::maxRoutes> modRouteTargets {};
    void refreshModLinks();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;
    std::array<float, numModValues> lastModValues {};
    mod::Settings modSettings;   ///< the audio thread's working copy (built in place, never allocated)
    ModCurves modCurves {};
    std::array<std::atomic<float>, modCurveCount * mod::curvePoints> modCurveValues {};
    std::atomic<std::uint32_t> modCurveGeneration { 1 };
    std::uint32_t appliedModCurveGeneration = 0;
    void publishModulationCurves() noexcept;
    void applyModulation (bool force) noexcept;
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
        std::array<float, 9> last { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
        std::array<std::atomic<float>*, 8> controls {};   ///< in layerControlNames() order
        std::array<float, 8> lastControls { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
        std::atomic<float>* reimagined = nullptr;   ///< B and C (A follows the instrument's)
        float lastReimagined = -1.0e9f;
        std::array<std::atomic<float>*, 15> modes {};   ///< in reimaginedModeNames() order
        std::array<float, 15> lastModes {};
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        bool lastAudible = true;
        std::array<std::atomic<float>*, 19> eq {};   ///< in eqParameterNames() order
        std::array<float, 19> lastEq {};
        eq::Settings eqSettings;
        std::array<std::atomic<float>*, 5> dampen {};   ///< in dampenParameterNames() order
        std::array<float, 5> lastDampen {};
        std::uint32_t dampenGeneration = 0;
        dampen::Settings dampenSettings;
        std::array<std::atomic<float>*, 3> grainOptions {};   ///< SCATTER, PAN, WINDOW (hint 20)
        std::atomic<float>* keyDnaAmount = nullptr;
    };
    std::atomic<float>* grainBudgetParam = nullptr;
    // KEY DNA: the message thread's settings and the profiles handed to the audio thread.
    mutable std::mutex keyDnaMutex;   ///< message-side (hosts may save state off the message thread)
    std::array<keydna::Settings, numLayers> keyDnaLayers {};
    std::array<ModelExchange<keydna::Profile>, numLayers> keyDnaExchange;
    std::uint64_t keyDnaGeneration = 0;
    std::array<const keydna::Profile*, numLayers> keyDnaPlaying {};   ///< audio thread
    std::array<std::atomic<std::uint64_t>, numLayers> keyDnaInUse {};
    void swapKeyDnaIfPending() noexcept;
    std::atomic<float>* mixXParam = nullptr;
    std::atomic<float>* mixYParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainLevelParam = nullptr;
    std::atomic<float>* voiceModeParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    float lastDecay = -1.0f, lastSustainLevel = -1.0f;
    std::array<LayerParams, numLayers> layerParams;
    // Shaping system v1.0 (the macro popups), in the order of shapingIds().
    static constexpr int numShapingParams = 56;
    std::array<std::atomic<float>*, numShapingParams> shapingParams {};
    std::array<float, numShapingParams> lastShaping {};
    Shaping shapingFromParameters() const noexcept;
    // MIDI-controlled macro values (CC 20-26; ECHO is 26), used until the host parameter moves again.
    std::array<float, 8> ccMacro { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 8> lastMacroParam { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
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
    std::atomic<float> wheelShown { 0.0f };
    std::array<std::atomic<int>, mod::ccSlots> modCcNumber {};
    std::atomic<bool> velToAmp { true };
    std::atomic<bool> arpRateStepped { false };
    int arpModOctaves = 0;   ///< audio thread: OCTAVES by modulation, after hysteresis (0: none)
    void applyArpModulation (Arpeggiator::Settings& settings) noexcept;
    int shownDynamicsCurve = 1;   ///< DYNAMICS' curve as last followed (message thread): moving it sets the VEL curve
    std::atomic<int> modCcLearn { -1 };
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
    std::atomic<bool> arpEditorOpen { false };
    std::atomic<int> modTab { 0 };
    std::set<std::uint64_t> userLoads;     // load ids started by the user (undoable), not by recall
    std::map<std::uint64_t, juce::String> userLoadNames;   ///< their undo names when not "Load sample" (MAP NOTES, ...)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OspAudioProcessor)
};

} // namespace osp::plugin
