// The signal chain's pedal light is the effect's real state: its ON parameter (a new effect) or
// its node's bypass (a built-in one), read again whenever it changes - a click on the light,
// automation, undo, a session or preset, a chain - never a copy the tile kept from its layout.
// Bypassed, only the pedal's own display (its motif) dims; the name, mode and knobs stay legible.
// Before/after screenshots: OSP_PEDAL_SHOTS=<folder> osp_plugin_tests "[pedal-led-shots]".
// Included at the end of PluginProcessorTests.cpp (after PopoverMacroTests.inc).

#include "FxParameters.h"

namespace
{
    struct PedalRig
    {
        TempDir tmp;
        ScopedEnvironment documents { "OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName() };
        ScopedEnvironment store { "OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName() };
        ScopedEnvironment library { "OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName() };
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        osp::plugin::OspAudioProcessorEditor* ui = nullptr;

        PedalRig()
        {
            editor.reset (p.createEditorIfNeeded());
            ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
            REQUIRE (ui != nullptr);
            ui->setSourceViewShown (true);
        }

        osp::plugin::FxRoutingView& view()
        {
            auto* v = dynamic_cast<osp::plugin::FxRoutingView*> (ui->sourceViewComponent());
            REQUIRE (v != nullptr);
            return *v;
        }

        const osp::plugin::FxRoutingView::Tile* tile (osp::fx::InstanceId id)
        {
            for (const auto& t : view().tiles())
                if (t.id == id)
                    return &t;
            return nullptr;
        }

        bool lit (osp::fx::InstanceId id)
        {
            const auto* t = tile (id);
            REQUIRE (t != nullptr);
            return ! t->bypassed;
        }

        juce::RangedAudioParameter& on (osp::fx::InstanceId id)
        {
            auto* param = p.apvts().getParameter (osp::plugin::fxparams::idOf (p.fxParameterPrefix(), id, "on"));
            REQUIRE (param != nullptr);
            return *param;
        }

        /** Lets the view's own timer run (as the message loop would) until `done`, or ~1.5 s. */
        template <typename Pred>
        bool settle (Pred done)
        {
           #if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
            for (int i = 0; i < 150 && ! done(); ++i)
            {
                for (int k = 0; k < 50 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++k)
                {
                }
                juce::Thread::sleep (10);
            }
           #endif
            return done();
        }

        /** The message loop for `ms` (the parameter tree syncs, and records undo, on its timer). */
        void pump (int ms)
        {
            const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (ms);
            settle ([end] { return juce::Time::getMillisecondCounter() >= end; });
        }

        juce::Image shot() { return view().createComponentSnapshot (view().getLocalBounds(), true, 1.0f); }
    };

    float luma (juce::Colour c) { return 0.2126f * c.getFloatRed() + 0.7152f * c.getFloatGreen() + 0.0722f * c.getFloatBlue(); }

    /** The light's centre brightness, as drawn. */
    float lightLuma (const juce::Image& image, const osp::plugin::FxRoutingView::Tile& t)
    {
        return luma (image.getPixelAt (t.light.getCentreX(), t.light.getCentreY()));
    }

    /** How strongly the motif (the tile's left fifth) stands out from its enamel: its spread. */
    float motifContrast (const juce::Image& image, const osp::plugin::FxRoutingView::Tile& t)
    {
        const auto area = t.bounds.withWidth (t.bounds.getWidth() / 5).reduced (3, t.bounds.getHeight() / 6);
        double sum = 0.0, sq = 0.0;
        int n = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const double v = luma (image.getPixelAt (x, y));
                sum += v;
                sq += v * v;
                ++n;
            }
        const double mean = sum / juce::jmax (1, n);
        return static_cast<float> (std::sqrt (juce::jmax (0.0, sq / juce::jmax (1, n) - mean * mean)));
    }
}

TEST_CASE ("pedal light: the light is the effect's ON; a click flips only that instance, and the display dims", "[plugin][fx][routing][pedal-led]")
{
    PedalRig rig;
    auto& view = rig.view();
    REQUIRE (view.insertNew (osp::fx::Type::eq));
    REQUIRE (view.insertNew (osp::fx::Type::eq));
    const osp::fx::InstanceId eqA { osp::fx::Type::eq, 0 }, eqB { osp::fx::Type::eq, 1 };
    REQUIRE (rig.tile (eqA) != nullptr);
    REQUIRE (rig.tile (eqB) != nullptr);
    CHECK (rig.lit (eqA));
    CHECK (rig.lit (eqB));
    const auto before = rig.shot();
    const float litLuma = lightLuma (before, *rig.tile (eqA));
    const float litMotif = motifContrast (before, *rig.tile (eqA));

    // A click on the light, as the mouse makes it: only that instance, and nothing else opens.
    const auto at = rig.tile (eqA)->light.getCentre().toFloat();
    const auto selectedBefore = view.selection();
    const juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &view, &view, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    view.mouseDown (down);
    const juce::MouseEvent up (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               &view, &view, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    view.mouseUp (up);
    CHECK (rig.on (eqA).getValue() < 0.5f);          // the authority
    CHECK_FALSE (rig.lit (eqA));                      // the light, at once
    CHECK (rig.on (eqB).getValue() >= 0.5f);
    CHECK (rig.lit (eqB));                            // the other instance untouched
    CHECK (view.selection() == selectedBefore);       // the light only toggles

    const auto after = rig.shot();
    // OFF: the lens dark, no glow; ON: lit. The other pedal's light is drawn as before.
    CHECK (lightLuma (after, *rig.tile (eqA)) < litLuma - 0.25f);
    CHECK (lightLuma (after, *rig.tile (eqB)) == Catch::Approx (lightLuma (before, *rig.tile (eqB))).margin (0.02f));
    // The display at about 35-50 %: the motif's contrast falls to that share; the face does not.
    const float ratio = motifContrast (after, *rig.tile (eqA)) / juce::jmax (1.0e-4f, litMotif);
    CHECK (ratio > 0.3f);
    CHECK (ratio < 0.55f);
    // The enamel to the right of the name keeps its colour (no veil over the whole module).
    const auto& t = *rig.tile (eqA);
    const juce::Point<int> enamel (t.bounds.getX() + t.bounds.getWidth() * 3 / 10, t.bounds.getBottom() - 3);
    CHECK (std::abs (luma (after.getPixelAt (enamel.x, enamel.y)) - luma (before.getPixelAt (enamel.x, enamel.y))) < 0.03f);

    // A second click: lit again.
    view.mouseDown (down);
    view.mouseUp (up);
    CHECK (rig.on (eqA).getValue() >= 0.5f);
    CHECK (rig.lit (eqA));
}

TEST_CASE ("pedal light: automation, undo, a session and a chain move the light without a click", "[plugin][fx][routing][pedal-led]")
{
    PedalRig rig;
    auto& view = rig.view();
    REQUIRE (view.insertNew (osp::fx::Type::eq));
    REQUIRE (view.insertNew (osp::fx::Type::compressor));
    const osp::fx::InstanceId eq { osp::fx::Type::eq, 0 }, comp { osp::fx::Type::compressor, 0 };
    REQUIRE (rig.lit (eq));
    REQUIRE (rig.lit (comp));

    SECTION ("automation (the host writes the parameter)")
    {
        rig.on (eq).setValueNotifyingHost (0.0f);
        CHECK (rig.settle ([&] { return ! rig.lit (eq); }));
        CHECK (rig.lit (comp));
        rig.on (eq).setValueNotifyingHost (1.0f);
        CHECK (rig.settle ([&] { return rig.lit (eq); }));
    }
    SECTION ("a macro's lamp or the rack's switch (the parameter, from another control)")
    {
        rig.on (comp).beginChangeGesture();
        rig.on (comp).setValueNotifyingHost (0.0f);
        rig.on (comp).endChangeGesture();
        CHECK (rig.settle ([&] { return ! rig.lit (comp); }));
        CHECK (rig.lit (eq));
    }
    SECTION ("undo and redo")
    {
        REQUIRE (view.toggleBypass (eq));
        REQUIRE_FALSE (rig.lit (eq));
        rig.pump (300);   // (a person's Cmd-Z comes later than the tree's sync)
        rig.p.undo().undo();
        CHECK (rig.on (eq).getValue() >= 0.5f);
        CHECK (rig.settle ([&] { return rig.lit (eq); }));
        rig.pump (300);
        rig.p.undo().redo();
        CHECK (rig.on (eq).getValue() < 0.5f);
        CHECK (rig.settle ([&] { return ! rig.lit (eq); }));
    }
    SECTION ("a session or preset restores the state it holds")
    {
        REQUIRE (view.toggleBypass (comp));
        const auto saved = fxStateOf (rig.p);   // COMP off
        REQUIRE (saved != nullptr);
        REQUIRE (view.toggleBypass (comp));
        REQUIRE (rig.lit (comp));
        fxRestore (rig.p, *saved);
        CHECK (rig.on (comp).getValue() < 0.5f);
        CHECK (rig.settle ([&] { return ! rig.lit (comp); }));
        CHECK (rig.lit (eq));
    }
    SECTION ("a reorder keeps each pedal's own state")
    {
        REQUIRE (view.toggleBypass (eq));
        REQUIRE (view.moveTo (eq, {}, static_cast<int> (rig.p.fxRoute().chain.size())));
        CHECK (rig.settle ([&] { return rig.tile (eq) != nullptr && ! rig.lit (eq); }));
        CHECK (rig.lit (comp));
    }
    SECTION ("the Mixer closed and opened again")
    {
        REQUIRE (view.toggleBypass (eq));
        rig.ui->setSourceViewShown (false);
        rig.on (eq).setValueNotifyingHost (1.0f);   // (changed while the view is hidden)
        rig.on (comp).setValueNotifyingHost (0.0f);
        rig.ui->setSourceViewShown (true);
        CHECK (rig.settle ([&] { return rig.lit (eq) && ! rig.lit (comp); }));
    }
}

TEST_CASE ("pedal light: a built-in effect's light is its node's bypass, with undo", "[plugin][fx][routing][pedal-led]")
{
    PedalRig rig;
    auto& view = rig.view();
    const osp::fx::InstanceId space { osp::fx::Type::space, 0 }, echo { osp::fx::Type::echo, 0 };
    REQUIRE (rig.tile (space) != nullptr);
    REQUIRE (rig.lit (space));
    REQUIRE (view.toggleBypass (space));
    CHECK (rig.settle ([&] { return rig.tile (space) != nullptr && ! rig.lit (space); }));
    CHECK (rig.lit (echo));
    rig.p.undo().undo();
    CHECK (rig.settle ([&] { return rig.tile (space) != nullptr && rig.lit (space); }));
}

TEST_CASE ("pedal light: before / after screenshots (OSP_PEDAL_SHOTS)", "[.][pedal-led-shots]")
{
    const char* dir = std::getenv ("OSP_PEDAL_SHOTS");
    if (dir == nullptr)
        return;
    const ScopedLook ceramic (skin::Look::ceramic);
    PedalRig rig;
    auto& view = rig.view();
    view.insertNew (osp::fx::Type::eq);
    view.insertNew (osp::fx::Type::compressor);
    view.insertNew (osp::fx::Type::eq);
    auto save = [dir] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    auto crop = [&] {
        auto image = rig.shot();
        juce::Rectangle<int> area;
        for (const auto& t : view.tiles())
            area = area.isEmpty() ? t.bounds : area.getUnion (t.bounds);
        return image.getClippedImage (area.expanded (12).getIntersection (image.getBounds()));
    };
    save (crop(), "pedal-01-all-on.png");
    view.toggleBypass ({ osp::fx::Type::eq, 0 });
    view.toggleBypass ({ osp::fx::Type::space, 0 });
    save (crop(), "pedal-02-eq1-and-space-bypassed.png");
    rig.on ({ osp::fx::Type::compressor, 0 }).setValueNotifyingHost (0.0f);   // (automation)
    rig.settle ([&] { return ! rig.lit ({ osp::fx::Type::compressor, 0 }); });
    save (crop(), "pedal-03-comp-bypassed-by-automation.png");
}
