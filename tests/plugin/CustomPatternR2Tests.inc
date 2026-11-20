// Custom Pattern UX redesign R2 (docs/research/custom-arp/05-r2.md): the ARP list's one Custom
// Patterns row and its browser, the pencil in the pattern field, the editor's layout (one grid,
// a folding take, TIMING / MAPPING selectors, a contextual toolbar), direct manipulation on the
// grid, recording and import that ask before replacing work, rebuilds that ask and undo whole,
// Preview / Apply / Save / Cancel kept apart, the discard question asked through the host's
// dismissal (never from a destructor), and the library in the ··· menu. Included after
// MenuVisualShots.inc (its screenshots open real menus).

#include "CustomPatternBrowser.h"

namespace customr2
{
    using Editor = osp::plugin::CustomPatternEditor;
    using Action = Editor::Action;
    using Browser = osp::plugin::CustomPatternBrowser;
    namespace ap = osp::arppattern;
    constexpr int T = osp::arp::ticksPerStep;

    std::vector<ap::CapturedNote> take()
    {
        return { { 0.0, 0.2, 60, 100 }, { 0.25, 0.2, 64, 100 }, { 0.5, 0.1, 67, 100 }, { 0.625, 0.1, 67, 100 },
                 { 0.75, 0.45, 72, 100 }, { 1.5, 0.2, 64, 100 }, { 1.75, 0.2, 62, 100 } };
    }

    std::vector<ap::CapturedNote> fiveUp()
    {
        std::vector<ap::CapturedNote> c;
        const int pitches[] { 60, 62, 64, 65, 67 };
        for (int i = 0; i < 5; ++i)
            c.push_back ({ i * 0.25, 0.2, pitches[i], 100 });
        return c;
    }

    ap::Document documentOf (const std::string& id, const std::string& name, int steps)
    {
        ap::Document d;
        d.id = id;
        d.name = name;
        for (int i = 0; i < steps; ++i)
            d.pattern.events.push_back ({ i % 3, false, T, 0 });
        d.pattern.maxRank = 2;
        return d;
    }

    void sized (Editor& e)
    {
        const auto c = e.cardSize();
        e.setBounds (0, 0, c.x + 2 * Editor::shadowMargin, c.y + 2 * Editor::shadowMargin);
    }

    struct Library
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("osp-r2-patterns-" + juce::Uuid().toDashedString());
        Library()
        {
            dir.createDirectory();
            Editor::setLibraryFolderForTests (dir);
            Browser::clearRecentForTests();
        }
        ~Library()
        {
            Editor::setLibraryFolderForTests ({});
            Browser::clearRecentForTests();
            dir.deleteRecursively();
        }
        void add (const ap::Document& d) const
        {
            std::string error;
            REQUIRE (osp::writeArpPattern (dir.getChildFile (juce::String (d.name) + osp::arpPatternExtension).getFullPathName().toStdString(), d, error));
        }
    };

    /** Every parameter's value and every card's pattern: what "nothing changed" compares. */
    juce::String snapshotOf (OspAudioProcessor& p)
    {
        juce::String s;
        for (auto* param : p.getParameters())
            s << param->getValue() << ",";
        for (int c = 0; c < 3; ++c)
            s << (p.customArpDocument (c) != nullptr ? juce::String (p.customArpDocument (c)->id) : juce::String ("-")) << ";";
        return s;
    }

    /** Counts what the host is told (parameter changes and gestures). */
    struct HostEars final : juce::AudioProcessorListener
    {
        int changes = 0, gestures = 0;
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++gestures; }
    };

    /** The editor with its ARP panel open. */
    struct Window
    {
        OspAudioProcessor& processor;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        osp::plugin::OspAudioProcessorEditor* ui = nullptr;
        explicit Window (OspAudioProcessor& p) : processor (p), base (p.createEditorIfNeeded())
        {
            ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (base.get());
            REQUIRE (ui != nullptr);
            ui->arpControl().onToggleEditor();
        }
        osp::plugin::ArpInlinePanel& arp() { return ui->arpInlinePanel(); }
        Browser* browser() { return dynamic_cast<Browser*> (ui->openPopupPanel()); }
        Editor* editor() { return dynamic_cast<Editor*> (ui->openPopupPanel()); }
    };
}

//==============================================================================
// Gate 1: the selector and the browser

TEST_CASE ("custom pattern r2: the ARP list keeps its eighteen styles and adds one Custom Patterns row", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    Window w (p);
    auto& arp = w.arp();
    CHECK (osp::plugin::ArpInlinePanel::customRows == 1);
    CHECK (osp::plugin::ArpInlinePanel::listRows == osp::arp::patternCount + 1);
    CHECK (osp::arp::patternCount == 18);
    // Every style is still chosen by its own index (arp.pattern unchanged), and turns CUSTOM off.
    const auto doc = documentOf ("a", "Kept", 4);
    p.setCustomArpDocument (0, &doc);
    for (int s = 0; s < osp::arp::patternCount; ++s)
    {
        p.setParameterValue ("arp.custom", 1.0f);
        arp.refresh();
        arp.choosePattern (s);
        CHECK (juce::roundToInt (p.parameterValue ("arp.pattern")) == s);
        CHECK (p.parameterValue ("arp.custom") == 0.0f);
    }
    CHECK (p.customArpDocument (0) != nullptr);   // (the card's pattern kept for later)
}

TEST_CASE ("custom pattern r2: the browser lists the card's pattern, recent ones, the library and the factory; opening it changes nothing", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    using Kind = Browser::Kind;
    {
        // An empty library: the factory group open, + New Pattern last.
        Browser b (p, 0, {});
        CHECK (b.row (0).kind == Kind::heading);
        CHECK (b.find (Kind::current) < 0);
        CHECK (b.find (Kind::none, "No pattern on this card yet") >= 0);
        CHECK (b.find (Kind::user) < 0);
        CHECK (b.factoryExpanded());
        int factory = 0;
        for (int i = 0; i < b.rowCount(); ++i)
            factory += b.row (i).kind == Kind::factory ? 1 : 0;
        CHECK (factory == static_cast<int> (ap::factoryPatterns().size()));
        CHECK (b.row (b.rowCount() - 1).kind == Kind::newPattern);
        CHECK (b.find (Kind::recent) < 0);
    }
    library.add (documentOf ("u1", "Five Climb", 5));
    library.add (documentOf ("u2", "Odd Pulse", 7));
    {
        juce::File broken = library.dir.getChildFile ("Broken" + juce::String (osp::arpPatternExtension));
        broken.replaceWithText ("{ not a pattern");
    }
    const auto onB = documentOf ("b1", "Card B pattern", 3);
    p.setCustomArpDocument (1, &onB);
    const auto onA = documentOf ("u1", "Five Climb", 5);
    p.setCustomArpDocument (0, &onA);
    const auto before = snapshotOf (p);
    HostEars ears;
    p.addListener (&ears);
    {
        Browser b (p, 0, {});
        CHECK (b.find (Kind::current, "Five Climb") >= 0);
        CHECK (b.find (Kind::user, "Five Climb") >= 0);
        CHECK (b.find (Kind::user, "Odd Pulse") >= 0);
        CHECK (b.find (Kind::user, "Broken") < 0);   // an unreadable file is left out, never an error
        CHECK (b.find (Kind::recent, "Card B pattern") >= 0);   // another card's, nowhere else here
        CHECK_FALSE (b.factoryExpanded());   // (your own patterns first)
        b.activate (b.find (Kind::factoryHeading));
        CHECK (b.factoryExpanded());
        CHECK (b.find (Kind::factory) >= 0);
    }
    p.removeListener (&ears);
    CHECK (snapshotOf (p) == before);   // opening, browsing and closing changed nothing
    CHECK (ears.changes == 0);
    CHECK (ears.gestures == 0);
}

TEST_CASE ("custom pattern r2: choosing in the browser puts the pattern on the card, CUSTOM on, and closes", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    using Kind = Browser::Kind;
    // Each of the nine factory patterns.
    for (const auto& f : ap::factoryPatterns())
    {
        p.setParameterValue ("arp.custom", 0.0f);
        bool closed = false;
        Browser b (p, 0, {});
        b.onClose = [&closed] { closed = true; };
        if (! b.factoryExpanded())
            b.activate (b.find (Kind::factoryHeading));
        const int row = b.find (Kind::factory, juce::String (f.name));
        REQUIRE (row >= 0);
        b.activate (row);
        CHECK (closed);
        REQUIRE (p.customArpDocument (0) != nullptr);
        CHECK (p.customArpDocument (0)->id == f.id);
        CHECK (p.customArpPattern (0) != nullptr);
        CHECK (p.parameterValue ("arp.custom") == 1.0f);
    }
    // A pattern of your own, on card C.
    library.add (documentOf ("mine", "Mine", 6));
    Browser c (p, 2, {});
    c.activate (c.find (Kind::user, "Mine"));
    REQUIRE (p.customArpDocument (2) != nullptr);
    CHECK (p.customArpDocument (2)->name == "Mine");
    CHECK (p.parameterValue ("arp.cardC.custom") == 1.0f);
    CHECK (p.customArpDocument (0)->id == ap::factoryPatterns().back().id);   // card A untouched
}

TEST_CASE ("custom pattern r2: the row opens the browser; Edit and + New Pattern open the editor; the pencil shows the name", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    Window w (p);
    auto& arp = w.arp();
    arp.openCustomBrowser();
    REQUIRE (w.browser() != nullptr);
    // + New Pattern: a fresh editor (the browser gives way to it).
    w.browser()->activate (w.browser()->find (Browser::Kind::newPattern));
    if (! pumpMessages())
        return;   // (a platform where the test cannot run the message loop)
    REQUIRE (w.editor() != nullptr);
    CHECK (w.editor()->document().pattern.notes() == 0);
    CHECK (w.editor()->document().pattern.lengthTicks() == 8 * T);   // eight empty steps to draw on
    w.ui->closePopup();

    // A pattern applied: the field shows its name and the pencil; the pencil opens it to edit.
    const auto doc = documentOf ("edit-me", "Descending Spiral", 6);
    p.setCustomArpDocument (0, &doc);
    arp.chooseCustom();
    arp.refresh();
    CHECK (arp.customPatternName() == "Descending Spiral");
    REQUIRE_FALSE (arp.pencilArea().isEmpty());
    CHECK (arp.patternFieldArea().contains (arp.pencilArea()));
    arp.openCustomEditor (false);
    REQUIRE (w.editor() != nullptr);
    CHECK (w.editor()->document().id == "edit-me");   // the card's document, not a copy
    w.ui->closePopup();
    // Edit in the browser: the same document.
    arp.openCustomBrowser();
    REQUIRE (w.browser() != nullptr);
    w.browser()->activate (w.browser()->find (Browser::Kind::current), true);
    pumpMessages();
    REQUIRE (w.editor() != nullptr);
    CHECK (w.editor()->document().id == "edit-me");
    w.ui->closePopup();
}

TEST_CASE ("custom pattern r2: clicks: the row opens the browser and it stays open; a pattern, + New Pattern, Edit and the pencil work by click, on every card", "[plugin][arp-custom][arp-custom-r2]")
{
    // Through the real window where there is one (PopoverRig): JUCE gives the press to the list,
    // then to the editor's outside-click watcher. The browser once opened inside the press and the
    // watcher, seeing a press on the list rather than the browser's anchor, closed it at once: a
    // click on the row showed nothing (the tests above open it by calling, so they missed it).
    using namespace customr2;
    Library library;
    PopoverRig rig;
    rig.p.setParameterValue ("arp.enabled", 1.0f);
    rig.p.setParameterValue ("arp.link", 0.0f);
    rig.ui->arpControl().onToggleEditor();
    rig.editor->setSize (1448, juce::roundToInt (rig.ui->instrumentHeight()));
    rig.ui->refreshNow();
    auto& arp = rig.ui->arpInlinePanel();
    auto click = [&] (juce::Component& c, juce::Point<float> local) {
        const auto at = rig.inEditor (c, local);
        rig.press (at);
        rig.release (at);
        pumpMessages();
    };
    auto customRow = [&arp] {
        for (int i = 0; i < 40 && arp.customRowArea (0).isEmpty(); ++i)
        {
            juce::MouseWheelDetails wheel;
            wheel.deltaY = -0.2f;
            const auto at = arp.patternListArea().getCentre();
            arp.mouseWheelMove (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &arp, &arp,
                                                  juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 0, false),
                                wheel);
        }
        return arp.customRowArea (0);
    };
    auto browser = [&rig] { return dynamic_cast<Browser*> (rig.ui->openPopupPanel()); };
    auto editor = [&rig] { return dynamic_cast<Editor*> (rig.ui->openPopupPanel()); };
    for (int card = 0; card < 3; ++card)
    {
        INFO ("card " << card << (rig.realWindow() ? " (real window)" : " (routed)"));
        arp.setEditCard (card);
        arp.refresh();
        REQUIRE_FALSE (customRow().isEmpty());
        click (arp, customRow().getCentre());
        REQUIRE (browser() != nullptr);
        CHECK (rig.p.customArpDocument (card) == nullptr);   // (opening it changes nothing)

        const int fresh = browser()->find (Browser::Kind::newPattern);
        REQUIRE (fresh >= 0);
        click (*browser(), browser()->rowArea (fresh).getCentre().toFloat());
        REQUIRE (editor() != nullptr);
        rig.ui->closePopup();

        click (arp, customRow().getCentre());
        REQUIRE (browser() != nullptr);
        if (! browser()->factoryExpanded())
        {
            const int heading = browser()->find (Browser::Kind::factoryHeading);
            REQUIRE (heading >= 0);
            click (*browser(), browser()->rowArea (heading).getCentre().toFloat());
            REQUIRE (browser() != nullptr);
        }
        const auto& factory = ap::factoryPatterns()[static_cast<std::size_t> (card)];
        const int pick = browser()->find (Browser::Kind::factory, juce::String (factory.name));
        REQUIRE (pick >= 0);
        click (*browser(), browser()->rowArea (pick).getCentre().toFloat());
        CHECK (rig.ui->openPopupPanel() == nullptr);
        REQUIRE (rig.p.customArpDocument (card) != nullptr);
        CHECK (juce::String (rig.p.customArpDocument (card)->name) == juce::String (factory.name));
        arp.refresh();

        REQUIRE_FALSE (arp.pencilArea().isEmpty());
        click (arp, arp.pencilArea().getCentre());
        REQUIRE (editor() != nullptr);
        CHECK (juce::String (editor()->document().name) == juce::String (factory.name));
        rig.ui->closePopup();

        click (arp, customRow().getCentre());
        REQUIRE (browser() != nullptr);
        REQUIRE (browser()->find (Browser::Kind::current) >= 0);
        click (*browser(), browser()->editArea().getCentre().toFloat());
        REQUIRE (editor() != nullptr);
        CHECK (juce::String (editor()->document().name) == juce::String (factory.name));
        rig.ui->closePopup();
    }
}

TEST_CASE ("custom pattern r2: linked and unlinked cards: the row, the name and the browser follow the card being edited", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    Window w (p);
    auto& arp = w.arp();
    const auto a = documentOf ("a", "Pattern A", 4), b = documentOf ("b", "Pattern B", 5);
    p.setCustomArpDocument (0, &a);
    p.setCustomArpDocument (1, &b);
    p.setParameterValue ("arp.custom", 1.0f);
    p.setParameterValue ("arp.cardB.custom", 1.0f);
    arp.refresh();
    REQUIRE (arp.showsCards());
    CHECK (arp.customCard() == 0);   // linked: card A's
    CHECK (arp.customPatternName() == "Pattern A");
    arp.toggleLink();
    arp.setEditCard (1);
    arp.refresh();
    CHECK (arp.customCard() == 1);
    CHECK (arp.customPatternName() == "Pattern B");
    arp.openCustomBrowser();
    REQUIRE (w.browser() != nullptr);
    CHECK (w.browser()->row (w.browser()->find (Browser::Kind::current)).label == "Pattern B");
    CHECK (w.browser()->find (Browser::Kind::recent, "Pattern A") >= 0);
    w.ui->closePopup();
    arp.setEditCard (2);
    arp.refresh();
    CHECK (arp.customPatternName().isEmpty());   // C has none
}

//==============================================================================
// Gate 2: the editor's layout and direct manipulation

TEST_CASE ("custom pattern r2: the editor is one dominant grid, a folding take and compact controls, nothing overlapping", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    Editor e (p, 0, true);
    sized (e);
    CHECK (e.cardSize() == juce::Point<int> (700, 424));
    CHECK (e.gridArea().getHeight() >= 200);
    CHECK (e.gridArea().getHeight() > e.card().getHeight() / 2);
    CHECK_FALSE (e.sourceExpanded());
    CHECK (e.recordedArea().isEmpty());   // no take: guidance, no empty box
    CHECK_FALSE (e.canPerform (Action::toggleSource));
    CHECK_FALSE (e.canPerform (Action::timingMenu));   // nothing to reinterpret
    CHECK (e.canPerform (Action::mappingMenu));
    e.useTake (take(), 112.0);
    CHECK (e.canPerform (Action::toggleSource));
    e.perform (Action::toggleSource);
    sized (e);
    CHECK (e.sourceExpanded());
    CHECK (e.cardSize().y == 424 + Editor::sourceViewHeight);
    CHECK_FALSE (e.recordedArea().isEmpty());
    CHECK (e.gridArea().getHeight() >= 200);   // the grid keeps its size; the card grows
    CHECK (e.recordedArea().getHeight() < e.gridArea().getHeight() / 2);
    e.perform (Action::toggleSource);
    sized (e);
    CHECK_FALSE (e.sourceExpanded());
    // The keys sit on the card and apart, at every state.
    for (const bool open : { false, true })
    {
        if (open != e.sourceExpanded())
        {
            e.perform (Action::toggleSource);
            sized (e);
        }
        const Action all[] { Action::help, Action::close, Action::record, Action::import, Action::overflow, Action::timingMenu, Action::gridMenu,
                             Action::mappingMenu, Action::shorter, Action::longer, Action::split, Action::rest, Action::insertStep,
                             Action::transform, Action::merge, Action::extend, Action::undo, Action::redo, Action::preview, Action::cancel, Action::use };
        for (const auto a : all)
        {
            INFO (static_cast<int> (a));
            CHECK (e.card().contains (e.keyArea (a)));
            CHECK_FALSE (e.keyArea (a).intersects (e.gridArea()));
            for (const auto b : all)
                if (a != b)
                    CHECK_FALSE (e.keyArea (a).intersects (e.keyArea (b)));
        }
        CHECK (e.card().contains (e.nameField().getBounds()));
        CHECK_FALSE (e.nameField().getBounds().intersects (e.keyArea (Action::record)));
    }
}

TEST_CASE ("custom pattern r2: short, 16-step, odd and long patterns read on the grid; long ones scroll, never shrink", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    for (const int steps : { 1, 3, 16, 31, 64, 128 })
    {
        INFO (steps);
        auto d = documentOf ("len", "Length", steps);
        p.setCustomArpDocument (0, &d);
        Editor e (p, 0);
        sized (e);
        CHECK (e.stepsShown() == std::min (steps, 32));
        const auto first = e.eventArea (0);
        CHECK_FALSE (first.isEmpty());
        CHECK (first.getWidth() >= 10.0f);   // a step stays wide enough to grab
        if (steps > 32)
        {
            CHECK (e.eventArea (steps - 1).isEmpty());   // beyond the view
            e.select (steps - 1);
            CHECK (e.firstStepShown() == steps - 32);
            CHECK_FALSE (e.eventArea (steps - 1).isEmpty());
            CHECK (e.eventArea (0).isEmpty());
            CHECK (e.document().pattern.events[0].duration == T);   // the view moved, not the pattern
        }
    }
    // Odd: 5 events of a quarter step and a half step (6 ticks short of whole steps: exact).
    ap::Document odd = documentOf ("odd", "Odd", 0);
    for (int i = 0; i < 5; ++i)
        odd.pattern.events.push_back ({ i, false, T / 4, 0 });
    odd.pattern.events.push_back ({ 0, true, T / 2, 0 });
    odd.pattern.maxRank = 4;
    p.setCustomArpDocument (0, &odd);
    Editor e (p, 0);
    sized (e);
    for (int i = 0; i + 1 < 6; ++i)
        CHECK (e.eventArea (i).getRight() <= e.eventArea (i + 1).getX() + 0.5f);   // in order, apart
}

TEST_CASE ("custom pattern r2: direct manipulation: rank, notes from rests, edges, all exact and one undo step each", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    Editor e (p, 0, true);   // eight empty steps
    sized (e);
    const int length = e.document().pattern.lengthTicks();
    CHECK (length == 8 * T);
    // A double-click in a rest: a note there, a step long, on the rank under the pointer.
    CHECK (e.addNoteAt (2 * T + 100, 3));
    const auto& pat = e.document().pattern;
    CHECK (pat.notes() == 1);
    CHECK (pat.lengthTicks() == length);
    const int note = e.selected();
    REQUIRE (note >= 0);
    CHECK_FALSE (pat.events[static_cast<std::size_t> (note)].rest);
    CHECK (pat.events[static_cast<std::size_t> (note)].rank == 3);
    CHECK (pat.onsetOf (note) == 2 * T);
    CHECK (pat.events[static_cast<std::size_t> (note)].duration == T);
    // On a note: selected, never overwritten.
    const auto noted = pat;
    CHECK_FALSE (e.addNoteAt (2 * T + 10, 0));
    CHECK (e.document().pattern == noted);
    // Outside the pattern: nothing made.
    CHECK_FALSE (e.addNoteAt (9 * T, 0));
    // The end dragged: longer into the rest, the pattern's length kept.
    CHECK (e.dragEdge (note, true, 2 * T + 2 * T + T / 2));
    CHECK (e.document().pattern.events[static_cast<std::size_t> (e.selected())].duration == 2 * T + T / 2);
    CHECK (e.document().pattern.lengthTicks() == length);
    // The start dragged earlier, into the rest before it.
    CHECK (e.dragEdge (e.selected(), false, T));
    CHECK (e.document().pattern.onsetOf (e.selected()) == T);
    CHECK (e.document().pattern.lengthTicks() == length);
    // A second note, then the first's end cannot cover it.
    CHECK (e.addNoteAt (6 * T, 1));
    int first = -1;
    for (int i = 0; i < static_cast<int> (e.document().pattern.events.size()); ++i)
        if (! e.document().pattern.events[static_cast<std::size_t> (i)].rest && e.document().pattern.events[static_cast<std::size_t> (i)].rank == 3)
            first = i;
    REQUIRE (first >= 0);
    const auto guarded = e.document().pattern;
    CHECK_FALSE (e.dragEdge (first, true, 7 * T));
    CHECK (e.document().pattern == guarded);   // refused: nothing changed
    // Rank (a vertical drag's end).
    e.select (first);
    e.setSelectedRank (5);
    CHECK (e.document().pattern.events[static_cast<std::size_t> (first)].rank == 5);
    CHECK (e.document().pattern.maxRank >= 5);
    // Every duration exact integer ticks, the pattern valid; each gesture one undo step.
    for (const auto& ev : e.document().pattern.events)
        CHECK (ev.duration >= osp::arp::minEventTicks);
    CHECK (e.document().pattern.valid());
    for (int i = 0; i < 5; ++i)
        e.perform (Action::undo);   // rank, second note, start, end, first note
    CHECK (e.document().pattern.notes() == 0);
    CHECK (e.document().pattern.lengthTicks() == length);
    for (int i = 0; i < 5; ++i)
        e.perform (Action::redo);
    CHECK (e.document().pattern.events[static_cast<std::size_t> (first)].rank == 5);
}

TEST_CASE ("custom pattern r2: the event menu: a rest made apart from time removed, labelled; every original edit reachable", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    Editor e (p, 0, true);
    sized (e);
    e.useTake (fiveUp(), 120.0);
    const auto noteItems = e.contextActions (0);
    CHECK (noteItems.contains ("Split"));
    CHECK (noteItems.contains ("Make Rest (keep its time)"));
    CHECK (noteItems.contains ("Insert Step After (pattern gets longer)"));
    CHECK (noteItems.contains ("Delete Note and Its Time (pattern gets shorter)"));
    const int length = e.document().pattern.lengthTicks();
    CHECK (e.performContext (0, "Make Rest (keep its time)"));
    CHECK (e.document().pattern.events[0].rest);
    CHECK (e.document().pattern.lengthTicks() == length);   // time kept
    const auto restItems = e.contextActions (0);
    CHECK (restItems.contains ("Make Note"));
    CHECK_FALSE (restItems.contains ("Make Rest (keep its time)"));
    CHECK (restItems.contains ("Delete Rest and Its Time (pattern gets shorter)"));
    // Make Rest on a rest does nothing else (it is not offered, and its key is off).
    e.select (0);
    CHECK_FALSE (e.canPerform (Action::rest));
    CHECK_FALSE (e.canPerform (Action::remove));
    CHECK (e.performContext (0, "Delete Rest and Its Time (pattern gets shorter)"));
    CHECK (e.document().pattern.lengthTicks() < length);   // explicitly shorter
    // Merge and Extend: in the menu (and the toolbar) only where they apply.
    e.select (0);
    e.perform (Action::split);
    CHECK (e.contextActions (0).contains ("Merge with Next"));
    CHECK (e.performContext (0, "Merge with Next"));
    // Every original action is still reachable: a key, a menu item, or the event menu.
    const auto overflow = e.overflowItems();
    for (const auto* item : { "Save to Library", "Save Copy...", "Duplicate...", "Browse Patterns...", "Show Library Folder", "Clear Pattern" })
        CHECK (overflow.contains (item));
    CHECK_FALSE (overflow.contains ("Rename..."));               // not in the library yet: not offered
    CHECK_FALSE (overflow.contains ("Delete from Library..."));
    e.perform (Action::save);
    CHECK (e.overflowItems().contains ("Rename..."));
    CHECK (e.overflowItems().contains ("Delete from Library..."));
    for (const auto a : { Action::reverse, Action::mirror, Action::rotate })
        CHECK (e.canPerform (a));   // (Transform ▾)
    for (const auto a : { Action::record, Action::import, Action::split, Action::rest, Action::insertStep, Action::shorter, Action::longer,
                          Action::undo, Action::preview, Action::cancel, Action::use, Action::timingMenu, Action::mappingMenu })
        CHECK_FALSE (e.keyArea (a).isEmpty());
}

TEST_CASE ("custom pattern r2: keys: Delete makes a rest, Shift-Delete removes the time; a focused name field keeps its keys", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    Editor e (p, 0, true);
    sized (e);
    e.useTake (fiveUp(), 120.0);
    const int length = e.document().pattern.lengthTicks();
    e.select (1);
    CHECK (e.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
    CHECK (e.document().pattern.events[1].rest);
    CHECK (e.document().pattern.lengthTicks() == length);
    // Delete again on the rest: nothing (the time stays unless asked explicitly).
    e.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    CHECK (e.document().pattern.lengthTicks() == length);
    CHECK (e.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey, juce::ModifierKeys::shiftModifier, 0)));
    CHECK (e.document().pattern.lengthTicks() < length);
    e.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 'z'));
    CHECK (e.document().pattern.lengthTicks() == length);
    e.select (2);
    CHECK (e.keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
    CHECK (e.document().pattern.events[2].rank == 3);
    // Escape is the host's (it asks the panel), never swallowed here.
    CHECK_FALSE (e.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
}

//==============================================================================
// Gate 3: recording and import

TEST_CASE ("custom pattern r2: recording: Record becomes Stop, the take opens and counts, Stop interprets; work is never replaced unasked", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    TempDir tmp;
    OspAudioProcessor p;
    cardarp::loadTwo (p, tmp.dir);
    arpOn (p, 0, 3, 50.0f, 4);   // the arpeggiator already running
    p.prepareToPlay (48000.0, 256);
    auto play = [&p] (const std::vector<int>& notes) {
        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos = 0; pos < 36000; pos += 256)
        {
            juce::MidiBuffer midi;
            for (std::size_t i = 0; i < notes.size(); ++i)
            {
                const int on = static_cast<int> (i) * 6000, off = on + 3000;
                if (on >= pos && on < pos + 256)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[i], static_cast<juce::uint8> (100)), on - pos);
                if (off >= pos && off < pos + 256)
                    midi.addEvent (juce::MidiMessage::noteOff (1, notes[i]), off - pos);
            }
            buffer.clear();
            p.processBlock (buffer, midi);
        }
    };
    {
        Editor e (p, 0, true);
        sized (e);
        e.perform (Action::record);   // nothing to lose: records at once
        CHECK (e.recording());
        CHECK (e.sourceExpanded());   // the take opens to show the live recording
        play ({ 60, 62, 64, 64, 67 });  // repeated notes kept
        p.patternCapture()->poll();
        CHECK (p.patternCapture()->notes().size() == 5);
        CHECK (e.hasUnsavedChanges());   // (a take in progress is work)
        e.perform (Action::record);   // Stop
        CHECK_FALSE (e.recording());
        CHECK_FALSE (p.patternCapture()->armed());   // the arpeggiator back
        CHECK (e.document().capture.size() == 5);
        CHECK (e.document().pattern.notes() == 5);
        // Edited, then Record again: asked first; Cancel keeps everything.
        e.select (0);
        e.perform (Action::split);
        const auto edited = e.document();
        e.perform (Action::record);
        REQUIRE (e.dialogShown());
        CHECK (e.dialogTitle() == "Record a new take?");
        e.answerDialog (false);
        CHECK_FALSE (e.recording());
        CHECK (e.document() == edited);
        // The original take is intact after editing.
        CHECK (e.document().capture.size() == 5);
        // Import over the edits asks too.
        e.perform (Action::import);
        REQUIRE (e.dialogShown());
        CHECK (e.dialogTitle() == "Import a MIDI phrase?");
        e.answerDialog (false);
        // Record, then close while recording: the take is dropped, the arpeggiator back.
        e.perform (Action::record);
        e.answerDialog (true);
        CHECK (e.recording());
    }
    CHECK_FALSE (p.patternCapture()->armed());
}

TEST_CASE ("custom pattern r2: import: one part directly, several parts by choice; capture to pattern timing exact", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    Editor e (p, 0, true);
    sized (e);
    std::vector<osp::MidiPhrase> one (1);
    one[0].name = "Lead";
    one[0].notes = fiveUp();
    one[0].bpm = 96.0;
    e.importPhrases (one);
    CHECK (e.document().captureBpm == Approx (96.0));
    CHECK (e.document().pattern.notes() == 5);
    // RHYTHM on a quarter step: a quarter-note phrase (one step each at 1/16 RATE... here 1/4 of a quarter) lands on whole steps.
    const auto& pat = e.document().pattern;
    for (int i = 0; i < static_cast<int> (pat.events.size()); ++i)
        CHECK (pat.onsetOf (i) % (T / 4) == 0);
    std::vector<osp::MidiPhrase> two (2);
    two[0].name = "Bass";
    two[0].notes = { { 0.0, 1.0, 36, 100 } };
    two[1].name = "Arp";
    two[1].notes = take();
    e.importPhrases (two, 1);
    CHECK (e.document().capture.size() == take().size());
}

//==============================================================================
// Gate 4: timing, mapping and undo

TEST_CASE ("custom pattern r2: TIMING and MAPPING: rebuilds ask only over manual edits; undo restores the controls with the pattern", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    Editor e (p, 0, true);
    sized (e);
    e.useTake (take(), 112.0);
    using Timing = ap::Interpretation::Timing;
    CHECK (e.document().interpretation.timing == Timing::rhythm);
    const auto rhythm = e.document().pattern;
    CHECK_FALSE (e.hasManualEdits());
    // Even Steps without edits: no question.
    e.perform (Action::timingSteps);
    CHECK_FALSE (e.dialogShown());
    CHECK (e.document().interpretation.timing == Timing::steps);
    CHECK (e.document().pattern.events.size() == take().size());
    // Back to Recorded Rhythm on 1/8, then 1/2: each a rebuild, each one undo step.
    e.perform (Action::timingEighth);
    CHECK (e.document().interpretation.timing == Timing::rhythm);
    CHECK (e.document().interpretation.quantumTicks == T / 8);
    e.perform (Action::timingHalf);
    CHECK (e.document().interpretation.quantumTicks == T / 2);
    e.perform (Action::undo);
    CHECK (e.document().interpretation.quantumTicks == T / 8);   // the control follows the pattern back
    e.perform (Action::undo);
    CHECK (e.document().interpretation.timing == Timing::steps);
    e.perform (Action::undo);
    CHECK (e.document().interpretation.timing == Timing::rhythm);
    CHECK (e.document().interpretation.quantumTicks == T / 4);
    CHECK (e.document().pattern == rhythm);
    e.perform (Action::redo);
    CHECK (e.document().interpretation.timing == Timing::steps);
    e.perform (Action::undo);
    // The same choice again: harmless, nothing asked, nothing pushed.
    e.perform (Action::timingQuarter);
    CHECK_FALSE (e.dialogShown());
    // Manual edits, then a rebuild: asked; Cancel keeps them; Rebuild replaces, undo brings them back.
    e.select (0);
    e.perform (Action::split);
    CHECK (e.hasManualEdits());
    const auto edited = e.document();
    e.perform (Action::timingSteps);
    REQUIRE (e.dialogShown());
    CHECK (e.dialogTitle() == "Rebuild pattern from original take?");
    e.answerDialog (false);
    CHECK (e.document() == edited);
    e.perform (Action::timingSteps);
    e.answerDialog (true);
    CHECK (e.document().interpretation.timing == Timing::steps);
    e.perform (Action::undo);
    CHECK (e.document() == edited);
    // MAPPING never rebuilds: no question, one undo step, OCTAVES untouched.
    const auto octaves = p.parameterValue ("arp.octaves");
    e.perform (Action::degrees);
    CHECK_FALSE (e.dialogShown());
    CHECK (e.document().pattern.mapping == osp::arp::CustomMapping::degrees);
    CHECK (e.document().interpretation.mapping == osp::arp::CustomMapping::degrees);
    CHECK (e.document().pattern.events == edited.pattern.events);
    e.perform (Action::undo);
    CHECK (e.document().pattern.mapping == osp::arp::CustomMapping::contour);
    CHECK (p.parameterValue ("arp.octaves") == octaves);
    // Saved and read back: the interpretation with it.
    e.perform (Action::timingEighth);
    e.answerDialog (true);
    e.perform (Action::save);
    const auto files = library.dir.findChildFiles (juce::File::findFiles, false, "*.andorpattern");
    REQUIRE (files.size() == 1);
    std::string error;
    const auto back = osp::readArpPattern (files[0].getFullPathName().toStdString(), error);
    REQUIRE (back.has_value());
    CHECK (back->interpretation == e.document().interpretation);
    CHECK (back->pattern == e.document().pattern);
}

//==============================================================================
// Gate 5: Preview, Apply, Save and Cancel; dismissal through the host

TEST_CASE ("custom pattern r2: Escape, a click outside and the close icon ask before discarding; untouched closes at once; Apply never asks", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    Window w (p);
    auto& arp = w.arp();
    const auto kept = documentOf ("kept", "Kept", 4);
    p.setCustomArpDocument (0, &kept);
    p.setParameterValue ("arp.custom", 1.0f);
    // Untouched: closes at once.
    arp.openCustomEditor (false);
    REQUIRE (w.editor() != nullptr);
    CHECK (w.editor()->dismissRequested());
    w.ui->requestClosePopup();
    CHECK (w.ui->openPopupPanel() == nullptr);
    // Changed (with Preview on): Escape asks; Keep Editing keeps it open and the preview going.
    arp.openCustomEditor (false);
    auto* e = w.editor();
    REQUIRE (e != nullptr);
    e->perform (Action::preview);
    e->perform (Action::reverse);
    CHECK (*p.customArpPattern (0) == ap::compile (e->document().pattern));
    w.ui->requestClosePopup();   // (what Escape and the backdrop do)
    REQUIRE (w.editor() == e);
    REQUIRE (e->dialogShown());
    CHECK (e->dialogTitle() == "Discard changes to this pattern?");
    e->answerDialog (false);   // Keep Editing
    CHECK (w.editor() == e);
    CHECK (e->previewing());
    // The close icon asks the same; Discard closes and the card is exactly as it was.
    e->perform (Action::close);
    REQUIRE (e->dialogShown());
    e->answerDialog (true);
    if (! pumpMessages())
        return;
    CHECK (w.ui->openPopupPanel() == nullptr);
    CHECK (*p.customArpPattern (0) == ap::compile (kept.pattern));
    CHECK (p.customArpDocument (0)->id == "kept");
    CHECK (p.parameterValue ("arp.custom") == 1.0f);
    // Escape through the editor's own key handling (asynchronous, as from a child).
    arp.openCustomEditor (false);
    w.editor()->perform (Action::mirror);
    w.ui->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    pumpMessages();
    REQUIRE (w.editor() != nullptr);
    CHECK (w.editor()->dialogShown());
    // A second Escape answers "Keep Editing" (the dialog goes, the panel stays).
    w.ui->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
    pumpMessages();
    REQUIRE (w.editor() != nullptr);
    CHECK_FALSE (w.editor()->dialogShown());
    // Apply: never asks, closes, the card plays the pattern.
    w.editor()->perform (Action::use);
    pumpMessages();
    CHECK (w.ui->openPopupPanel() == nullptr);
    CHECK (p.customArpDocument (0)->pattern != kept.pattern);
    // The host closing it for its own reasons: no question, the card restored.
    const auto applied = *p.customArpDocument (0);
    arp.openCustomEditor (false);
    w.editor()->perform (Action::preview);
    w.editor()->perform (Action::rotate);
    w.ui->closePopup();
    CHECK (w.ui->openPopupPanel() == nullptr);
    CHECK (*p.customArpPattern (0) == ap::compile (applied.pattern));
}

TEST_CASE ("custom pattern r2: Preview, Apply and Save to Library stay apart; saved work closes without asking", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    CHECK (p.parameterValue ("arp.custom") == 0.0f);
    {
        // Save to Library without Apply: the file exists, the card is untouched, nothing to ask.
        Editor e (p, 0, true);
        e.useTake (fiveUp(), 120.0);
        e.nameField().setText ("Saved Only", true);
        CHECK (e.hasUnsavedChanges());
        e.perform (Action::save);
        CHECK (library.dir.getChildFile ("Saved Only.andorpattern").existsAsFile());
        CHECK_FALSE (e.hasUnsavedChanges());
        CHECK (e.dismissRequested());
    }
    CHECK (p.customArpDocument (0) == nullptr);
    CHECK (p.parameterValue ("arp.custom") == 0.0f);
    {
        // Apply without saving: on the card, no new file.
        Editor e (p, 0, true);
        e.useTake (fiveUp(), 120.0);
        e.nameField().setText ("Applied Only", true);
        e.perform (Action::preview);
        e.perform (Action::use);
    }
    REQUIRE (p.customArpDocument (0) != nullptr);
    CHECK (p.customArpDocument (0)->name == "Applied Only");
    CHECK (p.parameterValue ("arp.custom") == 1.0f);
    CHECK_FALSE (library.dir.getChildFile ("Applied Only.andorpattern").exists());
    {
        // Both: applied and saved, one identity.
        Editor e (p, 1, true);
        e.useTake (fiveUp(), 120.0);
        e.nameField().setText ("Both", true);
        e.perform (Action::save);
        e.perform (Action::use);
        std::string error;
        const auto file = osp::readArpPattern (library.dir.getChildFile ("Both.andorpattern").getFullPathName().toStdString(), error);
        REQUIRE (file.has_value());
        CHECK (file->id == p.customArpDocument (1)->id);
    }
    {
        // Preview, then Cancel: the card as it was (B keeps "Both").
        Editor e (p, 1);
        e.perform (Action::preview);
        e.perform (Action::reverse);
        CHECK (p.customArpPattern (1)->count > 0);
    }
    CHECK (*p.customArpPattern (1) == ap::compile (p.customArpDocument (1)->pattern));
    // Loading a saved pattern over a working document: one undo step back.
    Editor e (p, 0);
    const auto working = e.document();
    std::string error;
    e.loadDocument (*osp::readArpPattern (library.dir.getChildFile ("Saved Only.andorpattern").getFullPathName().toStdString(), error));
    CHECK (e.document().name == "Saved Only");
    e.perform (Action::undo);
    CHECK (e.document() == working);
}

TEST_CASE ("custom pattern r2: harmless interaction tells the host nothing; switching cards during a preview restores the right card", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    OspAudioProcessor p;
    Window w (p);
    auto& arp = w.arp();
    HostEars ears;
    p.addListener (&ears);
    for (int i = 0; i < 5; ++i)
    {
        arp.openCustomBrowser();
        w.ui->requestClosePopup();
        arp.openCustomEditor (true);
        w.editor()->perform (Action::toggleSource);
        w.editor()->perform (Action::help);
        w.editor()->perform (Action::help);
        w.ui->requestClosePopup();
    }
    p.removeListener (&ears);
    CHECK (ears.changes == 0);
    CHECK (ears.gestures == 0);
    CHECK (w.ui->openPopupPanel() == nullptr);
    // A preview on card A, then the panel edits card B (unlinked): A is restored on close, B untouched.
    arp.openCustomEditor (true);
    auto* e = w.editor();
    REQUIRE (e != nullptr);
    e->useTake (fiveUp(), 120.0);
    e->perform (Action::preview);
    CHECK (p.customArpPattern (0) != nullptr);
    arp.toggleLink();
    arp.setEditCard (1);
    CHECK (p.customArpPattern (1) == nullptr);
    w.ui->closePopup();
    CHECK (p.customArpPattern (0) == nullptr);
    CHECK (p.parameterValue ("arp.custom") == 0.0f);
    CHECK (p.parameterValue ("arp.cardB.custom") == 0.0f);
}

//==============================================================================
// Gate 6: the library

TEST_CASE ("custom pattern r2: the library: save, update, rename, duplicate, factory copies, protected factory, deletion, malformed files", "[plugin][arp-custom][arp-custom-r2]")
{
    using namespace customr2;
    Library library;
    OspAudioProcessor p;
    Editor e (p, 0, true);
    e.useTake (fiveUp(), 120.0);
    e.nameField().setText ("Climb", true);
    e.perform (Action::save);
    const auto id = e.document().id;
    e.perform (Action::reverse);
    e.perform (Action::save);   // an update: the same file, the same id
    CHECK (library.dir.findChildFiles (juce::File::findFiles, false, "*.andorpattern").size() == 1);
    CHECK (e.document().id == id);
    e.perform (Action::rename);
    REQUIRE (e.dialogField() != nullptr);
    e.dialogField()->setText ("Climb Up", false);
    e.answerDialog (true);
    CHECK (library.dir.getChildFile ("Climb Up.andorpattern").existsAsFile());
    CHECK_FALSE (library.dir.getChildFile ("Climb.andorpattern").exists());
    CHECK (e.document().id == id);
    CHECK (e.nameField().getText() == "Climb Up");
    // Save Copy: a new identity in the library, the editor stays on the original.
    e.perform (Action::saveCopy);
    REQUIRE (e.dialogField() != nullptr);
    CHECK (e.dialogField()->getText() == "Climb Up copy");
    e.answerDialog (true);
    CHECK (library.dir.getChildFile ("Climb Up copy.andorpattern").existsAsFile());
    CHECK (e.document().id == id);
    // Duplicate: the copy is what is edited now.
    e.perform (Action::duplicate);
    e.dialogField()->setText ("Climb Twin", false);
    e.answerDialog (true);
    CHECK (e.document().id != id);
    CHECK (e.document().name == "Climb Twin");
    CHECK (library.dir.findChildFiles (juce::File::findFiles, false, "*.andorpattern").size() == 3);
    // Delete from Library: asked; Cancel keeps the file; Delete removes it.
    e.perform (Action::deleteFromLibrary);
    REQUIRE (e.dialogShown());
    e.answerDialog (false);
    CHECK (library.dir.getChildFile ("Climb Twin.andorpattern").existsAsFile());
    e.perform (Action::deleteFromLibrary);
    e.answerDialog (true);
    CHECK_FALSE (library.dir.getChildFile ("Climb Twin.andorpattern").exists());
    // A factory pattern: never deleted or renamed; Save Copy keeps a copy of your own, the factory unchanged.
    const auto factory = ap::factoryPatterns().front();
    e.loadDocument (factory);
    CHECK_FALSE (e.overflowItems().contains ("Delete from Library..."));
    CHECK_FALSE (e.overflowItems().contains ("Rename..."));
    CHECK_FALSE (e.canPerform (Action::deleteFromLibrary));
    e.perform (Action::reverse);
    e.perform (Action::saveCopy);
    e.dialogField()->setText ("My Factory Copy", false);
    e.answerDialog (true);
    std::string error;
    const auto copy = osp::readArpPattern (library.dir.getChildFile ("My Factory Copy.andorpattern").getFullPathName().toStdString(), error);
    REQUIRE (copy.has_value());
    CHECK_FALSE (copy->factory);
    CHECK (copy->id != factory.id);
    CHECK (ap::factoryPatterns().front() == factory);   // the factory document unchanged
    CHECK (e.document().factory);   // (Save Copy leaves the editor on the factory pattern)
    // Save to Library on a factory pattern: a copy too, a new identity (for every undo state).
    e.perform (Action::save);
    CHECK_FALSE (e.document().factory);
    CHECK (e.document().id != factory.id);
    e.perform (Action::undo);
    CHECK (e.document().id != factory.id);
    // A malformed file: skipped by the browser, never an error.
    library.dir.getChildFile ("Garbage.andorpattern").replaceWithText ("not json at all");
    Browser b (p, 0, {});
    CHECK (b.find (Browser::Kind::user, "Garbage") < 0);
    CHECK (b.find (Browser::Kind::user, "Climb Up") >= 0);
}

//==============================================================================
// Screenshots (on demand)

TEST_CASE ("custom pattern r2: screenshots (on demand: OSP_CUSTOM_ARP_SHOTS=<dir>)", "[.][custom-arp-shots]")
{
    using namespace customr2;
    const auto dir = juce::SystemStats::getEnvironmentVariable ("OSP_CUSTOM_ARP_SHOTS", {});
    if (dir.isEmpty())
        return;
    juce::File out (dir);
    out.createDirectory();
    Library library;
    library.add (documentOf ("five", "Five Climb", 5));
    library.add (documentOf ("spiral", "Descending Spiral", 8));
    library.add (documentOf ("pulse", "Odd Pulse", 7));
    auto write = [&out] (const juce::Image& image, const juce::String& name) {
        const auto f = out.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream stream (f);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto shoot = [&write] (juce::Component& c, const juce::String& name) { write (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), name); };

    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    {
        Editor e (p, 0, true);
        sized (e);
        shoot (e, "r2-01-editor-empty.png");
        e.useTake (take(), 112.0);
        e.nameField().setText ("Rise and Fall", false);
        e.select (3);
        shoot (e, "r2-02-editor-pattern.png");
        e.select (-1);
        shoot (e, "r2-03-source-collapsed.png");
        e.perform (Action::toggleSource);
        sized (e);
        shoot (e, "r2-04-source-expanded.png");
        e.select (4);
        shoot (e, "r2-06-selected-note.png");
        e.perform (Action::split);
        e.perform (Action::timingSteps);
        shoot (e, "r2-11a-dialog-rebuild.png");
        e.answerDialog (false);
        e.perform (Action::close);
        shoot (e, "r2-11b-dialog-discard.png");
        e.answerDialog (false);
        e.perform (Action::help);
        shoot (e, "r2-13-help.png");
        e.perform (Action::help);
        e.perform (Action::preview);
        shoot (e, "r2-14-previewing.png");
    }
    {
        Editor e (p, 0, true);
        sized (e);
        e.perform (Action::record);
        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos = 0, n = 0; pos < 24000; pos += 256)
        {
            juce::MidiBuffer midi;
            if (pos % 6144 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60 + 3 * (n++ % 4), static_cast<juce::uint8> (100)), 0);
            if (pos % 6144 == 3072)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
        p.patternCapture()->poll();
        shoot (e, "r2-05-recording.png");
        e.perform (Action::record);
    }
    {
        // A long pattern scrolls.
        auto d = documentOf ("long", "Long Walk", 64);
        p.setCustomArpDocument (0, &d);
        Editor e (p, 0);
        sized (e);
        e.select (40);
        shoot (e, "r2-15-long-pattern.png");
        p.setCustomArpDocument (0, nullptr);
    }

    // The menus, opened as a host shows them, and the browser and selector in the window.
    if (! wantRealWindow())
        return;   // (the menus need a window system: Xvfb is enough)
   #if JUCE_LINUX
    prepareXServer();
   #endif
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (base.get());
    REQUIRE (ui != nullptr);
    ui->setTopLeftPosition (0, 0);
    ui->setVisible (true);
    ui->addToDesktop (0);
    ui->arpControl().onToggleEditor();
    pumpMessages();
    auto& arp = ui->arpInlinePanel();
    p.setCustomArpDocument (0, &ap::factoryPatterns()[2]);
    arp.openCustomBrowser();
    pumpMessages();
    if (auto* b = dynamic_cast<Browser*> (ui->openPopupPanel()))
        b->activate (b->find (Browser::Kind::current));   // (chosen: the selector shows it)
    pumpMessages();
    arp.refresh();
    shoot (arp, "r2-12-arp-selector.png");
    arp.openCustomBrowser();
    pumpMessages();
    juce::Desktop::getInstance().getAnimator().cancelAllAnimations (true);   // (the popover's fade-in, finished)
    if (auto* panel = ui->openPopupPanel())
    {
        panel->setAlpha (1.0f);
        shoot (*panel, "r2-10b-browser-panel.png");
    }
    menuvisual::shot (*ui, ui->openPopupPanel(), out.getChildFile ("r2-10-browser.png"), false);
    ui->closePopup();
    arp.openCustomEditor (false);
    pumpMessages();
    if (auto* e = dynamic_cast<Editor*> (ui->openPopupPanel()))
    {
        e->useTake (take(), 112.0);
        e->select (2);
        for (const auto& [action, file] : { std::pair<Action, const char*> { Action::transform, "r2-07-transform-menu.png" },
                                           { Action::timingMenu, "r2-08-timing-menu.png" },
                                           { Action::mappingMenu, "r2-09-mapping-menu.png" },
                                           { Action::overflow, "r2-16-overflow-menu.png" } })
        {
            e->perform (action);
            pumpMessages();
            menuvisual::shot (*ui, e, out.getChildFile (file));
        }
    }
    ui->closePopup();
    ui->removeFromDesktop();
}
