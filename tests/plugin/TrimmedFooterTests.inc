// OSP's trimmed footer: under the keys' fronts a 2 px reveal and a 20 px front apron, then the
// instrument's edge (layout::trimmedHeight 1036 instead of 1086), with nothing above it moved or
// scaled. The status line is hidden (its essential messages go to the notice). Sister products
// keep the full 1086 px design.

TEST_CASE ("plugin: OSP's trimmed footer - the instrument ends under the keys at every size", "[ui][andor-ui]")
{
    namespace design = osp::plugin::design;
    namespace layout = osp::plugin::design::layout;
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (base.get());
    REQUIRE (ui != nullptr);

    // The canvas: the keyboard (its bottom 1014), the reveal and the apron; the sister products' stays 1086.
    CHECK (ui->instrumentHeight() == Approx (layout::keyboard.getBottom() + layout::underKeyReveal + layout::apronHeight));
    CHECK (osp::plugin::frame::instrumentHeight (false) == Approx (design::height));
    CHECK (osp::plugin::frame::instrumentHeight (true, true) == Approx (layout::trimmedHeight + layout::arpShift));

    // Every supported width (the resize limits and the default): the window keeps the trimmed
    // shape, the keyboard, the wheels and the ARP cards sit where they always did (instrument
    // coordinates), and the window's foot is the reveal and the apron under the keys.
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
        // The wheels' captions sit in the apron (clear of the keyboard row's foot); the slots stay.
        {
            const auto& wheel = ui->modulationWheel();
            const float captionTop = static_cast<float> (wheel.getY()) + wheel.captionLine();
            CHECK (captionTop >= layout::keyboard.getBottom() + layout::underKeyReveal);
            CHECK (captionTop + 18.0f <= layout::trimmedHeight);
            CHECK (wheel.socketArea().getY() == Approx (wheel.captionLine()));   // the socket follows the caption
        }
        // In instrument px the keys end the reveal plus the apron above the foot.
        const auto foot = keys.getParentComponent()->getLocalArea (ui, ui->getLocalBounds()).getBottom();
        CHECK (static_cast<float> (foot - keys.getBottom()) == Approx (layout::underKeyReveal + layout::apronHeight).margin (1.0f));
        if (auto* shape = ui->getConstrainer())
            CHECK (shape->getFixedAspectRatio() == Approx (design::width / layout::trimmedHeight));
    }

    // ARP open: the window grows by the arpeggiator's editor only; the apron stays under the keys.
    ui->setSize (1448, juce::roundToInt (layout::trimmedHeight));
    ui->setArpExpanded (true);
    ui->refreshNow();
    CHECK (ui->getHeight() == juce::roundToInt (layout::trimmedHeight + layout::arpShift));
    CHECK (ui->keyboardComponent().getBottom() == juce::roundToInt (layout::keyboard.getBottom() + layout::arpShift));
    ui->setArpExpanded (false);
}
