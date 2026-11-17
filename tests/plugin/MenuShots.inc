// The main menu as the editor opens it, rendered inside the editor (a child window, so a
// headless snapshot holds it): OSP_MENU_SHOTS=<folder> OSP_MENU_PREFIX=before|after
// osp_plugin_tests "[menu-shots]". Only APIs that existed before the menu work.

namespace
{
    void menuShot (juce::AudioProcessorEditor& editor, juce::PopupMenu& menu, juce::Component& target, const juce::File& file)
    {
        juce::PopupMenu::dismissAllActiveMenus();
        menu.showMenuAsync (juce::PopupMenu::Options().withParentComponent (&editor).withTargetComponent (&target));
        juce::Rectangle<int> area;
        for (auto* child : editor.getChildren())
            if (child->getName().isEmpty() && dynamic_cast<juce::AudioProcessorEditor*> (child) == nullptr && child->isVisible()
                && child->getComponentID().isEmpty() && child != editor.getChildComponent (0))
                area = area.isEmpty() ? child->getBounds() : area.getUnion (child->getBounds());
        const auto shotArea = (area.isEmpty() ? editor.getLocalBounds() : area.getUnion (target.getBounds().translated (0, 0)).expanded (24)).getIntersection (editor.getLocalBounds());
        const auto image = editor.createComponentSnapshot (shotArea, true, 2.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        juce::PopupMenu::dismissAllActiveMenus();
    }
}

TEST_CASE ("menus: screenshots of the main menu (OSP_MENU_SHOTS)", "[.][menu-shots]")
{
    const char* dir = std::getenv ("OSP_MENU_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    const juce::String prefix = std::getenv ("OSP_MENU_PREFIX") != nullptr ? juce::String (std::getenv ("OSP_MENU_PREFIX")) : juce::String ("after");
    TempDir tmp;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    e->setSize (1448, juce::roundToInt (ui->instrumentHeight() * 1448.0f / osp::plugin::design::width));
    juce::Array<juce::File> list;
    list.add (writeSource (tmp.dir, "Cello Sustain A2.wav", testsignals::vowel (midiToHz (45), 1.5, 48000.0, 3)));
    list.add (writeSource (tmp.dir, "Glass Pluck E4.wav", testsignals::pluck (midiToHz (64), 1.5, 48000.0, 4)));
    p.addLayers (list);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    ui->refreshNow();
    juce::PopupMenu captured;
    ui->showMenuForTest = [&captured] (juce::PopupMenu& m) { captured = m; };
    // The header's menu key: the rightmost button in the header row.
    juce::Component* key = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (child); b != nullptr && (b->getTooltip().startsWith ("Sounds, presets") || b->getTooltip().startsWith ("Instrument, sources")))
                key = child;
            else if (auto* b2 = dynamic_cast<juce::Button*> (child); b2 != nullptr && b2->getTitle() == "Menu")
                key = child;
            find (*child);
        }
    };
    find (*e);
    REQUIRE (key != nullptr);
    dynamic_cast<juce::Button*> (key)->onClick();
    REQUIRE (captured.getNumItems() > 0);
    menuShot (*e, captured, *key, out.getChildFile (prefix + "-01-main-menu.png"));
}
