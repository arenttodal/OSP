#include "LibraryCapture.h"

#include "Design.h"
#include "OspLookAndFeel.h"
#include "library/Classifier.h"
#include "library/FolderScan.h"
#include "library/Inbox.h"
#include "library/Slicer.h"

namespace osp::plugin
{

namespace
{
    namespace colour = design::colour;

    juce::String lengthOf (double seconds)
    {
        if (seconds <= 0.0)
            return {};
        if (seconds < 60.0)
            return juce::String (seconds, seconds < 10.0 ? 1 : 0) + " s";
        return juce::String (static_cast<int> (seconds) / 60) + ":" + juce::String (static_cast<int> (seconds) % 60).paddedLeft ('0', 2);
    }

    juce::String className (const juce::String& key)
    {
        if (const auto c = library::soundClassFrom (key.toStdString()))
            return library::displayName (*c);
        return key.isEmpty() ? juce::String ("Not analysed yet") : key;
    }

    /** The colour a classification chip carries (never the only sign: the chip has words). */
    juce::Colour classColour (const juce::String& key)
    {
        if (key == "likely-one-shot")
            return colour::macro (colour::Macro::character);
        if (key == "possible-one-shot")
            return colour::macro (colour::Macro::drive);
        if (key == "multiple-events")
            return colour::macro (colour::Macro::movement);
        if (key == "likely-loop")
            return colour::macro (colour::Macro::space);
        if (key == "long-recording")
            return colour::macro (colour::Macro::life);
        return colour::textMicro;
    }

    class AccentButton final : public juce::Button
    {
    public:
        explicit AccentButton (const juce::String& label) : juce::Button (label) { setTitle (label); }
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f, 1.5f);
            const float lift = isEnabled() ? (down ? 0.92f : (highlighted ? 1.06f : 1.0f)) : 1.0f;
            g.setGradientFill (juce::ColourGradient (colour::accentTop.withMultipliedBrightness (lift), r.getX(), r.getY(),
                                                     colour::accentBottom.withMultipliedBrightness (lift), r.getX(), r.getBottom(), false));
            g.setOpacity (isEnabled() ? 1.0f : 0.4f);
            g.fillRoundedRectangle (r, std::min (9.0f, r.getHeight() * 0.24f));
            g.setColour (juce::Colours::white.withAlpha (0.96f));
            g.setFont (type::button (std::min (17.0f, r.getHeight() * 0.42f)));
            g.drawText (getButtonText(), r.toNearestInt(), juce::Justification::centred, false);
        }
    };

    void drawChip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c)
    {
        g.setColour (c.withAlpha (0.16f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (c.darker (0.25f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setFont (type::micro (13.0f));
        g.drawText (text, r.toNearestInt(), juce::Justification::centred, true);
    }

    std::filesystem::path storeDirectory()
    {
        return std::filesystem::path (SampleStore::defaultDirectory().getFullPathName().toStdString());
    }
}

//==============================================================================

LibraryPanel::CandidateInspector::CandidateInspector (LibraryPanel& p) : panel (p)
{
    setTitle ("Candidate inspector");
    importButton = std::make_unique<AccentButton> ("Import to Library");
    importButton->onClick = [this] { panel.importSelectedCandidates(); };
    play.setTitle ("Preview");
    play.onClick = [this] { panel.previewSelected(); };
    kind.setTitle ("Kind");
    kind.addItem ("As analysed", 1);
    const char* keys[] { "likely-one-shot", "possible-one-shot", "multiple-events", "likely-loop", "long-recording" };
    for (int i = 0; i < 5; ++i)
        kind.addItem (className (keys[i]), i + 2);
    kind.onChange = [this, keys] {
        const int id = kind.getSelectedId();
        panel.classifySelected (id >= 2 ? juce::String (keys[id - 2]) : juce::String());
    };
    trim.setTitle ("Trim or slice");
    trim.onClick = [this] { panel.openTrimSheet(); };
    aside.onClick = [this] {
        const int i = panel.selectedRow();
        panel.setSelectedAside (i < 0 || ! panel.row (i).rejected);
    };
    for (int i = 0; i < 4; ++i)
    {
        auto& b = loadButtons[static_cast<std::size_t> (i)];
        b.setButtonText (i < 3 ? "Import to " + OspAudioProcessor::layerName (i) : juce::String ("Import, load to first empty"));
        b.setTitle (i < 3 ? "Import and load into " + OspAudioProcessor::layerName (i) : b.getButtonText());
        b.onClick = [this, i] { panel.importSelectedCandidates (i); };
        addAndMakeVisible (b);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &play, &kind, importButton.get(), &trim, &aside })
        addAndMakeVisible (c);
}

void LibraryPanel::CandidateInspector::update()
{
    const auto selected = panel.selectedRows();
    const bool one = selected.size() == 1;
    const bool any = ! selected.empty();
    const auto* r = one ? &panel.row (selected.front()) : nullptr;
    const bool imported = r != nullptr && ! r->importedAs.empty();
    const bool ready = r == nullptr || ! r->inInbox || r->state == "ready" || r->state == "failed";
    play.setVisible (one);
    kind.setVisible (one && ! r->path.isEmpty());
    trim.setVisible (one);
    importButton->setVisible (any);
    importButton->setEnabled (any && ! imported && ready);
    importButton->setButtonText (selected.size() > 1 ? "Import " + juce::String (static_cast<int> (selected.size())) + " to Library"
                                 : (imported ? juce::String ("In the Library") : juce::String ("Import to Library")));
    for (auto& b : loadButtons)
    {
        b.setVisible (one);
        b.setEnabled (one && ready);
    }
    aside.setVisible (any && ! panel.row (selected.front()).inInbox);
    aside.setButtonText (r != nullptr && r->rejected ? "Bring back" : "Set aside");
    if (r != nullptr)
    {
        const char* keys[] { "likely-one-shot", "possible-one-shot", "multiple-events", "likely-loop", "long-recording" };
        int id = 1;
        for (int k = 0; k < 5; ++k)
            if (r->classification == keys[k] && r->reason == "set by you")
                id = k + 2;
        kind.setSelectedId (id, juce::dontSendNotification);
    }
    resized();
    repaint();
}

void LibraryPanel::CandidateInspector::paint (juce::Graphics& g)
{
    design::draw::raised (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f, colour::panelTop, colour::panelBottom, 0.6f);
    const auto selected = panel.selectedRows();
    if (selected.empty())
    {
        g.setColour (colour::textMicro);
        g.setFont (type::micro (15.0f));
        g.drawFittedText (panel.scopeKey == "inbox" ? "Audio that arrives in your Inbox folders appears here. Select one to hear it, trim it and bring it in."
                                                    : "Files found in the folders you scanned. Select one (or several) to hear, sort and import them.",
                          getLocalBounds().reduced (30), juce::Justification::centred, 4);
        return;
    }
    if (selected.size() > 1)
    {
        g.setColour (colour::text);
        g.setFont (type::preset (0.82f));
        g.drawText (juce::String (static_cast<int> (selected.size())) + " selected", 16, 14, getWidth() - 32, 30, juce::Justification::centredLeft);
        g.setColour (colour::textSecondary);
        g.setFont (type::micro (14.0f));
        g.drawFittedText ("Import brings each one in with the tags its name, its folders and its sound suggest; you can change any of them later.",
                          juce::Rectangle<int> (16, 52, getWidth() - 32, 60), juce::Justification::topLeft, 3);
        return;
    }
    const auto& r = panel.row (selected.front());
    g.setColour (colour::text);
    g.setFont (type::preset (0.82f));
    g.drawText (r.name, juce::Rectangle<int> (play.getRight() + 10, 14, getWidth() - play.getRight() - 26, 30), juce::Justification::centredLeft, true);

    design::draw::well (g, wave.toFloat(), 8.0f, colour::wellA);
    const auto& slot = panel.processor.audition().slot (library::PreviewEngine::browserSlot);
    if (slot.file == juce::File (r.path) && slot.sound != nullptr)
    {
        const auto& s = *slot.sound;
        const auto area = wave.toFloat().reduced (8.0f, 10.0f);
        const float step = area.getWidth() / static_cast<float> (s.peakMax.size());
        g.setColour (colour::identity (2).wave.withAlpha (0.9f));
        for (std::size_t i = 0; i < s.peakMax.size(); ++i)
        {
            const float top = area.getCentreY() - s.peakMax[i] * area.getHeight() * 0.5f;
            const float bottom = area.getCentreY() - s.peakMin[i] * area.getHeight() * 0.5f;
            g.fillRect (juce::Rectangle<float> (area.getX() + static_cast<float> (i) * step, top, std::max (1.0f, step), std::max (1.0f, bottom - top)));
        }
        const double head = panel.processor.audition().playheadSeconds (library::PreviewEngine::browserSlot);
        if (head >= 0.0 && s.durationSeconds() > 0.0)
        {
            g.setColour (colour::accent);
            g.fillRect (juce::Rectangle<float> (area.getX() + area.getWidth() * static_cast<float> (head / s.durationSeconds()), area.getY() - 4.0f, 1.5f, area.getHeight() + 8.0f));
        }
    }
    else
    {
        g.setColour (colour::wellText);
        g.setFont (type::micro (13.0f));
        g.drawText (slot.file == juce::File (r.path) && slot.error.isNotEmpty() ? slot.error : juce::String::fromUTF8 ("Preparing\xe2\x80\xa6"), wave, juce::Justification::centred);
    }

    int y = wave.getBottom() + 12;
    const auto chip = juce::Rectangle<float> (16.0f, static_cast<float> (y), 170.0f, 24.0f);
    drawChip (g, chip, className (r.classification), classColour (r.classification));
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (13.0f));
    g.drawText (r.reason, chip.getRight() + 10.0f, chip.getY(), static_cast<float> (getWidth()) - chip.getRight() - 26.0f, 24.0f, juce::Justification::centredLeft, true);
    g.setColour (colour::textMicro);
    g.setFont (type::popupLabel (12.0f));
    g.drawText ("KIND (YOUR SAY WINS)", 16, kind.getY() - 18, 220, 16, juce::Justification::centredLeft);
    y = kind.getBottom() + 12;
    juce::StringArray lines;
    lines.add ("Length  " + lengthOf (r.seconds) + (r.sampleRate > 0.0 ? "   Rate  " + juce::String (r.sampleRate / 1000.0, 1) + " kHz" : juce::String())
               + (r.channels > 0 ? juce::String ("   ") + (r.channels == 1 ? "Mono" : "Stereo") : juce::String()));
    if (r.suggestedType.isNotEmpty())
        lines.add ("Sounds like  " + r.suggestedType);
    if (r.inInbox)
        lines.add ("Inbox  " + r.state + (r.error.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + r.error : juce::String()));
    else if (r.error.isNotEmpty())
        lines.add (r.error);
    if (! r.importedAs.empty())
        lines.add ("Already in the Library");
    lines.add (r.path);
    g.setFont (type::micro (13.5f));
    for (int i = 0; i < lines.size(); ++i)
    {
        g.setColour (i == lines.size() - 1 ? colour::textMicro : colour::textSecondary);
        g.drawText (lines[i], 16, y + i * 20, getWidth() - 32, 20, juce::Justification::centredLeft, true);
    }
}

void LibraryPanel::CandidateInspector::resized()
{
    const int w = getWidth();
    play.setBounds (14, 14, 32, 30);
    wave = { 16, 52, w - 32, 96 };
    kind.setBounds (16, wave.getBottom() + 64, w - 32, 30);
    const int bottom = getHeight() - 16;
    importButton->setBounds (16, bottom - 40, w - 32, 40);
    const int bw = (w - 32 - 12) / 3;
    for (int i = 0; i < 3; ++i)
        loadButtons[static_cast<std::size_t> (i)].setBounds (16 + i * (bw + 6), bottom - 84, bw, 32);
    loadButtons[3].setBounds (16, bottom - 122, w - 32, 32);
    trim.setBounds (16, bottom - 162, (w - 40) / 2, 32);
    aside.setBounds (24 + (w - 40) / 2, bottom - 162, (w - 40) / 2, 32);
    // With several selected: only import and set aside, at the bottom.
    if (panel.selectedRows().size() > 1)
        aside.setBounds (16, bottom - 84, w - 32, 32);
}

void LibraryPanel::CandidateInspector::mouseUp (const juce::MouseEvent& e)
{
    if (wave.contains (e.getPosition()))
        panel.previewSelected();
}

//==============================================================================
// Actions on candidates

void LibraryPanel::importSelectedCandidates (int loadIntoLayer, const juce::String& name)
{
    struct Job
    {
        std::string path;
        bool inbox = false;
        juce::String name, folder;
    };
    std::vector<Job> jobs;
    for (const int i : selectedRows())
    {
        const auto& r = row (i);
        if (r.candidate && r.importedAs.empty())
            jobs.push_back ({ r.path.toStdString(), r.inInbox, name.isNotEmpty() ? name : r.name, r.folder });
        else if (r.candidate && ! r.importedAs.empty() && loadIntoLayer >= 0)
        {
            // Already in the Library: just load it.
            loadSound (r.importedAs, loadIntoLayer < 3 ? loadIntoLayer : std::max (0, processor.firstFreeLayer()));
            return;
        }
    }
    if (jobs.empty())
        return;
    setStatus ("Importing " + juce::String (static_cast<int> (jobs.size())) + (jobs.size() == 1 ? " sound" : " sounds") + juce::String::fromUTF8 ("\xe2\x80\xa6"));
    const auto storeDir = storeDirectory();
    juce::Component::SafePointer<LibraryPanel> safe (this);
    struct Outcome
    {
        int ok = 0;
        std::vector<std::string> ids;
        juce::StringArray problems;
    };
    processor.libraryService().requestBackground<Outcome> (
        [jobs, storeDir] (library::Catalog& c) {
            Outcome out;
            for (const auto& job : jobs)
            {
                library::ImportResult result;
                if (job.inbox)
                    result = library::importInboxItem (c, storeDir, job.path, job.name.toStdString());
                else if (const auto candidate = c.scanFile (job.path))
                {
                    result = library::importCandidate (c, storeDir, *candidate, job.folder.toStdString());
                    if (result.ok && ! result.alreadyInLibrary)
                        c.rename (result.assetId, job.name.toStdString());   // the name the user gave it (else its file's)
                }
                else
                    result.error = "it is not among the scanned files any more";
                if (result.ok)
                {
                    ++out.ok;
                    out.ids.push_back (result.assetId);
                }
                else
                    out.problems.add (juce::String (job.name) + ": " + juce::String (result.error));
            }
            return out;
        },
        [safe, loadIntoLayer] (Outcome out) {
            if (safe == nullptr)
                return;
            juce::String message = "Imported " + juce::String (out.ok) + (out.ok == 1 ? " sound" : " sounds");
            if (! out.problems.isEmpty())
                message += ". Not imported: " + out.problems.joinIntoString ("; ");
            safe->setStatus (message);
            if (loadIntoLayer >= 0 && out.ids.size() == 1)
            {
                const int layer = loadIntoLayer < 3 ? loadIntoLayer : safe->processor.firstFreeLayer();
                if (layer >= 0)
                    safe->loadSound (out.ids.front(), layer);
                else
                    safe->choose ("A, B and C all hold a sound. Which one should it replace?", { "Replace A", "Replace B", "Replace C", "Cancel" },
                                  [safe, id = out.ids.front()] (int choice) {
                                      if (safe != nullptr && choice >= 0 && choice < 3)
                                          safe->loadSound (id, choice);
                                  });
            }
            safe->refresh();
        });
}

void LibraryPanel::setSelectedAside (bool aside)
{
    std::vector<std::string> paths;
    for (const int i : selectedRows())
        if (row (i).candidate && ! row (i).inInbox)
            paths.push_back (row (i).path.toStdString());
    if (paths.empty())
        return;
    edit (
        [paths, aside] (library::Catalog& c) {
            for (const auto& p : paths)
                c.setScanFileRejected (p, aside);
            return true;
        },
        aside ? juce::String ("Set aside (Set aside lists them; nothing is deleted)") : juce::String ("Brought back"));
}

void LibraryPanel::classifySelected (const juce::String& classification)
{
    std::vector<std::string> hashes;
    for (const int i : selectedRows())
        if (! row (i).contentHash.empty())
            hashes.push_back (row (i).contentHash);
    if (hashes.empty())
    {
        setStatus ("It has not been analysed yet: try again when the scan has reached it.");
        return;
    }
    edit (
        [hashes, key = classification.toStdString()] (library::Catalog& c) {
            for (const auto& h : hashes)
                c.setClassificationOverride (h, key);
            return true;
        },
        classification.isEmpty() ? juce::String ("Back to what the analysis says") : "Marked as " + className (classification));
}

void LibraryPanel::reconcileInbox()
{
    const auto folders = processor.audition().librarySettings().inboxFolders;
    if (folders.isEmpty())
        return;
    std::vector<std::filesystem::path> paths;
    for (const auto& f : folders)
        paths.emplace_back (f.toStdString());
    lastInboxPass = juce::Time::getMillisecondCounter();
    juce::Component::SafePointer<LibraryPanel> safe (this);
    processor.libraryService().requestBackground<library::InboxPass> (
        [paths] (library::Catalog& c) { return library::reconcileInbox (c, paths, juce::Time::currentTimeMillis()); },
        [safe] (library::InboxPass pass) {
            if (safe == nullptr)
                return;
            const auto counts = juce::String (pass.detected) + "/" + juce::String (pass.waiting) + "/" + juce::String (pass.ready) + "/"
                              + juce::String (pass.imported) + "/" + juce::String (pass.failed) + "/" + juce::String (pass.missing);
            if (pass.detected > 0 || counts != safe->lastInboxCounts)
                safe->refresh();
            safe->lastInboxCounts = counts;
        });
}

//==============================================================================
// View F: Scan Folder

class LibraryPanel::ScanSheet final : public LibraryPanel::Sheet, private juce::Timer, private juce::ListBoxModel
{
public:
    ScanSheet (LibraryPanel& p) : Sheet (p, "Scan folders for sounds", { 820, 600 })
    {
        auto& settings = p.processor.audition().librarySettings();
        subfolders.setButtonText ("Scan subfolders");
        subfolders.setToggleState (settings.scanSubfolders, juce::dontSendNotification);
        findOneShots.setButtonText ("Find likely one-shots (recommended: each file is analysed in the background)");
        findOneShots.setToggleState (settings.scanFindOneShots, juce::dontSendNotification);
        skipLong.setButtonText ("Skip files longer than 60 seconds");
        skipLong.setToggleState (settings.scanSkipLong, juce::dontSendNotification);
        include.setTextToShowWhenEmpty ("Only names like (e.g. *kick*; pads/*)", colour::textMicro);
        exclude.setTextToShowWhenEmpty ("Leave out (e.g. *loop*; old/*)", colour::textMicro);
        include.setTitle ("Only names like");
        exclude.setTitle ("Leave out");
        roots.setModel (this);
        roots.setRowHeight (30);
        roots.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        roots.setTitle ("Scanned folders");
        addFolder.onClick = [this] { chooseFolder(); };
        rescan.onClick = [this] { scanSelected(); };
        remove.onClick = [this] { removeSelected(); };
        pause.onClick = [this] {
            auto& c = panel.processor.libraryService().scanControl();
            c.paused = ! c.paused.load();
        };
        cancel.onClick = [this] { panel.processor.libraryService().scanControl().cancelled = true; };
        done.onClick = [this] { panel.closeSheet(); };
        for (auto* c : std::initializer_list<juce::Component*> { &roots, &addFolder, &rescan, &remove, &subfolders, &findOneShots, &skipLong, &include, &exclude, &pause, &cancel, &done })
            addAndMakeVisible (c);
        refreshRoots();
        startTimerHz (10);
    }
    ~ScanSheet() override
    {
        auto& settings = panel.processor.audition().librarySettings();
        settings.scanSubfolders = subfolders.getToggleState();
        settings.scanFindOneShots = findOneShots.getToggleState();
        settings.scanSkipLong = skipLong.getToggleState();
        settings.save();
    }
    void paintContent (juce::Graphics& g) override
    {
        auto r = content();
        g.setColour (colour::textMicro);
        g.setFont (type::popupLabel (12.0f));
        g.drawText ("FOLDERS YOU CHOSE (NOTHING ELSE IS EVER SCANNED)", r.getX(), r.getY(), 500, 16, juce::Justification::centredLeft);
        g.drawText ("SCANNING", progressArea.getX(), progressArea.getY(), 200, 16, juce::Justification::centredLeft);
        auto& progress = panel.processor.libraryService().scanProgress();
        const int stage = progress.stage.load();
        const bool busy = panel.processor.libraryService().backgroundBusy();
        juce::StringArray lines;
        lines.add ("Files found  " + juce::String (progress.found.load()) + "   new or changed  " + juce::String (progress.indexed.load()) + "   unchanged  "
                   + juce::String (progress.unchanged.load()) + "   unreadable  " + juce::String (progress.failed.load()));
        lines.add ("Analysed  " + juce::String (progress.analysed.load()) + (progress.toAnalyse.load() > 0 ? " of " + juce::String (progress.toAnalyse.load()) : juce::String())
                   + (progress.skipped.load() > 0 ? "   skipped (long)  " + juce::String (progress.skipped.load()) : juce::String()));
        lines.add ("Likely one-shots  " + juce::String (progress.likelyOneShots.load()) + "   possible  " + juce::String (progress.possibleOneShots.load())
                   + "   multiple events  " + juce::String (progress.multipleEvents.load()) + "   loops  " + juce::String (progress.loops.load()) + "   long  "
                   + juce::String (progress.longRecordings.load()));
        g.setColour (colour::textSecondary);
        g.setFont (type::micro (14.0f));
        for (int i = 0; i < lines.size(); ++i)
            g.drawText (lines[i], progressArea.getX(), progressArea.getY() + 22 + i * 22, progressArea.getWidth(), 22, juce::Justification::centredLeft, true);
        // Progress bar.
        const auto bar = juce::Rectangle<float> (static_cast<float> (progressArea.getX()), static_cast<float> (progressArea.getY() + 94), static_cast<float> (progressArea.getWidth()), 6.0f);
        g.setColour (colour::knobTrack);
        g.fillRoundedRectangle (bar, 3.0f);
        const int total = progress.toAnalyse.load();
        const float fraction = stage == 3 ? 1.0f : (stage == 2 && total > 0 ? static_cast<float> (progress.analysed.load()) / static_cast<float> (total) : (stage == 1 ? 0.05f : 0.0f));
        g.setColour (colour::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, fraction)), 3.0f);
        g.setColour (colour::textMicro);
        g.setFont (type::micro (12.5f));
        const auto state = ! busy && stage != 1 && stage != 2 ? (stage == 4 ? juce::String ("Cancelled: what was found so far is kept.") : (stage == 3 ? juce::String ("Done.") : juce::String ("Not scanning.")))
                         : (panel.processor.libraryService().scanControl().paused.load() ? juce::String ("Paused.")
                                                                                           : (stage == 1 ? juce::String::fromUTF8 ("Indexing\xe2\x80\xa6")
                                                                                                         : "Analysing " + juce::String (progress.currentFile()).fromLastOccurrenceOf ("/", false, false)));
        g.drawText (state, progressArea.getX(), progressArea.getY() + 104, progressArea.getWidth(), 20, juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        auto r = content().withTrimmedTop (20);
        auto top = r.removeFromTop (150);
        auto buttons = top.removeFromRight (150);
        roots.setBounds (top.withTrimmedRight (10));
        addFolder.setBounds (buttons.removeFromTop (32));
        buttons.removeFromTop (6);
        rescan.setBounds (buttons.removeFromTop (32));
        buttons.removeFromTop (6);
        remove.setBounds (buttons.removeFromTop (32));
        r.removeFromTop (10);
        subfolders.setBounds (r.removeFromTop (26));
        findOneShots.setBounds (r.removeFromTop (26));
        skipLong.setBounds (r.removeFromTop (26));
        r.removeFromTop (6);
        auto patterns = r.removeFromTop (30);
        include.setBounds (patterns.removeFromLeft (patterns.getWidth() / 2 - 4));
        patterns.removeFromLeft (8);
        exclude.setBounds (patterns);
        r.removeFromTop (14);
        progressArea = r.removeFromTop (126);
        auto foot = r.removeFromBottom (38);
        done.setBounds (foot.removeFromRight (110));
        foot.removeFromRight (8);
        cancel.setBounds (foot.removeFromRight (110));
        foot.removeFromRight (8);
        pause.setBounds (foot.removeFromRight (110));
    }

private:
    void timerCallback() override
    {
        const bool busy = panel.processor.libraryService().backgroundBusy();
        pause.setEnabled (busy);
        cancel.setEnabled (busy);
        pause.setButtonText (panel.processor.libraryService().scanControl().paused.load() ? "Resume" : "Pause");
        rescan.setEnabled (! busy && roots.getSelectedRow() >= 0);
        if (wasBusy && ! busy)
            refreshRoots();   // the folder is in the list once the scan has recorded it
        wasBusy = busy;
        repaint (progressArea.expanded (4));
    }
    int getNumRows() override { return static_cast<int> (list.size()); }
    void paintListBoxItem (int r, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (r < 0 || r >= getNumRows())
            return;
        if (selected)
        {
            g.setColour (colour::panelBottom.darker (0.05f));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, static_cast<float> (width), static_cast<float> (height) - 2.0f), 6.0f);
        }
        const auto& root = list[static_cast<std::size_t> (r)];
        const bool there = juce::File (juce::String (root.path)).isDirectory();
        g.setColour (there ? colour::text : colour::textMicro);
        g.setFont (type::micro (14.5f));
        g.drawText (juce::String (root.path) + (there ? juce::String() : juce::String ("  (not connected)")), 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }
    juce::String getNameForRow (int r) override { return r >= 0 && r < getNumRows() ? juce::String (list[static_cast<std::size_t> (r)].path) : juce::String(); }
    void refreshRoots()
    {
        juce::Component::SafePointer<ScanSheet> safe (this);
        panel.processor.libraryService().request<std::vector<library::ScanRoot>> ([] (library::Catalog& c) { return c.scanRoots(); },
                                                                                   [safe] (std::vector<library::ScanRoot> found) {
                                                                                       if (safe == nullptr)
                                                                                           return;
                                                                                       safe->list = std::move (found);
                                                                                       safe->roots.updateContent();
                                                                                       safe->roots.repaint();
                                                                                   });
    }
    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder to scan", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        juce::Component::SafePointer<ScanSheet> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr || ! fc.getResult().isDirectory())
                return;
            safe->start (fc.getResult().getFullPathName());
        });
    }
    void scanSelected()
    {
        const int r = roots.getSelectedRow();
        if (r >= 0 && r < getNumRows())
            start (juce::String (list[static_cast<std::size_t> (r)].path));
    }
    void removeSelected()
    {
        const int r = roots.getSelectedRow();
        if (r < 0 || r >= getNumRows())
            return;
        const auto id = list[static_cast<std::size_t> (r)].id;
        juce::Component::SafePointer<ScanSheet> safe (this);
        panel.processor.libraryService().request<bool> ([id] (library::Catalog& c) { return c.removeScanRoot (id); }, [safe] (bool) {
            if (safe != nullptr)
            {
                safe->refreshRoots();
                safe->panel.refresh();
            }
        });
    }

public:
    /** Adds (or updates) the folder and scans it in the background. */
    void start (const juce::String& folder)
    {
        auto& service = panel.processor.libraryService();
        if (service.backgroundBusy())
        {
            panel.setStatus ("A scan is running: it finishes (or Cancel stops it) before the next one starts.");
            return;
        }
        wasBusy = true;
        library::ScanSettings settings;
        settings.recursive = subfolders.getToggleState();
        settings.deep = findOneShots.getToggleState();
        settings.skipLongerThanSeconds = skipLong.getToggleState() ? 60.0 : 0.0;
        service.scanControl().cancelled = false;
        service.scanControl().paused = false;
        service.scanProgress().reset();
        service.scanProgress().stage = 1;
        juce::Component::SafePointer<LibraryPanel> owner (&panel);
        auto* control = &service.scanControl();
        auto* progress = &service.scanProgress();
        service.requestBackground<library::ScanSummary> (
            [path = folder.toStdString(), inc = include.getText().toStdString(), exc = exclude.getText().toStdString(), settings, control, progress] (library::Catalog& c) {
                library::ScanSummary summary;
                const auto id = c.addScanRoot (path, inc, exc);
                if (! id)
                {
                    summary.error = "the Library could not remember the folder";
                    return summary;
                }
                return library::scanFolder (c, *c.scanRoot (*id), settings, *control, *progress);
            },
            [owner] (library::ScanSummary s) {
                if (owner == nullptr)
                    return;
                if (auto* sheet = dynamic_cast<ScanSheet*> (owner->sheet()))
                    sheet->refreshRoots();   // the folder the scan recorded
                owner->setStatus (! s.error.empty() ? juce::String (s.error)
                                                    : "Scan " + juce::String (s.cancelled ? "cancelled" : "done") + ": " + juce::String (s.found) + " files, " + juce::String (s.analysed)
                                                          + " analysed" + (s.failed > 0 ? ", " + juce::String (s.failed) + " unreadable" : juce::String()));
                if (owner->view == View::sounds)
                    owner->selectScope ("scanned");
                else
                    owner->refresh();
            });
        refreshRoots();
    }

private:
    juce::ListBox roots;
    std::vector<library::ScanRoot> list;
    juce::TextButton addFolder { juce::String::fromUTF8 ("Add folder\xe2\x80\xa6") }, rescan { "Scan again" }, remove { "Stop scanning it" };
    juce::ToggleButton subfolders, findOneShots, skipLong;
    juce::TextEditor include, exclude;
    juce::TextButton pause { "Pause" }, cancel { "Cancel" }, done { "Done" };
    juce::Rectangle<int> progressArea;
    std::unique_ptr<juce::FileChooser> chooser;
    bool wasBusy = false;
};

void LibraryPanel::openScanSheet()
{
    showSheet (std::make_unique<ScanSheet> (*this), "scan");
}

void LibraryPanel::scanFolder (const juce::File& folder)
{
    if (openSheetName() != "scan")
        openScanSheet();
    if (auto* scan = dynamic_cast<ScanSheet*> (sheetComponent.get()); scan != nullptr && folder.isDirectory())
        scan->start (folder.getFullPathName());
}

//==============================================================================
// View I: Trim and slice

class LibraryPanel::TrimSheet final : public LibraryPanel::Sheet, private juce::ListBoxModel, private juce::Timer
{
public:
    struct Region
    {
        std::int64_t start = 0, end = 0;
        juce::String name;
        bool include = true;
    };

    TrimSheet (LibraryPanel& p, const LibraryRow& source) : Sheet (p, "Trim and slice", { 1240, 760 }), row (source)
    {
        title = "Trim and slice: " + source.name;
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle ("Slices");
        auto add = [this] (juce::Button& b, const juce::String& t, std::function<void()> f) {
            b.setButtonText (t);
            b.setTitle (t);
            b.onClick = std::move (f);
            addAndMakeVisible (b);
        };
        add (suggest, "Suggest trim", [this] { suggestTrim(); });
        add (findSlices, "Find slices", [this] { slice(); });
        add (split, "Split at cursor", [this] { splitAtCursor(); });
        add (merge, "Merge with next", [this] { mergeNext(); });
        add (removeSlice, "Remove slice", [this] { removeSelected(); });
        add (rename, juce::String::fromUTF8 ("Rename\xe2\x80\xa6"), [this] { renameSelected(); });
        add (previewButton, juce::String::fromUTF8 ("\xe2\x96\xb6 Slice"), [this] { previewSelected(); });
        add (reset, "Reset", [this] { resetRegions(); });
        add (zoomIn, "+", [this] { zoomBy (2.0); });
        add (zoomOut, juce::String::fromUTF8 ("\xe2\x88\x92"), [this] { zoomBy (0.5); });
        add (cancel, "Cancel", [this] { panel.closeSheet(); });
        save = std::make_unique<AccentButton> ("Save as New Sound");
        save->onClick = [this] { saveRegions(); };
        addAndMakeVisible (*save);
        normalise.setButtonText ("Bring each peak to -1 dB (off: keep the recording's level)");
        zeroCross.setButtonText ("Start each slice at a zero crossing");
        sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
        sensitivity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sensitivity.setRange (0.0, 1.0, 0.01);
        sensitivity.setValue (0.3, juce::dontSendNotification);
        sensitivity.setTitle ("Slice sensitivity");
        sensitivity.setColour (juce::Slider::thumbColourId, colour::accent);
        sensitivity.setColour (juce::Slider::trackColourId, colour::accentSoft);
        sensitivity.setColour (juce::Slider::backgroundColourId, colour::knobTrack);
        scroll.setRangeLimits (0.0, 1.0);
        scroll.addListener (&scrollListener);
        for (auto* c : std::initializer_list<juce::Component*> { &list, &normalise, &zeroCross, &sensitivity, &scroll })
            addAndMakeVisible (c);
        juce::Component::SafePointer<TrimSheet> safe (this);
        p.processor.audition().decode (source.candidate ? std::string() : source.id, source.candidate ? juce::File (source.path) : juce::File(),
                                       [safe] (std::shared_ptr<const AudioData> decoded, const juce::File&, const juce::String&, const juce::String& error) {
                                           if (safe == nullptr)
                                               return;
                                           safe->audio = decoded;
                                           safe->problem = error;
                                           if (decoded != nullptr)
                                               safe->resetRegions();
                                           safe->repaint();
                                       });
        startTimerHz (30);
    }
    void paintContent (juce::Graphics& g) override
    {
        design::draw::well (g, waveArea.toFloat(), 10.0f, colour::wellA);
        if (audio == nullptr)
        {
            g.setColour (colour::wellText);
            g.setFont (type::micro (14.0f));
            g.drawText (problem.isNotEmpty() ? problem : juce::String::fromUTF8 ("Reading the recording\xe2\x80\xa6"), waveArea, juce::Justification::centred);
            return;
        }
        const auto area = waveArea.toFloat().reduced (10.0f, 14.0f);
        // Regions under the waveform: a quiet fill, the selected one stronger, handles at their edges.
        const int selected = list.getSelectedRow();
        for (int i = 0; i < static_cast<int> (regions.size()); ++i)
        {
            const auto& r = regions[static_cast<std::size_t> (i)];
            const float x0 = xOf (r.start, area), x1 = xOf (r.end, area);
            if (x1 < area.getX() || x0 > area.getRight())
                continue;
            const auto box = juce::Rectangle<float> (x0, area.getY(), std::max (1.0f, x1 - x0), area.getHeight());
            g.setColour ((i == selected ? colour::accent : colour::identity (1).arc).withAlpha (r.include ? (i == selected ? 0.22f : 0.12f) : 0.05f));
            g.fillRect (box);
            g.setColour ((i == selected ? colour::accent : colour::wellText).withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (x0 - 1.0f, area.getY(), 2.0f, area.getHeight()));
            g.fillRect (juce::Rectangle<float> (x1 - 1.0f, area.getY(), 2.0f, area.getHeight()));
            g.setFont (type::micro (12.0f));
            g.drawText (r.name, juce::Rectangle<float> (x0 + 4.0f, area.getY() + 2.0f, std::max (20.0f, x1 - x0 - 8.0f), 16.0f).toNearestInt(), juce::Justification::centredLeft, true);
        }
        // The waveform: min/max per pixel of the visible range.
        g.setColour (colour::identity (0).wave.withAlpha (0.92f));
        const auto& samples = monoView();
        const auto frames = static_cast<std::int64_t> (samples.size());
        const int width = static_cast<int> (area.getWidth());
        for (int px = 0; px < width; ++px)
        {
            const auto from = frameAt (area.getX() + static_cast<float> (px), area);
            const auto to = std::max (from + 1, frameAt (area.getX() + static_cast<float> (px + 1), area));
            float lo = 0.0f, hi = 0.0f;
            for (auto i = std::max<std::int64_t> (0, from); i < std::min (to, frames); i += std::max<std::int64_t> (1, (to - from) / 64))
            {
                lo = std::min (lo, samples[static_cast<std::size_t> (i)]);
                hi = std::max (hi, samples[static_cast<std::size_t> (i)]);
            }
            const float top = area.getCentreY() - hi * area.getHeight() * 0.5f, bottom = area.getCentreY() - lo * area.getHeight() * 0.5f;
            g.fillRect (juce::Rectangle<float> (area.getX() + static_cast<float> (px), top, 1.0f, std::max (1.0f, bottom - top)));
        }
        // The cursor (where Split cuts) and the playhead of the slice being heard.
        g.setColour (colour::accentSoft);
        g.fillRect (juce::Rectangle<float> (xOf (cursor, area), area.getY() - 6.0f, 1.0f, area.getHeight() + 12.0f));
        const double head = panel.processor.audition().playheadSeconds (library::PreviewEngine::browserSlot);
        if (head >= 0.0 && heardFrom >= 0)
        {
            g.setColour (colour::accent);
            g.fillRect (juce::Rectangle<float> (xOf (heardFrom + static_cast<std::int64_t> (head * audio->sampleRate), area), area.getY() - 6.0f, 1.5f, area.getHeight() + 12.0f));
        }
        g.setColour (colour::wellText);
        g.setFont (type::micro (12.5f));
        g.drawText (lengthOf (audio->durationSeconds()) + "   " + juce::String (audio->numChannels() == 1 ? "mono" : "stereo") + "   zoom " + juce::String (zoom, 0) + "x",
                    waveArea.reduced (12, 6), juce::Justification::bottomRight);
        g.setColour (colour::textMicro);
        g.setFont (type::micro (12.5f));
        g.drawText (juce::String::fromUTF8 ("Click to place the cursor; drag a slice's edge to move it. The recording itself is never changed; fades 0.5 ms in, 5 ms out."),
                    waveArea.getX(), waveArea.getBottom() + 22, waveArea.getWidth(), 18, juce::Justification::centredLeft);
        g.drawText ("SENSITIVITY", sensitivity.getX(), sensitivity.getY() - 14, 120, 14, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = content().withTrimmedTop (6);
        auto side = r.removeFromRight (300);
        r.removeFromRight (16);
        waveArea = r.removeFromTop (300);
        scroll.setBounds (waveArea.getX(), waveArea.getBottom() + 4, waveArea.getWidth(), 14);
        r.removeFromTop (50);
        auto row1 = r.removeFromTop (34);
        for (auto* b : std::initializer_list<juce::Component*> { &suggest, &findSlices })
        {
            b->setBounds (row1.removeFromLeft (140));
            row1.removeFromLeft (8);
        }
        sensitivity.setBounds (row1.removeFromLeft (180).withTrimmedTop (12));
        row1.removeFromLeft (12);
        zoomOut.setBounds (row1.removeFromRight (40));
        row1.removeFromRight (6);
        zoomIn.setBounds (row1.removeFromRight (40));
        r.removeFromTop (8);
        auto row2 = r.removeFromTop (34);
        for (auto* b : std::initializer_list<juce::Component*> { &split, &merge, &removeSlice, &rename, &previewButton, &reset })
        {
            b->setBounds (row2.removeFromLeft (122));
            row2.removeFromLeft (8);
        }
        r.removeFromTop (14);
        normalise.setBounds (r.removeFromTop (26));
        zeroCross.setBounds (r.removeFromTop (26));
        auto foot = r.removeFromBottom (40);
        save->setBounds (foot.removeFromRight (230));
        foot.removeFromRight (8);
        cancel.setBounds (foot.removeFromRight (110));
        list.setBounds (side.withTrimmedBottom (50));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragEdge = -1;
        if (audio == nullptr || ! waveArea.contains (e.getPosition()))
        {
            Sheet::mouseUp (e);
            return;
        }
        const auto area = waveArea.toFloat().reduced (10.0f, 14.0f);
        // An edge within 6 px: drag it. Else: the cursor, and the slice under it is selected.
        for (int i = 0; i < static_cast<int> (regions.size()); ++i)
            for (int edge = 0; edge < 2; ++edge)
                if (std::abs (xOf (edge == 0 ? regions[static_cast<std::size_t> (i)].start : regions[static_cast<std::size_t> (i)].end, area) - e.position.x) < 6.0f)
                {
                    dragEdge = i * 2 + edge;
                    list.selectRow (i);
                    return;
                }
        cursor = std::clamp<std::int64_t> (frameAt (e.position.x, area), 0, audio->numFrames());
        for (int i = 0; i < static_cast<int> (regions.size()); ++i)
            if (cursor >= regions[static_cast<std::size_t> (i)].start && cursor < regions[static_cast<std::size_t> (i)].end)
                list.selectRow (i);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragEdge < 0 || audio == nullptr)
            return;
        const auto area = waveArea.toFloat().reduced (10.0f, 14.0f);
        auto& r = regions[static_cast<std::size_t> (dragEdge / 2)];
        const auto frame = std::clamp<std::int64_t> (frameAt (e.position.x, area), 0, audio->numFrames());
        const auto minLength = static_cast<std::int64_t> (0.01 * audio->sampleRate);
        // Slices never overlap: an edge stops at its neighbours.
        const auto index = static_cast<std::size_t> (dragEdge / 2);
        const auto lower = index > 0 ? regions[index - 1].end : 0;
        const auto upper = index + 1 < regions.size() ? regions[index + 1].start : audio->numFrames();
        if (dragEdge % 2 == 0)
            r.start = std::clamp (frame, lower, r.end - minLength);
        else
            r.end = std::clamp (frame, r.start + minLength, upper);
        list.repaint();
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragEdge < 0 && ! waveArea.contains (e.getPosition()) && ! card().contains (e.getPosition()))
            panel.closeSheet();
        dragEdge = -1;
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (waveArea.contains (e.getPosition()))
            zoomBy (w.deltaY > 0 ? 1.25 : 0.8);
    }
    const std::vector<Region>& slices() const noexcept { return regions; }   ///< tests

    void suggestTrim()
    {
        if (audio == nullptr)
            return;
        const auto t = library::suggestTrim (*audio);
        regions = { { t.start, t.end, row.name + " (trimmed)", true } };
        changed();
    }
    void slice()
    {
        if (audio == nullptr)
            return;
        library::SliceOptions options;
        options.sensitivity = sensitivity.getValue();
        regions.clear();
        int n = 1;
        for (const auto& s : library::sliceRecording (*audio, options))
            regions.push_back ({ s.start, s.end, row.name + " " + juce::String (n++).paddedLeft ('0', 2), true });
        if (regions.empty())
            suggestTrim();
        changed();
    }

private:
    void timerCallback() override
    {
        if (panel.processor.audition().isSounding())
            repaint (waveArea);
    }
    const std::vector<float>& monoView()
    {
        if (mono.empty() && audio != nullptr)
            mono = library::monoOf (*audio);
        return mono;
    }
    float xOf (std::int64_t frame, juce::Rectangle<float> area) const
    {
        const double total = audio != nullptr ? static_cast<double> (audio->numFrames()) : 1.0;
        const double visible = total / zoom;
        return area.getX() + static_cast<float> ((static_cast<double> (frame) - offset * total) / visible * area.getWidth());
    }
    std::int64_t frameAt (float x, juce::Rectangle<float> area) const
    {
        const double total = audio != nullptr ? static_cast<double> (audio->numFrames()) : 1.0;
        const double visible = total / zoom;
        return static_cast<std::int64_t> (offset * total + (x - area.getX()) / area.getWidth() * visible);
    }
    void zoomBy (double factor)
    {
        zoom = std::clamp (zoom * factor, 1.0, 256.0);
        offset = std::clamp (offset, 0.0, 1.0 - 1.0 / zoom);
        scroll.setCurrentRange (offset, 1.0 / zoom, juce::dontSendNotification);
        repaint();
    }
    void resetRegions()
    {
        if (audio == nullptr)
            return;
        regions = { { 0, audio->numFrames(), row.name, true } };
        zoom = 1.0;
        offset = 0.0;
        scroll.setCurrentRange (0.0, 1.0, juce::dontSendNotification);
        changed();
    }
    void changed()
    {
        list.updateContent();
        list.selectRow (0);
        save->setButtonText (included() > 1 ? "Save " + juce::String (included()) + " Slices to Library" : juce::String ("Save as New Sound"));
        list.repaint();
        repaint();
    }
    int included() const
    {
        return static_cast<int> (std::count_if (regions.begin(), regions.end(), [] (const Region& r) { return r.include; }));
    }
    void splitAtCursor()
    {
        for (std::size_t i = 0; i < regions.size(); ++i)
            if (cursor > regions[i].start + 64 && cursor < regions[i].end - 64)
            {
                auto right = regions[i];
                right.start = cursor;
                right.name = regions[i].name + " b";
                regions[i].end = cursor;
                regions.insert (regions.begin() + static_cast<std::ptrdiff_t> (i) + 1, right);
                changed();
                list.selectRow (static_cast<int> (i) + 1);
                return;
            }
    }
    void mergeNext()
    {
        const int i = list.getSelectedRow();
        if (i < 0 || i + 1 >= static_cast<int> (regions.size()))
            return;
        regions[static_cast<std::size_t> (i)].end = regions[static_cast<std::size_t> (i) + 1].end;
        regions.erase (regions.begin() + i + 1);
        changed();
        list.selectRow (i);
    }
    void removeSelected()
    {
        const int i = list.getSelectedRow();
        if (i < 0 || i >= static_cast<int> (regions.size()) || regions.size() == 1)
            return;
        regions.erase (regions.begin() + i);   // only the proposal: the recording is untouched
        changed();
    }
    void renameSelected()
    {
        const int i = list.getSelectedRow();
        if (i < 0 || i >= static_cast<int> (regions.size()))
            return;
        // A name field in place (a sheet cannot open over a sheet).
        auto* editor = new juce::TextEditor();
        editor->setText (regions[static_cast<std::size_t> (i)].name, false);
        editor->setBounds (list.getX(), list.getY() + i * list.getRowHeight(), list.getWidth(), list.getRowHeight());
        editor->setTitle ("Slice name");
        juce::Component::SafePointer<TrimSheet> safe (this);
        editor->onReturnKey = editor->onFocusLost = [safe, editor, i] {
            if (safe == nullptr)
                return;
            const auto text = editor->getText().trim();
            if (text.isNotEmpty() && i < static_cast<int> (safe->regions.size()))
                safe->regions[static_cast<std::size_t> (i)].name = text;
            safe->nameEditor.reset();
            safe->list.repaint();
            safe->repaint();
        };
        nameEditor.reset (editor);
        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
        editor->selectAll();
    }
    library::RenderOptions renderOptions (const Region& r) const
    {
        library::RenderOptions options;
        options.snapToZeroCrossing = zeroCross.getToggleState();
        if (normalise.getToggleState())
            options.gainDb = library::peakGainDb (*audio, { r.start, r.end }, -1.0);
        return options;
    }
    void previewSelected()
    {
        const int i = list.getSelectedRow();
        if (audio == nullptr || i < 0 || i >= static_cast<int> (regions.size()))
            return;
        const auto& r = regions[static_cast<std::size_t> (i)];
        heardFrom = r.start;
        panel.processor.audition().previewAudio (std::make_shared<const AudioData> (library::renderSlice (*audio, { r.start, r.end }, renderOptions (r))), r.name);
    }
    void saveRegions()
    {
        if (audio == nullptr || included() == 0)
            return;
        struct Piece
        {
            AudioData audio;
            library::DerivedSound derived;
        };
        auto pieces = std::make_shared<std::vector<Piece>>();
        for (const auto& r : regions)
            if (r.include)
            {
                Piece piece;
                const auto options = renderOptions (r);
                piece.audio = library::renderSlice (*audio, { r.start, r.end }, options);
                auto& d = piece.derived;
                d.name = r.name.toStdString();
                d.startSeconds = static_cast<double> (r.start) / audio->sampleRate;
                d.endSeconds = static_cast<double> (r.end) / audio->sampleRate;
                d.category = row.category.isNotEmpty() ? row.category.toStdString() : row.suggestedType.toStdString();
                d.origin = row.inInbox ? library::Origin::captured : (row.candidate ? library::Origin::user : row.origin);
                d.operations = juce::String ("{\"fadeIn\":" + juce::String (options.fadeInSeconds) + ",\"fadeOut\":" + juce::String (options.fadeOutSeconds)
                                             + ",\"gainDb\":" + juce::String (options.gainDb, 2) + ",\"zeroCrossing\":" + (options.snapToZeroCrossing ? "true" : "false")
                                             + ",\"slicer\":\"sampleforge-1\",\"analysis\":" + juce::String (library::classifierVersion)
                                             + (row.candidate ? ",\"source\":" + juce::JSON::toString (row.path) : juce::String()) + "}")
                                   .toStdString();
                pieces->push_back (std::move (piece));
            }
        juce::Component::SafePointer<LibraryPanel> owner (&panel);
        const auto count = static_cast<int> (pieces->size());
        panel.setStatus ("Saving " + juce::String (count) + (count == 1 ? " sound" : " slices") + juce::String::fromUTF8 ("\xe2\x80\xa6"));
        struct Saved
        {
            int added = 0, same = 0, failed = 0;
            bool originalImported = false;
        };
        struct Original
        {
            bool candidate = false, inbox = false;
            std::string id, path, folder, name;
        };
        const Original original { row.candidate, row.inInbox, row.candidate ? row.importedAs : row.id, row.path.toStdString(), row.folder.toStdString(),
                                  row.name.toStdString() };
        panel.processor.libraryService().requestBackground<Saved> (
            [pieces, original, storeDir = storeDirectory()] (library::Catalog& c) {
                Saved saved;
                // A slice names the sound it came from (spec 8.5): a candidate not in the Library
                // yet comes in first, unchanged, with its evidence.
                auto parent = original.id;
                if (original.candidate && parent.empty())
                {
                    library::ImportResult imported;
                    if (original.inbox)
                        imported = library::importInboxItem (c, storeDir, original.path, original.name);
                    else if (const auto candidate = c.scanFile (original.path))
                        imported = library::importCandidate (c, storeDir, *candidate, original.folder);
                    parent = imported.ok ? imported.assetId : std::string();
                    saved.originalImported = imported.ok;
                }
                for (auto p : *pieces)
                {
                    p.derived.parentAssetId = parent;
                    const auto result = library::saveDerivedSound (c, storeDir, p.audio, p.derived);
                    ++(! result.ok ? saved.failed : (result.alreadyInLibrary ? saved.same : saved.added));
                }
                return saved;
            },
            [owner] (Saved saved) {
                if (owner == nullptr)
                    return;
                juce::String message = "Saved " + juce::String (saved.added) + (saved.added == 1 ? " new sound" : " new sounds") + " (the original is unchanged)";
                if (saved.same > 0)
                    message += "; " + juce::String (saved.same) + " had exactly the audio of a sound already in the Library";
                if (saved.failed > 0)
                    message += "; " + juce::String (saved.failed) + " could not be saved";
                if (saved.originalImported)
                    message += "; the whole recording is in the Library too";
                owner->setStatus (message);
                owner->refresh();
            });
        panel.closeSheet();
    }

    int getNumRows() override { return static_cast<int> (regions.size()); }
    void paintListBoxItem (int r, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (r < 0 || r >= getNumRows() || audio == nullptr)
            return;
        const auto& region = regions[static_cast<std::size_t> (r)];
        if (selected)
        {
            g.setColour (colour::panelBottom.darker (0.05f));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, static_cast<float> (width), static_cast<float> (height) - 2.0f), 6.0f);
        }
        // The include box (a click on it toggles; the row's text says it too).
        const auto box = juce::Rectangle<float> (8.0f, static_cast<float> (height) * 0.5f - 7.0f, 14.0f, 14.0f);
        g.setColour (region.include ? colour::accent : colour::divider);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (region.include ? colour::text : colour::textMicro);
        g.setFont (type::micro (14.0f));
        g.drawText (region.name, 30, 0, width - 100, height, juce::Justification::centredLeft, true);
        g.setColour (colour::textMicro);
        g.drawText (lengthOf (static_cast<double> (region.end - region.start) / audio->sampleRate), width - 70, 0, 62, height, juce::Justification::centredRight);
    }
    juce::String getNameForRow (int r) override
    {
        return r >= 0 && r < getNumRows() ? regions[static_cast<std::size_t> (r)].name + (regions[static_cast<std::size_t> (r)].include ? ", included" : ", left out") : juce::String();
    }
    void listBoxItemClicked (int r, const juce::MouseEvent& e) override
    {
        if (r >= 0 && r < getNumRows() && e.x < 28)
        {
            regions[static_cast<std::size_t> (r)].include = ! regions[static_cast<std::size_t> (r)].include;
            changed();
            list.selectRow (r);
        }
        repaint();
    }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { previewSelected(); }
    void selectedRowsChanged (int) override { repaint(); }

    struct ScrollListener final : juce::ScrollBar::Listener
    {
        explicit ScrollListener (TrimSheet& s) : sheet (s) {}
        void scrollBarMoved (juce::ScrollBar*, double start) override
        {
            sheet.offset = start;
            sheet.repaint();
        }
        TrimSheet& sheet;
    };

    LibraryRow row;
    std::shared_ptr<const AudioData> audio;
    std::vector<float> mono;
    juce::String problem;
    std::vector<Region> regions;
    std::int64_t cursor = 0, heardFrom = -1;
    double zoom = 1.0, offset = 0.0;
    int dragEdge = -1;
    juce::Rectangle<int> waveArea;
    juce::ListBox list;
    juce::TextButton suggest, findSlices, split, merge, removeSlice, rename, previewButton, reset, zoomIn, zoomOut, cancel;
    std::unique_ptr<juce::Button> save;
    juce::ToggleButton normalise, zeroCross;
    juce::Slider sensitivity;
    juce::ScrollBar scroll { false };
    ScrollListener scrollListener { *this };
    std::unique_ptr<juce::TextEditor> nameEditor;
};

void LibraryPanel::openTrimSheet()
{
    const int i = selectedRow();
    if (i < 0 || view != View::sounds || row (i).trashed)
        return;
    showSheet (std::make_unique<TrimSheet> (*this, row (i)), "trim");
}

} // namespace osp::plugin
