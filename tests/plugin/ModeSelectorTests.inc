// The playback-mode selector (One Shot / Granular) as symbols: the closed key, its menu (names,
// descriptions, the check), choosing, undo, automation, sessions, tooltips and accessibility,
// and the key's place in one, two and three cards. A presentation change only: the parameter,
// its values and the sound are as before. Included at the end of PluginProcessorTests.cpp.

TEST_CASE ("plugin mode selector: the symbol key, its menu, choosing, undo, automation, sessions, layouts", "[plugin][ui][mode-selector]")
{
    using osp::plugin::InstrumentHost;
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 3);
    const auto modeId = [] (int l) { return InstrumentHost::layerParameterId (l, "sourceMode"); };
    // The parameter itself is unchanged: two choices, One Shot by default.
    auto* param = dynamic_cast<juce::AudioParameterChoice*> (p.parameters.getParameter (modeId (0)));
    REQUIRE (param != nullptr);
    CHECK (param->choices.size() == 2);
    CHECK (p.parameterValue (modeId (0)) == 0.0f);
    const auto before = audioHash (renderHeld (p, chord8, 1.0));

    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    e->setSize (1448, 1086);
    ui->refreshNow();
    auto* selector = ui->engineCard (1).modeSelector();
    REQUIRE (selector != nullptr);
    CHECK (selector->iconOnly());
    CHECK (selector->mode() == 0);
    CHECK (selector->getTooltip() == "Playback mode: One Shot");
    CHECK (selector->getDescription() == "Playback mode: One Shot");
    CHECK (selector->getTitle().isNotEmpty());
    // (JUCE hands out a component's handler only on screen: the selector's own, built directly.)
    const auto ownHandler = selector->createAccessibilityHandler();
    auto* handler = ownHandler.get();
    REQUIRE (handler != nullptr);
    CHECK (handler->getRole() == juce::AccessibilityRole::popupMenu);
    REQUIRE (handler->getValueInterface() != nullptr);
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "One Shot");
    CHECK (handler->getActions().contains (juce::AccessibilityActionType::press));

    // The menu: both modes with their names, a line each and a check by the active one.
    auto items = [] (juce::PopupMenu menu) {
        std::vector<juce::PopupMenu::Item> out;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            out.push_back (it.getItem());
        return out;
    };
    {
        const auto menu = items (selector->createMenu());
        REQUIRE (menu.size() == 2);
        CHECK (menu[0].text == "One Shot");
        CHECK (menu[1].text == "Granular");
        CHECK (menu[0].isTicked);
        CHECK_FALSE (menu[1].isTicked);
        CHECK (menu[0].customComponent != nullptr);
        CHECK (menu[1].customComponent != nullptr);
        CHECK (menu[0].customComponent->getDescription() == "Play the original recording");
        CHECK (menu[1].customComponent->getDescription() == "Resynthesize as a grain cloud");
        // Choosing Granular (what its item does): the parameter, the key and the check follow; one undo step.
        p.undo().clearUndoHistory();
        p.undo().beginNewTransaction();
        REQUIRE (menu[1].action != nullptr);
        menu[1].action();
        p.parameters.copyState();
    }
    CHECK (p.parameterValue (modeId (1)) == 1.0f);
    CHECK (selector->mode() == 1);
    CHECK (selector->getTooltip() == "Playback mode: Granular");
    CHECK (handler->getValueInterface()->getCurrentValueAsString() == "Granular");
    {
        const auto menu = items (selector->createMenu());
        CHECK_FALSE (menu[0].isTicked);
        CHECK (menu[1].isTicked);
    }
    CHECK (p.parameterValue (modeId (0)) == 0.0f);   // only B's
    REQUIRE (p.undo().undo());
    CHECK (p.parameterValue (modeId (1)) == 0.0f);
    CHECK (selector->mode() == 0);
    // Host automation moves the key.
    p.setParameterValue (modeId (1), 1.0f);
    CHECK (selector->mode() == 1);
    CHECK (selector->getTooltip() == "Playback mode: Granular");
    p.setParameterValue (modeId (1), 0.0f);
    CHECK (selector->mode() == 0);

    // The key in every layout: a small key (38-44 px) on the header's line, clear of its neighbours.
    for (int layers = 1; layers <= 3; ++layers)
        for (const auto& size : { juce::Point<int> { 1448, 1086 }, juce::Point<int> { 1086, 814 } })
        {
            OspAudioProcessor q;
            loadLayers (q, tmp.dir, layers);
            std::unique_ptr<juce::AudioProcessorEditor> qe (q.createEditorIfNeeded());
            auto* qui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (qe.get());
            REQUIRE (qui != nullptr);
            qe->setSize (size.x, size.y);
            qui->refreshNow();
            for (int l = 0; l < layers; ++l)
            {
                auto& card = qui->engineCard (l);
                auto* key = card.modeSelector();
                REQUIRE (key != nullptr);
                INFO (layers << " layers at " << size.x << ", card " << l);
                const auto r = key->getBounds();
                CHECK (r.getWidth() >= 38);
                CHECK (r.getWidth() <= 44);
                CHECK (card.getLocalBounds().contains (r));
                for (auto* child : card.getChildren())
                    if (child != key && child->isVisible() && child->getBounds().getY() < 60 && child->getBounds().getBottom() > 10)
                        CHECK_FALSE (child->getBounds().intersects (r));
            }
        }

    // Sessions reopen in their modes; the sound is the parameter's, as before.
    p.setParameterValue (modeId (2), 1.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    OspAudioProcessor reopened;
    reopened.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (reopened.waitForLoads (30000));
    reopened.pollLoads();
    std::unique_ptr<juce::AudioProcessorEditor> re (reopened.createEditorIfNeeded());
    auto* rui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (re.get());
    REQUIRE (rui != nullptr);
    rui->refreshNow();
    CHECK (rui->engineCard (2).modeSelector()->mode() == 1);
    CHECK (rui->engineCard (0).modeSelector()->mode() == 0);
    p.setParameterValue (modeId (2), 0.0f);
    e.reset();
    CHECK (audioHash (renderHeld (p, chord8, 1.0)) == before);
}

TEST_CASE ("plugin mode selector: a product's own first-mode name keeps the named key and plain menu", "[plugin][ui][mode-selector]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    e->setSize (1448, 1086);
    ui->refreshNow();
    auto& card = ui->engineCard (0);
    card.setFirstModeName ("Standard");   // as a shell generator does
    card.setModeFitsName (true);
    auto* selector = card.modeSelector();
    REQUIRE (selector != nullptr);
    CHECK_FALSE (selector->iconOnly());
    CHECK (selector->getWidth() > 60);
    CHECK (selector->getTooltip().contains ("play the recording through"));
    const auto menu = selector->createMenu();
    juce::PopupMenu::MenuItemIterator it (menu);
    REQUIRE (it.next());
    CHECK (it.getItem().text == "Standard");
    CHECK (it.getItem().customComponent == nullptr);
}
