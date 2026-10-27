#pragma once

#include "AndorStore.h"

#include "andor/atlas/NoteAtlas.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseEngine.h"
#include "model/ModelExchange.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace osp::andorlab
{

/**
    ANDOR Phrase (docs/andor-lab/phrase.md): a recorded monophonic performance becomes a
    polyphonic instrument. The message thread owns the document (source, Note Atlas, undo);
    a worker decodes and analyses; the audio thread only plays an immutable PhraseModel handed
    over through ModelExchange. Nothing the audio thread touches is shared mutable state.
*/
class PhraseProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int stateVersion = 1;
    static constexpr const char* stateTag = "ANDOR_PHRASE_STATE";
    static constexpr const char* presetExtension = ".andorphrase";

    PhraseProcessor();
    ~PhraseProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ANDOR Phrase"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState state;

    //==========================================================================
    // Document (message thread)
    struct Document
    {
        std::shared_ptr<const LoadedSource> source;      ///< null until a sample is loaded
        std::shared_ptr<const PlaybackSource> playback;
        atlas::NoteAtlas atlas;
        bool sourceMissing = false;                      ///< a recalled session whose audio is not found
        std::string missingName;
        std::vector<std::string> warnings;
    };
    const Document& document() const noexcept { return doc; }
    bool hasSource() const noexcept { return doc.source != nullptr; }

    void loadFile (const juce::File& file);
    /** The generated sung phrase (until owned example recordings exist). */
    void loadExample();
    void cancelLoad();
    bool isBusy() const noexcept { return job != nullptr; }
    float progress() const noexcept;
    juce::String status() const;
    /** Processes a finished load (the timer calls it; tests call it while waiting). */
    void pollLoader();
    /** Tests / offline: waits for the current load to finish (pumping pollLoader). */
    bool waitForLoad (int timeoutMs);

    /** An undoable change to the atlas (`name` labels the undo step). */
    bool edit (const juce::String& name, const std::function<bool (atlas::NoteAtlas&)>& change);
    juce::UndoManager& undoManager() noexcept { return undo; }
    /** Re-analyses the whole recording, keeping every note the user corrected (undoable). */
    void reanalyse();

    bool exportAtlas (const juce::File& file, std::string& error) const;
    bool relink (const juce::File& file, std::string& error);

    bool savePreset (const juce::File& file, std::string& error);
    bool loadPreset (const juce::File& file, std::string& error);
    static juce::File presetFolder();

    std::shared_ptr<const phrase::PhraseModel> model() const { return models.latestModel(); }
    phrase::PhraseParams currentParams() const;

    /** The editor listens here for document changes (message thread). */
    std::function<void()> onDocumentChanged;

    // Display (written by the audio thread, read by the editor).
    DisplayMailbox<32> display;

    /** For audition from the editor: one note on/off, lock-free to the audio thread. */
    void audition (int key, bool down) { auditions.push (key, down); }
    AuditionQueue auditions;   ///< the editor's keys, lock-free to the audio thread

private:
    struct Job;
    void timerCallback() override { pollLoader(); }
    void startJob (std::function<void (Job&)> work);
    void setAtlas (atlas::NoteAtlas atlas);
    void publishModel();
    void documentChanged();
    juce::ValueTree toTree();
    bool fromTree (const juce::ValueTree& tree, std::string& error);

    AndorStore store;
    Document doc;
    juce::UndoManager undo;
    ModelExchange<phrase::PhraseModel> models;
    std::uint64_t generation = 0;
    std::shared_ptr<Job> job;
    std::unique_ptr<juce::ThreadPool> pool;
    juce::String lastStatus;

    // Audio thread
    phrase::PhraseEngine engine;
    bool wasPlaying = false;
    std::array<std::atomic<float>*, 24> raw {};

    friend class AtlasEdit;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhraseProcessor)
};

} // namespace osp::andorlab
