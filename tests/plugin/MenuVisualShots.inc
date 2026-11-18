// The shared menu look (docs/research/menu-visual): every real menu, opened by its own code
// path as a host shows it (its own desktop window), drawn over the editor where it stands. OSP_MENU_VISUAL_SHOTS=<folder> OSP_MENU_PREFIX=before|after
// osp_plugin_tests "[menu-visual-shots]".

namespace menuvisual
{
    bool isMenuWindow (juce::Component& c)
    {
        return juce::String (typeid (c).name()).contains ("MenuWindow") && c.isVisible();
    }

    /** Every open menu window: on the desktop (how a menu opens in a host: its own window) or
        inside `editor` (a parented menu), in screen order of opening. */
    std::vector<juce::Component*> menuWindows (juce::Component& editor)
    {
        std::vector<juce::Component*> found;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* c = desktop.getComponent (i); c != nullptr && isMenuWindow (*c))
                found.push_back (c);
        for (auto* child : editor.getChildren())
            if (isMenuWindow (*child))
                found.push_back (child);
        return found;
    }

    /** The editor around the open menus, with each menu window drawn where it stands on screen
        (a desktop window is not part of the editor's own snapshot). */
    void shot (juce::Component& editor, juce::Component* anchor, const juce::File& file, bool dismiss = true)
    {
        const auto origin = editor.getScreenPosition();
        auto windows = menuWindows (editor);
        auto inEditor = [&] (juce::Component* w) { return w->getParentComponent() == &editor ? w->getBounds() : w->getScreenBounds() - origin; };
        juce::Rectangle<int> area;
        for (auto* w : windows)
            area = area.isEmpty() ? inEditor (w) : area.getUnion (inEditor (w));
        if (anchor != nullptr)
            area = area.getUnion (editor.getLocalArea (anchor->getParentComponent(), anchor->getBounds()));
        area = (area.isEmpty() ? editor.getLocalBounds() : area.expanded (28)).getIntersection (editor.getLocalBounds());
        constexpr float scale = 2.0f;
        auto image = editor.createComponentSnapshot (area, true, scale);
        {
            juce::Graphics g (image);
            for (auto* w : windows)
                if (w->getParentComponent() != &editor)
                {
                    const auto b = inEditor (w);
                    const auto picture = w->createComponentSnapshot (w->getLocalBounds(), true, scale);
                    g.drawImageAt (picture, juce::roundToInt ((float) (b.getX() - area.getX()) * scale), juce::roundToInt ((float) (b.getY() - area.getY()) * scale));
                }
        }
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        if (! dismiss)
            return;
        juce::PopupMenu::dismissAllActiveMenus();
        pumpMessages();   // (the dismissed windows go before the next menu opens)
    }

    /** The pointer over the `index`th row of `window` (in screen order), as a hover: the menu
        follows the mouse on its own timer. */
    void hoverRow (juce::Component& window, int index)
    {
        std::vector<juce::Component*> rows;
        for (auto* c : window.getChildren())
            if (c->isVisible())
                rows.push_back (c);
        std::sort (rows.begin(), rows.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
        if (index >= static_cast<int> (rows.size()))
            return;
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (rows[static_cast<std::size_t> (index)]->getScreenBounds().getCentre().toFloat());
        for (int i = 0; i < 8; ++i)
        {
            juce::Thread::sleep (40);
            pumpMessages();
        }
    }

    juce::Button* findButton (juce::Component& root, const std::function<bool (juce::Button&)>& match)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && match (*b))
                return b;
            if (auto* inner = findButton (*child, match))
                return inner;
        }
        return nullptr;
    }

    /** The row of the item named `text` in `menu` (headers, separators and items as the look
        measures them), for placing a flyout beside it. */
    int rowTop (juce::LookAndFeel& lf, const juce::PopupMenu& menu, const juce::String& text)
    {
        int y = 0;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.text == text && ! item.isSectionHeader)
                return y;
            int w = 0, h = 0;
            if (item.isSectionHeader)
                lf.getIdealPopupMenuSectionHeaderSizeWithOptions (item.text, -1, w, h, juce::PopupMenu::Options());
            else
                lf.getIdealPopupMenuItemSizeWithOptions (item.text, item.isSeparator, -1, w, h, juce::PopupMenu::Options());
            y += h;
        }
        return -1;
    }
}

TEST_CASE ("menus: the shared look on every menu, before and after (OSP_MENU_VISUAL_SHOTS)", "[.][menu-visual-shots]")
{
    using namespace menuvisual;
    const char* dir = std::getenv ("OSP_MENU_VISUAL_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    const juce::String prefix = std::getenv ("OSP_MENU_PREFIX") != nullptr ? juce::String (std::getenv ("OSP_MENU_PREFIX")) : juce::String ("after");
    auto file = [&] (const char* name) { return out.getChildFile (prefix + "-" + name + ".png"); };

    // A bare X server composites nothing: the translucent card is forced so the design is seen.
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (true);
    LibraryRig rig;   // (an isolated library with three sounds, the editor at 1448 x 1086, the Library open)
    auto& ui = *rig.ui;
    auto& editor = *rig.editor;
    {
        juce::Array<juce::File> list;
        list.add (writeSource (rig.tmp.dir, "Cello Sustain A2.wav", testsignals::vowel (midiToHz (45), 1.5, 48000.0, 3)));
        list.add (writeSource (rig.tmp.dir, "Glass Pluck E4.wav", testsignals::pluck (midiToHz (64), 1.5, 48000.0, 4)));
        rig.p.addLayers (list);
        REQUIRE (rig.p.waitForLoads (30000));
        rig.p.pollLoads();
    }
    // Categorised user presets (the preset dropdown's categories and their flyouts).
    for (const auto* name : { "Keys - Felt Upright", "Keys - Glass Rhodes", "Keys - Night Celesta", "Keys - Toy Piano", "Pad - Slow Bloom",
                              "Pad - Wide Air", "Lead - Saw Glide", "Pluck - Kalimba", "Morning Light", "Cathedral" })
        savePresetAs (rig.p, name);
    REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
    rig.p.refreshPresetIndex();
    ui.refreshNow();

    // 7. The Library's context menu (a sound row), while the Library is open.
    rig.sounds();
    REQUIRE (rig.panel->rowCount() > 0);
    rig.panel->showRowMenuForTest (0, rig.panel->getScreenBounds().getCentre());
    shot (editor, nullptr, file ("07-library-menu"));
    ui.closeLibrary();
    ui.refreshNow();

    // 1. The global menu, from its key (the real handler).
    auto* key = findButton (editor, [] (juce::Button& b) { return b.getTitle() == "Menu" || b.getTooltip().startsWith ("Instrument, sources"); });
    REQUIRE (key != nullptr);
    key->onClick();
    if (const auto windows = menuWindows (editor); ! windows.empty())
        hoverRow (*windows.front(), 2);   // (Save instrument: the highlight)
    shot (editor, key, file ("01-global-menu"));

    // 2, 3. Source A and B's menus, from their cards' menu keys.
    for (int layer = 0; layer < 2; ++layer)
    {
        auto& card = ui.engineCard (layer);
        auto* dots = findButton (card, [] (juce::Button& b) { return b.getTooltip().startsWith ("Replace, root"); });
        REQUIRE (dots != nullptr);
        card.onMenu (layer, *dots);
        shot (editor, dots, file (layer == 0 ? "02-source-a-menu" : "03-source-b-menu"));
    }

    // 4. The preset dropdown, from the preset bar; 5. a category's flyout beside its row.
    {
        auto& bar = ui.presetBarComponent();
        auto menu = bar.presetMenu();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bar).withMaximumNumColumns (1));
        shot (editor, &bar, file ("04-preset-menu"), false);   // (kept open: 5 adds its flyout)
        juce::PopupMenu flyout;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text == "Keys" && it.getItem().subMenu != nullptr)
                flyout = *it.getItem().subMenu;
        REQUIRE (flyout.getNumItems() > 0);
        const auto windows = menuWindows (editor);
        REQUIRE (! windows.empty());
        auto& lf = windows.front()->getLookAndFeel();   // (the look the menu really took)
        const int border = lf.getPopupMenuBorderSizeWithOptions (juce::PopupMenu::Options());
        const int top = rowTop (lf, menu, "Keys");
        const auto parent = windows.front()->getScreenBounds();
        // (JUCE's rows span the whole window: its border is vertical only.)
        const juce::Rectangle<int> row (parent.getX(), parent.getY() + border + juce::jmax (0, top), parent.getWidth(), 26);
        // Where JUCE places a submenu: its left edge on the row's right, its top a border above the
        // row (a 1 px target just above that point: a menu opens below its target).
        flyout.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ row.getRight(), row.getY() - border - 1, 1, 1 }).withMaximumNumColumns (1));
        shot (editor, &bar, file ("05-category-submenu"));
    }

    // 6. A USER macro's right-click menu (its label's menu), in the USER bank.
    ui.userMacros().showBank (1);
    ui.refreshNow();
    ui.userMacros().labelMenu (0);
    shot (editor, nullptr, file ("06-user-macro-menu"));
    ui.userMacros().showBank (0);

    // 8. The FX routing view's settings (its gear).
    ui.sourceViewToggle().onClick();   // (a click: triggerClick would post it)
    ui.refreshNow();
    REQUIRE (ui.sourceViewShown());
    REQUIRE (ui.sourceViewComponent() != nullptr);
    auto* gear = findButton (*ui.sourceViewComponent(), [] (juce::Button& b) { return b.getTooltip() == "Routing settings"; });
    REQUIRE (gear != nullptr);
    gear->onClick();
    shot (editor, gear, file ("08-fx-routing-menu"));
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (false);
}

TEST_CASE ("menus: the ANDOR look is every menu's while an editor is open, and only then", "[plugin][menus][menu-visual]")
{
    // A PopupMenu without a look of its own opens a desktop window that takes JUCE's default
    // look: while an editor is open, that is the shared OspLookAndFeel (SharedMenuLook).
    auto shared = [] { return dynamic_cast<osp::plugin::OspLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()) != nullptr; };
    CHECK_FALSE (shared());
    {
        OspAudioProcessor a;
        std::unique_ptr<juce::AudioProcessorEditor> first (a.createEditorIfNeeded());
        CHECK (shared());
        auto* look = &juce::LookAndFeel::getDefaultLookAndFeel();
        {
            OspAudioProcessor b;
            std::unique_ptr<juce::AudioProcessorEditor> second (b.createEditorIfNeeded());
            CHECK (&juce::LookAndFeel::getDefaultLookAndFeel() == look);   // (one look, shared)
        }
        CHECK (shared());   // (the other editor still holds it)
    }
    CHECK_FALSE (shared());   // the last editor gone: JUCE's own again
}

TEST_CASE ("menus: the card sits on its anchor and a submenu opens edge to edge, at every UI scale", "[plugin][menus][menu-visual]")
{
    using namespace menuvisual;
    if (! wantRealWindow())
        SKIP ("needs a window system (a Linux display; Xvfb is enough)");
   #if JUCE_LINUX
    prepareXServer();
   #endif
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (true);
    IsolatedPresets isolated;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    // On screen, as in a host (JUCE closes a menu whose anchor is not showing).
    e->setTopLeftPosition (0, 0);
    e->setVisible (true);
    e->addToDesktop (0);
    constexpr int margin = osp::plugin::OspLookAndFeel::popupShadowMargin;
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        INFO ("UI scale " << scale);
        ui->setScaleFactor (scale);
        auto& bar = ui->presetBarComponent();
        auto menu = bar.presetMenu();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bar).withMaximumNumColumns (1));
        auto windows = menuWindows (*e);
        REQUIRE (windows.size() == 1);
        auto* top = windows.front();
        CHECK (dynamic_cast<osp::plugin::OspLookAndFeel*> (&top->getLookAndFeel()) != nullptr);
        CHECK_FALSE (top->isOpaque());
        // The card's left edge on the anchor's, its top 3 px under it (the menu's own units: a
        // menu window is scaled as its anchor is, by the editor's size and the UI scale).
        const float drawn = juce::Component::getApproximateScaleFactorForComponent (&bar);
        const auto anchor = bar.getScreenBounds().toFloat() / drawn;
        const auto window = top->getBounds().toFloat();
        CHECK (window.getX() + static_cast<float> (margin) == Catch::Approx (anchor.getX()).margin (2.0));   // (scaled units round)
        CHECK (window.getY() + static_cast<float> (margin) == Catch::Approx (anchor.getBottom() + 3.0f).margin (2.0));
        // A real submenu, opened from its row as the keyboard does it (Down to the row, Right to
        // open: JUCE's own submenu path, as a hover takes): its card meets the parent's, 3 px
        // over it, its first row level with the row that opened it.
        int index = 0, row = -1, presses = 0;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next(); ++index)
        {
            const auto& item = it.getItem();
            if (row < 0 && ! item.isSeparator && ! item.isSectionHeader && item.isEnabled)
                ++presses;
            if (item.text == "Starting States")
                row = index;
        }
        REQUIRE (row >= 0);
        for (int k = 0; k < presses; ++k)
            top->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        top->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        windows = menuWindows (*e);   // (at once: a headless server has no focused window to keep menus open for long)
        REQUIRE (windows.size() == 2);
        auto* sub = windows.front() == top ? windows.back() : windows.front();
        CHECK (sub->getX() + margin == top->getRight() - margin - 3);
        std::vector<juce::Component*> rows;
        for (auto* c : top->getChildren())
            if (c->isVisible())
                rows.push_back (c);
        std::sort (rows.begin(), rows.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
        const int border = top->getLookAndFeel().getPopupMenuBorderSize();
        CHECK (std::abs (sub->getY() + border - (top->getY() + rows[static_cast<std::size_t> (row)]->getY())) <= 2);
        juce::PopupMenu::dismissAllActiveMenus();
        pumpMessages();
    }
    e->removeFromDesktop();
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (false);
}
