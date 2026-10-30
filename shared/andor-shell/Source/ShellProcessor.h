#pragma once

#include "InstrumentHost.h"
#include "InstrumentLoader.h"
#include "ShellProduct.h"

#include "engine/Arpeggiator.h"
#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

namespace andor::shell
{

/**
    The ANDOR Instrument Shell's processor: everything a product built from the shell does
    around its engine. It is the host of the shared ANDOR interface (InstrumentHost), with
    the whole ANDOR parameter schema, three source layers loaded in the background (the
    loader ANDOR OSP uses), presets in the product's own folder, the keyboard and wheels.
    The product's engine (Engine) makes the sound.

    Threads:
      - audio:   processBlock(): MIDI, wheels, the sources' hand-over, the engine.
      - message: parameters, editor, state, presets, publishing loaded sources.
      - loader:  one background thread importing and analysing dropped files.
*/
class ShellProcessor final : public juce::AudioProcessor, public osp::plugin::InstrumentHost, private juce::Timer
{
public:
    /** The product's engine (createEngine()); tests may hand in another. */
    explicit ShellProcessor (std::unique_ptr<Engine> engine = createEngine());
    ~ShellProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return productInfo().name; }
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

    // InstrumentHost: parameters
    juce::AudioProcessorValueTreeState& apvts() override { return parameters; }
    const juce::AudioProcessorValueTreeState& apvts() const override { return parameters; }
    juce::UndoManager& undo() override { return undoManager; }
    double hostSampleRate() const override { return getSampleRate(); }

    // InstrumentHost: sources
    LoadState loadState (int layer = -1) const noexcept override;
    std::shared_ptr<const osp::plugin::LoadedInstrument> currentInstrument (int layer = -1) const override;
    std::optional<double> rootOverride (int) const override { return std::nullopt; }
    bool isLayerOccupied (int layer) const override;
    bool generatedSource (int layer, GeneratedSource& out) const override;
    const osp::InstrumentEngine::GrainSnapshot& grainSnapshot (int layer) const noexcept override;

    // InstrumentHost: presets (Documents/<product>/Presets, *.<product extension>)
    juce::String presetDisplayName() const override;
    void stepPresetList (int delta) override;
    std::vector<PresetEntry> presetList() const override;
    void openPresetEntry (const PresetEntry& entry) override;
    bool isFavourite() const override;
    void toggleFavourite() override;
    void initPatch() override;
    void resetSettings() override;
    bool canRestorePreviousState() const noexcept override { return previousState != nullptr; }
    juce::String previousStateDescription() const override { return previousStateLabel; }
    bool restorePreviousState() override;

    // InstrumentHost: modulation (the routes themselves are InstrumentHost's)
    osp::mod::Curve modulationCurve (int index) const override;
    void setModulationCurve (int index, const osp::mod::Curve& curve, bool undoable = true) override;
    osp::InstrumentEngine::ModView modulationView() const noexcept override { return engine->modulationView(); }
    int modControllerNumber (int slot) const noexcept override;
    void setModControllerNumber (int slot, int controller) noexcept override;
    void learnModController (int slot) noexcept override { ccLearn.store (slot, std::memory_order_release); }
    int learningModController() const noexcept override { return ccLearn.load (std::memory_order_acquire); }
    bool velocityAppliesToAmp() const noexcept override { return velToAmp.load(); }
    void setVelocityAppliesToAmp (bool on) noexcept override { velToAmp = on; }
    bool arpRateIsStepped() const noexcept override { return arpStepped.load(); }
    void setArpRateStepped (bool stepped) noexcept override { arpStepped = stepped; }

    // InstrumentHost: live displays
    int recentVelocityAt (int index) const noexcept override;
    int playedVelocityCount() const noexcept override { return velocityCount.load (std::memory_order_acquire); }
    int lastPlayedNoteNumber() const noexcept override { return lastNote.load (std::memory_order_relaxed); }
    ArpView arpView() const noexcept override;

    // InstrumentHost: the macro popovers
    int editLayer() const noexcept override { return editLayerIndex.load(); }
    float hostTempo() const noexcept override { return tempo.load (std::memory_order_relaxed); }
    float shaperPhase() const noexcept override { return engine->shaperPhase(); }
    osp::ShaperPattern shaperCustomPattern() const override;
    void setShaperCustomPattern (const osp::ShaperPattern& pattern, bool undoable = true) override;
    bool shaperEditorLarge() const noexcept override { return shaperLarge.load(); }
    void setShaperEditorLarge (bool large) noexcept override { shaperLarge = large; }
    juce::String reimaginedModeUnavailable (int layer, int mode) const override { return engine->reimaginedModeUnavailable (layer, mode); }
    bool hasKeyDna() const noexcept override { return engine->hasKeyDna(); }

    // The shell -------------------------------------------------------------------------
    const ProductInfo& product() const noexcept { return productInfo(); }
    const Engine::Description& engineDescription() const noexcept { return description; }
    /** The parameter IDs and patterns the product's engine reads (Engine::boundParameters). */
    const juce::StringArray& boundParameters() const noexcept { return bound; }
    /** The engine binds the arpeggiator's parameters (arp.*): the shell then plays ANDOR OSP's
        arpeggiator in front of it, and the engine is given the notes it generates. */
    bool runsArpeggiator() const noexcept { return arpBound; }
    /** The engine makes its own sound (Engine::Description::generator): layer A is its card,
        and nothing is ever loaded. */
    bool isGenerator() const noexcept { return generatorName.isNotEmpty(); }
    const juce::String& generatedSourceName() const noexcept { return generatorName; }
    /** The macro row as the editor shows it: the product's (ProductInfo::macros), or ANDOR
        OSP's own while showReferenceMacros() is on. */
    const std::array<osp::plugin::frame::MacroSpec, osp::plugin::frame::macroCount>& macroSpecs() const noexcept { return macros; }
    /** The visual regression's parity render only: ANDOR OSP's macro names and parameters,
        so a product that renames its macros is compared with ANDOR OSP where nothing else
        differs. Call before the editor opens. */
    void showReferenceMacros() { macros = osp::plugin::frame::ospMacroSpecs(); }

    /** Each file becomes a new layer in the first free slots (beyond the third: not loaded). Returns how many. */
    int addLayers (const juce::Array<juce::File>& files);
    /** A sound for one layer (it replaces what the layer had). */
    void loadFile (const juce::File& file, int layer);
    /** Empties a layer; the layers above move down (A, A+B, A+B+C stay contiguous). */
    void removeLayer (int layer);
    /** How many cards the instrument shows (every layer up to the highest one with a sound). */
    int slotCount() const;
    void setEditLayer (int layer) noexcept { editLayerIndex = std::clamp (layer, 0, numLayers - 1); }
    /** Blocks until pending loads have finished (tests, offline hosts). */
    bool waitForLoads (int timeoutMs);
    /** Message thread: applies finished loads now (normally done by a timer). */
    void pollLoads() { timerCallback(); }
    juce::String statusMessage() const;

    // Presets and files
    /** Documents/<product's preset folder> (env ANDOR_DOCUMENTS_DIR overrides, for tests). */
    juce::File documentsFolder() const;
    juce::File presetFolder() const { return documentsFolder().getChildFile ("Presets"); }
    /** The product's own sample store (copies of the dropped files; env ANDOR_SAMPLE_STORE overrides). */
    juce::File sampleStoreFolder() const;
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    juce::File currentPresetFile() const { return lastPresetFile; }

    // The keyboard row and view settings (saved with the session)
    void setScreenPitchWheel (float value) noexcept { screenPitch = std::clamp (value, -1.0f, 1.0f); }
    void setScreenModWheel (float value) noexcept { screenMod = std::clamp (value, 0.0f, 1.0f); }
    float modWheelPosition() const noexcept { return wheelShown.load (std::memory_order_relaxed); }
    bool arpEditorExpanded() const noexcept { return arpOpen.load(); }
    void setArpEditorExpanded (bool open) noexcept { arpOpen = open; }
    int modPanelTab() const noexcept { return modTab.load(); }
    void setModPanelTab (int tab) noexcept { modTab = std::clamp (tab, 0, 5); }

    /** The saved state's version (its root's schemaVersion attribute). */
    static constexpr int stateVersion = 1;

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState parameters;
    juce::MidiKeyboardState keyboardState;

private:
    void timerCallback() override;
    int resolve (int layer) const noexcept { return std::clamp (layer < 0 ? editLayer() : layer, 0, numLayers - 1); }
    void enqueueLoad (osp::plugin::LoadRequest request, int layer);
    void enqueueRefine (std::shared_ptr<const osp::plugin::LoadedInstrument> base, std::shared_ptr<const osp::AudioData> audio, int layer);
    void pushResult (osp::plugin::LoadResult result, int layer);
    void clearLayer (int layer);
    std::unique_ptr<juce::XmlElement> stateXml() const;
    bool applyStateXml (const juce::XmlElement& xml);
    void rememberPreviousState (const juce::String& label);
    const juce::StringArray& favourites() const;
    juce::File favouritesFile() const { return documentsFolder().getChildFile ("Favourites.txt"); }

    std::unique_ptr<Engine> engine;
    Engine::Description description;
    juce::StringArray bound;
    juce::String generatorName;
    std::array<osp::plugin::frame::MacroSpec, osp::plugin::frame::macroCount> macros;

    struct Layer
    {
        std::atomic<LoadState> state { LoadState::empty };
        std::shared_ptr<const osp::plugin::LoadedInstrument> instrument;   ///< message thread
        osp::ModelExchange<osp::plugin::LoadedInstrument> exchange;
        std::atomic<bool> clearRequested { false };
        std::atomic<std::uint64_t> latestLoadId { 0 };
        const osp::plugin::LoadedInstrument* playing = nullptr;            ///< audio thread
    };
    std::array<Layer, numLayers> layers;
    std::array<osp::InstrumentEngine::GrainSnapshot, numLayers> noGrains;
    std::atomic<int> editLayerIndex { 0 };

    std::unique_ptr<osp::plugin::SampleStore> store;
    juce::ThreadPool loaderPool { 1 };
    std::atomic<int> pendingLoads { 0 };
    std::atomic<std::uint64_t> nextGeneration { 1 };
    std::atomic<bool> closing { false };
    struct Finished
    {
        int layer = 0;
        osp::plugin::LoadResult result;
    };
    std::mutex resultsMutex;
    std::deque<Finished> finished;
    mutable std::mutex messageMutex;
    juce::String lastMessage;

    // Modulation curves (message thread, under their lock) and their points as the audio
    // thread hands them to the engine (x, y, tension per point, then the count; a new
    // generation means "changed").
    mutable juce::CriticalSection curveLock;
    ModCurves curves = defaultModulationCurves();
    static constexpr int curveValues = 3 * osp::mod::maxCurvePoints + 1;
    std::array<std::array<std::atomic<float>, curveValues>, modCurveCount> curveShared {};
    std::array<std::atomic<std::uint32_t>, modCurveCount> curveGeneration {};
    std::array<std::uint32_t, modCurveCount> curveGenerationSeen {};   ///< audio thread
    void publishCurve (int index) noexcept;
    std::array<std::atomic<int>, osp::mod::ccSlots> ccNumbers {};
    std::atomic<int> ccLearn { -1 };
    std::atomic<bool> velToAmp { true }, arpStepped { false };

    // Modulation, when the engine binds the routes (mod.*): the settings gathered from the
    // parameters, the curves and the controllers (audio thread), handed to the engine when
    // any of it changes. A depth route's target is resolved on the message thread.
    void applyModulation() noexcept;
    void refreshModTargets();
    bool modBound = false, modDirty = true;
    std::array<std::atomic<float>*, 2 * 8> modLfoParams {};
    std::array<std::atomic<float>*, 2 * 7> modEnvParams {};
    std::array<std::atomic<float>*, 4 * osp::mod::maxRoutes> modRouteParams {};
    std::array<std::atomic<int>, osp::mod::maxRoutes> modRouteTargets {};
    static constexpr int modValueCount = 2 * 8 + 2 * 7 + 5 * osp::mod::maxRoutes + osp::mod::ccSlots + 2;
    std::array<float, modValueCount> lastModValues {};
    osp::mod::Settings modSettings;
    std::array<int, osp::mod::ccSlots> modControllers {};

    // The arpeggiator (audio thread), when the engine binds arp.*: ANDOR OSP's, run as its
    // processor runs it. Its display is published lock-free for the ARP section.
    const juce::MidiBuffer& runArpeggiator (const juce::MidiBuffer& midi, int numSamples, const osp::HostTiming& timing) noexcept;
    void publishArpDisplay() noexcept;
    /** ARP RATE's Stepped mode: the musical ratio nearest a speed (as ANDOR OSP's). */
    static double steppedArpRatio (double multiplier) noexcept;
    bool arpBound = false;
    int arpModOctaves = 0;
    osp::Arpeggiator arpeggiator;
    juce::MidiBuffer arpMidi;
    bool hostWasPlaying = false;
    std::atomic<float>* arpEnabledParam = nullptr;
    std::atomic<float>* arpPatternParam = nullptr;
    std::atomic<float>* arpRateParam = nullptr;
    std::atomic<float>* arpGateParam = nullptr;
    std::atomic<float>* arpOctavesParam = nullptr;
    std::atomic<float>* arpSwingParam = nullptr;
    std::atomic<float>* arpVelocityParam = nullptr;
    std::atomic<float>* seedParam = nullptr;
    std::array<std::atomic<int>, osp::Arpeggiator::displaySteps> arpDisplayLow {}, arpDisplayHigh {};
    std::atomic<int> arpDisplayCurrent { -1 };
    std::atomic<bool> arpDisplayActive { false };

    // SHAPER CUSTOM: the pattern (message thread, under its lock) and its steps as the audio
    // thread reads them (start, end, shape per step; a new generation means "changed").
    mutable juce::CriticalSection shaperLock;
    osp::ShaperPattern shaperPattern = osp::RhythmicShaper::patternSteps (osp::ShaperParams().pattern);
    std::array<std::atomic<float>, 3 * osp::RhythmicShaper::steps> shaperSteps {};
    std::atomic<std::uint32_t> shaperGeneration { 1 };
    std::uint32_t shaperGenerationSeen = 0;   ///< audio thread
    void publishShaperPattern() noexcept;
    std::atomic<float> tempo { 120.0f };

    // Live displays and the wheels.
    std::array<std::atomic<int>, velocityHistory> recentVelocity {};
    std::atomic<int> velocityCount { 0 }, lastNote { -1 };
    std::atomic<float> screenPitch { 0.0f }, screenMod { 0.0f }, wheelShown { 0.0f };
    float lastScreenPitch = 0.0f, lastScreenMod = 0.0f;   ///< audio thread
    Engine::Performance performance;                       ///< audio thread

    // View settings.
    std::atomic<bool> arpOpen { false }, shaperLarge { false };
    std::atomic<int> modTab { 0 };

    // Presets.
    juce::File lastPresetFile;
    std::unique_ptr<juce::XmlElement> previousState;
    juce::String previousStateLabel;
    mutable juce::StringArray favouriteNames;
    mutable bool favouritesLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShellProcessor)
};

} // namespace andor::shell
