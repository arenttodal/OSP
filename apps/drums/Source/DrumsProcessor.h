#pragma once

#include "InstrumentLoader.h"
#include "LoadedInstrument.h"

#include "drums/DrumKitEngine.h"
#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace osp::drumsplugin
{

using plugin::LoadedInstrument;

/** What the message thread knows about a pad (its identity is its index 0..8, never its
    place on screen). */
struct PadInfo
{
    std::string name;                      ///< empty: the category's name
    drums::DrumCategory category = drums::DrumCategory::unknown;
    int chokeGroup = 0;
    drums::VoiceMode voiceMode = drums::VoiceMode::poly;
    drums::TriggerMode trigger = drums::TriggerMode::oneShot;

    std::shared_ptr<const LoadedInstrument> sound;   ///< what plays (null: empty pad)
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
    static constexpr int stateVersion = 2;   ///< 2: pad order, per-pad envelope/START/REVERSE parameters
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
    void clearPad (int pad);
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
    bool nineLayout = false;   ///< 3x3 instead of 2x4
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
    void startLoad (int pad, plugin::LoadRequest request);
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
        std::uint64_t generation = 0;
        plugin::LoadResult result;
    };
    std::vector<Finished> finished;

    // Message -> audio: sounds
    std::array<ModelExchange<LoadedInstrument>, numPads> exchange;
    std::array<std::atomic<bool>, numPads> clearRequest {};
    // Audio thread: what plays and what still rings out
    std::array<const LoadedInstrument*, numPads> playing {};
    std::array<std::array<const LoadedInstrument*, 8>, numPads> retired {};

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
    };
    juce::AbstractFifo auditionFifo { 64 };
    std::array<Audition, 64> auditions {};   // written by the message thread only

    // Audio -> message: MIDI Learn and held notes
    std::atomic<bool> learnArmed { false };
    juce::AbstractFifo learnFifo { 128 };
    std::array<int, 128> learnNotes {};
    std::array<std::atomic<std::uint32_t>, 4> heldBits {};
    std::atomic<int> lastNote { -1 };

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
