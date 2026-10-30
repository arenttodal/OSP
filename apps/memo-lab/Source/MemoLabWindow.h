#pragma once

#include "InstrumentFrame.h"
#include "MemoSession.h"
#include "NoteMapEditor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <mutex>
#include <memory>

namespace osp::memolab
{

using plugin::NoteMapEditor;
using plugin::OspKeyboard;

/** The selected recording as one waveform: click to seek, drag to choose a passage. */
class WaveformView final : public juce::Component
{
public:
    std::function<void (std::int64_t frame)> onSeek;
    std::function<void()> onSelectionChanged;

    void setAudio (std::shared_ptr<const DecodedAudio> audio);
    void setMessage (const juce::String& message);
    void setPlayhead (std::int64_t frame);
    /** Empty: none (the whole recording is used). */
    juce::Range<std::int64_t> selection() const noexcept { return chosen; }
    void setSelection (juce::Range<std::int64_t> range);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const;
    float xFor (std::int64_t frame) const;
    std::int64_t frameFor (float x) const;

    std::shared_ptr<const DecodedAudio> audio;
    std::vector<std::pair<float, float>> peaks;   ///< min / max per bin, the whole recording
    juce::Range<std::int64_t> chosen;
    std::int64_t playhead = -1, dragFrom = 0;
    bool dragging = false;
    juce::String message { "Choose a recording" };
};

/**
    ANDOR Memo Lab's window: recordings on the left, the chosen one's waveform and passage in
    the middle, ONE SHOT / MAP NOTES below it, then the instrument (its notes, the keyboard,
    Save Instrument). The processor is ANDOR OSP's own; `devices` may be null (tests).
*/
class MemoLabComponent final : public juce::Component, private juce::Timer, private juce::MidiKeyboardState::Listener
{
public:
    MemoLabComponent (OspAudioProcessor& processor, juce::AudioDeviceManager* devices, SourceSettings& settings);
    ~MemoLabComponent() override;

    PreviewPlayer& preview() noexcept { return previewPlayer; }
    MemoSession& session() noexcept { return memo; }

    /** Scans the approved folders (on a background thread). */
    void refresh();
    /** Tests and screenshots: the recordings as listed, choosing one, waiting for work. */
    const std::vector<Recording>& recordings() const noexcept { return shown; }
    void selectRecording (int index);
    bool waitForIdle (int timeoutMs);
    WaveformView& waveform() noexcept { return wave; }
    void convert (MemoSession::Mode mode);
    void tick() { timerCallback(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct ListModel;
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int, float) override { notePlayed.store (true); }
    void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override {}
    /** Background jobs hand their results here; the timer runs them on the message thread. */
    void post (std::function<void()> f);
    void drainInbox();
    void applyFilter();
    void scanFinished (ScanResult result, int generation);
    void decodeFinished (std::shared_ptr<DecodedAudio> audio, juce::String error, int generation);
    void togglePreview();
    void showFolders();
    void addFolder();
    void showAudioSettings();
    void showRootMenu();
    void saveInstrument();
    void updateInstrumentArea();
    void updateButtons();
    juce::String selectionText() const;

    OspAudioProcessor& osp;
    juce::AudioDeviceManager* devices;
    SourceSettings& settings;
    MemoSession memo;
    PreviewPlayer previewPlayer;
    juce::ThreadPool pool { 2 };
    std::unique_ptr<ListModel> model;

    juce::TextEditor search;
    juce::TextButton refreshButton { "Refresh" }, addButton { juce::String::fromUTF8 ("Add Folder\xe2\x80\xa6") }, foldersButton { "Folders" },
        audioButton { "Audio / MIDI" };
    juce::ListBox list;
    WaveformView wave;
    juce::TextButton playButton { "Play" }, wholeButton { "Whole recording" }, clearButton { "Clear selection" };
    juce::TextButton oneShotButton { "ONE SHOT" }, mapNotesButton { "MAP NOTES" };
    juce::TextButton rootButton, saveButton { juce::String::fromUTF8 ("Save Instrument\xe2\x80\xa6") };
    OspKeyboard keyboard;
    std::array<OspKeyboard::KeyTint, 128> keyTints {};
    std::unique_ptr<NoteMapEditor> noteMap;
    std::shared_ptr<const atlas::NoteAtlas> tintedAtlas;

    std::vector<Recording> all, shown;
    std::vector<FolderReport> folderReports;
    int selected = -1, scanGeneration = 0, decodeGeneration = 0, pendingJobs = 0;
    std::shared_ptr<const DecodedAudio> decoded;
    juce::String sourceProblem;
    std::atomic<bool> notePlayed { false }, cancelJobs { false };
    std::mutex inboxLock;
    std::vector<std::function<void()>> inbox;
    bool scanning = false;
};

} // namespace osp::memolab
