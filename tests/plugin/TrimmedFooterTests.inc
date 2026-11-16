// OSP's trimmed footer: the keyboard row as designed (keys in their recess, PITCH and MOD under
// the wheels), 8-12 px of room under the captions, then the 22 px front apron and the edge
// (layout::trimmedHeight 1046 instead of 1086), nothing above moved or scaled. The status line is
// hidden (its essential messages go to the notice). The shell (sister products) draws the same.

TEST_CASE ("plugin: OSP's trimmed footer - the instrument ends under the keys at every size", "[ui][andor-ui]")
{
    namespace design = osp::plugin::design;
    namespace layout = osp::plugin::design::layout;
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (base.get());
    REQUIRE (ui != nullptr);

    // The canvas: the keyboard row, the room under the captions and the apron (the full design stays 1086).
    CHECK (ui->instrumentHeight() == Approx (layout::apronTop + layout::underKeyReveal + layout::apronHeight));
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
        // PITCH and MOD as designed (caption line 88 px under the wheel's top, MOD's socket with
        // it), and the wheels' area ends where the apron starts: nothing clickable in the apron.
        for (auto* wheel : { &ui->pitchBendWheel(), &ui->modulationWheel() })
        {
            CHECK (wheel->getBottom() == juce::roundToInt (layout::apronTop));
            CHECK (wheel->socketArea().getY() == Approx (88.0f));
        }
        // 8-12 px of clear room between the captions' lowest visible pixel and the apron, measured
        // in the rendered window (converted to instrument px).
        {
            const auto shot = ui->createComponentSnapshot (ui->getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
            const float k = static_cast<float> (width) / design::width;
            int lowest = -1;
            for (int y = juce::roundToInt (1001.0f * k); y < juce::roundToInt (layout::apronTop * k); ++y)
                for (int x = juce::roundToInt (layout::pitchWheel.getX() * k); x < juce::roundToInt (layout::modWheel.getRight() * k); ++x)
                    if (shot.getPixelAt (x, y).getPerceivedBrightness() < 0.55f)
                        lowest = y;
            REQUIRE (lowest > 0);
            const float clearance = layout::apronTop - (static_cast<float> (lowest) + 1.0f) / k;
            INFO ("caption clearance " << clearance << " instrument px");
            CHECK (clearance >= 7.5f);
            CHECK (clearance <= 12.5f);
        }
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
