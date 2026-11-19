// Contextual popovers stay open while their own controls are assigned to USER macros and their
// ranges edited (docs/research/popover-macro-interaction.md). The presses go through the real
// window: the editor is put on the desktop and the events are given to its native peer, so
// JUCE's own routing decides who gets them (the overlay's hitTest, the control's handler) and
// the editor's global outside-click watcher hears them as a host's would. Included after
// LibraryPolishTests.inc.

#if JUCE_LINUX
 #include <dlfcn.h>
#endif

#if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);   // (juce_events: Linux / Windows)
}
#endif

namespace
{
    /** Runs what was posted to the message thread (a popover's asynchronous close). Where the
        platform cannot be pumped from a test (macOS without modal loops): false. */
    bool pumpMessages()
    {
       #if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
        for (int i = 0; i < 200 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i)
        {
        }
        return true;
       #else
        return false;
       #endif
    }

#if JUCE_LINUX
    /** Xvfb with no window manager lacks the _NET_WM_* atoms JUCE looks up (only if they
        exist) when it makes a window: made here, on the server, so a real window can be made. */
    void prepareXServer()
    {
        auto* x11 = dlopen ("libX11.so.6", RTLD_NOW);
        if (x11 == nullptr)
            return;
        using Open = void* (*) (const char*);
        using Intern = unsigned long (*) (void*, const char*, int);
        using Close = int (*) (void*);
        auto open = reinterpret_cast<Open> (dlsym (x11, "XOpenDisplay"));
        auto intern = reinterpret_cast<Intern> (dlsym (x11, "XInternAtom"));
        auto close = reinterpret_cast<Close> (dlsym (x11, "XCloseDisplay"));
        if (open == nullptr || intern == nullptr || close == nullptr)
            return;
        if (auto* d = open (nullptr))
        {
            for (const auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                                      "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FULLSCREEN",
                                      "_NET_WM_MOVERESIZE", "_NET_FRAME_EXTENTS", "_NET_WORKAREA", "_MOTIF_WM_HINTS", "_WIN_HINTS",
                                      "KWM_WIN_DECORATION", "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", "_NET_WM_WINDOW_TYPE_NORMAL",
                                      "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_ABOVE",
                                      "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
                                      "_NET_WM_PID", "_NET_WM_ICON", "_NET_WM_NAME", "UTF8_STRING", "_NET_ACTIVE_WINDOW",
                                      "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_WINDOW_OPACITY",
                                      "_NET_SUPPORTED", "_NET_WM_ALLOWED_ACTIONS" })
                intern (d, name, 0);
            close (d);
        }
    }
#endif

    /** Real window events where a window system is at hand (a Linux display - Xvfb is enough -
        or OSP_REAL_EVENTS=1); otherwise the presses are routed as JUCE routes them, step by step. */
    bool wantRealWindow()
    {
        if (juce::SystemStats::getEnvironmentVariable ("OSP_REAL_EVENTS", {}) == "1")
            return true;
       #if JUCE_LINUX
        return juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isNotEmpty();
       #else
        return false;
       #endif
    }

    struct PopoverRig
    {
        TempDir tmp;
        ScopedEnvironment documents { "OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName() };
        ScopedEnvironment store { "OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName() };
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        osp::plugin::OspAudioProcessorEditor* ui = nullptr;
        juce::ComponentPeer* peer = nullptr;
        juce::PopupMenu lastMenu;
        int menus = 0;
        // The routed path's own state (no window): the pressed component, as JUCE keeps it.
        juce::Component::SafePointer<juce::Component> pressed;
        juce::Point<float> downAt;

        PopoverRig()
        {
            loadPluck (p);
            editor.reset (p.createEditorIfNeeded());
            ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
            REQUIRE (ui != nullptr);
            editor->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
            if (wantRealWindow())
            {
               #if JUCE_LINUX
                prepareXServer();
               #endif
                editor->addToDesktop (0);
                peer = editor->getPeer();
            }
            editor->setVisible (true);
            ui->refreshNow();
            // The menus the overlay would show, captured (a real PopupMenu would wait for the
            // user); everything up to showing it is the real path.
            overlay().showMenuForTest = [this] (juce::PopupMenu& m) { lastMenu = m; ++menus; };
        }
        ~PopoverRig()
        {
            if (editor != nullptr)
                overlay().showMenuForTest = nullptr;
            juce::ModifierKeys::currentModifiers = {};
        }
        bool realWindow() const noexcept { return peer != nullptr; }
        osp::plugin::ModulationOverlay& overlay() { return ui->modulationOverlay(); }
        /** The control of `id` inside the open popover. */
        juce::Slider* inPopup (const juce::String& id)
        {
            auto* panel = ui->openPopupPanel();
            if (panel == nullptr)
                return nullptr;
            juce::Slider* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
                for (auto* child : c.getChildren())
                {
                    if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && found == nullptr && s->getProperties()["paramId"].toString() == id)
                        found = s;
                    walk (*child);
                }
            };
            walk (*panel);
            return found;
        }
        juce::Point<float> inEditor (juce::Component& c, juce::Point<float> local) { return editor->getLocalPoint (&c, local); }
        juce::Point<float> fromOverlay (juce::Point<float> p) { return editor->getLocalPoint (&overlay(), p); }
        juce::MouseEvent eventFor (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods, juce::Point<float> down)
        {
            const auto local = c.getLocalPoint (editor.get(), at);
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c,
                                     juce::Time::getCurrentTime(), c.getLocalPoint (editor.get(), down), juce::Time::getCurrentTime(), 1, false);
        }
        void send (juce::Point<float> at, juce::ModifierKeys mods)
        {
            juce::ModifierKeys::currentModifiers = mods;
            if (peer != nullptr)
            {
                peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, at, mods, juce::MouseInputSource::defaultPressure,
                                        juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
                pumpMessages();
                return;
            }
            // As JUCE routes a press: the component under the point with the buttons as they are
            // now (its hitTest decides), its own handler, the listeners its parents keep for their
            // nested children (the overlay's on the root), then the desktop's (the editor's
            // outside-click watcher); a drag and the release go to the pressed component.
            const bool down = mods.isAnyMouseButtonDown();
            if (down && pressed == nullptr)
            {
                auto* c = editor->getComponentAt (at.roundToInt());
                if (c == nullptr)
                    return;
                pressed = c;
                downAt = at;
                const auto e = eventFor (*c, at, mods, at);
                c->mouseDown (e);
                if (pressed != nullptr && ! overlay().isParentOf (c))
                    overlay().rightClickedControl (e);
                ui->mouseDownAnywhere (c);
            }
            else if (down)
            {
                if (pressed != nullptr)
                    pressed->mouseDrag (eventFor (*pressed, at, mods, downAt));
            }
            else if (pressed != nullptr)
            {
                auto* c = pressed.getComponent();
                pressed = nullptr;
                c->mouseUp (eventFor (*c, at, mods, downAt));
            }
            else if (auto* c = editor->getComponentAt (at.roundToInt()))
                c->mouseMove (eventFor (*c, at, mods, at));
        }
        /** Escape to whatever has the keyboard focus (the overlay while it edits a range, as it
            takes the focus; else the editor). */
        bool escape()
        {
            const juce::KeyPress key (juce::KeyPress::escapeKey);
            if (peer != nullptr)
            {
                const bool used = peer->handleKeyPress (key);
                pumpMessages();
                return used;
            }
            if (overlay().isEditingMacro() && overlay().keyPressed (key))
                return true;
            return ui->keyPressed (key);
        }
        void move (juce::Point<float> at) { send (at, {}); }
        void press (juce::Point<float> at, bool right = false)
        {
            move (at);
            send (at, juce::ModifierKeys (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier));
        }
        void drag (juce::Point<float> at, bool shift = false)
        {
            send (at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | (shift ? juce::ModifierKeys::shiftModifier : 0)));
        }
        void release (juce::Point<float> at) { send (at, {}); }
        void rightClick (juce::Point<float> at)
        {
            press (at, true);
            release (at);
        }
        void controlClick (juce::Point<float> at)
        {
            // macOS Control-click: the left button with Control, a popup-menu press.
            move (at);
            send (at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
            send (at, juce::ModifierKeys (juce::ModifierKeys::ctrlModifier));
        }
        static const juce::PopupMenu::Item* itemOf (const juce::PopupMenu& menu, const juce::String& text)
        {
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
                if (it.getItem().text.startsWith (text))
                    return &it.getItem();
            return nullptr;
        }
        /** A point on `id`'s halo at a knob position (editor coordinates). */
        juce::Point<float> ring (const juce::String& id, float position)
        {
            auto* s = inPopup (id);
            const auto rotary = s->getRotaryParameters();
            const float angle = rotary.startAngleRadians + position * (rotary.endAngleRadians - rotary.startAngleRadians);
            return fromOverlay (overlay().haloCentre (id).getPointOnCircumference (overlay().haloRadius (id), angle));
        }
        juce::Point<float> handle (const juce::String& id, int macro, int end) { return fromOverlay (overlay().macroHandle (id, macro, end)); }
        float value (const juce::String& id) { return p.parameters.getParameter (id)->getValue(); }
        const osp::plugin::UserMacroState& state() { return *p.userMacroAssignments(); }
    };
} // namespace

TEST_CASE ("popover macros: LIFE stays open through a right-click, Assign to Macro, the halo's ends, Shift, invert, reset, Escape and normal turns", "[plugin][usermacro][popover]")
{
    PopoverRig rig;
    INFO ((rig.realWindow() ? "real window events" : "events routed as JUCE routes them"));
    rig.ui->openPopup (0);   // LIFE
    auto* life = rig.ui->openPopupPanel();
    REQUIRE (life != nullptr);
    rig.ui->refreshNow();
    auto* pitch = rig.inPopup ("life.pitch");
    REQUIRE (pitch != nullptr);
    const auto centre = rig.inEditor (*pitch, pitch->getLocalBounds().getCentre().toFloat());

    // 1. A real right-click on PITCH: its menu, and LIFE still open (the same panel).
    rig.rightClick (centre);
    REQUIRE (rig.menus == 1);
    CHECK (rig.ui->openPopupPanel() == life);
    // 2. Assign to Macro -> Macro 1: assigned, LIFE open, the focused halo on PITCH.
    const auto* item = PopoverRig::itemOf (rig.lastMenu, "Macro 1");
    REQUIRE (item != nullptr);
    item->action();
    rig.ui->refreshNow();
    REQUIRE (rig.state().find (0, "life.pitch") == 0);
    CHECK (rig.ui->openPopupPanel() == life);
    CHECK (rig.overlay().macroFocus() == 0);
    REQUIRE (rig.overlay().macroArcsOf ("life.pitch").size() == 1);
    CHECK (rig.ui->userMacros().bankShown() == 0);   // (the tab never switches)
    // 3. The 100 % end dragged along the ring (past the panel's edge on the way), one undo step.
    const float base = rig.value ("life.pitch");
    const auto undoBefore = rig.p.undo().getNumActionsInCurrentTransaction();
    juce::ignoreUnused (undoBefore);
    const auto from = rig.handle ("life.pitch", 0, 1);
    REQUIRE (from != rig.fromOverlay ({}));
    rig.press (from);
    CHECK (rig.overlay().isEditingMacro());
    CHECK (rig.ui->openPopupPanel() == life);
    rig.drag (from + juce::Point<float> (0.0f, 400.0f));   // (far outside LIFE: the gesture stays)
    CHECK (rig.overlay().isEditingMacro());
    CHECK (rig.ui->openPopupPanel() == life);
    const auto to = rig.ring ("life.pitch", 0.9f);
    rig.drag (to);
    rig.release (to);
    CHECK_FALSE (rig.overlay().isEditingMacro());
    CHECK (rig.ui->openPopupPanel() == life);
    const float highAfterDrag = rig.state().maps[0][0].map.high;
    CHECK (highAfterDrag == Approx (0.9f).margin (0.03f));
    CHECK (rig.value ("life.pitch") == Approx (base));   // (the 100 % end never moves the knob)
    CHECK (rig.p.undo().getUndoDescription() == "Macro range");
    // 4. The 0 % end: the knob itself moves there (made at macro 0 %), LIFE open.
    {
        const auto zero = rig.handle ("life.pitch", 0, 0);
        rig.press (zero);
        const auto target = rig.ring ("life.pitch", 0.2f);
        rig.drag (target);
        rig.release (target);
        CHECK (rig.ui->openPopupPanel() == life);
        CHECK (rig.value ("life.pitch") == Approx (0.2f).margin (0.03f));
    }
    // 5. Inverted: the 100 % end below the 0 % end.
    {
        const auto top = rig.handle ("life.pitch", 0, 1);
        rig.press (top);
        const auto low = rig.ring ("life.pitch", 0.05f);
        rig.drag (low);
        rig.release (low);
        CHECK (rig.ui->openPopupPanel() == life);
        const auto arcs = rig.overlay().macroArcsOf ("life.pitch");
        REQUIRE (arcs.size() == 1);
        CHECK (arcs[0].atMax < arcs[0].atMin);
    }
    // 6. Shift: fine (a fifth of the pointer's travel).
    {
        const float before = rig.overlay().macroArcsOf ("life.pitch")[0].atMax;
        const auto top = rig.handle ("life.pitch", 0, 1);
        rig.press (top);
        const auto further = rig.ring ("life.pitch", before + 0.5f);
        rig.drag (further, true);
        rig.release (further);
        CHECK (rig.overlay().macroArcsOf ("life.pitch")[0].atMax == Approx (before + 0.1f).margin (0.03f));
        CHECK (rig.ui->openPopupPanel() == life);
    }
    // 7. Escape mid-drag: the edit cancelled (mapping and knob as before, no undo step), LIFE open.
    {
        const auto mapping = rig.state();
        const float knob = rig.value ("life.pitch");
        const auto undoText = rig.p.undo().getUndoDescription();
        const auto zero = rig.handle ("life.pitch", 0, 0);
        rig.press (zero);
        rig.drag (rig.ring ("life.pitch", 0.7f));
        REQUIRE (rig.overlay().isEditingMacro());
        CHECK (rig.escape());
        CHECK_FALSE (rig.overlay().isEditingMacro());
        rig.release (rig.ring ("life.pitch", 0.7f));
        CHECK (rig.state() == mapping);
        CHECK (rig.value ("life.pitch") == Approx (knob));
        CHECK (rig.p.undo().getUndoDescription() == undoText);
        CHECK (rig.ui->openPopupPanel() == life);
    }
    // 8. Reset from the control's menu (a real right-click, then its item).
    {
        rig.rightClick (centre);
        REQUIRE (rig.menus == 2);
        CHECK (rig.ui->openPopupPanel() == life);
        const auto* reset = PopoverRig::itemOf (rig.lastMenu, "Reset Macro Range");
        REQUIRE (reset != nullptr);
        reset->action();
        rig.ui->refreshNow();
        CHECK (rig.ui->openPopupPanel() == life);
        const auto arcs = rig.overlay().macroArcsOf ("life.pitch");
        REQUIRE (arcs.size() == 1);
        CHECK (arcs[0].atMax > arcs[0].atMin);
    }
    // 9. A menu cancelled (no item chosen): nothing changes, LIFE open.
    rig.rightClick (centre);
    CHECK (rig.menus == 3);
    CHECK (rig.ui->openPopupPanel() == life);
    // 10. The knob turned normally (a press on its body, a drag): LIFE open, the halo still focused.
    {
        const float before = rig.value ("life.pitch");
        rig.press (centre);
        rig.drag (centre + juce::Point<float> (0.0f, -40.0f));
        rig.release (centre + juce::Point<float> (0.0f, -40.0f));
        CHECK (rig.value ("life.pitch") > before);
        CHECK (rig.ui->openPopupPanel() == life);
        CHECK (rig.overlay().macroFocus() == 0);
    }
    // 11. Undo / redo the range edits: the mapping follows.
    {
        const auto now = rig.state().maps[0][0].map;
        while (rig.p.undo().canUndo() && rig.p.undo().getUndoDescription() != "Macro range")
            rig.p.undo().undo();
        REQUIRE (rig.p.undo().canUndo());
        rig.p.undo().undo();
        CHECK_FALSE (rig.state().maps[0][0].map == now);
        rig.p.undo().redo();
    }
    // 12. macOS Control-click: the same menu, LIFE open (JUCE makes Control a popup-menu press
    // only on macOS; elsewhere it is an ordinary click).
   #if JUCE_MAC
    rig.controlClick (centre);
    CHECK (rig.menus == 4);
    CHECK (rig.ui->openPopupPanel() == life);
   #endif
    // A real right-click is routed to the overlay (its hitTest claims it): the root cause, kept
    // in view - the pressed component is the overlay, never the popover.
    {
        juce::ModifierKeys::currentModifiers = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
        CHECK (rig.editor->getComponentAt (centre.roundToInt()) == &rig.overlay());
        juce::ModifierKeys::currentModifiers = {};
    }
    // 13. A genuine outside click (the empty housing under the cards) still closes LIFE; no
    // gesture left behind.
    const auto outside = rig.inEditor (*rig.editor, { 20.0f, 20.0f });
    rig.press (outside);
    rig.release (outside);
    CHECK (rig.ui->openPopupPanel() == nullptr);
    CHECK_FALSE (rig.overlay().isEditingMacro());
}

TEST_CASE ("popover macros: every popover with eligible knobs keeps itself open through a right-click on its own knob; Escape with nothing active still closes", "[plugin][usermacro][popover]")
{
    PopoverRig rig;
    INFO ((rig.realWindow() ? "real window events" : "events routed as JUCE routes them"));
    using Editor = osp::plugin::OspAudioProcessorEditor;
    const std::vector<std::pair<int, const char*>> popups {
        { 0, "LIFE" }, { 1, "DRIVE" }, { 2, "FILTER" }, { 3, "MOVEMENT" }, { 4, "SPACE" }, { Editor::echoPopup, "ECHO" },
        { Editor::reimaginedPopup, "REIMAGINED" }, { Editor::pitchWheelPopup, "PITCH BEND" }, { Editor::modWheelPopup, "MOD WHEEL" },
        { Editor::advancedPopup, "ADVANCED" }
    };
    for (const auto& [which, name] : popups)
    {
        INFO (name);
        rig.ui->openPopup (which);
        auto* panel = rig.ui->openPopupPanel();
        REQUIRE (panel != nullptr);
        rig.ui->refreshNow();
        // Its first eligible knob (as the overlay finds it: a parameter a USER macro may move).
        juce::Slider* knob = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (! child->isVisible())
                    continue;
                if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && knob == nullptr)
                    if (const auto id = s->getProperties()["paramId"].toString(); id.isNotEmpty() && osp::plugin::usermacros::eligible (rig.p, id))
                        knob = s;
                walk (*child);
            }
        };
        walk (*panel);
        if (knob == nullptr)
        {
            WARN (juce::String (name) + ": no eligible knob");
            rig.ui->closePopup();
            continue;
        }
        const int before = rig.menus;
        rig.rightClick (rig.inEditor (*knob, knob->getLocalBounds().getCentre().toFloat()));
        CHECK (rig.menus == before + 1);
        CHECK (rig.ui->openPopupPanel() == panel);
        CHECK (PopoverRig::itemOf (rig.lastMenu, "Assign to Macro") != nullptr);
        // Escape (nothing being edited): closes it, as ever.
        CHECK (rig.escape());
        if (pumpMessages())
            CHECK (rig.ui->openPopupPanel() == nullptr);
        else
            rig.ui->closePopup();   // (macOS: the posted close cannot be run from here)
    }
}

TEST_CASE ("popover macros: the editor closed while a control's menu is pending - its items do nothing unsafe", "[plugin][usermacro][popover]")
{
    PopoverRig rig;
    INFO ((rig.realWindow() ? "real window events" : "events routed as JUCE routes them"));
    rig.ui->openPopup (0);
    rig.ui->refreshNow();
    auto* pitch = rig.inPopup ("life.pitch");
    REQUIRE (pitch != nullptr);
    rig.rightClick (rig.inEditor (*pitch, pitch->getLocalBounds().getCentre().toFloat()));
    REQUIRE (rig.menus == 1);
    const auto menu = rig.lastMenu;
    rig.overlay().showMenuForTest = nullptr;
    rig.editor.reset();   // (the window closes with the menu still up)
    // Choosing an item afterwards: every item's callback holds safe pointers only.
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
        if (it.getItem().action != nullptr && it.getItem().text.startsWith ("Macro 1"))
            it.getItem().action();
    CHECK (rig.p.userMacroAssignments() != nullptr);
}

TEST_CASE ("popover macros: an LFO's depth dragged on a halo inside a popover keeps the popover open; an outside click still closes it", "[plugin][popover][modulation]")
{
    PopoverRig rig;
    INFO ((rig.realWindow() ? "real window events" : "events routed as JUCE routes them"));
    using Editor = osp::plugin::OspAudioProcessorEditor;
    int tested = 0;
    for (const int which : { 0, 1, 2, 3, 4, Editor::echoPopup })
    {
        INFO ("popup " << which);
        rig.ui->openPopup (which);
        auto* panel = rig.ui->openPopupPanel();
        REQUIRE (panel != nullptr);
        rig.ui->refreshNow();
        // Its first knob a modulation source can reach (a destination).
        juce::Slider* knob = nullptr;
        juce::String id;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                if (! child->isVisible())
                    continue;
                if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && knob == nullptr)
                    if (const auto pid = s->getProperties()["paramId"].toString();
                        pid.isNotEmpty() && osp::plugin::modui::destinationFor (pid, false) != osp::mod::Dest::none)
                    {
                        knob = s;
                        id = pid;
                    }
                walk (*child);
            }
        };
        walk (*panel);
        if (knob == nullptr)
        {
            rig.ui->closePopup();
            continue;
        }
        ++tested;
        const int slot = rig.p.addModulationRoute (osp::mod::Source::lfo1, osp::plugin::modui::destinationFor (id, false), 30.0f);
        REQUIRE (slot >= 0);
        auto* depth = rig.p.parameters.getParameter (osp::plugin::InstrumentHost::modRouteId (slot, "depth"));
        REQUIRE (depth != nullptr);
        const float depthBefore = depth->getValue();
        rig.ui->refreshNow();
        // The halo's ring (where the route's arc is drawn), dragged up: the depth deepens.
        const auto onRing = rig.fromOverlay (rig.overlay().haloCentre (id).getPointOnCircumference (rig.overlay().haloRadius (id), 0.0f));
        rig.press (onRing);
        CHECK (rig.ui->openPopupPanel() == panel);
        rig.drag (onRing + juce::Point<float> (0.0f, -60.0f));
        rig.release (onRing + juce::Point<float> (0.0f, -60.0f));
        CHECK (rig.ui->openPopupPanel() == panel);
        CHECK (depth->getValue() > depthBefore);   // (the route deepened: the halo took the drag)
        rig.ui->closePopup();
    }
    CHECK (tested >= 1);   // (most popover knobs are macro-only; FILTER's reach the modulation)
}

namespace
{
    /** Where `c` sits among the instrument's layers (its index among the content's children). */
    int layerOf (juce::Component& c)
    {
        auto* parent = c.getParentComponent();
        return parent != nullptr ? parent->getIndexOfChildComponent (&c) : -1;
    }
}

TEST_CASE ("routing view: popovers open above the Mixer / FX Routing view and stay there through the timer; the halos and every gesture work; closing one leaves the view open", "[plugin][usermacro][popover][routing]")
{
    PopoverRig rig;
    INFO ((rig.realWindow() ? "real window events" : "events routed as JUCE routes them"));
    auto& ui = *rig.ui;
    ui.setSourceViewShown (true);
    REQUIRE (ui.sourceViewShown());
    auto* routing = ui.sourceViewComponent();
    REQUIRE (routing != nullptr);
    // A single click on LIFE's name (the label's own deferred click, as its timer acts on it).
    auto* label = dynamic_cast<osp::plugin::MacroLabel*> (ui.macroRow()[0].label.get());
    REQUIRE (label != nullptr);
    label->onClick();
    auto* life = ui.openPopupPanel();
    REQUIRE (life != nullptr);
    // Through many timer ticks (the routing branch once raised the view at every layout).
    for (int tick = 0; tick < 10; ++tick)
        ui.refreshNow();
    CHECK (ui.openPopupPanel() == life);
    CHECK (routing->isVisible());
    CHECK (layerOf (*life) > layerOf (*routing));
    CHECK (layerOf (rig.overlay()) > layerOf (*life));   // (its halos above it)
    // The popover's knob takes a real press (not the routing view under it).
    auto* pitch = rig.inPopup ("life.pitch");
    REQUIRE (pitch != nullptr);
    const auto centre = rig.inEditor (*pitch, pitch->getLocalBounds().getCentre().toFloat());
    CHECK (rig.editor->getComponentAt (centre.roundToInt()) != routing);
    CHECK_FALSE (routing->isParentOf (rig.editor->getComponentAt (centre.roundToInt())));
    {
        const float before = rig.value ("life.pitch");
        rig.press (centre);
        rig.drag (centre + juce::Point<float> (0.0f, -30.0f));
        rig.release (centre + juce::Point<float> (0.0f, -30.0f));
        CHECK (rig.value ("life.pitch") > before);
        CHECK (ui.openPopupPanel() == life);
    }
    // Right-click -> Assign to Macro 1 -> the halo -> its 100 % end dragged: LIFE and the view open.
    rig.rightClick (centre);
    REQUIRE (rig.menus == 1);
    CHECK (ui.openPopupPanel() == life);
    const auto* item = PopoverRig::itemOf (rig.lastMenu, "Macro 1");
    REQUIRE (item != nullptr);
    item->action();
    ui.refreshNow();
    REQUIRE (rig.state().find (0, "life.pitch") == 0);
    const auto from = rig.handle ("life.pitch", 0, 1);
    rig.press (from);
    CHECK (rig.overlay().isEditingMacro());
    const auto to = rig.ring ("life.pitch", 0.85f);
    rig.drag (to);
    rig.release (to);
    CHECK (rig.state().maps[0][0].map.high == Approx (0.85f).margin (0.03f));
    CHECK (ui.openPopupPanel() == life);
    CHECK (ui.sourceViewShown());
    // Escape mid-drag: cancelled, LIFE open.
    rig.press (rig.handle ("life.pitch", 0, 1));
    rig.drag (rig.ring ("life.pitch", 0.5f));
    CHECK (rig.escape());
    rig.release (rig.ring ("life.pitch", 0.5f));
    CHECK (rig.state().maps[0][0].map.high == Approx (0.85f).margin (0.03f));
    CHECK (ui.openPopupPanel() == life);
    // Another popover (FILTER), switched to: above the view too.
    ui.openPopup (2);
    for (int tick = 0; tick < 5; ++tick)
        ui.refreshNow();
    auto* filter = ui.openPopupPanel();
    REQUIRE (filter != nullptr);
    CHECK (layerOf (*filter) > layerOf (*routing));
    // A genuine outside click (the housing's corner): the popover closes, the view stays.
    const juce::Point<float> outside { 20.0f, 20.0f };
    rig.press (outside);
    rig.release (outside);
    CHECK (ui.openPopupPanel() == nullptr);
    CHECK (ui.sourceViewShown());
    CHECK (routing->isVisible());
    // A USER macro's inspector (anchored above its name) above the view as well.
    ui.userMacros().showBank (1);
    ui.userMacros().openPanel (0);
    for (int tick = 0; tick < 5; ++tick)
        ui.refreshNow();
    auto* inspector = ui.openPopupPanel();
    REQUIRE (inspector != nullptr);
    CHECK (layerOf (*inspector) > layerOf (*routing));
    ui.closePopup();
    ui.userMacros().showBank (0);
    // A popover already open when the view opens: still above it.
    ui.setSourceViewShown (false);
    ui.openPopup (0);
    ui.setSourceViewShown (true);
    for (int tick = 0; tick < 5; ++tick)
        ui.refreshNow();
    REQUIRE (ui.openPopupPanel() != nullptr);
    CHECK (layerOf (*ui.openPopupPanel()) > layerOf (*routing));
    ui.closePopup();
    // Back to the cards.
    ui.setSourceViewShown (false);
    CHECK_FALSE (routing->isVisible());
    CHECK (ui.engineCard (0).isVisible());
}

TEST_CASE ("routing view: the layering holds at every window size and after a resize with a popover open", "[plugin][popover][routing]")
{
    for (const int width : { 869, 1448, 1810 })
    {
        INFO ("width " << width);
        PopoverRig rig;
        auto& ui = *rig.ui;
        rig.editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        ui.setSourceViewShown (true);
        using E = osp::plugin::OspAudioProcessorEditor;
        for (const int which : { 0, 1, 2, 3, 4, E::echoPopup, E::advancedPopup, E::pitchWheelPopup, E::modWheelPopup })
        {
            ui.openPopup (which);
            ui.refreshNow();
            rig.editor->setSize (width + 40, juce::roundToInt (static_cast<float> (width + 40) * ui.instrumentHeight() / 1448.0f));
            for (int tick = 0; tick < 3; ++tick)
                ui.refreshNow();
            REQUIRE (ui.openPopupPanel() != nullptr);
            CHECK (layerOf (*ui.openPopupPanel()) > layerOf (*ui.sourceViewComponent()));
            rig.editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / 1448.0f));
        }
        ui.closePopup();
    }
}

TEST_CASE ("popover macros: screenshots - a focused halo inside LIFE; with the routing view: LIFE, its knob's menu, a halo being dragged (OSP_POPOVER_SHOTS)", "[.][popover-shots]")
{
    const char* dir = std::getenv ("OSP_POPOVER_SHOTS");
    REQUIRE (dir != nullptr);
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    osp::plugin::skin::setLook (osp::plugin::skin::Look::ceramic);
    PopoverRig rig;
    auto& ui = *rig.ui;
    auto write = [&] (const juce::String& name, juce::Rectangle<int> area = {}) {
        ui.refreshNow();
        const auto image = rig.editor->createComponentSnapshot (area.isEmpty() ? rig.editor->getLocalBounds() : area, true, area.isEmpty() ? 1.0f : 2.0f);
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    auto assignPitch = [&] {
        auto* pitch = rig.inPopup ("life.pitch");
        REQUIRE (pitch != nullptr);
        rig.rightClick (rig.inEditor (*pitch, pitch->getLocalBounds().getCentre().toFloat()));
        const auto* item = PopoverRig::itemOf (rig.lastMenu, "Macro 1");
        REQUIRE (item != nullptr);
        item->action();
        ui.refreshNow();
    };
    auto lifeArea = [&] {
        auto* panel = ui.openPopupPanel();
        return rig.editor->getLocalArea (panel, panel->getLocalBounds()).expanded (30).getIntersection (rig.editor->getLocalBounds());
    };
    // 1. LIFE open, PITCH assigned to Macro 1: the focused halo with its two ends, LIFE still open.
    ui.openPopup (0);
    ui.refreshNow();
    assignPitch();
    write ("01-life-focused-halo");
    write ("02-life-focused-halo-zoom", lifeArea());
    // 2. The routing view, LIFE open above it.
    ui.setSourceViewShown (true);
    for (int t = 0; t < 4; ++t)
        ui.refreshNow();
    write ("03-routing-life-open");
    // 3. PITCH's menu over LIFE (shown inside the editor so the snapshot holds it).
    {
        auto* pitch = rig.inPopup ("life.pitch");
        auto menu = rig.overlay().contextMenu ("life.pitch");
        menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (rig.editor.get()).withTargetComponent (pitch));
        write ("04-routing-life-menu");
        juce::PopupMenu::dismissAllActiveMenus();
    }
    // 4. A halo being dragged inside LIFE (the gesture held for the picture).
    {
        const auto from = rig.handle ("life.pitch", 0, 1);
        rig.press (from);
        rig.drag (rig.ring ("life.pitch", 0.85f));
        write ("05-routing-life-halo-drag");
        rig.release (rig.ring ("life.pitch", 0.85f));
    }
}
