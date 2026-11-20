// Custom ARP Patterns, CP3 (docs/research/custom-arp): recording before the arpeggiator (the
// arpeggiator steps aside while armed and its own output is never captured), the editor (a take
// becomes an editable pattern; every action one undo step; PREVIEW, CANCEL and USE PATTERN act on
// the card exactly as they say), MIDI import with several parts, the pattern library (save,
// rename, duplicate, delete; factory patterns never overwritten), and the ARP panel's CUSTOM rows.

#include "CustomPatternEditor.h"
#include "model/ArpFactoryPatterns.h"

namespace customui
{
    using Editor = osp::plugin::CustomPatternEditor;
    using Action = Editor::Action;

    /** Plays `notes` (one every `gap` samples, each `length` long) through processBlock, 256 at a time. */
    int playNotes (OspAudioProcessor& p, const std::vector<int>& notes, int gap, int length, int total)
    {
        juce::AudioBuffer<float> buffer (2, 256);
        const int before = p.velocityCount.load();
        for (int pos = 0; pos < total; pos += 256)
        {
            juce::MidiBuffer midi;
            for (std::size_t i = 0; i < notes.size(); ++i)
            {
                const int on = static_cast<int> (i) * gap, off = on + length;
                if (on >= pos && on < pos + 256)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[i], static_cast<juce::uint8> (100)), on - pos);
                if (off >= pos && off < pos + 256)
                    midi.addEvent (juce::MidiMessage::noteOff (1, notes[i]), off - pos);
            }
            buffer.clear();
            p.processBlock (buffer, midi);
        }
        return p.velocityCount.load() - before;
    }

    std::vector<osp::arppattern::CapturedNote> fiveUp()
    {
        std::vector<osp::arppattern::CapturedNote> c;
        const int pitches[] { 60, 62, 64, 65, 67 };
        for (int i = 0; i < 5; ++i)
            c.push_back ({ i * 0.25, 0.2, pitches[i], 100 });
        return c;
    }

    struct TempFolder
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("osp-patterns-" + juce::Uuid().toDashedString());
        TempFolder()
        {
            dir.createDirectory();
            Editor::setLibraryFolderForTests (dir);
        }
        ~TempFolder()
        {
            Editor::setLibraryFolderForTests ({});
            dir.deleteRecursively();
        }
    };
}

TEST_CASE ("custom arp ui: recording captures the keys before the arpeggiator, which steps aside and comes back", "[plugin][arp-custom][arp-custom-ui]")
{
    using namespace customui;
    TempDir tmp;
    OspAudioProcessor p;
    cardarp::loadTwo (p, tmp.dir);
    arpOn (p, 0, 3, 50.0f, 4);   // UP, 1/32, four octaves: many notes per key held
    p.prepareToPlay (48000.0, 256);
    auto* capture = p.patternCapture();
    REQUIRE (capture != nullptr);

    // Not armed: the arpeggiator multiplies the keys.
    const int arpeggiated = playNotes (p, { 60, 64, 67 }, 12000, 11000, 48000);
    CHECK (arpeggiated > 6);

    // Armed: the keys sound as played (one note-on each) and exactly they are captured.
    capture->start (48000.0);
    const int played = playNotes (p, { 60, 62, 64, 65, 67 }, 6000, 3000, 36000);
    capture->stop();
    CHECK (played == 5);   // the engine received each key once, as played
    REQUIRE (capture->notes().size() == 5);
    const double quarter = 48000.0 * 60.0 / 120.0;   // (no host: 120 BPM)
    for (std::size_t i = 0; i < 5; ++i)
    {
        CHECK (capture->notes()[i].onset == Approx (static_cast<double> (i) * 6000.0 / quarter).margin (1.0e-6));
        CHECK (capture->notes()[i].duration == Approx (3000.0 / quarter).margin (1.0e-6));
    }
    CHECK (capture->notes()[0].pitch == 60);
    CHECK (capture->notes()[4].pitch == 67);

    // Stopped: the arpeggiator is back.
    CHECK (playNotes (p, { 60, 64, 67 }, 12000, 11000, 48000) == arpeggiated);
    // Cancelled mid-take: back as well, nothing kept.
    capture->start (48000.0);
    playNotes (p, { 60 }, 0, 3000, 6000);
    capture->cancel();
    CHECK (capture->notes().empty());
    CHECK_FALSE (capture->armed());
}

TEST_CASE ("custom arp ui: no allocation on the audio thread while recording", "[plugin][arp-custom][arp-custom-ui]")
{
    TempDir tmp;
    OspAudioProcessor p;
    cardarp::loadTwo (p, tmp.dir);
    arpOn (p, 0, 3, 50.0f, 4);
    p.prepareToPlay (48000.0, 256);
    p.patternCapture()->start (48000.0);
    juce::AudioBuffer<float> buffer (2, 256);
    int allocations = 0;
    for (int block = 0; block < 200; ++block)
    {
        juce::MidiBuffer midi;
        midi.ensureSize (64);
        if (block % 10 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 60 + block % 12, static_cast<juce::uint8> (90)), 3);
        if (block % 10 == 5)
            midi.addEvent (juce::MidiMessage::noteOff (1, 60 + (block - 5) % 12), 7);
        buffer.clear();
        test::AllocationScope scope;
        p.processBlock (buffer, midi);
        allocations += scope.count();
    }
    CHECK (allocations == 0);
    p.patternCapture()->stop();
    CHECK (p.patternCapture()->notes().size() == 20);
}

TEST_CASE ("custom arp ui: a take becomes an editable pattern; every action is one undo step", "[plugin][arp-custom][arp-custom-ui]")
{
    using namespace customui;
    OspAudioProcessor p;
    Editor editor (p, 0, true);
    editor.setBounds (0, 0, Editor::width + 2 * Editor::shadowMargin, Editor::height + 2 * Editor::shadowMargin);
    CHECK_FALSE (editor.canPerform (Action::use));      // nothing yet
    editor.useTake (fiveUp(), 120.0);
    const auto& d = editor.document();
    REQUIRE (d.pattern.notes() == 5);
    CHECK (d.pattern.maxRank == 4);
    CHECK (d.capture.size() == 5);
    CHECK (editor.message().contains ("5 of 5 notes"));

    const auto original = d.pattern;
    editor.select (1);
    editor.perform (Action::split);
    CHECK (editor.document().pattern.events.size() == original.events.size() + 1);
    CHECK (editor.document().pattern.lengthTicks() == original.lengthTicks());
    editor.perform (Action::merge);
    CHECK (editor.document().pattern == original);
    editor.perform (Action::rest);
    CHECK (editor.document().pattern.events[1].rest);
    editor.setSelectedRank (6);
    CHECK (editor.document().pattern.events[1].rank == 6);
    CHECK (editor.document().pattern.maxRank == 6);
    editor.perform (Action::reverse);
    editor.perform (Action::longer);
    CHECK (editor.document().pattern.lengthTicks() == original.lengthTicks() + osp::arp::ticksPerStep);
    editor.perform (Action::degrees);
    CHECK (editor.document().pattern.mapping == osp::arp::CustomMapping::degrees);
    // Back, one action at a time, to the take as interpreted.
    for (int i = 0; i < 7; ++i)
        editor.perform (Action::undo);
    CHECK (editor.document().pattern == original);
    editor.perform (Action::redo);
    CHECK (editor.document().pattern.events.size() == original.events.size() + 1);
    // Another quantisation re-reads the take (STEPS: one step per note).
    editor.perform (Action::timingSteps);
    CHECK (editor.document().pattern.events.size() == 5);
    CHECK (editor.document().pattern.lengthTicks() == 5 * osp::arp::ticksPerStep);
    // Places: every key on the card, none overlapping.
    for (const auto a : { Action::record, Action::import, Action::split, Action::merge, Action::extend, Action::rest, Action::remove, Action::undo,
                          Action::redo, Action::preview, Action::contour, Action::degrees, Action::clear, Action::cancel, Action::use, Action::save, Action::library })
    {
        INFO (static_cast<int> (a));
        CHECK (editor.card().contains (editor.keyArea (a)));
    }
    CHECK_FALSE (editor.keyArea (Action::undo).intersects (editor.keyArea (Action::redo)));
    CHECK_FALSE (editor.keyArea (Action::rotate).intersects (editor.keyArea (Action::undo)));
}

TEST_CASE ("custom arp ui: PREVIEW plays on the card; closing without USE PATTERN puts the card back; USE keeps it", "[plugin][arp-custom][arp-custom-ui]")
{
    using namespace customui;
    OspAudioProcessor p;
    CHECK (p.parameterValue ("arp.custom") == 0.0f);
    {
        Editor editor (p, 0, true);
        editor.useTake (fiveUp(), 120.0);
        editor.perform (Action::preview);
        CHECK (editor.previewing());
        CHECK (p.customArpPattern (0) != nullptr);
        CHECK (p.parameterValue ("arp.custom") == 1.0f);
        editor.perform (Action::split);   // follows the edit
        CHECK (p.customArpPattern (0)->count == editor.document().pattern.events.size());
    }   // closed (CANCEL, Escape, a click outside)
    CHECK (p.customArpPattern (0) == nullptr);
    CHECK (p.customArpDocument (0) == nullptr);
    CHECK (p.parameterValue ("arp.custom") == 0.0f);

    {
        Editor editor (p, 1, true);   // card B
        editor.useTake (fiveUp(), 120.0);
        editor.nameField().setText ("Five up", true);
        editor.perform (Action::use);
    }
    REQUIRE (p.customArpDocument (1) != nullptr);
    CHECK (p.customArpDocument (1)->name == "Five up");
    CHECK (p.customArpPattern (1) != nullptr);
    CHECK (p.parameterValue ("arp.cardB.custom") == 1.0f);

    // Editing the card's pattern then cancelling: the card's own pattern again (not the edit).
    const auto kept = *p.customArpDocument (1);
    {
        Editor editor (p, 1);
        CHECK (editor.document().pattern == kept.pattern);
        editor.perform (Action::preview);
        editor.perform (Action::reverse);
        CHECK (*p.customArpPattern (1) == osp::arppattern::compile (editor.document().pattern));
    }
    CHECK (*p.customArpPattern (1) == osp::arppattern::compile (kept.pattern));
    CHECK (p.parameterValue ("arp.cardB.custom") == 1.0f);
}

TEST_CASE ("custom arp ui: MIDI import with several parts picks one; the library saves, renames, duplicates and deletes", "[plugin][arp-custom][arp-custom-ui]")
{
    using namespace customui;
    OspAudioProcessor p;
    Editor editor (p, 0, true);
    std::vector<osp::MidiPhrase> phrases (2);
    phrases[0].name = "Track 1 · Bass";
    phrases[0].notes = { { 0.0, 1.0, 36, 100 } };
    phrases[1].name = "Track 2 · Lead";
    phrases[1].notes = fiveUp();
    phrases[1].bpm = 100.0;
    editor.importPhrases (phrases, 1);
    CHECK (editor.document().pattern.notes() == 5);
    CHECK (editor.document().captureBpm == Approx (100.0));
    CHECK (editor.message().contains ("Lead"));

    TempFolder folder;
    editor.nameField().setText ("Climb", true);
    editor.perform (Action::save);
    CHECK (folder.dir.getChildFile ("Climb.andorpattern").existsAsFile());
    editor.perform (Action::save);   // the same pattern again: the same file
    CHECK (folder.dir.findChildFiles (juce::File::findFiles, false, "*.andorpattern").size() == 1);
    editor.nameField().setText ("Climb Higher", true);
    editor.perform (Action::rename);
    CHECK (folder.dir.getChildFile ("Climb Higher.andorpattern").existsAsFile());
    CHECK_FALSE (folder.dir.getChildFile ("Climb.andorpattern").exists());
    const auto firstId = editor.document().id;
    editor.perform (Action::duplicate);
    CHECK (editor.document().id != firstId);
    CHECK (folder.dir.getChildFile ("Climb Higher copy.andorpattern").existsAsFile());
    editor.perform (Action::deleteFromLibrary);
    CHECK_FALSE (folder.dir.getChildFile ("Climb Higher copy.andorpattern").exists());
    CHECK (folder.dir.getChildFile ("Climb Higher.andorpattern").existsAsFile());

    // A factory pattern: SAVE keeps a copy of your own; the factory one is unchanged.
    const auto& factory = osp::arppattern::factoryPatterns().front();
    editor.loadDocument (factory);
    CHECK (editor.document().factory);
    CHECK_FALSE (editor.canPerform (Action::deleteFromLibrary));
    editor.nameField().setText ("My Climb", true);
    editor.perform (Action::save);
    CHECK_FALSE (editor.document().factory);
    CHECK (editor.document().id != factory.id);
    CHECK (osp::arppattern::factoryPatterns().front().factory);
    CHECK (folder.dir.getChildFile ("My Climb.andorpattern").existsAsFile());
}

TEST_CASE ("custom arp ui: the ARP list ends with CUSTOM, Create Pattern... and Custom Patterns...; a style turns CUSTOM off", "[plugin][arp-custom][arp-custom-ui]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    ui->arpControl().onToggleEditor();
    auto& arp = ui->arpInlinePanel();
    CHECK (osp::plugin::ArpInlinePanel::listRows == osp::arp::patternCount + 3);
    // At the top, the custom rows are scrolled out of view.
    CHECK (arp.customRowArea (0).isEmpty());
    // A pattern for card A, chosen: CUSTOM on; a style chosen: CUSTOM off, the pattern kept.
    const auto& factory = osp::arppattern::factoryPatterns()[1];
    p.setCustomArpDocument (0, &factory);
    arp.chooseCustom();
    CHECK (p.parameterValue ("arp.custom") == 1.0f);
    arp.refresh();
    CHECK (arp.customChosen());
    CHECK (arp.fieldPattern() == -2);   // the picture: the custom pattern
    // The list scrolls to show CUSTOM (chosen), below the last style, with the two actions.
    CHECK_FALSE (arp.customRowArea (0).isEmpty());
    CHECK (arp.customRowArea (0).getY() > arp.patternListArea().getY());
    arp.choosePattern (static_cast<int> (osp::ArpPattern::down));
    CHECK (p.parameterValue ("arp.custom") == 0.0f);
    CHECK (p.customArpDocument (0) != nullptr);
    arp.refresh();
    CHECK_FALSE (arp.customChosen());
    CHECK (arp.fieldPattern() == static_cast<int> (osp::ArpPattern::down));
}

TEST_CASE ("custom arp ui: the factory patterns are valid, marked factory and distinct", "[plugin][arp-custom][arp-custom-ui]")
{
    // (The shell's recording and state are checked in tests/shell/ShellTests.cpp, per product.)
    std::set<std::string> ids;
    for (const auto& d : osp::arppattern::factoryPatterns())
    {
        INFO (d.name);
        CHECK (d.factory);
        CHECK (d.pattern.valid());
        CHECK (ids.insert (d.id).second);
    }
    CHECK (ids.size() >= 8);
}

TEST_CASE ("custom arp ui: screenshots (on demand: OSP_CUSTOM_ARP_SHOTS=<dir>)", "[.][custom-arp-shots]")
{
    using namespace customui;
    const auto dir = juce::SystemStats::getEnvironmentVariable ("OSP_CUSTOM_ARP_SHOTS", {});
    if (dir.isEmpty())
        return;
    juce::File out (dir);
    out.createDirectory();
    OspAudioProcessor p;
    auto shoot = [&out] (juce::Component& c, const juce::String& name) {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
        juce::FileOutputStream stream (out.getChildFile (name));
        stream.setPosition (0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    {
        Editor editor (p, 0, true);
        editor.setBounds (0, 0, Editor::width + 2 * Editor::shadowMargin, Editor::height + 2 * Editor::shadowMargin);
        shoot (editor, "custom-1-editor-empty.png");
        std::vector<osp::arppattern::CapturedNote> take { { 0.0, 0.2, 60, 100 }, { 0.25, 0.2, 64, 100 }, { 0.5, 0.1, 67, 100 }, { 0.625, 0.1, 67, 100 },
                                                          { 0.75, 0.45, 72, 100 }, { 1.5, 0.2, 64, 100 }, { 1.75, 0.2, 62, 100 } };
        editor.useTake (take, 112.0);
        editor.nameField().setText ("Rise and fall", false);
        editor.select (3);
        shoot (editor, "custom-2-editor-take.png");
        editor.perform (Action::split);
        editor.perform (Action::preview);
        shoot (editor, "custom-3-editor-split-preview.png");
        editor.perform (Action::use);
    }
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    ui->arpControl().onToggleEditor();
    auto& arp = ui->arpInlinePanel();
    arp.refresh();
    shoot (arp, "custom-4-arp-panel-custom.png");
}
