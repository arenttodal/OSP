// USER MACROS in ANDOR OSP (docs/research/osp-user-macros/): ANDOR ANALOG's six USER macros,
// ported - the parameters, the anchored zero-jump assignments the engine adds before anything reads
// a parameter, the state, undo and eligibility. Included after AtlasRefineTests.inc.
#include "UserMacroEditor.h"

namespace
{
    /** A sound on layer A (the plucked tutorial note), prepared at 48 kHz. */
    void loadPluck (OspAudioProcessor& p)
    {
        juce::String error;
        const auto files = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::pluck, error);
        REQUIRE (files.size() == 1);
        p.loadFile (files[0], 0);
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        p.prepareToPlay (48000.0, 256);
    }

    /** `blocks` blocks of 256 holding A3 from the first: the left channel. */
    std::vector<float> renderNote (OspAudioProcessor& p, int blocks, const std::function<void (int)>& eachBlock = {})
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 256);
        for (int b = 0; b < blocks; ++b)
        {
            if (eachBlock)
                eachBlock (b);
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        return out;
    }

    void setNormalised (OspAudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (value);
    }
} // namespace

TEST_CASE ("user macros: six appended parameters at 0; nothing assigned is bit-exact, moving a macro included", "[plugin][usermacro]")
{
    OspAudioProcessor p;
    const auto& all = p.getParameters();
    // Six consecutive parameters, appended after every earlier one (later versions, such as
    // STRUM, append after them; the whole tail's order is pinned in UnifiedPhase2Tests).
    int first = -1;
    for (int i = 0; i < all.size() && first < 0; ++i)
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (all[i]); ranged != nullptr && ranged->getParameterID() == "userMacro.1")
            first = i;
    REQUIRE (first >= 0);
    REQUIRE (first + 6 <= all.size());
    for (int m = 0; m < 6; ++m)
    {
        // In order, 0..100 %, default 0.
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (all[first + m]);
        REQUIRE (param != nullptr);
        CHECK (param->paramID == "userMacro." + juce::String (m + 1));
        CHECK (param->getName (40) == "User Macro " + juce::String (m + 1));
        CHECK (param->get() == 0.0f);
        CHECK (param->getNormalisableRange().end == 100.0f);
        CHECK_FALSE (p.userMacroReads (param->paramID));   // (a macro never moves a macro)
    }
    // Nothing saved for them (no assignment, default names).
    CHECK (p.userMacroAssignments()->isDefault());
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getChildByName ("UserMacros") == nullptr);
    }
    // Moving an unassigned macro: bit-exact.
    loadPluck (p);
    const auto still = renderNote (p, 40);
    OspAudioProcessor q;
    loadPluck (q);
    const auto moved = renderNote (q, 40, [&q] (int b) { setNormalised (q, "userMacro.1", static_cast<float> (b % 10) / 10.0f); });
    CHECK (still == moved);
}

TEST_CASE ("user macros: an assignment never jumps, then moves the engine's value, never the knob; undo; state round trip", "[plugin][usermacro]")
{
    OspAudioProcessor p;
    loadPluck (p);
    namespace um = osp::plugin::usermacros;
    // Eligible: a continuous control the engine reads; not a switch, a route, the seed or a macro.
    CHECK (um::eligible (p, "layerA.level"));
    CHECK (um::eligible (p, "space"));
    CHECK_FALSE (um::eligible (p, "layerA.mute"));
    CHECK_FALSE (um::eligible (p, "mod.route1.depth"));
    CHECK_FALSE (um::eligible (p, "seed"));
    CHECK_FALSE (um::eligible (p, "userMacro.2"));
    CHECK_FALSE (um::eligible (p, "no.such.parameter"));
    // Macro 1 at 73 %, LEVEL where it is: assigned, the sound unchanged at the anchor.
    setNormalised (p, "userMacro.1", 0.73f);
    renderNote (p, 4);
    const float base = p.parameters.getParameter ("layerA.level")->getValue();
    REQUIRE (um::assign (p, 0, "layerA.level"));
    REQUIRE (p.userMacroTable().count == 1);
    const auto& map = p.userMacroAssignments()->maps[0].front().map;
    CHECK (map.anchorMacro == Approx (0.73f).margin (1.0e-3));
    CHECK (map.anchorTarget == Approx (base));
    CHECK (map.low < map.anchorTarget);    // (a visible default range: a quarter of the travel each way)
    CHECK (map.high > map.anchorTarget);
    renderNote (p, 4);
    CHECK (std::abs (p.userMacroOffsetOf ("layerA.level")) < 1.0e-6f);   // 0 at the anchor (float rounding of 73 %): no jump
    // Macro to 100 %: the engine's LEVEL rises (glides in ~20 ms); the knob itself never moves.
    setNormalised (p, "userMacro.1", 1.0f);
    renderNote (p, 40);
    CHECK (p.userMacroOffsetOf ("layerA.level") == Approx (map.high - map.anchorTarget).margin (1.0e-3));
    CHECK (p.parameters.getParameter ("layerA.level")->getValue() == Approx (base));
    // Undo: no assignment; the offset glides out.
    CHECK (p.undo().getUndoDescription().startsWith ("Assign"));
    REQUIRE (p.undo().undo());
    CHECK (p.userMacroTable().count == 0);
    renderNote (p, 60);
    CHECK (p.userMacroOffsetOf ("layerA.level") == 0.0f);
    REQUIRE (p.undo().redo());
    CHECK (p.userMacroTable().count == 1);
    // Named, saved, recalled; a state without them clears them.
    um::edit (p, [] (osp::plugin::UserMacroState& s) { s.names[0] = "BLOOM"; }, "Rename");
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    OspAudioProcessor r;
    r.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (r.waitForLoads (60000));
    r.pollLoads();
    CHECK (*r.userMacroAssignments() == *p.userMacroAssignments());
    CHECK (r.userMacroAssignments()->displayName (0) == "BLOOM");
    CHECK (r.userMacroTable().count == 1);
    OspAudioProcessor blank;
    juce::MemoryBlock none;
    blank.getStateInformation (none);
    r.setStateInformation (none.getData(), static_cast<int> (none.getSize()));
    CHECK (r.userMacroAssignments()->isDefault());
    CHECK (r.userMacroTable().count == 0);
}

TEST_CASE ("user macros: bad saved data is made safe - unknown targets, duplicates, over eight, NaN, long names", "[plugin][usermacro]")
{
    juce::XmlElement xml ("UserMacros");
    xml.setAttribute ("schema", 1);
    auto* macro = xml.createNewChildElement ("Macro");
    macro->setAttribute ("index", 0);
    macro->setAttribute ("name", juce::String::fromUTF8 ("Br\xc3\xa4nde \xe2\x80\x94 a very long name indeed, far too long"));
    for (int i = 0; i < 12; ++i)
    {
        auto* m = macro->createNewChildElement ("Map");
        m->setAttribute ("target", i == 1 ? juce::String ("layerA.level") : (i == 2 ? juce::String ("bad target!") : "layerA." + juce::String (i == 0 ? "level" : "pan")));
        m->setAttribute ("anchorMacro", i == 3 ? std::numeric_limits<double>::quiet_NaN() : 0.5);
        m->setAttribute ("low", 7.0);
    }
    xml.createNewChildElement ("Macro")->setAttribute ("index", 9);
    const auto s = osp::plugin::UserMacroState::fromXml (&xml);
    CHECK (s.names[0].length() <= 24);
    REQUIRE (s.maps[0].size() == 2);   // level once (no duplicate), pan once, the bad ID rejected
    for (const auto& a : s.maps[0])
    {
        CHECK (std::isfinite (a.map.anchorMacro));
        CHECK (a.map.low <= 1.0f);
    }
    OspAudioProcessor p;
    // An unknown target in a session is kept but never compiled (never another parameter).
    auto withUnknown = s;
    withUnknown.maps[1].push_back ({ "no.such.parameter", {}, false, {} });
    p.setUserMacroAssignments (withUnknown);
    CHECK (p.userMacroTable().count == 2);
}

// -- The editor (UM4-UM6): ANDOR ANALOG's USER MACROS interface in ANDOR OSP ---------------------

namespace
{
    /** OSP with the pluck on A, its editor at `width`, LEVEL at 50 %; the bank preference in a
        scratch Documents folder (never a real musician's). */
    struct UserMacroRig
    {
        TempDir tmp;
        ScopedEnvironment documents { "OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName() };
        ScopedEnvironment store { "OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName() };
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        osp::plugin::OspAudioProcessorEditor* ui = nullptr;
        explicit UserMacroRig (int width = 1448)
        {
            loadPluck (p);
            setNormalised (p, "layerA.level", 0.5f);
            editor.reset (p.createEditorIfNeeded());
            ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
            REQUIRE (ui != nullptr);
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / 1448.0f));
            editor->setVisible (true);
            ui->refreshNow();
        }
        osp::plugin::UserMacroController& macros() { return ui->userMacros(); }
        osp::plugin::ModulationOverlay& overlay() { return ui->modulationOverlay(); }
        const osp::plugin::UserMacroState& state() { return *p.userMacroAssignments(); }
        float value (const juce::String& id) { return p.parameters.getParameter (id)->getValue(); }
        juce::Slider* slider (const juce::String& id)
        {
            juce::Slider* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (! child->isVisible())
                        continue;
                    if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && found == nullptr && s->getProperties()["paramId"].toString() == id)
                        found = s;
                    walk (*child);
                }
            };
            walk (*editor);
            return found;
        }
        static const juce::PopupMenu::Item* itemOf (const juce::PopupMenu& menu, const juce::String& text)
        {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return &it.getItem();
            return nullptr;
        }
        void choose (const juce::String& id, const juce::String& text)
        {
            const auto menu = overlay().contextMenu (id);
            const auto* item = itemOf (menu, text);
            REQUIRE (item != nullptr);
            REQUIRE (item->action != nullptr);
            item->action();
            ui->refreshNow();
        }
        juce::Point<float> ringPoint (const juce::String& id, float position)
        {
            auto* s = slider (id);
            const auto rotary = s->getRotaryParameters();
            const float angle = rotary.startAngleRadians + position * (rotary.endAngleRadians - rotary.startAngleRadians);
            return overlay().haloCentre (id).getPointOnCircumference (overlay().haloRadius (id), angle);
        }
        juce::MouseEvent mouse (juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {})
        {
            auto& o = overlay();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &o, &o, juce::Time::getCurrentTime(), down,
                                     juce::Time::getCurrentTime(), 1, false);
        }
        void dragEnd (const juce::String& id, int macro, int end, float position, bool cancel = false)
        {
            auto& o = overlay();
            const auto from = o.macroHandle (id, macro, end);
            REQUIRE (from != juce::Point<float>());
            o.mouseMove (mouse (from, from));
            o.mouseDown (mouse (from, from));
            REQUIRE (o.isEditingMacro());
            const auto to = ringPoint (id, position);
            o.mouseDrag (mouse (from + 0.5f * (to - from), from));
            o.mouseDrag (mouse (to, from));
            if (cancel)
                CHECK (o.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            else
                o.mouseUp (mouse (to, from));
            CHECK_FALSE (o.isEditingMacro());
            ui->refreshNow();
        }
    };

    std::vector<float> allValues (OspAudioProcessor& p)
    {
        std::vector<float> v;
        for (auto* param : p.getParameters())
            v.push_back (param->getValue());
        return v;
    }
} // namespace

TEST_CASE ("user macros UI: CORE and USER folder tabs on the macro panel's edge; switching changes only what the knobs show, remembered by this computer", "[plugin][usermacro][ui]")
{
    for (const int width : { 869, 1448, 1810 })
    {
        INFO ("width " << width);
        UserMacroRig rig (width);
        auto& um = rig.macros();
        auto* tab = um.tab();
        REQUIRE (tab != nullptr);
        CHECK (um.bankShown() == 0);   // (a new musician: CORE)
        for (const int b : { 0, 1 })
        {
            const auto t = tab->tabArea (b);
            CHECK (t.getWidth() >= 35.0f);
            CHECK (t.getWidth() <= 42.0f);
            CHECK (t.getHeight() >= 12.0f);
            CHECK (t.getHeight() <= 15.0f);
            CHECK (tab->half (b).getWidth() >= 40);
            CHECK (tab->half (b).getHeight() >= 24);
        }
        const auto panel = osp::plugin::design::layout::macroPanel.translated (0.0f, -rig.ui->sourceTrim());
        const auto box = tab->getBounds();
        CHECK (box.getY() + tab->drawnArea().getBottom() == Approx (panel.getY()).margin (1.0));   // (they stand on the edge)
        CHECK (box.getX() >= juce::roundToInt (panel.getX()));
        auto& core = rig.ui->macroRow();
        CHECK (box.getBottom() <= core[0].label->getY());
        std::vector<juce::Rectangle<int>> knobs, labels;
        for (auto& k : core)
        {
            knobs.push_back (k.slider.getBounds());
            labels.push_back (k.label->getBounds());
        }
        const int height = rig.editor->getHeight();
        const auto values = allValues (rig.p);
        for (int round = 0; round < 50; ++round)
        {
            um.showBank (round % 2 == 0 ? 1 : 0);
            rig.ui->refreshNow();
        }
        um.showBank (1);
        rig.ui->refreshNow();
        CHECK (allValues (rig.p) == values);
        CHECK (rig.editor->getHeight() == height);
        for (int i = 0; i < 6; ++i)
        {
            auto& knob = um.row()[static_cast<std::size_t> (i)];
            CHECK (knob.slider.isVisible());
            CHECK_FALSE (core[static_cast<std::size_t> (i)].slider.isVisible());
            CHECK_FALSE (core[static_cast<std::size_t> (i)].label->isVisible());
            const auto at = std::find (knobs.begin(), knobs.end(), knob.slider.getBounds());
            REQUIRE (at != knobs.end());
            CHECK (knob.label->getBounds() == labels[static_cast<std::size_t> (at - knobs.begin())]);
            if (i > 0)
                CHECK (knob.slider.getX() > um.row()[static_cast<std::size_t> (i - 1)].slider.getX());
            CHECK (knob.label->getTitle() == "MACRO " + juce::String (i + 1));
            CHECK (um.lens (i)->isVisible());
        }
        CHECK (&um.paintedRow() == &um.row());   // (the frame lights the USER knobs)
        CHECK (rig.p.macroBank() == 1);
        // The USER knob is the USER parameter.
        um.row()[2].slider.setValue (42.0, juce::sendNotificationSync);
        CHECK (rig.p.parameters.getParameter ("userMacro.3")->getValue() == Approx (0.42f).margin (0.002f));
        // Remembered by this computer, never by the sound: a new editor opens on USER; a preset
        // or undo never switches it.
        rig.editor.reset();
        rig.editor.reset (rig.p.createEditorIfNeeded());
        rig.ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (rig.editor.get());
        rig.editor->setSize (width, juce::roundToInt (static_cast<float> (width) * rig.ui->instrumentHeight() / 1448.0f));
        rig.ui->refreshNow();
        CHECK (rig.ui->userMacros().bankShown() == 1);
        OspAudioProcessor fresh;
        CHECK (fresh.macroBank() == 1);   // (the file: Documents/OSP/Interface.xml)
        CHECK (rig.tmp.dir.getChildFile ("documents/Interface.xml").loadFileAsString().contains ("shown=\"user\""));
        rig.ui->userMacros().showBank (0);
        CHECK (rig.p.macroBank() == 0);
    }
}

TEST_CASE ("user macros UI: lens drag, right-click Assign to Macro, pick mode, rename, the panel above the name", "[plugin][usermacro][ui]")
{
    namespace um = osp::plugin::usermacros;
    UserMacroRig rig;
    auto& macros = rig.macros();
    macros.showBank (1);
    rig.ui->refreshNow();
    auto* level = rig.slider ("layerA.level");
    REQUIRE (level != nullptr);
    const auto lens = macros.lens (0)->getScreenBounds().getCentre();
    const auto onTarget = level->getScreenBounds().getCentre();
    // Drag cancelled: nothing made, no undo step.
    macros.dragLens (0, lens, onTarget, true);
    CHECK (rig.state().isDefault());
    CHECK_FALSE ((rig.p.undo().canUndo() && rig.p.undo().getUndoDescription().startsWith ("Assign")));
    // Dropped on LEVEL: a neutral assignment, the panel open on it, above the macro's name.
    macros.dragLens (0, lens, onTarget);
    CHECK (rig.state().find (0, "layerA.level") == 0);
    auto* panel = dynamic_cast<osp::plugin::UserMacroPanel*> (rig.ui->openPopupPanel());
    REQUIRE (panel != nullptr);
    CHECK (panel->macro() == 0);
    CHECK (panel->rowText (0).startsWith (um::targetName (rig.p, "layerA.level")));
    CHECK (macros.panelMacro() == 0);
    CHECK (panel->getBoundsInParent().getCentreY() < macros.row()[0].label->getY());   // (it unfolds above the name)
    CHECK (panel->getBoundsInParent().getHorizontalRange().contains (macros.row()[0].label->getBounds().getCentreX()));   // (over it, kept inside the instrument)
    CHECK (macros.row()[0].label->isCustomised());   // (its lens lights: it moves something)
    // Dropped nowhere eligible: nothing.
    macros.dragLens (1, macros.lens (1)->getScreenBounds().getCentre(), macros.lens (1)->getScreenBounds().getCentre() + juce::Point<int> (0, -300));
    CHECK (rig.state().maps[1].empty());
    // The panel: HIGH edits the range, SWAP swaps, undo.
    macros.openPanel (0, "layerA.level");
    panel = dynamic_cast<osp::plugin::UserMacroPanel*> (rig.ui->openPopupPanel());
    REQUIRE (panel != nullptr);
    panel->highSlider().setValue (0.9, juce::sendNotificationSync);
    CHECK (rig.state().maps[0][0].map.high == Approx (0.9f));
    panel->swapButton().onClick();
    CHECK (rig.state().maps[0][0].map.low == Approx (0.9f));
    rig.p.undo().undo();
    CHECK (rig.state().maps[0][0].map.high == Approx (0.9f));
    // Closing the panel: its name no longer open.
    rig.ui->closePopup();
    CHECK (macros.panelMacro() == -1);
    // Right-click a control: Assign to Macro; on a switch or a route, nothing.
    CHECK (UserMacroRig::itemOf (rig.overlay().contextMenu ("space"), "Assign to Macro") != nullptr);
    CHECK (UserMacroRig::itemOf (rig.overlay().contextMenu ("layerA.mute"), "Assign to Macro") == nullptr);
    // Pick mode: + ADD ASSIGNMENT folds the panel into the banner; Escape cancels; a click assigns.
    macros.openPanel (2);
    panel = dynamic_cast<osp::plugin::UserMacroPanel*> (rig.ui->openPopupPanel());
    REQUIRE (panel != nullptr);
    REQUIRE (panel->onAddAssignment != nullptr);
    panel->onAddAssignment (2);
    CHECK (rig.ui->openPopupPanel() == nullptr);
    CHECK (macros.assignOverlay().mode() == osp::plugin::MacroAssignOverlay::Mode::pick);
    CHECK (macros.assignOverlay().bannerText().startsWith ("Assign to MACRO 3"));
    CHECK (macros.assignOverlay().getLocalBounds().toFloat().contains (macros.assignOverlay().bannerArea()));   // (over the whole instrument: its banner and rings drawn)
    bool levelRinged = false;
    for (const auto& c : macros.assignOverlay().eligibleControls())
        levelRinged = levelRinged || c.parameterId == "layerA.level";
    CHECK (levelRinged);
    CHECK (rig.ui->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (macros.assignOverlay().mode() != osp::plugin::MacroAssignOverlay::Mode::pick);
    CHECK (rig.state().maps[2].empty());
    macros.beginPick (2);
    macros.assignOverlay().onPicked ("layerA.level");
    CHECK (rig.state().find (2, "layerA.level") == 0);
    REQUIRE (dynamic_cast<osp::plugin::UserMacroPanel*> (rig.ui->openPopupPanel()) != nullptr);
    // Rename in place: Return commits (undoable); the host still sees "User Macro 1".
    rig.ui->closePopup();
    macros.startRename (0);
    auto* field = macros.renameEditor();
    REQUIRE (field != nullptr);
    field->setText ("bloom");
    field->onReturnKey();
    rig.ui->refreshNow();
    CHECK (rig.state().displayName (0) == "BLOOM");
    CHECK (macros.row()[0].label->getTitle() == "BLOOM");
    CHECK (rig.p.parameters.getParameter ("userMacro.1")->getName (32) == "User Macro 1");
    rig.p.undo().undo();
    rig.ui->refreshNow();
    CHECK (rig.state().displayName (0) == "MACRO 1");
    CHECK (macros.row()[0].label->getTitle() == "MACRO 1");
}

TEST_CASE ("user macros UI: right-click LEVEL - Assign to Macro 1; its range on the halo at once, no jump; the halo's ends set the range; one undo step; Escape cancels; the tab never switches", "[plugin][usermacro][ui][halo]")
{
    UserMacroRig rig;
    auto& macros = rig.macros();
    REQUIRE (macros.bankShown() == 0);
    namespace um = osp::plugin::usermacros;
    um::edit (rig.p, [] (osp::plugin::UserMacroState& s) { s.names[0] = "BRIGHTNESS"; }, "Rename");
    rig.ui->refreshNow();
    {
        const auto menu = rig.overlay().contextMenu ("layerA.level");
        REQUIRE (UserMacroRig::itemOf (menu, "Assign to Macro") != nullptr);
        CHECK (UserMacroRig::itemOf (menu, juce::String::fromUTF8 ("Macro 1 \xe2\x80\x94 BRIGHTNESS")) != nullptr);
        CHECK (UserMacroRig::itemOf (menu, "Macro 2") != nullptr);
        CHECK (UserMacroRig::itemOf (menu, "Remove from") == nullptr);
    }
    rig.choose ("layerA.level", juce::String::fromUTF8 ("Macro 1 \xe2\x80\x94 BRIGHTNESS"));
    REQUIRE (rig.state().find (0, "layerA.level") == 0);
    CHECK (macros.bankShown() == 0);   // (assigning from a control's menu never switches the tab)
    const auto& map = rig.state().maps[0][0].map;
    CHECK (map.anchorTarget == Approx (0.5f));
    CHECK (map.high == Approx (0.75f));
    CHECK (map.low == Approx (0.25f));
    renderNote (rig.p, 8);
    CHECK (rig.p.userMacroOffsetOf ("layerA.level") == 0.0f);   // (made at macro 0 %: no jump)
    CHECK (macros.chosen() == 0);
    CHECK (rig.overlay().macroFocus() == 0);
    auto arcs = rig.overlay().macroArcsOf ("layerA.level");
    REQUIRE (arcs.size() == 1);
    CHECK (arcs[0].atMin == Approx (0.5f));
    CHECK (arcs[0].atMax == Approx (0.75f));
    CHECK (arcs[0].name == juce::String::fromUTF8 ("MACRO 1 \xc2\xb7 BRIGHTNESS"));
    {
        const auto menu = rig.overlay().contextMenu ("layerA.level");
        const auto* tick = UserMacroRig::itemOf (menu, juce::String::fromUTF8 ("Macro 1 \xe2\x80\x94 BRIGHTNESS"));
        REQUIRE (tick != nullptr);
        CHECK (tick->isTicked);
        for (const auto* text : { "Edit Macro Range", "Reset Macro Range", "Show Macro Assignment Details", "Remove from Macro 1" })
        {
            INFO (text);
            CHECK (UserMacroRig::itemOf (menu, text) != nullptr);
        }
    }
    // The halo's 100 % end to 85 %: the range changes, the knob never moves.
    rig.dragEnd ("layerA.level", 0, 1, 0.85f);
    CHECK (rig.state().maps[0][0].map.high == Approx (0.85f).margin (0.01f));
    CHECK (rig.value ("layerA.level") == Approx (0.5f));
    CHECK (macros.bankShown() == 0);   // (a halo drag never switches the tab either)
    // The engine follows: macro at 100 % -> LEVEL at 85 %.
    setNormalised (rig.p, "userMacro.1", 1.0f);
    renderNote (rig.p, 40);
    CHECK (rig.value ("layerA.level") + rig.p.userMacroOffsetOf ("layerA.level") == Approx (0.85f).margin (0.01f));
    setNormalised (rig.p, "userMacro.1", 0.0f);
    renderNote (rig.p, 40);
    rig.ui->refreshNow();
    // One undo step per drag.
    rig.p.undo().undo();
    rig.ui->refreshNow();
    CHECK (rig.overlay().macroArcsOf ("layerA.level")[0].atMax == Approx (0.75f).margin (0.01f));
    rig.p.undo().redo();
    rig.ui->refreshNow();
    CHECK (rig.overlay().macroArcsOf ("layerA.level")[0].atMax == Approx (0.85f).margin (0.01f));
    // Escape mid-drag: nothing changed, no undo step.
    const auto before = rig.state();
    const auto undoText = rig.p.undo().getUndoDescription();
    rig.dragEnd ("layerA.level", 0, 1, 0.3f, true);
    CHECK (rig.state() == before);
    CHECK (rig.p.undo().getUndoDescription() == undoText);
    // A click elsewhere: back to the quiet arc (the assignment stays).
    rig.ui->mouseDownAnywhere (rig.slider ("space"));
    rig.ui->refreshNow();
    CHECK (macros.chosen() == -1);
    CHECK (rig.overlay().macroFocus() == -1);
    CHECK (rig.overlay().macroArcsOf ("layerA.level").size() == 1);
    // Removed from the control's menu: undoable.
    rig.choose ("layerA.level", "Remove from Macro 1");
    CHECK (rig.state().maps[0].empty());
    rig.p.undo().undo();
    CHECK (rig.state().find (0, "layerA.level") == 0);
}

TEST_CASE ("user macros UI: a plain right-click on the knob itself (the press the host gives the knob) opens its menu with Assign to Macro", "[plugin][usermacro][ui][rightclick]")
{
    UserMacroRig rig;
    auto* level = rig.slider ("layerA.level");
    REQUIRE (level != nullptr);
    juce::StringArray shown;
    rig.overlay().showMenuForTest = [&shown] (juce::PopupMenu& menu) {
        for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            shown.add (it.getItem().text);
    };
    const auto at = level->getLocalBounds().getCentre().toFloat();
    const juce::MouseEvent press (juce::Desktop::getInstance().getMainMouseSource(), at,
                                  juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, level, level,
                                  juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    CHECK (rig.overlay().rightClickedControl (press));
    CHECK (shown.contains ("Assign to Macro"));
    rig.overlay().showMenuForTest = nullptr;
}

TEST_CASE ("user macros: the coverage table - every parameter, eligible or why not (OSP_USER_MACRO_COVERAGE)", "[.][usermacro-coverage]")
{
    const char* path = std::getenv ("OSP_USER_MACRO_COVERAGE");
    REQUIRE (path != nullptr);
    OspAudioProcessor p;
    juce::StringArray rows;
    int eligible = 0, total = 0;
    rows.add ("| Parameter | Name | Kind | USER macro | Why |");
    rows.add ("|---|---|---|---|---|");
    for (auto* param : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;
        ++total;
        const auto id = ranged->paramID;
        const bool isFloat = dynamic_cast<juce::AudioParameterFloat*> (ranged) != nullptr;
        const juce::String kind = isFloat ? "continuous" : (dynamic_cast<juce::AudioParameterBool*> (ranged) != nullptr ? "switch" : "choice");
        const bool ok = osp::plugin::usermacros::eligible (p, id);
        eligible += ok ? 1 : 0;
        juce::String why;
        if (! ok)
        {
            if (id.startsWith ("userMacro."))
                why = "a USER macro itself (a macro never moves a macro)";
            else if (id.startsWith ("mod.route"))
                why = "a modulation route (its own depth editing)";
            else if (id == "seed" || id == "grainBudget")
                why = "structural (the performance seed / CPU budget)";
            else if (! isFloat)
                why = "a switch or a choice (a macro moves continuous controls)";
            else
                why = "not read live by the engine";
        }
        rows.add ("| `" + id + "` | " + ranged->getName (40) + " | " + kind + " | " + (ok ? "yes" : "no") + " | " + why + " |");
    }
    rows.insert (0, "**" + juce::String (eligible) + " of " + juce::String (total) + " parameters may be moved by a USER macro.**\n");
    juce::File (juce::String::fromUTF8 (path)).replaceWithText (rows.joinIntoString ("\n") + "\n");
}

TEST_CASE ("user macros: screenshots - CORE / USER tabs, the USER bank, the panel above a name, a halo's range, pick mode (OSP_USER_MACRO_SHOTS)", "[.][usermacro-shots]")
{
    const char* dir = std::getenv ("OSP_USER_MACRO_SHOTS");
    REQUIRE (dir != nullptr);
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    UserMacroRig rig;
    auto write = [&] (const juce::Image& image, const juce::String& name) {
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto& um = rig.macros();
    auto& ed = *rig.editor;
    const auto macroArea = [&] {
        auto area = ed.getLocalArea (um.tab(), um.tab()->getLocalBounds());
        for (auto& k : rig.ui->macroRow())
            area = area.getUnion (ed.getLocalArea (&k.slider, k.slider.getLocalBounds())).getUnion (ed.getLocalArea (k.label.get(), k.label->getLocalBounds()));
        return area.expanded (24, 40).getIntersection (ed.getLocalBounds());
    };
    write (ed.createComponentSnapshot (macroArea(), true, 2.0f), "01-core-tabs");
    um.showBank (1);
    rig.ui->refreshNow();
    write (ed.createComponentSnapshot (macroArea(), true, 2.0f), "02-user-bank");
    rig.choose ("layerA.level", "Macro 1");
    um.showBank (1);
    rig.dragEnd ("layerA.level", 0, 1, 0.9f);
    setNormalised (rig.p, "userMacro.1", 0.6f);
    renderNote (rig.p, 30);
    rig.ui->refreshNow();
    write (ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f), "03-full-halo-user");
    auto* level = rig.slider ("layerA.level");
    REQUIRE (level != nullptr);
    write (ed.createComponentSnapshot (ed.getLocalArea (level, level->getLocalBounds()).expanded (60), true, 3.0f), "04-level-halo");
    um.openPanel (0, "layerA.level");
    rig.ui->refreshNow();
    write (ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f), "05-panel-above-name");
    um.beginPick (1);
    rig.ui->refreshNow();
    write (ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f), "06-pick-mode");
    um.cancelAssign();
    um.showBank (0);
}

TEST_CASE ("user macros: CPU - six macros moving 48 assignments against none (OSP_USER_MACRO_CPU)", "[.][usermacro-cpu]")
{
    auto time = [] (bool assigned) {
        OspAudioProcessor p;
        loadPluck (p);
        if (assigned)
        {
            namespace um = osp::plugin::usermacros;
            juce::StringArray ids;
            for (auto* param : p.getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (param); r != nullptr && um::eligible (p, r->paramID))
                    ids.add (r->paramID);
            int made = 0;
            for (int m = 0; m < 6; ++m)
                for (int k = 0; k < 8 && made < ids.size(); ++k)
                    if (um::assign (p, m, ids[made++]))
                        continue;
            CHECK (p.userMacroTable().count == 48);
        }
        const auto start = juce::Time::getMillisecondCounterHiRes();
        renderNote (p, 2000, [&p, assigned] (int b) {
            if (assigned)
                for (int m = 0; m < 6; ++m)
                    setNormalised (p, "userMacro." + juce::String (m + 1), static_cast<float> ((b + 13 * m) % 100) / 100.0f);
        });
        return juce::Time::getMillisecondCounterHiRes() - start;
    };
    const double none = time (false), moving = time (true);
    const double audio = 2000.0 * 256.0 / 48000.0 * 1000.0;
    INFO ("2000 blocks (" << audio << " ms of audio): none " << none << " ms, 48 assignments moving " << moving << " ms");
    if (const char* path = std::getenv ("OSP_USER_MACRO_CPU"))
        juce::File (juce::String::fromUTF8 (path)).replaceWithText ("audio_ms " + juce::String (audio) + "\nnone_ms " + juce::String (none) + "\nmoving48_ms " + juce::String (moving) + "\n");
    CHECK (moving < none * 1.5 + 50.0);
}
