#include "MemoLabWindow.h"

#include "Design.h"
#include "OspLookAndFeel.h"
#include "core/PitchMath.h"

#include <cmath>

namespace osp::memolab
{

using namespace osp::plugin;

namespace
{
    constexpr int listWidth = 320, gutter = 16;
    const juce::Colour light { 0xffeee8dd };

    juce::String clock (double seconds)
    {
        if (! (seconds >= 0.0))
            seconds = 0.0;
        const int m = static_cast<int> (seconds / 60.0);
        return juce::String (m) + ":" + juce::String (seconds - 60.0 * m, 2).paddedLeft ('0', 5);
    }

    juce::String dateText (const juce::Time& t)
    {
        return t.toMilliseconds() > 0 ? t.formatted ("%d %b %Y, %H:%M") : juce::String();
    }
}

//==============================================================================
void WaveformView::setAudio (std::shared_ptr<const DecodedAudio> a)
{
    audio = std::move (a);
    chosen = {};
    playhead = -1;
    peaks.clear();
    if (audio != nullptr)
    {
        // Peaks once per recording (2048 bins); drawing reads only these.
        const int bins = 2048, frames = audio->buffer.getNumSamples();
        peaks.resize (bins, { 0.0f, 0.0f });
        for (int c = 0; c < audio->buffer.getNumChannels(); ++c)
        {
            const float* data = audio->buffer.getReadPointer (c);
            for (int b = 0; b < bins; ++b)
            {
                const int s0 = static_cast<int> (static_cast<std::int64_t> (b) * frames / bins), s1 = static_cast<int> (static_cast<std::int64_t> (b + 1) * frames / bins);
                auto& [lo, hi] = peaks[static_cast<std::size_t> (b)];
                for (int s = s0; s < s1; ++s)
                {
                    lo = std::min (lo, data[s]);
                    hi = std::max (hi, data[s]);
                }
            }
        }
    }
    repaint();
}

void WaveformView::setMessage (const juce::String& m)
{
    message = m;
    repaint();
}

void WaveformView::setPlayhead (std::int64_t frame)
{
    if (frame != playhead)
    {
        playhead = frame;
        repaint();
    }
}

void WaveformView::setSelection (juce::Range<std::int64_t> range)
{
    chosen = range;
    repaint();
    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

juce::Rectangle<float> WaveformView::plot() const
{
    return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedBottom (14.0f);
}

float WaveformView::xFor (std::int64_t frame) const
{
    const auto p = plot();
    const auto total = audio != nullptr ? std::max (1, audio->buffer.getNumSamples()) : 1;
    return p.getX() + p.getWidth() * static_cast<float> (static_cast<double> (frame) / total);
}

std::int64_t WaveformView::frameFor (float x) const
{
    const auto p = plot();
    const auto total = audio != nullptr ? audio->buffer.getNumSamples() : 0;
    const double t = std::clamp (static_cast<double> ((x - p.getX()) / std::max (1.0f, p.getWidth())), 0.0, 1.0);
    return static_cast<std::int64_t> (std::llround (t * total));
}

void WaveformView::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    draw::well (g, bounds, 10.0f, colour::wellA);
    const auto p = plot();
    if (audio == nullptr || peaks.empty())
    {
        g.setFont (type::popupLabel (13.0f));
        g.setColour (colour::wellText.brighter (0.2f));
        g.drawFittedText (message, p.reduced (20.0f).toNearestInt(), juce::Justification::centred, 3);
        return;
    }
    const auto rate = audio->info.sampleRate;
    const auto total = audio->buffer.getNumSamples();
    // Seconds along the bottom (a label every 100 px at most, round values).
    {
        const double seconds = total / rate;
        const double target = seconds / std::max (1.0, static_cast<double> (p.getWidth()) / 100.0);
        double step = 0.1;
        for (double s : { 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0 })
            if (s >= target)
            {
                step = s;
                break;
            }
        g.setFont (type::popupLabel (10.5f));
        for (double t = step; t < seconds; t += step)
        {
            const float x = xFor (static_cast<std::int64_t> (t * rate));
            g.setColour (colour::wellGrid.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (std::floor (x), p.getY(), 1.0f, p.getHeight()));
            g.setColour (colour::wellText);
            g.drawText (step < 1.0 ? juce::String (t, 1) + " s" : juce::String (juce::roundToInt (t)) + " s",
                        juce::Rectangle<float> (x - 24.0f, p.getBottom() + 2.0f, 48.0f, 12.0f), juce::Justification::centred, false);
        }
    }
    // The chosen passage: a quiet field and its two edges.
    if (! chosen.isEmpty())
    {
        const float x0 = xFor (chosen.getStart()), x1 = xFor (chosen.getEnd());
        g.setColour (colour::identity (0).arc.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (x0, p.getY(), x1 - x0, p.getHeight()));
        g.setColour (colour::identity (0).arc);
        g.fillRect (juce::Rectangle<float> (std::floor (x0), p.getY(), 1.5f, p.getHeight()));
        g.fillRect (juce::Rectangle<float> (std::floor (x1) - 0.5f, p.getY(), 1.5f, p.getHeight()));
    }
    // The waveform: one continuous shape (brighter inside the passage).
    const float mid = p.getCentreY(), half = 0.47f * p.getHeight();
    const int columns = std::max (1, static_cast<int> (p.getWidth()));
    for (int c = 0; c < columns; ++c)
    {
        const auto b0 = static_cast<std::size_t> (static_cast<std::int64_t> (c) * static_cast<std::int64_t> (peaks.size()) / columns);
        const auto b1 = std::max (b0 + 1, static_cast<std::size_t> (static_cast<std::int64_t> (c + 1) * static_cast<std::int64_t> (peaks.size()) / columns));
        float lo = 0.0f, hi = 0.0f;
        for (auto b = b0; b < std::min (b1, peaks.size()); ++b)
        {
            lo = std::min (lo, peaks[b].first);
            hi = std::max (hi, peaks[b].second);
        }
        const float x = p.getX() + static_cast<float> (c);
        const auto frame = static_cast<std::int64_t> (static_cast<double> (c) / columns * total);
        const bool inside = chosen.isEmpty() || chosen.contains (frame);
        g.setColour (colour::identity (0).wave.withAlpha (inside ? 0.9f : 0.38f));
        g.fillRect (juce::Rectangle<float> (x, mid - hi * half, 1.0f, std::max (1.0f, (hi - lo) * half)));
    }
    if (playhead >= 0)
    {
        g.setColour (light);
        g.fillRect (juce::Rectangle<float> (std::floor (xFor (playhead)), p.getY() - 4.0f, 1.5f, p.getHeight() + 8.0f));
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;
    dragFrom = frameFor (e.position.x);
    dragging = false;
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr || e.getDistanceFromDragStart() < 4)
        return;
    dragging = true;
    const auto now = frameFor (e.position.x);
    chosen = { std::min (dragFrom, now), std::max (dragFrom, now) };
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (audio == nullptr)
        return;
    if (dragging)
    {
        if (onSelectionChanged != nullptr)
            onSelectionChanged();
    }
    else if (onSeek != nullptr)
        onSeek (dragFrom);
    dragging = false;
}

//==============================================================================
struct MemoLabComponent::ListModel final : juce::ListBoxModel
{
    explicit ListModel (MemoLabComponent& o) : owner (o) {}
    int getNumRows() override { return static_cast<int> (owner.shown.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected) override
    {
        using namespace design;
        if (row < 0 || row >= getNumRows())
            return;
        const auto& r = owner.shown[static_cast<std::size_t> (row)];
        const bool ready = r.state == Recording::State::ready;
        const auto box = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height)).reduced (6.0f, 2.0f);
        if (isSelected)
        {
            g.setColour (colour::text.withAlpha (0.07f));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (colour::identity (0).arc);
            g.fillRoundedRectangle (box.withWidth (3.0f).reduced (0.0f, 8.0f), 1.5f);
        }
        g.setColour (colour::text.withAlpha (ready ? 0.92f : 0.5f));
        g.setFont (type::popupValue (14.0f));
        g.drawText (r.title, box.reduced (14.0f, 0.0f).withTrimmedBottom (box.getHeight() * 0.48f), juce::Justification::bottomLeft, true);
        g.setFont (type::popupLabel (11.5f));
        g.setColour (colour::textSecondary.withAlpha (ready ? 1.0f : 0.8f));
        // A title made from the date already says when; then only how long.
        juce::String detail = r.title == titleFromFileName (r.file.getFileNameWithoutExtension()) && dateFromFileName (r.file.getFileNameWithoutExtension())
                                  ? juce::String()
                                  : dateText (r.date);
        if (r.seconds > 0.0)
            detail << (detail.isEmpty() ? "" : "   ") << clock (r.seconds);
        if (! ready)
            detail = r.state == Recording::State::notDownloaded ? juce::String ("Not downloaded") : (r.state == Recording::State::changing ? "Still syncing" : "Cannot be read");
        g.drawText (detail, box.reduced (14.0f, 0.0f).withTrimmedTop (box.getHeight() * 0.56f), juce::Justification::topLeft, true);
    }

    void selectedRowsChanged (int row) override { owner.selectRecording (row); }
    MemoLabComponent& owner;
};

//==============================================================================
MemoLabComponent::MemoLabComponent (OspAudioProcessor& p, juce::AudioDeviceManager* d, SourceSettings& s)
    : osp (p), devices (d), settings (s), memo (p), keyboard (p.keyboardState)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    model = std::make_unique<ListModel> (*this);

    search.setTextToShowWhenEmpty ("Search recordings", design::colour::textMicro);
    search.onTextChange = [this] { applyFilter(); };
    search.setFont (type::popupLabel (14.0f));
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (10, 0);
    addAndMakeVisible (search);
    for (auto* b : { &refreshButton, &addButton, &foldersButton, &audioButton, &playButton, &wholeButton, &clearButton, &oneShotButton, &mapNotesButton,
                     &rootButton, &saveButton })
        addAndMakeVisible (*b);
    refreshButton.onClick = [this] { refresh(); };
    addButton.onClick = [this] { addFolder(); };
    foldersButton.onClick = [this] { showFolders(); };
    audioButton.onClick = [this] { showAudioSettings(); };
    audioButton.setEnabled (devices != nullptr);
    playButton.onClick = [this] { togglePreview(); };
    wholeButton.onClick = [this] {
        if (decoded != nullptr)
            wave.setSelection ({ 0, decoded->buffer.getNumSamples() });
    };
    clearButton.onClick = [this] { wave.setSelection ({}); };
    oneShotButton.onClick = [this] { convert (MemoSession::Mode::oneShot); };
    mapNotesButton.onClick = [this] { convert (MemoSession::Mode::mapNotes); };
    rootButton.onClick = [this] { showRootMenu(); };
    saveButton.onClick = [this] { saveInstrument(); };
    oneShotButton.setTooltip ("The passage as one sample, played across the keyboard");
    mapNotesButton.setTooltip ("Find the notes of a sung or played phrase: each becomes its own key");

    list.setModel (model.get());
    list.setRowHeight (52);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    wave.onSeek = [this] (std::int64_t frame) {
        wave.setSelection ({});
        if (previewPlayer.isPlaying())
            previewPlayer.play ({}, frame);
        wave.setPlayhead (frame);
    };
    wave.onSelectionChanged = [this] { updateButtons(); repaint(); };
    addAndMakeVisible (wave);

    keyboard.tintOf = [this] (int note) { return keyTints[static_cast<std::size_t> (std::clamp (note, 0, 127))]; };
    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (36);
    keyboard.setKeyWidth (22.0f);
    keyboard.setWantsKeyboardFocus (true);
    addChildComponent (keyboard);
    osp.keyboardState.addListener (this);

    // First launch: the Mac's Voice Memos folder is approved once (it can be removed); what
    // happens there is reported, never assumed.
    if (! settings.offeredVoiceMemos())
    {
        settings.addFolder (voiceMemosFolder());
        settings.setOfferedVoiceMemos();
    }
    updateButtons();
    refresh();
    startTimerHz (30);
}

MemoLabComponent::~MemoLabComponent()
{
    stopTimer();
    osp.keyboardState.removeListener (this);
    cancelJobs.store (true);
    pool.removeAllJobs (true, 30000);
    previewPlayer.setAudio (nullptr);
}

//==============================================================================
void MemoLabComponent::refresh()
{
    const int generation = ++scanGeneration;
    scanning = true;
    ++pendingJobs;
    const auto roots = settings.folders();
    pool.addJob ([this, roots, generation] {
        auto result = std::make_shared<ScanResult> (scanFolders (roots, cancelJobs));
        post ([this, result, generation] { scanFinished (std::move (*result), generation); });
    });
    repaint();
}

void MemoLabComponent::post (std::function<void()> f)
{
    const std::lock_guard<std::mutex> lock (inboxLock);
    inbox.push_back (std::move (f));
}

void MemoLabComponent::drainInbox()
{
    std::vector<std::function<void()>> now;
    {
        const std::lock_guard<std::mutex> lock (inboxLock);
        now.swap (inbox);
    }
    for (auto& f : now)
        f();
}

void MemoLabComponent::scanFinished (ScanResult result, int generation)
{
    --pendingJobs;
    if (generation != scanGeneration)
        return;
    scanning = false;
    const auto keep = selected >= 0 && selected < static_cast<int> (shown.size()) ? shown[static_cast<std::size_t> (selected)].file : juce::File();
    all = std::move (result.recordings);
    folderReports = std::move (result.folders);
    applyFilter();
    for (std::size_t i = 0; i < shown.size(); ++i)
        if (shown[i].file == keep)
        {
            list.selectRow (static_cast<int> (i), true, true);
            selected = static_cast<int> (i);
        }
    repaint();
}

void MemoLabComponent::applyFilter()
{
    const auto query = search.getText().trim();
    shown.clear();
    for (const auto& r : all)
        if (query.isEmpty() || r.title.containsIgnoreCase (query) || r.file.getFileName().containsIgnoreCase (query))
            shown.push_back (r);
    selected = -1;
    list.updateContent();
    list.deselectAllRows();
    repaint();
}

void MemoLabComponent::selectRecording (int index)
{
    if (index < 0 || index >= static_cast<int> (shown.size()))
        return;
    if (list.getSelectedRow() != index)
        list.selectRow (index, true, true);
    selected = index;
    previewPlayer.setAudio (nullptr);
    decoded = nullptr;
    wave.setAudio (nullptr);
    const auto& r = shown[static_cast<std::size_t> (index)];
    if (r.state != Recording::State::ready)
    {
        wave.setMessage (r.problem);
        updateButtons();
        return;
    }
    // Decoded only now, when chosen (never the whole archive), off the message thread.
    wave.setMessage (juce::String::fromUTF8 ("Reading\xe2\x80\xa6"));
    const int generation = ++decodeGeneration;
    ++pendingJobs;
    const auto file = r.file;
    pool.addJob ([this, file, generation] {
        auto audio = std::make_shared<DecodedAudio>();
        juce::String error;
        if (cancelJobs.load() || ! decode (file, *audio, error))
            audio.reset();
        post ([this, audio, error, generation] { decodeFinished (audio, error, generation); });
    });
    updateButtons();
}

void MemoLabComponent::decodeFinished (std::shared_ptr<DecodedAudio> audio, juce::String error, int generation)
{
    --pendingJobs;
    if (generation != decodeGeneration)
        return;
    decoded = audio;
    previewPlayer.setAudio (audio);
    wave.setAudio (audio);
    if (audio == nullptr)
        wave.setMessage (error);
    updateButtons();
    repaint();
}

bool MemoLabComponent::waitForIdle (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        juce::Thread::sleep (10);
        osp.pollLoads();
        tick();
        const auto stage = memo.status().stage;
        if (pendingJobs == 0 && ! memo.busy() && stage != MemoSession::Status::Stage::building && stage != MemoSession::Status::Stage::playable)
            return true;
    }
    return false;
}

//==============================================================================
void MemoLabComponent::togglePreview()
{
    if (decoded == nullptr)
        return;
    if (previewPlayer.isPlaying())
    {
        previewPlayer.stop();
        return;
    }
    osp.keyboardState.allNotesOff (0);   // one thing heard at a time
    notePlayed.store (false);
    previewPlayer.play (wave.selection());
}

void MemoLabComponent::convert (MemoSession::Mode mode)
{
    if (decoded == nullptr || selected < 0)
        return;
    previewPlayer.stop();
    juce::String error;
    if (! memo.convert (shown[static_cast<std::size_t> (selected)], *decoded, wave.selection(), mode, error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot make the instrument", error);
    noteMap.reset();
    updateInstrumentArea();
    updateButtons();
    repaint();
}

void MemoLabComponent::timerCallback()
{
    drainInbox();
    // A played note silences the preview (the keyboard state's listener may run on the audio thread).
    if (notePlayed.exchange (false) && previewPlayer.isPlaying())
        previewPlayer.stop();
    wave.setPlayhead (previewPlayer.position());
    playButton.setButtonText (previewPlayer.isPlaying() ? "Stop" : "Play");

    const auto before = memo.status();
    const auto& now = memo.update();
    if (now.stage != before.stage || now.text != before.text || now.atlas != before.atlas)
    {
        updateInstrumentArea();
        updateButtons();
        repaint();
    }
    if (noteMap != nullptr)
        noteMap->refresh();
    // The keys show the map (the plugin's light strip): recorded, filled, silent.
    const auto atlas = osp.layerAtlas (0);
    if (atlas != tintedAtlas)
    {
        tintedAtlas = atlas;
        const auto instrument = osp.currentInstrument (0);
        const InstrumentSet* set = instrument != nullptr && instrument->atlas != nullptr ? instrument->set.get() : nullptr;
        keyTints = atlas != nullptr && memo.status().atlas ? NoteMapEditor::keyMap (*atlas, set, false) : std::array<OspKeyboard::KeyTint, 128> {};
        keyboard.repaint();
    }
}

void MemoLabComponent::updateInstrumentArea()
{
    const auto& s = memo.status();
    const bool show = s.canPlay();
    keyboard.setVisible (show);
    saveButton.setVisible (show);
    rootButton.setVisible (show && ! s.atlas);
    if (show && ! s.atlas)
        rootButton.setButtonText ("Root " + juce::String (midiNoteName (static_cast<int> (std::lround (osp.effectiveRootMidi (0))))) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    if (show && s.atlas && noteMap == nullptr)
    {
        // The plugin's own NOTE MAP (pitch, exclude, audition, split, merge; one undo each).
        noteMap = std::make_unique<NoteMapEditor> (osp, 0);
        noteMap->onClose = [] {};
        noteMap->onShowEq = [] (bool) {};
        addAndMakeVisible (*noteMap);
    }
    else if (! (show && s.atlas))
        noteMap.reset();
    if (show && ! keyboard.hasKeyboardFocus (true) && ! search.hasKeyboardFocus (true))
        keyboard.grabKeyboardFocus();
    resized();
}

void MemoLabComponent::updateButtons()
{
    const bool ready = decoded != nullptr;
    const bool working = memo.status().stage == MemoSession::Status::Stage::building;
    playButton.setEnabled (ready);
    wholeButton.setEnabled (ready);
    clearButton.setEnabled (ready && ! wave.selection().isEmpty());
    oneShotButton.setEnabled (ready && ! working);
    mapNotesButton.setEnabled (ready && ! working);
    saveButton.setEnabled (memo.status().stage == MemoSession::Status::Stage::ready || memo.status().stage == MemoSession::Status::Stage::unsuitable);
}

juce::String MemoLabComponent::selectionText() const
{
    if (decoded == nullptr)
        return {};
    const double rate = decoded->info.sampleRate;
    const auto total = decoded->buffer.getNumSamples();
    const auto sel = wave.selection();
    juce::String text = juce::String::fromUTF8 ("Recording ") + clock (total / rate);
    if (decoded->truncated)
        text << " (its first 15 minutes)";
    if (! sel.isEmpty())
        text << juce::String::fromUTF8 ("   \xc2\xb7   Passage ") << clock (sel.getStart() / rate) << juce::String::fromUTF8 (" \xe2\x80\x93 ") << clock (sel.getEnd() / rate)
             << "  (" << juce::String (sel.getLength() / rate, 2) << " s)";
    else
        text << juce::String::fromUTF8 ("   \xc2\xb7   The whole recording is used (drag over the waveform to choose a passage)");
    return text;
}

//==============================================================================
void MemoLabComponent::showFolders()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Approved folders (read only)");
    int id = 1;
    for (const auto& report : folderReports)
    {
        const bool memos = report.folder == voiceMemosFolder();
        juce::PopupMenu sub;
        sub.addItem (id + 1000, report.folder.getFullPathName(), false);
        sub.addItem (id + 2000, report.message, false);
        if (report.readable)
            sub.addItem (id + 3000, "Show in Finder");
        sub.addItem (id, "Remove from Memo Lab");
        menu.addSubMenu ((memos ? juce::String ("Voice Memos") : report.folder.getFileName()) + "   " + (report.readable ? juce::String (report.recordings) : juce::String ("!")), sub);
        ++id;
    }
    if (! settings.folders().contains (voiceMemosFolder()))
        menu.addItem (500, "Add the Voice Memos folder again");
    menu.addItem (501, juce::String::fromUTF8 ("Add Folder\xe2\x80\xa6"));
    juce::Component::SafePointer<MemoLabComponent> safe (this);
    const auto reports = folderReports;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (foldersButton), [safe, reports] (int result) {
        if (safe == nullptr || result == 0)
            return;
        if (result == 500)
        {
            safe->settings.addFolder (voiceMemosFolder());
            safe->refresh();
        }
        else if (result == 501)
            safe->addFolder();
        else if (result > 3000 && result - 3001 < static_cast<int> (reports.size()))
            reports[static_cast<std::size_t> (result - 3001)].folder.revealToUser();
        else if (result >= 1 && result - 1 < static_cast<int> (reports.size()))
        {
            safe->settings.removeFolder (reports[static_cast<std::size_t> (result - 1)].folder);
            safe->refresh();
        }
    });
}

void MemoLabComponent::addFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Choose a folder of recordings (Memo Lab only reads it)",
                                                        juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    juce::Component::SafePointer<MemoLabComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe, chooser] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        safe->settings.addFolder (fc.getResult());
        safe->refresh();
    });
}

void MemoLabComponent::showAudioSettings()
{
    if (devices == nullptr)
        return;
    auto* selector = new juce::AudioDeviceSelectorComponent (*devices, 0, 0, 2, 2, true, false, true, false);
    selector->setSize (520, 420);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio and MIDI";
    options.dialogBackgroundColour = design::colour::housingTop;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MemoLabComponent::showRootMenu()
{
    const int detected = static_cast<int> (std::lround (osp.effectiveRootMidi (0)));
    juce::PopupMenu menu;
    menu.addItem (1, "As detected", true, ! osp.rootOverride (0).has_value());
    menu.addSeparator();
    for (int n = detected + 12; n >= detected - 12; --n)
        menu.addItem (1000 + n, juce::String (midiNoteName (n)), true, n == detected && osp.rootOverride (0).has_value());
    juce::Component::SafePointer<MemoLabComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (rootButton), [safe] (int result) {
        if (safe == nullptr || result == 0)
            return;
        safe->osp.changeRootOverride (result == 1 ? std::optional<double> {} : std::optional<double> (result - 1000), 0);
        safe->updateInstrumentArea();
    });
}

void MemoLabComponent::saveInstrument()
{
    auto folder = OspAudioProcessor::instrumentFolder();
    folder.createDirectory();
    auto chooser = std::make_shared<juce::FileChooser> ("Save the instrument (it opens in ANDOR OSP)", folder.getChildFile (memo.suggestedFileName()),
                                                        juce::String ("*") + OspAudioProcessor::instrumentExtension);
    juce::Component::SafePointer<MemoLabComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, chooser] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File())
            return;
        juce::String error;
        const auto file = fc.getResult().withFileExtension (OspAudioProcessor::instrumentExtension);
        if (safe->memo.save (file, error))
        {
            const bool listed = file.isAChildOf (OspAudioProcessor::instrumentFolder());
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Saved",
                                                    "In ANDOR OSP, open the main menu: "
                                                        + (listed ? "Instruments > " + file.getFileNameWithoutExtension() + juce::String (" (or Open instrument file...).")
                                                                  : juce::String ("Open instrument file..., then choose it."))
                                                        + "\n\n" + file.getFullPathName());
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "The instrument was not saved", error);
    });
}

bool MemoLabComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey && ! search.hasKeyboardFocus (false))
    {
        togglePreview();
        return true;
    }
    return false;
}

//==============================================================================
void MemoLabComponent::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds();
    g.setGradientFill (juce::ColourGradient (colour::housingTop, 0.0f, 0.0f, colour::housingBottom, 0.0f, static_cast<float> (getHeight()), false));
    g.fillAll();
    // The recordings' side: a quiet card.
    const auto side = bounds.withWidth (listWidth).reduced (gutter, gutter).toFloat();
    draw::raised (g, side, 12.0f, colour::cardTop, colour::cardBottom, 1.2f);

    // Identity.
    g.setColour (colour::text);
    g.setFont (fonts::make (22.0f, fonts::Weight::displayBold, 0.01f));
    g.drawText ("ANDOR/MEMO LAB", juce::Rectangle<int> (listWidth + 8, gutter, 400, 28), juce::Justification::centredLeft);
    g.setColour (colour::textMicro);
    g.setFont (type::popupTitle (11.5f));
    g.drawText ("RECORDINGS TO INSTRUMENTS", juce::Rectangle<int> (listWidth + 10, gutter + 28, 400, 14), juce::Justification::centredLeft);

    // The list's empty state: what happened in each folder, plainly.
    if (shown.empty())
    {
        auto r = list.getBounds().reduced (18, 10);
        g.setFont (type::popupLabel (12.5f));
        g.setColour (colour::textSecondary);
        juce::String text = scanning ? juce::String::fromUTF8 ("Looking for recordings\xe2\x80\xa6") : juce::String ("No recordings found.");
        if (! scanning)
        {
            for (const auto& report : folderReports)
                text << "\n\n" << (report.folder == voiceMemosFolder() ? juce::String ("Voice Memos") : report.folder.getFileName()) << ": " << report.message;
            if (folderReports.empty())
                text << "\n\nAdd a folder of recordings.";
            text << "\n\nRecordings from your iPhone appear here once Voice Memos has synced them to this Mac (iCloud). If the Voice Memos folder cannot be "
                    "read, choose Add Folder and pick a folder of recordings instead.";
        }
        g.drawFittedText (text, r, juce::Justification::topLeft, 30, 1.0f);
    }

    // Under the waveform: its times; under the buttons: what is happening.
    g.setFont (type::popupLabel (12.5f));
    g.setColour (colour::textSecondary);
    g.drawText (selectionText(), wholeButton.getBounds().withX (playButton.getRight() + 12).withRight (wholeButton.getX() - 12), juce::Justification::centredLeft, true);

    const auto& s = memo.status();
    if (s.stage != MemoSession::Status::Stage::idle)
    {
        const bool bad = s.stage == MemoSession::Status::Stage::failed || s.stage == MemoSession::Status::Stage::unsuitable;
        g.setColour (bad ? juce::Colour (0xffb5532a) : colour::text.withAlpha (0.85f));
        g.setFont (type::popupValue (13.5f));
        juce::String text = s.text;
        if (s.stage == MemoSession::Status::Stage::building)
            text << juce::String::fromUTF8 ("\xe2\x80\xa6");
        g.drawFittedText (text, juce::Rectangle<int> (mapNotesButton.getRight() + 16, mapNotesButton.getY(), getWidth() - mapNotesButton.getRight() - 32, mapNotesButton.getHeight()),
                          juce::Justification::centredLeft, 2);
    }
    if (s.canPlay())
    {
        g.setColour (colour::text);
        g.setFont (type::popupValue (15.0f));
        const auto header = juce::Rectangle<int> (listWidth, saveButton.getY(), saveButton.getX() - listWidth - 12, saveButton.getHeight());
        g.drawText (memo.instrumentName(), header.withTrimmedRight (header.getWidth() / 2), juce::Justification::centredLeft, true);
        g.setFont (type::popupLabel (12.0f));
        g.setColour (colour::textSecondary);
        g.drawText ((s.atlas ? juce::String ("Note Atlas") : juce::String ("One Shot")) + juce::String::fromUTF8 ("  \xc2\xb7  from ") + memo.sourceName()
                        + juce::String::fromUTF8 ("  \xc2\xb7  play from the keyboard or a MIDI controller"),
                    header.withTrimmedLeft (header.getWidth() / 2 - 40).withTrimmedRight (rootButton.isVisible() ? rootButton.getWidth() + 8 : 0), juce::Justification::centredLeft, true);
    }
}

void MemoLabComponent::resized()
{
    auto side = getLocalBounds().withWidth (listWidth).reduced (gutter, gutter).reduced (10, 12);
    auto top = side.removeFromTop (34);
    search.setBounds (top);
    side.removeFromTop (8);
    auto row = side.removeFromBottom (30);
    const int w = (row.getWidth() - 8) / 3;
    refreshButton.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (4);
    foldersButton.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (4);
    addButton.setBounds (row);
    side.removeFromBottom (8);
    list.setBounds (side);

    auto main = getLocalBounds().withTrimmedLeft (listWidth).reduced (gutter, gutter);
    audioButton.setBounds (main.removeFromTop (44).removeFromRight (130).withSizeKeepingCentre (130, 30));
    main.removeFromTop (6);
    const bool instrument = memo.status().canPlay();
    const int waveHeight = instrument ? (noteMap != nullptr ? 150 : std::max (220, main.getHeight() - 300)) : std::max (220, main.getHeight() - 140);
    wave.setBounds (main.removeFromTop (waveHeight));
    main.removeFromTop (8);
    auto transport = main.removeFromTop (30);
    playButton.setBounds (transport.removeFromLeft (80));
    clearButton.setBounds (transport.removeFromRight (124));
    transport.removeFromRight (6);
    wholeButton.setBounds (transport.removeFromRight (132));
    main.removeFromTop (12);
    auto make = main.removeFromTop (40);
    oneShotButton.setBounds (make.removeFromLeft (140));
    make.removeFromLeft (8);
    mapNotesButton.setBounds (make.removeFromLeft (140));
    main.removeFromTop (16);
    if (! instrument)
        return;
    auto head = main.removeFromTop (32);
    saveButton.setBounds (head.removeFromRight (170));
    head.removeFromRight (8);
    rootButton.setBounds (head.removeFromRight (110));
    main.removeFromTop (8);
    if (noteMap == nullptr)
    {
        keyboard.setBounds (main.removeFromTop (std::min (110, main.getHeight())));
        return;
    }
    keyboard.setBounds (main.removeFromBottom (std::min (110, main.getHeight() / 2)));
    main.removeFromBottom (10);
    noteMap->setBounds (main);
}

} // namespace osp::memolab
