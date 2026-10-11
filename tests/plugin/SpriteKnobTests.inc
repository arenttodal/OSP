// The rendered macro knob (tools/blender-assets -> SpriteKnobRenderer): the embedded asset,
// its frame order against the drawn knob's angles, the pivot, the caches, the knob skin
// switch (looks only), shared resources, fallbacks; hidden: measurements and the comparison
// screenshots. Included at the end of PluginProcessorTests.cpp.

#include "SpriteKnobRenderer.h"
#include "OspLookAndFeel.h"
#include "Design.h"

namespace
{
    using osp::plugin::KnobSkin;
    using osp::plugin::SpriteKnobAsset;
    using osp::plugin::SpriteKnobLibrary;

    struct ScopedSkin
    {
        explicit ScopedSkin (KnobSkin s) : before (osp::plugin::knobSkin()) { osp::plugin::setKnobSkin (s); }
        ~ScopedSkin() { osp::plugin::setKnobSkin (before); }
        KnobSkin before;
    };

    juce::Slider* findMacro (juce::Component& root, const juce::String& paramId)
    {
        if (auto* s = dynamic_cast<juce::Slider*> (&root); s != nullptr && s->getProperties()["paramId"].toString() == paramId)
            return s;
        for (auto* child : root.getChildren())
            if (auto* found = findMacro (*child, paramId))
                return found;
        return nullptr;
    }

    /** The clockwise-from-12 angle of the darkest opaque pixels' centroid (the indicator),
        relative to the image centre; NaN when there are none. */
    float indicatorAngle (const juce::Image& image)
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        double sx = 0.0, sy = 0.0;
        int n = 0;
        const double cx = 0.5 * (image.getWidth() - 1), cy = 0.5 * (image.getHeight() - 1);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto c = data.getPixelColour (x, y);
                if (c.getAlpha() < 250 || c.getPerceivedBrightness() > 0.35f)
                    continue;
                sx += x - cx;
                sy += y - cy;
                ++n;
            }
        if (n < 10)
            return std::numeric_limits<float>::quiet_NaN();
        return static_cast<float> (std::atan2 (sx, -sy));
    }

    float angleDistance (float a, float b)
    {
        const float twoPi = juce::MathConstants<float>::twoPi;
        float d = std::fmod (std::abs (a - b), twoPi);
        return std::min (d, twoPi - d);
    }

    /** Centroid and mean radius of the fully opaque pixels (the cap, not its soft shadow). */
    struct Body
    {
        juce::Point<double> centre;
        double radius = 0.0;
        int count = 0;
    };

    Body opaqueBody (const juce::Image& image)
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        Body b;
        double sx = 0.0, sy = 0.0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (data.getPixelColour (x, y).getAlpha() >= 250)
                {
                    sx += x + 0.5;
                    sy += y + 0.5;
                    ++b.count;
                }
        if (b.count > 0)
        {
            b.centre = { sx / b.count, sy / b.count };
            b.radius = std::sqrt (static_cast<double> (b.count) / juce::MathConstants<double>::pi);
        }
        return b;
    }

    juce::Image drawAsset (SpriteKnobAsset& asset, int side, float scale, juce::Point<float> centre, float radius, float angle)
    {
        juce::Image image (juce::Image::ARGB, side, side, true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        asset.draw (g, centre, radius, angle);
        return image;
    }

    std::vector<std::pair<const void*, size_t>> zipOf (const std::vector<std::pair<juce::String, juce::MemoryBlock>>& entries, juce::MemoryBlock& storage)
    {
        juce::ZipFile::Builder builder;
        for (const auto& [name, data] : entries)
            builder.addEntry (new juce::MemoryInputStream (data, true), 0, name, juce::Time (2026, 0, 1, 0, 0));
        juce::MemoryOutputStream out (storage, false);
        builder.writeToStream (out, nullptr);
        out.flush();
        return { { storage.getData(), storage.getSize() } };
    }
} // namespace

TEST_CASE ("plugin: the rendered macro knob is embedded whole: every frame, each size, the knob's own turn", "[plugin][sprite-knob]")
{
    SpriteKnobLibrary library;
    auto* asset = library.find ("macro-ivory");
    REQUIRE (asset != nullptr);
    const auto& info = asset->info();
    CHECK (info.frameCount == 129);
    // The same turn as the drawn knob: frame 0 is the minimum, the last the maximum.
    CHECK (info.angleStart == Approx (osp::plugin::OspLookAndFeel::rotaryStart).margin (1.0e-5));
    CHECK (info.angleEnd == Approx (osp::plugin::OspLookAndFeel::rotaryEnd).margin (1.0e-5));
    REQUIRE (info.levels.size() >= 2);
    CHECK (info.bodyRadius > 0.3f);
    CHECK (info.bodyRadius < 0.4f);
    const auto started = std::chrono::steady_clock::now();
    for (int level = 0; level < static_cast<int> (info.levels.size()); ++level)
    {
        const int side = info.levels[static_cast<size_t> (level)];
        for (int i = 0; i < info.frameCount; ++i)
        {
            const auto frame = asset->storedFrame (level, i);
            REQUIRE (frame.isValid());
            REQUIRE (frame.getWidth() == side);
            REQUIRE (frame.getHeight() == side);
            // Transparent around (no background baked in), solid in the middle.
            CHECK (frame.getPixelAt (0, 0).getAlpha() == 0);
            CHECK (frame.getPixelAt (side - 1, side - 1).getAlpha() == 0);
            CHECK (frame.getPixelAt (side - 1, 0).getAlpha() == 0);
            CHECK (frame.getPixelAt (side / 2, side / 2 + side / 6).getAlpha() == 255);
        }
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - started).count();
    const auto usage = asset->usage();
    CHECK (usage.decodedFrames == info.frameCount * static_cast<int> (info.levels.size()));
    UNSCOPED_INFO ("decoded every frame of every size in " << ms << " ms, " << usage.bytes / (1024 * 1024) << " MB");
    asset->release();
    CHECK (asset->usage().bytes == 0);
}

TEST_CASE ("plugin: the rendered knob's frames turn like the drawn knob: minimum, quarter points, maximum", "[plugin][sprite-knob]")
{
    SpriteKnobLibrary library;
    auto* asset = library.find ("macro-ivory");
    REQUIRE (asset != nullptr);
    const int last = asset->info().frameCount - 1;
    CHECK (asset->frameForAngle (osp::plugin::OspLookAndFeel::rotaryStart) == 0);
    CHECK (asset->frameForAngle (osp::plugin::OspLookAndFeel::rotaryEnd) == last);
    CHECK (asset->frameForAngle (osp::plugin::OspLookAndFeel::rotaryStart - 1.0f) == 0);     // clamped
    CHECK (asset->frameForAngle (osp::plugin::OspLookAndFeel::rotaryEnd + 1.0f) == last);
    CHECK (asset->frameForAngle (0.5f * (osp::plugin::OspLookAndFeel::rotaryStart + osp::plugin::OspLookAndFeel::rotaryEnd)) == last / 2);
    // The indicator in the pixels points where the angle says, for every frame.
    const int top = static_cast<int> (asset->info().levels.size()) - 1;
    float worst = 0.0f;
    for (int i = 0; i <= last; ++i)
    {
        const float seen = indicatorAngle (asset->storedFrame (top, i));
        REQUIRE (! std::isnan (seen));
        worst = std::max (worst, angleDistance (seen, asset->angleOfFrame (i)));
    }
    UNSCOPED_INFO ("largest indicator error: " << juce::radiansToDegrees (worst) << " degrees");
    CHECK (juce::radiansToDegrees (worst) < 3.0f);
    // Minimum at 7:30 (lower left), maximum at 4:30 (lower right), the middle straight up.
    CHECK (angleDistance (indicatorAngle (asset->storedFrame (top, 0)), juce::degreesToRadians (225.0f)) < 0.05f);
    CHECK (angleDistance (indicatorAngle (asset->storedFrame (top, last)), juce::degreesToRadians (135.0f)) < 0.05f);
    CHECK (angleDistance (indicatorAngle (asset->storedFrame (top, last / 2)), 0.0f) < 0.05f);
}

TEST_CASE ("plugin: the rendered knob sits on the slider's centre at its radius, at every scale", "[plugin][sprite-knob]")
{
    SpriteKnobLibrary library;
    auto* asset = library.find ("macro-ivory");
    REQUIRE (asset != nullptr);
    for (const float scale : { 0.6f, 0.75f, 1.0f, 1.5f, 2.0f, 2.5f })
    {
        const float radius = 46.5f;                    // the macro's body radius (design px)
        const juce::Point<float> centre (70.3f, 71.6f);
        const int side = juce::roundToInt (150.0f * scale);
        const auto image = drawAsset (*asset, side, scale, centre, radius, osp::plugin::OspLookAndFeel::rotaryStart);
        const auto body = opaqueBody (image);
        REQUIRE (body.count > 0);
        INFO ("scale " << scale);
        CHECK (std::abs (body.centre.x - centre.x * scale) < 0.75);
        CHECK (std::abs (body.centre.y - centre.y * scale) < 0.75);
        // The solid cap stays inside the body radius (the arc ring at 1.13 r stays clear).
        CHECK (body.radius < radius * scale * 1.0);
        CHECK (body.radius > radius * scale * 0.9);
    }
}

TEST_CASE ("plugin: a repaint reuses the rendered knob's frames; a new size makes them once", "[plugin][sprite-knob]")
{
    SpriteKnobLibrary library;
    auto* asset = library.find ("macro-ivory");
    REQUIRE (asset != nullptr);
    asset->release();
    const float angle = osp::plugin::OspLookAndFeel::rotaryStart + 1.0f;
    drawAsset (*asset, 160, 1.5f, { 50.0f, 50.0f }, 30.0f, angle);
    const auto first = asset->usage();
    CHECK (first.decodedFrames == 0);   // decoded, resampled to the drawn size, let go
    CHECK (first.sizedFrames == 1);
    for (int i = 0; i < 20; ++i)
        drawAsset (*asset, 160, 1.5f, { 50.0f, 50.0f }, 30.0f, angle);
    const auto again = asset->usage();
    CHECK (again.decodedFrames == first.decodedFrames);
    CHECK (again.sizedFrames == first.sizedFrames);
    CHECK (again.bytes == first.bytes);
    drawAsset (*asset, 200, 2.0f, { 50.0f, 50.0f }, 30.0f, angle);
    CHECK (asset->usage().sizedFrames == first.sizedFrames + 1);
    // A resizing window passes through many sizes: only the latest few are kept.
    for (int s = 0; s < 12; ++s)
        drawAsset (*asset, 200, 1.0f + 0.07f * static_cast<float> (s), { 50.0f, 50.0f }, 30.0f, angle);
    CHECK (asset->usage().sizedFrames <= 3);
    asset->release();
}

TEST_CASE ("plugin: area resampling keeps flat colour flat and averages exactly", "[plugin][sprite-knob]")
{
    juce::Image flat (juce::Image::ARGB, 7, 7, false);
    flat.clear (flat.getBounds(), juce::Colour (0xff8a6f52));
    const auto small = osp::plugin::resampleByArea (flat, 3);
    REQUIRE (small.getWidth() == 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            CHECK (small.getPixelAt (x, y).getARGB() == 0xff8a6f52);
    // A black / white checkerboard halves to mid grey; half-transparent edges keep their colour.
    juce::Image checks (juce::Image::ARGB, 8, 8, true);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            checks.setPixelAt (x, y, (x + y) % 2 == 0 ? juce::Colours::white : juce::Colours::black);
    const auto grey = osp::plugin::resampleByArea (checks, 4);
    CHECK (std::abs (static_cast<int> (grey.getPixelAt (1, 1).getRed()) - 128) <= 1);
    juce::Image edge (juce::Image::ARGB, 4, 4, true);
    edge.clear ({ 0, 0, 2, 4 }, juce::Colour (0xff336699));
    const auto half = osp::plugin::resampleByArea (edge, 2);
    const auto c = half.getPixelAt (1, 0);
    CHECK (c.getAlpha() == 0);
    const auto solid = half.getPixelAt (0, 0);
    CHECK (solid.getARGB() == 0xff336699);
    juce::Image mixed (juce::Image::ARGB, 2, 2, true);
    mixed.clear ({ 0, 0, 1, 2 }, juce::Colour (0xff336699));
    const auto one = osp::plugin::resampleByArea (mixed, 1).getPixelAt (0, 0);
    CHECK (std::abs (static_cast<int> (one.getAlpha()) - 128) <= 1);
    // No dark fringe: the colour is kept (read back unpremultiplied: rounding of +-2).
    CHECK (std::abs (static_cast<int> (one.getRed()) - 0x33) <= 2);
    CHECK (std::abs (static_cast<int> (one.getBlue()) - 0x99) <= 2);
}

TEST_CASE ("plugin: a damaged knob pack is refused and the drawn knob stays", "[plugin][sprite-knob]")
{
    const char junk[] = "not a zip at all";
    SpriteKnobAsset garbage ({ { junk, sizeof (junk) } });
    CHECK_FALSE (garbage.isValid());
    CHECK (garbage.problem().isNotEmpty());
    juce::MemoryBlock storage;
    juce::MemoryBlock manifest;
    const char* text = "{\"schemaVersion\":1,\"name\":\"x\",\"frameCount\":3,\"frameSize\":16,\"bodyRadius\":0.34,"
                       "\"angleStart\":3.9,\"angleEnd\":8.6,\"frames\":\"frames/%03d.png\"}";
    manifest.append (text, std::strlen (text));
    SpriteKnobAsset missing (zipOf ({ { "manifest.json", manifest } }, storage));
    CHECK_FALSE (missing.isValid());
    CHECK (missing.problem().contains ("missing"));
    // Drawn anyway: the vector knob, not an empty space.
    juce::Image image (juce::Image::ARGB, 100, 100, true);
    {
        juce::Graphics g (image);
        osp::plugin::design::draw::KnobStyle style;
        style.sprite = &missing;
        osp::plugin::design::draw::knob (g, { 50.0f, 50.0f }, 30.0f, 0.5f, style);
    }
    CHECK (image.getPixelAt (50, 60).getAlpha() == 255);
}

TEST_CASE ("plugin: the knob skin changes looks only: parameters, state, automation and sound are untouched", "[plugin][sprite-knob]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedSkin restore (KnobSkin::pilot);
    const auto file = writeSource (tmp.dir, "Steady Pluck C4.wav", testsignals::pluck (261.6256, 1.2, 48000.0, 21));
    const auto quiet = [&] {
        OspAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        loadAndWait (p, file);
        return renderQaChord (p, 1.5);
    }();
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    loadAndWait (p, file);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    auto* life = findMacro (*editor, "life");
    REQUIRE (life != nullptr);
    juce::MemoryBlock before;
    p.getStateInformation (before);
    const auto lifeBefore = p.parameters.getParameter ("life")->getValue();

    // Each skin, painted while the instrument plays; automation moves LIFE meanwhile.
    std::map<KnobSkin, juce::Image> shots;
    const auto busy = renderQaChord (p, 1.5, [&] (int block) {
        if (block % 20 != 0)
            return;
        const auto skin = static_cast<KnobSkin> ((block / 20) % 3);
        osp::plugin::setKnobSkin (skin);
        ui->refreshNow();
        shots[skin] = life->createComponentSnapshot (life->getLocalBounds(), true, 1.0f);
    });
    REQUIRE (quiet.size() == busy.size());
    CHECK (std::memcmp (quiet.data(), busy.data(), quiet.size() * sizeof (float)) == 0);   // bit for bit

    juce::MemoryBlock after;
    p.getStateInformation (after);
    CHECK (before == after);
    CHECK (p.parameters.getParameter ("life")->getValue() == lifeBefore);

    // The skins really differ on LIFE (the switch works), and LIFE follows its parameter.
    REQUIRE (shots.size() == 3);
    auto differs = [] (const juce::Image& a, const juce::Image& b) {
        int n = 0;
        for (int y = 0; y < a.getHeight(); y += 2)
            for (int x = 0; x < a.getWidth(); x += 2)
                n += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
        return n;
    };
    CHECK (differs (shots[KnobSkin::original], shots[KnobSkin::pilot]) > 100);
    CHECK (differs (shots[KnobSkin::pilot], shots[KnobSkin::macros]) == 0);   // LIFE is the pilot in both

    // Host automation: the rendered knob's frame follows the parameter.
    osp::plugin::setKnobSkin (KnobSkin::pilot);
    auto* asset = dynamic_cast<osp::plugin::OspLookAndFeel&> (editor->getLookAndFeel()).spriteKnobs().find ("macro-ivory");
    REQUIRE (asset != nullptr);
    for (const float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        p.parameters.getParameter ("life")->setValueNotifyingHost (v);
        ui->refreshNow();
        const float angle = life->getRotaryParameters().startAngleRadians
                            + static_cast<float> (life->valueToProportionOfLength (life->getValue()))
                                  * (life->getRotaryParameters().endAngleRadians - life->getRotaryParameters().startAngleRadians);
        CHECK (asset->frameForAngle (angle) == juce::roundToInt (v * static_cast<float> (asset->info().frameCount - 1)));
    }
    // Other macros keep the drawn knob under the pilot skin.
    auto* drive = findMacro (*editor, "drive");
    REQUIRE (drive != nullptr);
    auto& lnf = dynamic_cast<osp::plugin::OspLookAndFeel&> (editor->getLookAndFeel());
    CHECK (osp::plugin::spriteFor (*life, lnf.spriteKnobs()) != nullptr);
    CHECK (osp::plugin::spriteFor (*drive, lnf.spriteKnobs()) == nullptr);
    osp::plugin::setKnobSkin (KnobSkin::macros);
    CHECK (osp::plugin::spriteFor (*drive, lnf.spriteKnobs()) != nullptr);
    osp::plugin::setKnobSkin (KnobSkin::original);
    CHECK (osp::plugin::spriteFor (*life, lnf.spriteKnobs()) == nullptr);
}

TEST_CASE ("plugin: halos stay centred on the rendered knob, with one route, several and meta-modulation", "[plugin][sprite-knob]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedSkin restore (KnobSkin::pilot);
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    const auto first = p.assignModulation (osp::mod::Source::lfo1, osp::mod::Dest::life, 40.0f);
    INFO (first.error);
    REQUIRE (first.slot >= 0);
    const auto second = p.assignModulation (osp::mod::Source::modWheel, osp::mod::Dest::life, -25.0f);
    INFO (second.error);
    REQUIRE (second.slot >= 0);
    const auto meta = p.assignDepthModulation (osp::mod::Source::env1, first.slot, 30.0f);
    INFO (meta.error);
    ui->refreshNow();
    auto* life = findMacro (*editor, "life");
    REQUIRE (life != nullptr);
    const auto& overlay = ui->modulationOverlay();
    std::map<KnobSkin, juce::Point<float>> centres;
    std::map<KnobSkin, float> radii;
    for (const auto skin : { KnobSkin::original, KnobSkin::pilot })
    {
        osp::plugin::setKnobSkin (skin);
        ui->refreshNow();
        centres[skin] = overlay.haloCentre ("life");
        radii[skin] = overlay.haloRadius ("life");
    }
    CHECK (radii[KnobSkin::pilot] > 0.0f);
    CHECK (centres[KnobSkin::original] == centres[KnobSkin::pilot]);
    CHECK (radii[KnobSkin::original] == radii[KnobSkin::pilot]);
    // ... and the rendered cap is centred on the halo (its centre in the slider's terms).
    osp::plugin::setKnobSkin (KnobSkin::pilot);
    const auto haloInSlider = life->getLocalPoint (&overlay, centres[KnobSkin::pilot]);
    juce::Image only (juce::Image::ARGB, life->getWidth() * 2, life->getHeight() * 2, true);
    {
        juce::Graphics g (only);
        g.addTransform (juce::AffineTransform::scale (2.0f));
        auto* asset = dynamic_cast<osp::plugin::OspLookAndFeel&> (editor->getLookAndFeel()).spriteKnobs().find ("macro-ivory");
        REQUIRE (asset != nullptr);
        // Exactly as the slider draws it: centred in its bounds, its body radius.
        const auto r = life->getLocalBounds().toFloat();
        const float side = std::min (r.getWidth(), r.getHeight());
        asset->draw (g, r.getCentre(), 0.5f * side / 1.42f, osp::plugin::OspLookAndFeel::rotaryStart);
    }
    const auto body = opaqueBody (only);
    CHECK (std::abs (body.centre.x / 2.0 - haloInSlider.x) < 1.0);
    CHECK (std::abs (body.centre.y / 2.0 - haloInSlider.y) < 1.0);
}

TEST_CASE ("plugin: every editor of the process shares one rendered knob", "[plugin][sprite-knob]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedSkin restore (KnobSkin::macros);
    OspAudioProcessor a, b;
    std::unique_ptr<juce::AudioProcessorEditor> ea (a.createEditorIfNeeded()), eb (b.createEditorIfNeeded());
    ea->setSize (1448, 1086);
    eb->setSize (1448, 1086);
    auto& la = dynamic_cast<osp::plugin::OspLookAndFeel&> (ea->getLookAndFeel());
    auto& lb = dynamic_cast<osp::plugin::OspLookAndFeel&> (eb->getLookAndFeel());
    CHECK (&la.spriteKnobs() == &lb.spriteKnobs());
    auto* asset = la.spriteKnobs().find ("macro-ivory");
    REQUIRE (asset != nullptr);
    asset->release();
    ea->createComponentSnapshot (ea->getLocalBounds(), true, 1.0f);
    const auto once = asset->usage();
    CHECK (once.sizedFrames > 0);
    CHECK (once.sizedFrames <= 6);   // only the frames the six macros show
    eb->createComponentSnapshot (eb->getLocalBounds(), true, 1.0f);
    CHECK (asset->usage().sizedFrames == once.sizedFrames);   // the second instance makes nothing new
    CHECK (asset->usage().bytes == once.bytes);
}

TEST_CASE ("plugin: rendered knob, measured: paint time and memory, original vs rendered", "[.][knob-perf]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedSkin restore (KnobSkin::pilot);
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1086, 815);
    auto* life = findMacro (*editor, "life");
    REQUIRE (life != nullptr);
    auto* asset = dynamic_cast<osp::plugin::OspLookAndFeel&> (editor->getLookAndFeel()).spriteKnobs().find ("macro-ivory");
    REQUIRE (asset != nullptr);
    auto timeIt = [] (int runs, const std::function<void (int)>& body) {
        std::vector<double> ms;
        for (int i = 0; i < runs; ++i)
        {
            const auto t0 = std::chrono::steady_clock::now();
            body (i);
            ms.push_back (std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
        }
        std::sort (ms.begin(), ms.end());
        return std::make_pair (ms[ms.size() / 2], ms[ms.size() * 95 / 100]);
    };
    for (const float display : { 1.0f, 2.0f })
    {
        juce::Image target (juce::Image::ARGB, juce::roundToInt (life->getWidth() * display), juce::roundToInt (life->getHeight() * display), true);
        auto paintKnob = [&] (int i) {
            life->setValue (life->getMinimum() + (life->getMaximum() - life->getMinimum()) * (i % 129) / 128.0, juce::dontSendNotification);
            juce::Graphics g (target);
            g.addTransform (juce::AffineTransform::scale (display));
            life->paintEntireComponent (g, false);
        };
        osp::plugin::setKnobSkin (KnobSkin::original);
        const auto vector = timeIt (400, paintKnob);
        osp::plugin::setKnobSkin (KnobSkin::pilot);
        asset->release();
        const auto cold = timeIt (129, paintKnob);   // every frame shown for the first time
        const auto warm = timeIt (400, paintKnob);
        WARN ("LIFE knob paint at display scale " << display << " (window 1086 px): drawn median " << vector.first << " ms (p95 " << vector.second
              << "); rendered first show median " << cold.first << " ms (p95 " << cold.second << "), then median " << warm.first
              << " ms (p95 " << warm.second << "); all 129 frames held: " << asset->usage().bytes / 1024 << " KB");
    }
    // The whole editor at 1x, original vs pilot vs all macros rendered.
    for (const auto skin : { KnobSkin::original, KnobSkin::pilot, KnobSkin::macros })
    {
        osp::plugin::setKnobSkin (skin);
        asset->release();
        editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);   // warm up
        const auto whole = timeIt (30, [&] (int) { editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f); });
        WARN ("whole editor at 2x, skin " << osp::plugin::knobSkinName (skin) << ": median " << whole.first << " ms, p95 " << whole.second
              << " ms; rendered frames held " << asset->usage().bytes / 1024 << " KB");
    }
}

TEST_CASE ("plugin: rendered knob comparison screenshots (original vs rendered, values, modulation)", "[.][knob-shots]")
{
    const char* dir = std::getenv ("OSP_SNAPSHOT_DIR");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedSkin restore (KnobSkin::pilot);
    const auto a = writeSource (tmp.dir, "MMZT_one_shot_sailboat_C.wav", testsignals::vowel (midiToHz (60), 2.1, 48000.0, 3));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.addLayers ({ a });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    auto set = [&] (const char* id, float value) { p.setParameterValue (id, value); };
    set ("life", 50.0f);
    set ("dynamics", 22.0f);
    set ("character", 78.0f);
    set ("motion", 72.0f);
    set ("space", 40.0f);
    auto save = [&] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    auto full = [&] (int width, float display, const juce::String& name) {
        editor->setSize (width, juce::roundToInt (width * 1086.0f / 1448.0f));
        ui->refreshNow();
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, display), name);
    };
    // The whole plugin, each skin, at the default window size (1x and Retina) and 1448.
    for (const auto skin : { KnobSkin::original, KnobSkin::pilot, KnobSkin::macros })
    {
        osp::plugin::setKnobSkin (skin);
        const auto tag = osp::plugin::knobSkinName (skin);
        full (1086, 1.0f, "knob-full-" + tag + "-1086-1x.png");
        full (1086, 2.0f, "knob-full-" + tag + "-1086-2x.png");
        full (1448, 1.0f, "knob-full-" + tag + "-1448-1x.png");
    }
    // Close-ups of LIFE at the default window size, Retina: values, then modulation.
    editor->setSize (1086, 815);
    auto* life = findMacro (*editor, "life");
    REQUIRE (life != nullptr);
    auto closeUp = [&] (const juce::String& name) {
        ui->refreshNow();
        // The knob and its halo: a square around the slider in editor coordinates.
        const auto centre = editor->getLocalPoint (life, life->getLocalBounds().getCentre());
        const int half = 82;
        const auto area = juce::Rectangle<int> (2 * half, 2 * half).withCentre (centre);
        for (const auto skin : { KnobSkin::original, KnobSkin::pilot })
        {
            osp::plugin::setKnobSkin (skin);
            ui->refreshNow();
            save (editor->createComponentSnapshot (area, true, 2.0f), "knob-" + name + "-" + osp::plugin::knobSkinName (skin) + ".png");
        }
    };
    for (const int v : { 0, 25, 50, 75, 100 })
    {
        set ("life", static_cast<float> (v));
        closeUp ("value-" + juce::String (v));
    }
    set ("life", 50.0f);
    const auto one = p.assignModulation (osp::mod::Source::lfo1, osp::mod::Dest::life, 40.0f);
    REQUIRE (one.slot >= 0);
    closeUp ("mod-one");
    const auto two = p.assignModulation (osp::mod::Source::modWheel, osp::mod::Dest::life, -25.0f);
    REQUIRE (two.slot >= 0);
    closeUp ("mod-several");
    const auto meta = p.assignDepthModulation (osp::mod::Source::env1, one.slot, 30.0f);
    INFO (meta.error);
    CHECK (meta.slot >= 0);
    closeUp ("mod-meta");
}
