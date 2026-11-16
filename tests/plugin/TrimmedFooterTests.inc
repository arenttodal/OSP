// OSP's trimmed footer: the keyboard row as designed (keys in their recess, PITCH and MOD under
// the wheels), 8-12 px of room under the captions, then the 22 px front apron and the edge
// (layout::trimmedHeight 1046 instead of 1086), nothing above moved or scaled. The status line is
// hidden (its essential messages go to the notice). The shell (sister products) draws the same.


namespace trimmedfooter
{
    /** The caption checks shared by OSP and the shell: drawn text and socket never clipped by the
        wheel's own bounds, at least 6 px apart from their neighbours, and clear of the apron. */
    template <typename Ui>
    void checkCaptions (Ui& ui)
    {
        namespace layout = osp::plugin::design::layout;
        auto absolute = [] (osp::plugin::Wheel& w, juce::Rectangle<float> r) { return r.translated (static_cast<float> (w.getX()), static_cast<float> (w.getY())); };
        auto& pitch = ui.pitchBendWheel();
        auto& mod = ui.modulationWheel();
        for (auto* w : { &pitch, &mod })
        {
            const auto local = w->getLocalBounds().toFloat();
            CHECK (local.contains (w->captionBounds()));                       // the text (and socket) is never clipped
            if (! w->socketCircle().isEmpty())
                CHECK (local.contains (w->socketCircle()));                    // the whole circle is drawn
            CHECK (w->getBottom() == juce::roundToInt (layout::apronTop));      // nothing clickable in the apron
            CHECK (w->socketArea().getY() == Approx (88.0f));                   // the caption line as designed
            CHECK (layout::apronTop - absolute (*w, w->captionBounds()).getBottom() >= 7.5f);
        }
        // The slots exactly where they always were (the components only gained caption room).
        CHECK (pitch.getBounds().toFloat().getCentreX() == Approx (layout::pitchWheel.getCentreX()));
        CHECK (mod.getBounds().toFloat().getCentreX() == Approx (layout::modWheel.getCentreX()));
        const auto p = absolute (pitch, pitch.captionBounds());
        const auto m = absolute (mod, mod.captionBounds());
        CHECK (p.getX() - layout::housing.getX() >= 6.0f);       // clear of the teak rail
        CHECK (m.getX() - p.getRight() >= 6.0f);                  // PITCH and MOD apart
        CHECK (layout::keyboard.getX() - 2.0f - m.getRight() >= 6.0f);   // MOD's socket clear of the key recess
        // Only the designed column and the caption respond: a click between the two captions,
        // beside PITCH's slot, does not reach either wheel.
        CHECK_FALSE (pitch.hitTest (juce::roundToInt (pitch.getWidth() - 3), 40));
        CHECK (pitch.hitTest (juce::roundToInt (0.5f * pitch.getWidth()), 40));
        CHECK (mod.hitTest (juce::roundToInt (m.getRight() - static_cast<float> (mod.getX()) - 2.0f), 97));   // the socket
    }

    /** The teak rails' shadow on the ivory runs on through the apron: across the band beside each
        rail, the profile under the apron is the profile above it, only shaded (no patch, no step). */
    inline void checkRailShadow (const juce::Image& shot, float k)
    {
        namespace layout = osp::plugin::design::layout;
        auto lum = [&] (float x, float y) { return 255.0f * shot.getPixelAt (juce::roundToInt (x * k), juce::roundToInt (y * k)).getPerceivedBrightness(); };
        for (const auto [edge, dir] : { std::pair<float, float> { layout::housing.getX(), 1.0f }, std::pair<float, float> { layout::housing.getRight() - 1.0f, -1.0f } })
        {
            INFO ("rail edge " << edge);
            // The apron is a translucent shade (towards #302418, luminance ~38): it scales each
            // pixel's distance from that colour by the same factor, so the ratio below/above is
            // the same across the rail's shadow as on plain ivory. An opaque fill (the bug) turned
            // the shadow's 81 into the face's 216: a ratio near 5 instead of 0.95.
            const float above = layout::apronTop - 3.0f, below = layout::apronTop + 12.0f;
            auto ratio = [&] (float x) { return (lum (x, below) - 38.0f) / std::max (1.0f, lum (x, above) - 38.0f); };
            const float plain = ratio (edge + dir * 30.0f);
            float worst = 0.0f;
            for (float d = 1.0f; d <= 10.0f; d += 1.0f)
                worst = std::max (worst, std::abs (ratio (edge + dir * d) - plain));
            CHECK (worst <= 0.2f);   // the rail shadow's shape continues through the apron
        }
    }
}

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
        // PITCH and MOD as designed, never clipped, apart from their neighbours, nothing
        // clickable in the apron.
        trimmedfooter::checkCaptions (*ui);
        // 8-12 px of clear room between the captions' lowest visible pixel and the apron, measured
        // in the rendered window (converted to instrument px).
        {
            const auto shot = ui->createComponentSnapshot (ui->getLocalBounds(), true, 1.0f, juce::SoftwareImageType());
            const float k = static_cast<float> (width) / design::width;
            trimmedfooter::checkRailShadow (shot, k);
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
