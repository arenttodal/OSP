#pragma once

#include "AndorUi.h"
#include "PhraseProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace osp::andorlab
{

/** The phrase display: the recording, its detected notes, their markers and the read heads. */
class PhraseWaveform final : public WaveformView
{
public:
    explicit PhraseWaveform (PhraseProcessor&);
    void refresh();                       ///< after a document change
    void setReadHeads (std::vector<std::pair<int, float>> heads);
    std::string selected;                 ///< note id
    std::int64_t cursor = -1;             ///< split point (source frames), -1 none
    std::function<void (const std::string&)> onSelect;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void paintOverlay (juce::Graphics&, juce::Rectangle<float> area) override;
    enum class Handle { none, attack, sustainStart, sustainEnd, end };
    Handle handleAt (juce::Point<float>) const;
    std::int64_t markerFrame (const atlas::NoteEvent&, Handle) const;

    PhraseProcessor& processor;
    std::vector<std::pair<int, float>> readHeads;   // region index, frame
    Handle dragging = Handle::none;
    std::int64_t dragFrame = 0;
    int auditionKey = -1;
};

/** The selected note's identity and corrections. */
class NotePanel final : public juce::Component
{
public:
    explicit NotePanel (PhraseProcessor&);
    void setNote (const std::string& id);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<std::int64_t()> splitPoint;   ///< the waveform's cursor
    std::function<void (const std::string&)> onSelect;

private:
    const atlas::NoteEvent* note() const;
    PhraseProcessor& processor;
    std::string id;
    Key rootDown { "-" }, rootUp { "+" }, octDown { "OCT -" }, octUp { "OCT +" };
    Key keyDown { "-" }, keyUp { "+" }, keyAuto { "AUTO" };
    Key include { "INCLUDED" }, audition { "AUDITION" }, split { "SPLIT" }, merge { "MERGE >" }, reanalyse { "RE-ANALYSE NOTE" };
    Key undo { "UNDO" }, redo { "REDO" }, previous { "< PREV" }, next { "NEXT >" };
};

class PhraseEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit PhraseEditor (PhraseProcessor&);
    ~PhraseEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

    static constexpr int designWidth = 1240;
    static constexpr int designHeight = 820;

    /** Tests and screenshots: the parts by role. */
    PhraseWaveform& waveform() noexcept { return content.wave; }
    NotePanel& notePanel() noexcept { return content.notes; }
    void select (const std::string& noteId) { content.select (noteId); }
    /** Pulls the audio thread's display state now (tests, screenshots). */
    void refreshNow() { timerCallback(); }

private:
    struct Content final : public juce::Component
    {
        explicit Content (PhraseProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void select (const std::string& id);
        void showMenu();
        void chooseFile();

        PhraseProcessor& processor;
        PhraseWaveform wave;
        KeyboardMap keys;
        NotePanel notes;
        Key importKey, menuKey, cancelKey, octaveDown { "<" }, octaveUp { ">" };
        Segmented mode, fill, variation, tuning;
        Knob attack, release, tone, synthetic, preserve, character, pitch, levelMatch, space, echo, velocity, master;
        juce::String statusText;
        bool dragOver = false;
        std::unique_ptr<juce::FileChooser> chooser;
    };

    void timerCallback() override;

    PhraseProcessor& processor;
    plugin::OspLookAndFeel look;
    Content content;
    ScaledContent scaler;
    juce::ComponentBoundsConstrainer constrainer;
};

} // namespace osp::andorlab
