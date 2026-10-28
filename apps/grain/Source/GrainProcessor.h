#pragma once

#include "AndorStore.h"

#include "andor/atlas/NoteAtlas.h"
#include "andor/grain/GrainEngine.h"
#include "andor/grain/KeyDna.h"
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
    ANDOR Grain (docs/andor-lab/grain.md): a polyphonic granular hybrid where every key has its
    own stable granular identity (KEY DNA). Sources: a sample, or a Note Atlas from ANDOR Phrase
    (pitch-aware regions). The message thread owns the source and the KEY DNA curves; the audio
    thread plays an immutable GrainModel and a curve snapshot, both handed over lock-free.
*/
class GrainProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int stateVersion = 1;
    static constexpr const char* stateTag = "ANDOR_GRAIN_STATE";
    static constexpr const char* presetExtension = ".andorgrain";

    GrainProcessor();
    ~GrainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ANDOR Grain"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState state;

    //==========================================================================
    struct Document
    {
        std::shared_ptr<const LoadedSource> source;
        std::shared_ptr<const atlas::NoteAtlas> atlas;   ///< set when the source came from ANDOR Phrase
        double detectedRoot = 60.0;
        bool sourceMissing = false;
        std::string missingName;
        std::vector<std::string> warnings;
    };
    const Document& document() const noexcept { return doc; }
    bool hasSource() const noexcept { return doc.source != nullptr; }

    void loadSample (const juce::File& file);
    /** A Note Atlas (.andoratlas) exported by ANDOR Phrase: its audio is found next to it, at its
        last known path or in the shared store (by fingerprint). */
    void loadAtlas (const juce::File& file);
    /** A Note Atlas already in memory (and its audio file): the Phrase -> Grain hand-over. */
    void loadAtlas (const atlas::NoteAtlas& atlas, const juce::File& atlasFolder);
    enum class Example { pad, phraseAtlas };
    void loadExample (Example);
    void clearSource();
    bool isBusy() const noexcept { return job != nullptr; }
    float progress() const noexcept;
    juce::String status() const;
    void pollLoader();
    bool waitForLoad (int timeoutMs);

    // KEY DNA curves (message thread). Every change is published to the audio thread.
    const grain::KeyDnaSettings& dnaSettings() const noexcept { return dna; }
    void setCurve (grain::DnaParam p, const grain::DnaCurve& curve);
    void resetCurve (grain::DnaParam p);
    /** A seeded random curve for one parameter (deterministic from the DNA seed and the parameter). */
    void randomiseCurve (grain::DnaParam p);
    /** A fresh variation seed (the per-key individuality changes, the curves stay). */
    void regenerateSeed();
    /** The table the editor shows (computed from the curves and the DNA parameters). */
    grain::KeyDnaTable dnaTable() const;

    bool savePreset (const juce::File& file, std::string& error);
    bool loadPreset (const juce::File& file, std::string& error);
    static juce::File presetFolder();

    grain::GrainParams currentParams() const;
    std::shared_ptr<const grain::GrainModel> model() const { return models.latestModel(); }
    void audition (int key, bool down) { auditions.push (key, down); }
    AuditionQueue auditions;   ///< the editor's keys, lock-free to the audio thread

    std::function<void()> onDocumentChanged;

    /** Display state written by the audio thread. a: grain position (0..1), b: level, c: voice. */
    DisplayMailbox<256> grainsDisplay;
    /** Per sounding voice: key and the effective normalised DNA values. */
    struct VoiceDisplay
    {
        std::atomic<int> count { 0 };
        std::array<std::atomic<int>, 16> keys {};
        std::array<std::array<std::atomic<float>, grain::numDnaParams>, 16> effective {};
        std::array<std::atomic<float>, 16> regionStart {}, regionEnd {};
    } voicesDisplay;

private:
    struct Job;
    struct CurveSnapshot
    {
        std::uint64_t generation = 0;
        std::array<grain::DnaCurve, grain::numDnaParams> curves {};
    };
    void timerCallback() override { pollLoader(); }
    void startJob (std::function<void (Job&)> work);
    void publishModel();
    void publishCurves();
    void documentChanged();
    juce::ValueTree toTree();
    bool fromTree (const juce::ValueTree& tree, std::string& error);

    AndorStore store;
    Document doc;
    grain::KeyDnaSettings dna;
    ModelExchange<grain::GrainModel> models;
    ModelExchange<CurveSnapshot> curves;
    std::uint64_t generation = 0, curveGeneration = 0;
    std::shared_ptr<Job> job;
    std::unique_ptr<juce::ThreadPool> pool;
    juce::String lastStatus;

    // Audio thread
    grain::GrainEngine engine;
    std::array<grain::DnaCurve, grain::numDnaParams> audioCurves {};
    std::uint64_t audioCurveGeneration = 0;
    bool wasPlaying = false;
    std::vector<std::atomic<float>*> raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrainProcessor)
};

} // namespace osp::andorlab
