#pragma once

#include "InstrumentLoader.h"
#include "LoadedInstrument.h"

#include "drums/BreakSlicer.h"
#include "drums/DrumKitEngine.h"
#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>
#include <map>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace osp::drumsplugin
{

using plugin::LoadedInstrument;

/** One sample layer of a pad (message thread). */
struct PadLayer
{
    std::shared_ptr<const LoadedInstrument> sound;   ///< null: missing (recall found no file)
    std::string name;                                ///< empty: the file's name
    drums::LayerParams params;
    /** A missing layer keeps how to find it again (saved with the session as it was). */
    std::optional<plugin::LoadRequest> missing;

    std::string fileName() const { return sound != nullptr ? sound->filename : missing.has_value() ? missing->filename : std::string(); }
    std::string displayName() const { return ! name.empty() ? name : fileName(); }
    bool operator== (const PadLayer& o) const
    {
        return sound == o.sound && name == o.name && params == o.params && missing.has_value() == o.missing.has_value()
               && (! missing.has_value() || (missing->expectedHash == o.missing->expectedHash && missing->filename == o.missing->filename));
    }
};

/** A pad's layers and how hits use them (DrumLayers.h). */
struct PadLayers
{
    std::vector<PadLayer> layers;
    drums::LayerMode mode = drums::LayerMode::roundRobin;
    drums::RoundRobinOrder order = drums::RoundRobinOrder::shuffle;
    bool operator== (const PadLayers&) const = default;
};

/** How dropped files join a pad (spec §3.2). */
enum class LayerDrop
{
    replace,      ///< the files become the pad's layers (one file: a plain one-sample pad)
    roundRobin,   ///< added as round-robin alternatives (full velocity range each)
    velocity,     ///< added as velocity layers, softest to loudest by their recorded level
    stack         ///< added to sound together with every hit
};

/** What the message thread knows about a pad (its identity is its index 0..8, never its
    place on screen). */
struct PadInfo
{
    std::string name;                      ///< empty: the category's name
    drums::DrumCategory category = drums::DrumCategory::unknown;
    int chokeGroup = 0;
    drums::VoiceMode voiceMode = drums::VoiceMode::poly;
    drums::TriggerMode trigger = drums::TriggerMode::oneShot;

    std::shared_ptr<const LoadedInstrument> sound;   ///< the first layer that plays (null: nothing plays)
    PadLayers layers;                                ///< every layer (sound is the first loaded one)
    bool loading = false;
    std::string error;                     ///< the last load's failure, shown on the pad
    /** A sound the saved session refers to but which could not be found (relink later). */
    std::string missingFilename;
};

/**
    ANDOR Drums: nine pads, each an OSP engine (DrumKitEngine), sharing OSP's post effects.
    The audio thread receives sounds only through one ModelExchange per pad, and settings
    only through parameters and seqlocked atomics; it never allocates, locks or does I/O.
*/
class DrumsProcessor final : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    static constexpr int numPads = drums::maxPads;
    static constexpr int stateVersion = 3;   ///< 2: pad order, per-pad envelope/START/REVERSE parameters; 3: sample layers
    static constexpr const char* stateTag = "AndorDrums";

    DrumsProcessor();
    ~DrumsProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Kit"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message thread -----------------------------------------------------------------
    juce::AudioProcessorValueTreeState& parameters() noexcept { return apvts; }
    static juce::String padParamId (int pad, const char* name) { return "pad" + juce::String (pad + 1) + "." + name; }

    /** Loads a file onto a pad (replacing what it holds once the new sound is ready). */
    void loadPad (int pad, const juce::File& file);
    /** Loads several files onto a pad; when all are ready they join it as `how` says, as
        ONE undoable step. Files that fail are skipped (the pad shows why if none loads). */
    void addPadLayers (int pad, const juce::Array<juce::File>& files, LayerDrop how);
    /** Undoable: empties the pad. */
    void clearPad (int pad);

    // Layers (every edit is undoable; `newTransaction` false joins the current undo step,
    // e.g. while dragging a value)
    const PadLayers& padLayers (int pad) const noexcept { return pads[index (pad)].layers; }
    void applyLayerEdit (int pad, const PadLayers& next, const juce::String& actionName, bool newTransaction = true);
    void setLayerParams (int pad, int layer, const drums::LayerParams& params, bool newTransaction = true);
    void setLayerMode (int pad, drums::LayerMode mode);
    void setLayerOrder (int pad, drums::RoundRobinOrder order);
    void renameLayer (int pad, int layer, const juce::String& name);
    void removeLayer (int pad, int layer);
    void duplicateLayer (int pad, int layer);
    void moveLayer (int pad, int from, int to);
    // Pads
    /** Copies pad `from` onto pad `to`: its layers (sounds shared, no reload), name,
        category, play modes and every per-pad parameter except SOLO. Each pad keeps its own
        MIDI note and zone. One undo step. False if nothing to copy or the same pad. */
    bool duplicatePad (int from, int to);
    /** The first pad with no layers (visible pads first), or -1. */
    int firstEmptyPad() const;

    // Kits: .andorkit files with their own versioned schema (spec 10.2, 10.5)
    static constexpr int kitSchemaVersion = 1;
    static constexpr const char* kitTag = "AndorDrumsKit";
    static constexpr const char* kitExtension = ".andorkit";
    /** Everything about the kit: pads, layers, sounds (by content hash, original path),
        key maps, chokes, envelopes, effects, macros, keyboard mode, layout and name. */
    bool saveKit (const juce::File& file, juce::String& error);
    /** Replaces the kit (one undo step). Never reads an OSP preset or session as a kit. */
    bool loadKit (const juce::File& file, juce::String& error);
    /** An empty kit with factory pads and defaults (one undo step). */
    void newKit();
    /** A self-contained kit (spec 10.3): <parent>/<kit name>/<kit name>.andorkit and a
        Samples/ folder with a copy of every sound, referenced by relative path (no private
        absolute paths). Returns the kit file, or an empty File and the error. */
    juce::File exportPortableKit (const juce::File& parentDirectory, juce::String& error);
    juce::File kitFile;   ///< the kit file last saved or opened (view; not saved)

    // Missing samples (spec 10.4): never substituted silently
    int missingLayerCount() const;
    /** Points one missing (or any) layer at a file the user chose (one undo step). */
    void relinkLayer (int pad, int layer, const juce::File& file);
    /** Searches a folder (and below) for the missing samples: same name first, then any
        audio file; a file is used only when its content hash matches. Background; the
        result is reported in relinkReport. */
    void findMissingSamples (const juce::File& folder);
    juce::String relinkReport;

    // Break slicing (spec Part 7) ---------------------------------------------------------
    /** A break being sliced: one loaded sound (every slice on a pad is a layer of it, its
        own START/END: no audio is copied), the decoded audio, the slices and the plan. */
    struct BreakSession
    {
        juce::File file;
        std::shared_ptr<const LoadedInstrument> sound;
        std::shared_ptr<const AudioData> audio;
        drums::SliceSettings settings;
        std::vector<drums::BreakSlice> slices;
        double bpm = 0.0;
        bool loading = false;
        std::string error;
        bool open() const noexcept { return loading || sound != nullptr || ! error.empty(); }
    };
    const BreakSession& breakSession() const noexcept { return breakState; }
    /** Loads and slices a break (background); the session opens at once (loading). */
    void importBreak (const juce::File& file);
    void closeBreak();
    /** Slices again with new settings (the plan is kept for slices that survive). Undoable. */
    void resliceBreak (const drums::SliceSettings& settings);
    /** Any edit of the slices or the plan (boundaries, pads, categories). Undoable. */
    void editBreakSlices (const std::vector<drums::BreakSlice>& slices, const juce::String& actionName, bool newTransaction = true);
    /** The default plan: the slices, in order, onto the empty pads (all pads for a new kit);
        the rest stay in the tray. */
    void suggestBreakPlan (bool forNewKit = false);
    /** Plays one slice of the break (lock-free preview, independent of the pads). */
    void auditionSlice (int index);
    enum class BreakApply
    {
        newKit,      ///< a new kit from the break
        emptyPads,   ///< only into pads that hold nothing (others are left alone, reported)
        merge        ///< into the kit: empty pads get the slices, others get them as round robins
    };
    /** Puts the planned slices on their pads (several on one pad: round robin), keeps the
        groove, closes the session. One undo step. Returns a short report. */
    juce::String applyBreak (BreakApply how);
    /** The layer that plays slice `s` of the session's sound (START/END fractions). */
    PadLayer sliceLayer (const drums::BreakSlice& s, int number) const;

    /** The last applied break's groove (original timing, spec 7.13), saved with the kit. */
    struct GrooveHit
    {
        double seconds = 0.0;   ///< from the first hit
        int pad = -1;
        double strength = 1.0;
    };
    struct Groove
    {
        std::string filename, hash;
        double bpm = 120.0;
        std::vector<GrooveHit> hits;
    };
    Groove groove;
    /** The groove as a MIDI file: each hit on its pad's PAD-mode note, velocity from its
        strength, at the groove's tempo. */
    bool writeGrooveMidi (const juce::File& file, juce::String& error, const Groove* source = nullptr) const;
    /** The open break's groove as planned now (before applying it). */
    Groove sessionGroove() const;

    /** Mouse audition of one layer (lock-free; plays it alone, at the pad's settings). */
    void auditionLayer (int pad, int layer, int velocity);
    /** The layer the pad's last hit played (display; -1 before any). */
    int lastLayerPlayed (int pad) const noexcept { return lastLayer[index (pad)].load (std::memory_order_relaxed); }
    /** The layer shown in the editor (view state, not saved). */
    std::array<int, drums::maxPads> selectedLayer {};
    const PadInfo& padInfo (int pad) const noexcept { return pads[index (pad)]; }
    juce::String padDisplayName (int pad) const;
    void setPadName (int pad, const juce::String& name);
    void setPadCategory (int pad, drums::DrumCategory category, bool applyCategoryDefaults);
    void setPadChoke (int pad, int group);
    void setPadVoiceMode (int pad, drums::VoiceMode mode);
    void setPadTrigger (int pad, drums::TriggerMode mode);

    const drums::KeyMap& keyMap() const noexcept { return map; }
    /** Replaces the key map without an undo step (state recall, tests). */
    void setKeyMap (const drums::KeyMap& newMap);

    /**
        The kit's arrangement: both key maps, the pads' screen order and their names. Every
        user edit of it (zone move, resize, root, swap, note assignment, Auto Map, pad
        reorder, rename) goes through applyEdit as ONE undoable transaction.
    */
    struct Arrangement
    {
        drums::KeyMap map;
        std::array<int, drums::maxPads> order {};   ///< screen slot -> pad identity
        std::array<std::string, drums::maxPads> names;
        bool operator== (const Arrangement&) const = default;
    };
    Arrangement arrangement() const;
    void applyEdit (const Arrangement& next, const juce::String& actionName);
    juce::UndoManager& undoManager() noexcept { return undo; }
    /** The pad shown in screen slot `slot` (0..8). */
    int padAtSlot (int slot) const noexcept { return order[index (slot)]; }
    int slotOfPad (int pad) const noexcept;
    /** Exchanges two pads' screen places (identity, sounds and MIDI stay with the pads). */
    void swapPadSlots (int slotA, int slotB);

    /** MIDI Learn: while armed, the audio thread queues incoming note-ons (lock-free); the
        message thread takes them with takeLearnedNotes. */
    void setLearnArmed (bool armed);
    bool isLearnArmed() const noexcept { return learnArmed.load (std::memory_order_relaxed); }
    std::vector<int> takeLearnedNotes();
    /** Notes held at the MIDI input right now, and the last note played (display). */
    bool isNoteHeld (int note) const noexcept;
    int lastPlayedNote() const noexcept { return lastNote.load (std::memory_order_relaxed); }
    drums::KeyboardMode keyboardMode() const;
    void setKeyboardMode (drums::KeyboardMode mode);

    /** Mouse audition (lock-free hand-over to the audio thread). */
    void auditionPad (int pad, int velocity);
    void releaseAuditionedPad (int pad);

    /** View settings, saved with the session (not parameters, not undoable). */
    /** The pad grid: one row of 8 (the default, as the mockup), 2 x 4, or 3 x 3. */
    enum class PadLayout
    {
        row8,
        grid8,
        grid9
    };
    PadLayout padLayout = PadLayout::row8;
    std::string kitName = "New Kit";   ///< shown in the top bar (double-click to rename); saved
    bool isNineLayout() const noexcept { return padLayout == PadLayout::grid9; }
    int visiblePads() const noexcept { return isNineLayout() ? 9 : 8; }
    static const char* layoutName (PadLayout l) noexcept { return l == PadLayout::grid9 ? "3x3" : l == PadLayout::grid8 ? "2x4" : "1x8"; }

    /** Output peaks since the last call (display meter; lock-free, resets on read). */
    std::pair<float, float> takeOutputPeaks() noexcept
    {
        return { outPeakL.exchange (0.0f, std::memory_order_relaxed), outPeakR.exchange (0.0f, std::memory_order_relaxed) };
    }
    int selectedPad = 0;

    /** Display: hits since prepare and sounding voices (lock-free). */
    std::uint32_t padHits (int pad) const noexcept { return engine.padHitsForDisplay (pad); }
    int padVoices (int pad) const noexcept { return engine.padVoicesForDisplay (pad); }

    /** Tests: run the message-thread side of loading until nothing is pending (or timeout). */
    bool waitForLoads (int timeoutMs = 20000);

private:
    static std::size_t index (int pad) noexcept { return static_cast<std::size_t> (juce::jlimit (0, numPads - 1, pad)); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void publishFinishedLoads();
    /** A group of files loading for one pad (a drop, or a recalled pad's layers). */
    struct Batch
    {
        int pad = 0;
        LayerDrop how = LayerDrop::replace;
        bool recall = false;               ///< state recall: becomes the pad as saved, no undo step
        PadLayers result;                  ///< recall: the saved layers (sounds filled in as they load)
        int targetLayer = -1;              ///< relinkLayer: the layer the one file replaces
        bool relink = false;               ///< findMissingSamples: fill the missing layers whose hash matches
        std::vector<std::shared_ptr<const LoadedInstrument>> loaded;   ///< a drop: one per file (null: failed)
        int remaining = 0;
        std::string error;
    };
    void startBatch (int pad, Batch batch, std::vector<plugin::LoadRequest> requests);
    void finishBatch (std::uint64_t id, Batch& batch);
    void setPadLayers (int pad, const PadLayers& layers);   // no undo step; publishes
    /** The kit as XML (session state and kit files share it). `soundPath` (portable export)
        returns a sound's path relative to the kit, written as kitPath. */
    std::unique_ptr<juce::XmlElement> createStateXml (const char* tag, std::function<juce::String (const LoadedInstrument&)> soundPath = {});
    /** Restores a kit from XML; `baseDirectory` resolves kitPath (portable kits). No undo step. */
    void restoreStateXml (const juce::XmlElement& root, const juce::File& baseDirectory = {});
    void resetKit();   // factory pads, defaults; no undo step
    void factoryPad (int pad);
    struct Found
    {
        int pad = 0;
        std::string hash;     ///< "sha256:<hex>"
        juce::File file;
    };
    std::vector<Found> found;   // loader -> message thread (resultsMutex)
    juce::String foundReport;

    // Break session (message thread) and its loader hand-over
    BreakSession breakState;
    std::uint64_t breakRequest = 0;
    struct BreakLoaded
    {
        std::uint64_t request = 0;
        plugin::LoadResult result;
        std::vector<drums::BreakSlice> slices;
    };
    std::vector<BreakLoaded> breakLoaded;   // resultsMutex
    void setBreakSlices (const std::vector<drums::BreakSlice>& slices);
    void applyBreakNow (BreakApply how, juce::String& report);

    // Slice preview (message -> audio): a region of a decoded break, lock-free
    struct Preview
    {
        const AudioData* audio = nullptr;
        std::int64_t start = 0, end = 0;
    };
    juce::AbstractFifo previewFifo { 16 };
    std::array<Preview, 16> previews {};
    std::vector<std::shared_ptr<const AudioData>> previewKeep;   // alive while queued or playing
    std::atomic<const AudioData*> previewInUse { nullptr };
    // Audio thread
    Preview previewNow;
    double previewPosition = 0.0;
    int previewFade = 0;
    void renderPreview (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;
    double hostRate = 48000.0;
    void publishLayers (int pad);
    void publishPadConfig();
    void pullSettings() noexcept;          // audio thread
    void pullKeyMap() noexcept;            // audio thread
    void takeNewSounds() noexcept;         // audio thread

    juce::AudioProcessorValueTreeState apvts;
    drums::DrumKitEngine engine;
    plugin::SampleStore store { plugin::SampleStore::defaultDirectory() };

    // Message thread
    std::array<PadInfo, numPads> pads;
    std::array<int, numPads> order { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    juce::UndoManager undo;
    void setArrangement (const Arrangement& a);   // no undo step
    drums::KeyMap map = drums::KeyMap::defaults();
    std::array<std::uint64_t, numPads> latestRequest {};   ///< newest load generation per pad (older results are dropped)

    // Loader
    juce::ThreadPool loaderPool { 1 };
    std::atomic<std::uint64_t> nextGeneration { 1 };
    std::atomic<int> pendingLoads { 0 };
    std::mutex resultsMutex;   // loader <-> message thread only
    struct Finished
    {
        int pad = 0;
        std::uint64_t generation = 0;   ///< the batch
        int slot = 0;                   ///< which file of the batch
        plugin::LoadResult result;
    };
    std::vector<Finished> finished;
    std::map<std::uint64_t, Batch> batches;   // message thread

    /** What a pad plays, as one immutable bundle (message -> audio): the layers' sounds
        and the plan for them (the layer settings and mode). Every layer edit publishes a
        new one; sounds are shared between bundles. */
    struct PadSound
    {
        std::uint64_t generation = 0;
        std::vector<std::shared_ptr<const LoadedInstrument>> sounds;   ///< plan.count of them
        drums::LayerPlan plan;
        std::array<int, drums::maxLayers> uiIndex {};   ///< engine layer -> PadLayers index (missing layers are skipped)
    };
    void applySound (int pad, const PadSound* sound) noexcept;   // audio thread
    std::array<ModelExchange<PadSound>, numPads> exchange;
    // Audio thread: what plays and what still rings out
    std::array<const PadSound*, numPads> playing {};
    std::array<std::array<const PadSound*, 8>, numPads> retired {};
    bool stillSounding (int pad, const PadSound& old) const noexcept;
    std::array<std::atomic<int>, numPads> lastLayer {};

    // Message -> audio: key map and pad modes, seqlocked (odd generation = being written)
    std::atomic<std::uint32_t> configGeneration { 0 };
    std::uint32_t configSeen = ~0u;
    std::array<std::atomic<int>, numPads> aNote {}, aLow {}, aHigh {}, aRoot {}, aChoke {}, aVoice {}, aTrigger {};
    std::atomic<bool> aOverlap { false };

    // Message -> audio: auditions
    struct Audition
    {
        int pad = 0;
        int velocity = 0;   ///< 0 = release
        int layer = -1;     ///< one layer alone (-1: the pad as it plays)
    };
    juce::AbstractFifo auditionFifo { 64 };
    std::array<Audition, 64> auditions {};   // written by the message thread only

    // Audio -> message: MIDI Learn and held notes
    std::atomic<bool> learnArmed { false };
    juce::AbstractFifo learnFifo { 128 };
    std::array<int, 128> learnNotes {};
    std::array<std::atomic<std::uint32_t>, 4> heldBits {};
    std::atomic<int> lastNote { -1 };
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };

    // Audio thread caches
    struct Pulled
    {
        double gain = 1.0e9, pan = 1.0e9, tune = 1.0e9, start = 1.0e9;
        double attack = 1.0e9, decay = 1.0e9, sustain = 1.0e9, release = 1.0e9;
        bool mute = false, solo = false, reverse = false;
        int choke = -1, voice = -1, trigger = -1;
        bool operator== (const Pulled&) const = default;   // exact: any change is pushed
    };
    std::array<Pulled, numPads> pulled;
    std::array<double, 8> pulledKit {};
    bool kitPulled = false;
    drums::KeyMap audioMap = drums::KeyMap::defaults();
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsProcessor)
};

} // namespace osp::drumsplugin
