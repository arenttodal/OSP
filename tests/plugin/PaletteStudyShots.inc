// CORE macro palette study (Gate 2 of the menu/content work): the CORE bank in the current
// palette, with only macros 3 and 4 (FILTER, MOVEMENT) refined, and with the harmonised palette;
// the USER bank (its own frozen palette, unchanged); the effect pedals. Native size (scale 1) and
// 2x. The macro beads are rendered lens sprites (tools/blender-assets/skin): for the candidate
// palettes their pixels are recoloured here from the rendered lens (shading and highlight kept) as
// a preview of a re-render; nothing ships until the palette is approved.
// OSP_PALETTE_SHOTS=<folder> osp_plugin_tests "[palette-shots]".

namespace
{
    using MacroPalette = std::array<juce::Colour, 6>;   // LIFE, DRIVE, FILTER, MOVEMENT, SPACE, ECHO (enum order)

    MacroPalette currentPalette()
    {
        MacroPalette p;
        for (int i = 0; i < 6; ++i)
            p[static_cast<std::size_t> (i)] = osp::plugin::design::colour::macro (i);
        return p;
    }

    /** A lens pixel recoloured from its rendered colour to the candidate's, keeping the
        rendered shading: the candidate's hue; saturation and brightness scaled by the two
        colours' ratio (so the satin, the dome's shade and the highlight stay). The lens only
        (its 9 px), not the housing around it. */
    void recolourBeads (juce::Image& image, const std::vector<juce::Point<float>>& centres, float radius, const MacroPalette& from, const MacroPalette& to)
    {
        juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
        for (std::size_t m = 0; m < centres.size(); ++m)
        {
            const auto a = from[m], b = to[m];
            if (a == b)
                continue;
            const auto c = centres[m];
            for (int y = juce::roundToInt (c.y - radius); y <= juce::roundToInt (c.y + radius); ++y)
                for (int x = juce::roundToInt (c.x - radius); x <= juce::roundToInt (c.x + radius); ++x)
                {
                    if (x < 0 || y < 0 || x >= image.getWidth() || y >= image.getHeight() || c.getDistanceFrom ({ static_cast<float> (x), static_cast<float> (y) }) > radius)
                        continue;
                    const auto px = data.getPixelColour (x, y);
                    const float sat = juce::jlimit (0.0f, 1.0f, px.getSaturation() * b.getSaturation() / juce::jmax (0.05f, a.getSaturation()));
                    const float bri = juce::jlimit (0.0f, 1.0f, px.getBrightness() * b.getBrightness() / juce::jmax (0.05f, a.getBrightness()));
                    data.setPixelColour (x, y, juce::Colour::fromHSV (b.getHue(), sat, bri, px.getFloatAlpha()));
                }
        }
    }

    void writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    /** The macro bank of a fresh editor (built in the current materials), at `scale`. */
    juce::Image macroBank (OspAudioProcessor& p, float scale, std::optional<MacroPalette> beadsTo, const MacroPalette& rendered)
    {
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (ui != nullptr);
        ui->refreshNow();
        auto& row = ui->macroRow();
        juce::Rectangle<int> area;
        std::vector<juce::Point<float>> beads;
        for (auto& knob : row)
        {
            const auto b = e->getLocalArea (knob.slider.getParentComponent(), knob.slider.getBounds());
            area = area.isEmpty() ? b : area.getUnion (b);
            beads.push_back (e->getLocalPoint (knob.slider.getParentComponent(), juce::Point<int> (knob.slider.getBounds().getCentreX(), knob.slider.getBounds().getCentreY() + 65)).toFloat());
        }
        area = area.withTop (area.getY() - 70).withBottom (area.getBottom() + 90).expanded (36, 0).getIntersection (e->getLocalBounds());
        auto image = e->createComponentSnapshot (area, true, scale);
        if (beadsTo)
        {
            std::vector<juce::Point<float>> local;
            for (auto c : beads)
                local.push_back ((c - area.getPosition().toFloat()) * scale);
            recolourBeads (image, local, 4.6f * scale, rendered, *beadsTo);   // (the beads were rendered in this palette)
        }
        p.editorBeingDeleted (e.get());
        return image;
    }
}

TEST_CASE ("palette study: CORE bank as is, with macros 3-4 refined, harmonised; the USER bank; the pedals (OSP_PALETTE_SHOTS)", "[.][palette-shots]")
{
    const char* dir = std::getenv ("OSP_PALETTE_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    const ScopedLook ceramic (skin::Look::ceramic);
    const auto current = currentPalette();
    const MacroPalette refined34 { current[0], current[1], juce::Colour (0xff518b88), juce::Colour (0xffb09a61), current[4], current[5] };
    const MacroPalette harmonised { juce::Colour (0xff879674), juce::Colour (0xffbb784e), juce::Colour (0xff518b88), juce::Colour (0xffb09a61),
                                    juce::Colour (0xff758899), juce::Colour (0xff94776b) };
    struct Variant { const char* name; std::optional<MacroPalette> palette; };
    const Variant variants[] { { "1-core-current", std::nullopt }, { "2-core-filter-movement-refined", refined34 }, { "3-core-harmonised", harmonised } };
    for (const auto& v : variants)
    {
        auto m = osp::plugin::design::materials();
        if (v.palette)
            m.macros = *v.palette;
        const osp::plugin::design::ScopedMaterials scoped (m);
        OspAudioProcessor p;
        // The macros away from their defaults a little, so every arc shows its colour.
        for (const auto* id : { "life", "dynamics", "drive", "character", "motion", "echo", "space" })
            if (auto* param = p.apvts().getParameter (id))
                param->setValueNotifyingHost (0.62f);
        for (float scale : { 1.0f, 2.0f })
            writePng (macroBank (p, scale, v.palette, current), out.getChildFile (juce::String (v.name) + (scale > 1.0f ? "@2x" : "") + ".png"));
    }
    {
        // 4: the USER bank (its own palette, whatever CORE's).
        OspAudioProcessor p;
        p.setMacroBank (1);
        for (int i = 1; i <= 6; ++i)
            if (auto* param = p.apvts().getParameter ("userMacro." + juce::String (i)))
                param->setValueNotifyingHost (0.62f);
        for (float scale : { 1.0f, 2.0f })
            writePng (macroBank (p, scale, std::nullopt, current), out.getChildFile (juce::String ("4-user-bank") + (scale > 1.0f ? "@2x" : "") + ".png"));
        p.setMacroBank (0);
    }
    {
        // 5: the effect pedals (their own V5 identities: unchanged by this study).
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        ui->setSourceViewShown (true);
        ui->refreshNow();
        auto* view = dynamic_cast<osp::plugin::FxRoutingView*> (ui->sourceViewComponent());
        REQUIRE (view != nullptr);
        juce::Rectangle<int> area;
        for (const auto& t : view->tiles())
            area = area.isEmpty() ? t.bounds : area.getUnion (t.bounds);
        writePng (view->createComponentSnapshot (area.expanded (12).getIntersection (view->getLocalBounds()), true, 2.0f), out.getChildFile ("5-pedals@2x.png"));
    }
}
