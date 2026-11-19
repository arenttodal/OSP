// Final interface polish:
// - INFO's "See tutorials";
// - the preset detail card (the heart beside its name; Load and the menu on one centre line);
// - the sound inspector's compact ROOT NOTE selector and its TYPE dropdown in ANDOR's look.
// Screenshots: OSP_FINAL_POLISH_SHOTS=<folder> osp_plugin_tests "[final-polish-shots]".
// Included at the end of PluginProcessorTests.cpp (after PedalLedTests.inc; uses LibraryRig from
// LibraryPolishTests.inc and LearnRig from OspLearnTests.inc).

#include "RootNoteSelector.h"

namespace
{
    /** Lets posted messages run (a note's key posts its action). */
    void runPosted()
    {
       #if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
        for (int i = 0; i < 100 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i)
        {
        }
       #endif
    }

    const andor::learn::HelpTopic* helpTopic (andor::learn::LearnController& learn, bool withLesson)
    {
        for (const auto& h : learn.catalog().help)
            if (h.lessonId.isNotEmpty() == withLesson && learn.targets().find (h.targetId) != nullptr)
                return &h;
        return nullptr;
    }

    void savePresetNamed (LibraryRig& rig, const juce::String& name)
    {
        OspAudioProcessor::presetFolder().createDirectory();
        REQUIRE (rig.p.savePreset (OspAudioProcessor::presetFolder().getChildFile (name + ".osppreset")));
        REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
    }

    int presetRow (LibraryRig& rig, const juce::String& name)
    {
        for (int i = 0; i < rig.panel->rowCount(); ++i)
            if (rig.panel->row (i).name == name)
                return i;
        return -1;
    }
}

TEST_CASE ("final polish: INFO's See tutorials opens the tutorial list, closes Info, starts nothing; it sits beside Close", "[plugin][learn][final-polish]")
{
    LearnRig rig;
    const auto* plain = helpTopic (rig.learn, false);
    REQUIRE (plain != nullptr);
    rig.learn.setLearnMode (true);
    rig.learn.inspect (plain->targetId);
    rig.learn.layoutNow();
    auto* note = rig.learn.note();
    REQUIRE (note != nullptr);
    REQUIRE (rig.learn.noteKind() == LearnNote::help);
    REQUIRE (note->actionIds().contains ("tutorials"));
    REQUIRE (note->actionIds().contains ("close"));
    CHECK (note->actionLabels()[note->actionIds().indexOf ("tutorials")] == "See tutorials");
    // Beside Close: the same height and centre line, Close to its right, 8 px apart, inside the card.
    auto* tutorials = note->button ("tutorials");
    auto* close = note->button ("close");
    REQUIRE (tutorials != nullptr);
    REQUIRE (close != nullptr);
    CHECK (tutorials->getHeight() == close->getHeight());
    CHECK (tutorials->getBounds().getCentreY() == close->getBounds().getCentreY());
    CHECK (close->getX() - tutorials->getRight() == 8);
    CHECK (note->card().contains (tutorials->getBounds()));
    CHECK (note->card().contains (close->getBounds()));
    // Close keeps its primary styling (no lesson: the accent key); See tutorials is neutral: their
    // centres paint different colours.
    const auto image = note->createComponentSnapshot (note->getLocalBounds(), true, 1.0f);
    const auto tutorialsFace = image.getPixelAt (tutorials->getX() + 6, tutorials->getBounds().getCentreY());
    const auto closeFace = image.getPixelAt (close->getX() + 4, close->getBounds().getCentreY());
    CHECK (tutorialsFace.getPerceivedBrightness() > closeFace.getPerceivedBrightness() + 0.15f);

    // A real click on the key (its action is posted), then the list: Info is off, no lesson.
    tutorials->triggerClick();
    runPosted();
   #if JUCE_MAC
    rig.learn.noteAction ("tutorials");
   #endif
    CHECK (rig.learn.browser() != nullptr);
    CHECK_FALSE (rig.learn.learnMode());
    CHECK (rig.learn.noteKind() == LearnNote::none);
    CHECK (rig.learn.runner().lesson() == nullptr);
    rig.learn.closeBrowser();

    // A topic with a lesson: four keys still fit the card (they wrap to a second row), the
    // primary Start Lesson last.
    if (const auto* withLesson = helpTopic (rig.learn, true))
    {
        rig.learn.setLearnMode (true);
        rig.learn.inspect (withLesson->targetId);
        rig.learn.layoutNow();
        auto* n = rig.learn.note();
        REQUIRE (n != nullptr);
        REQUIRE (n->actionIds().contains ("tutorials"));
        for (const auto& id : n->actionIds())
            CHECK (n->card().contains (n->button (id)->getBounds()));
        auto* t = n->button ("tutorials");
        auto* c = n->button ("close");
        CHECK (t->getBounds().getCentreY() == c->getBounds().getCentreY());
        CHECK (c->getX() - t->getRight() == 8);
        rig.learn.setLearnMode (false);
    }
}

TEST_CASE ("final polish: the preset card - the heart 8-12 px after the name on its centre line; a long name truncates before it; Load and the menu share a centre line", "[plugin][library][final-polish]")
{
    LibraryRig rig;
    savePresetNamed (rig, "Warm Hymn");
    savePresetNamed (rig, "A Very Long Preset Name That Keeps Going Past The Card Width To Show Truncation Before The Heart Never Under It Ever At Any Size");
    rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
    rig.panel->refresh();
    REQUIRE (rig.panel->settle());
    auto* card = rig.panel->detailCard();
    REQUIRE (card != nullptr);

    const int shortRow = presetRow (rig, "Warm Hymn");
    REQUIRE (shortRow >= 0);
    rig.panel->selectRow (shortRow);
    REQUIRE (rig.panel->settle());
    auto lay = rig.panel->detailLayout();
    CHECK_FALSE (lay.truncated);
    const float gapAfterName = lay.heart.getX() - (lay.name.getX() + lay.nameWidth);
    CHECK (gapAfterName >= 8.0f);
    CHECK (gapAfterName <= 12.0f);
    CHECK (std::abs (lay.heart.getCentreY() - lay.name.getCentreY()) <= 1.5f);
    // Left group in order: artwork, then the name and heart beside it.
    CHECK (lay.name.getX() > lay.artwork.getRight());
    // Right group: Load and the menu, one centre line, the same height, the card's own centre.
    CHECK (lay.load.getCentreY() == lay.more.getCentreY());
    CHECK (lay.load.getHeight() == lay.more.getHeight());
    CHECK (std::abs (lay.load.getCentreY() - card->getHeight() / 2) <= 1);
    CHECK (lay.heart.getRight() < static_cast<float> (lay.load.getX()));
    // The heart is drawn where the layout says (an outline heart: dark pixels in its box).
    {
        const auto image = card->createComponentSnapshot (card->getLocalBounds(), true, 1.0f);
        int ink = 0;
        for (int y = juce::roundToInt (lay.heart.getY()); y < juce::roundToInt (lay.heart.getBottom()); ++y)
            for (int x = juce::roundToInt (lay.heart.getX()); x < juce::roundToInt (lay.heart.getRight()); ++x)
                ink += image.getPixelAt (x, y).getPerceivedBrightness() < 0.75f ? 1 : 0;
        CHECK (ink > 6);
    }

    const int longRow = presetRow (rig, "A Very Long Preset Name That Keeps Going Past The Card Width To Show Truncation Before The Heart Never Under It Ever At Any Size");
    REQUIRE (longRow >= 0);
    rig.panel->selectRow (longRow);
    REQUIRE (rig.panel->settle());
    lay = rig.panel->detailLayout();
    CHECK (lay.truncated);
    CHECK (lay.heart.getX() - lay.name.getRight() >= 8.0f);    // the name stops before the heart
    CHECK (lay.heart.getX() - lay.name.getRight() <= 12.0f);
    CHECK (lay.heart.getRight() < static_cast<float> (lay.load.getX()));
}

TEST_CASE ("final polish: ROOT NOTE - Detected, a pitch class and an octave; every MIDI root; the edges disabled; the catalog edit persists", "[plugin][library][final-polish][root-note]")
{
    using osp::plugin::RootNoteSelector;
    SECTION ("every MIDI root 0-127 is reachable, and nothing beyond")
    {
        RootNoteSelector s;
        for (int m = 0; m <= 127; ++m)
        {
            s.setRoot (m, std::nullopt);
            CHECK (s.octave() == m / 12 - 1);
            CHECK (s.keyEnabled (m % 12));
            CHECK (RootNoteSelector::midiFor (m % 12, s.octave()) == m);
        }
        // Octave 9: C9-G9 exist (120-127), G#9-B9 do not.
        s.setRoot (120, std::nullopt);
        CHECK (s.octave() == 9);
        for (int pc = 0; pc < 12; ++pc)
            CHECK (s.keyEnabled (pc) == (pc <= 7));
        CHECK_FALSE (s.canStepOctave (1));
        // A9 does not exist: from A8 the step up is disabled, never clamped.
        s.setRoot (117, std::nullopt);
        CHECK_FALSE (s.canStepOctave (1));
        s.stepOctave (1);
        CHECK (s.root() == 117);
        // Octave -1: every key exists (MIDI 0-11); nothing below.
        s.setRoot (0, std::nullopt);
        CHECK (s.octave() == -1);
        for (int pc = 0; pc < 12; ++pc)
            CHECK (s.keyEnabled (pc));
        CHECK_FALSE (s.canStepOctave (-1));
        CHECK (RootNoteSelector::nameOf (0) == "C-1");
        CHECK (RootNoteSelector::nameOf (60) == "C4");
        CHECK (RootNoteSelector::nameOf (127) == "G9");
    }
    SECTION ("in the inspector: choices write the catalog's root (as the list did) and come back")
    {
        LibraryRig rig;
        rig.sounds();
        rig.panel->selectRow (rig.rowOf (rig.idDrone));
        REQUIRE (rig.panel->settle());
        auto* root = rig.panel->rootNoteSelector();
        REQUIRE (root != nullptr);
        CHECK_FALSE (root->root().has_value());
        CHECK (root->fieldText().startsWith ("Detected"));
        root->showPicker (true);
        REQUIRE (root->pickerShown());
        auto* picker = root->pickerComponent();
        REQUIRE (picker != nullptr);
        CHECK (picker->getParentComponent() == root->getParentComponent());
        CHECK (picker->getParentComponent()->getLocalBounds().contains (picker->getBounds()));
        // A3 = 57: octave 3, pitch class A.
        while (root->octave() > 3)
            root->stepOctave (-1);
        while (root->octave() < 3)
            root->stepOctave (1);
        root->choosePitchClass (9);
        REQUIRE (rig.panel->settle());
        int row = rig.rowOf (rig.idDrone);
        REQUIRE (row >= 0);
        REQUIRE (rig.panel->row (row).rootMidi.has_value());
        CHECK (*rig.panel->row (row).rootMidi == Catch::Approx (57.0));
        CHECK (root->fieldText() == "A3");
        // The octave moves the given root, keeping its pitch class.
        root->stepOctave (-1);
        REQUIRE (rig.panel->settle());
        row = rig.rowOf (rig.idDrone);
        CHECK (*rig.panel->row (row).rootMidi == Catch::Approx (45.0));
        // Escape closes the picker; the choice stays.
        CHECK (root->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
        CHECK_FALSE (root->pickerShown());
        // Another sound, then back: the root comes back from the catalog.
        rig.panel->selectRow (rig.rowOf (rig.idPluck));
        REQUIRE (rig.panel->settle());
        CHECK_FALSE (root->root().has_value());
        rig.panel->selectRow (rig.rowOf (rig.idDrone));
        REQUIRE (rig.panel->settle());
        CHECK (root->root() == 45);
        CHECK (root->fieldText() == "A2");
        // Detected: the analysis decides again.
        root->showPicker (true);
        root->chooseDetected();
        REQUIRE (rig.panel->settle());
        CHECK_FALSE (rig.panel->row (rig.rowOf (rig.idDrone)).rootMidi.has_value());
        root->showPicker (false);
    }
}

TEST_CASE ("final polish: TYPE is ANDOR's dropdown - the same choices and catalog field, the rack field's look", "[plugin][library][final-polish]")
{
    LibraryRig rig;
    rig.sounds();
    rig.panel->selectRow (rig.rowOf (rig.idChoir));
    REQUIRE (rig.panel->settle());
    auto* type = rig.panel->typeDropdown();
    REQUIRE (type != nullptr);
    CHECK (dynamic_cast<osp::plugin::AndorDropdown*> (type) != nullptr);
    REQUIRE (type->getNumItems() > 3);
    // Its own label is hidden: the field draws the value once, in the racks' capitals.
    for (auto* child : type->getChildren())
        if (dynamic_cast<juce::Label*> (child) != nullptr)
            CHECK_FALSE (child->isVisible());
    // The light ceramic face (the racks' MODE field), not the old raised card.
    const auto image = type->createComponentSnapshot (type->getLocalBounds(), true, 1.0f);
    const auto face = image.getPixelAt (6, type->getHeight() / 2);
    CHECK (std::abs (face.getFloatRed() - juce::Colour (0xfff6f2eb).getFloatRed()) < 0.05f);
    CHECK (std::abs (face.getFloatBlue() - juce::Colour (0xfff6f2eb).getFloatBlue()) < 0.05f);
    // A choice writes the sound's type, as before.
    const auto choice = type->getItemText (2);
    type->setSelectedItemIndex (2, juce::sendNotificationSync);
    REQUIRE (rig.panel->settle());
    CHECK (rig.panel->row (rig.rowOf (rig.idChoir)).category == choice);
}

TEST_CASE ("final polish: the ROOT NOTE picker open (OSP_FINAL_POLISH_SHOTS)", "[.][final-polish-shots]")
{
    const char* dir = std::getenv ("OSP_FINAL_POLISH_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    LibraryRig rig;
    rig.sounds();
    rig.panel->selectRow (rig.rowOf (rig.idDrone));
    REQUIRE (rig.panel->settle());
    auto* root = rig.panel->rootNoteSelector();
    REQUIRE (root != nullptr);
    root->showPicker (true);
    root->choosePitchClass (9);
    REQUIRE (rig.panel->settle());
    rig.ui->refreshNow();
    auto* inspector = root->getParentComponent();
    const auto area = rig.editor->getLocalArea (inspector, inspector->getLocalBounds().withHeight (juce::jmin (inspector->getHeight(), root->pickerComponent()->getBottom() + 16)));
    const auto image = rig.editor->createComponentSnapshot (area, true, 2.0f);
    const auto file = out.getChildFile ("after-04-root-picker-open.png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
}
