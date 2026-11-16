// OSP's trimmed footer: the instrument ends 3 px under the keys' fronts (layout::trimmedHeight)
// instead of at 1086, with nothing above it moved or scaled. The status line is hidden (its
// essential messages go to the notice). Sister products keep the full 1086 px design.

TEST_CASE ("plugin: OSP's trimmed footer - the instrument ends under the keys at every size", "[ui][andor-ui]")
{
    namespace design = osp::plugin::design;
    namespace layout = osp::plugin::design::layout;
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (base.get());
    REQUIRE (ui != nullptr);

    // The canvas: 3 px under the keyboard (its bottom 1014); the sister products' stays 1086.
    CHECK (ui->instrumentHeight() == Approx (layout::keyboard.getBottom() + 3.0f));
    CHECK (osp::plugin::frame::instrumentHeight (false) == Approx (design::height));
    CHECK (osp::plugin::frame::instrumentHeight (true, true) == Approx (layout::trimmedHeight + layout::arpShift));

    // Every supported width (the resize limits and the default): the window keeps the trimmed
    // shape, the keyboard, the wheels and the ARP cards sit where they always did (instrument
    // coordinates), and the window's foot is 3 px under the keys - never cutting anything.
    for (const int width : { 869, 1086, 1448, 1810 })
    {
        const int height = juce::roundToInt (static_cast<float> (width) * layout::trimmedHeight / design::width);
        ui->setSize (width, height);
        ui->refreshNow();
        INFO ("width " << width);
        CHECK (ui->getHeight() == height);
        const auto& keys = ui->keyboardComponent();
        CHECK (keys.getBounds().toFloat() == layout::keyboard);
        CHECK (ui->arpControl().getBounds().toFloat() == layout::arpControl);
        CHECK (ui->advancedSettingsButton().getBottom() <= juce::roundToInt (layout::trimmedHeight));
        CHECK (ui->modulationWheel().getY() == juce::roundToInt (layout::modWheel.getY()));
        // In window pixels the keys end 3 instrument px above the foot.
        const auto foot = keys.getParentComponent()->getLocalArea (ui, ui->getLocalBounds()).getBottom();
        CHECK (static_cast<float> (foot - keys.getBottom()) == Approx (3.0f).margin (1.0f));
        if (auto* shape = ui->getConstrainer())
            CHECK (shape->getFixedAspectRatio() == Approx (design::width / layout::trimmedHeight));
    }

    // ARP open: the window grows by the arpeggiator's editor only; the foot stays 3 px under the keys.
    ui->setSize (1448, juce::roundToInt (layout::trimmedHeight));
    ui->setArpExpanded (true);
    ui->refreshNow();
    CHECK (ui->getHeight() == juce::roundToInt (layout::trimmedHeight + layout::arpShift));
    CHECK (ui->keyboardComponent().getBottom() == juce::roundToInt (layout::keyboard.getBottom() + layout::arpShift));
    ui->setArpExpanded (false);
}
