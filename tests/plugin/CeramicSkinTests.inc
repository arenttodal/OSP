// The ceramic skin (tools/blender-assets/skin -> CeramicSkin): the embedded pack, nine-slice
// composition (exact at the rendered size), the caches, keys drawn by their surface, the
// look switch (looks only), the classic fallback; hidden: the comparison screenshots.
// Included at the end of PluginProcessorTests.cpp (after SpriteKnobTests.inc).

#include "CeramicSkin.h"

namespace
{
    namespace skin = osp::plugin::skin;

    struct ScopedLook
    {
        explicit ScopedLook (skin::Look l) : before (skin::look()) { skin::setLook (l); }
        ~ScopedLook() { skin::setLook (before); }
        skin::Look before;
    };

    int maxDifference (const juce::Image& a, const juce::Image& b)
    {
        int worst = 0;
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto* p = reinterpret_cast<const juce::PixelARGB*> (da.getPixelPointer (x, y));
                const auto* q = reinterpret_cast<const juce::PixelARGB*> (db.getPixelPointer (x, y));
                worst = std::max ({ worst, std::abs (p->getAlpha() - q->getAlpha()), std::abs (p->getRed() - q->getRed()),
                                    std::abs (p->getGreen() - q->getGreen()), std::abs (p->getBlue() - q->getBlue()) });
            }
        return worst;
    }

    const std::vector<juce::String>& ceramicPieceNames()
    {
        static const std::vector<juce::String> names = [] {
            std::vector<juce::String> n { "housing", "card-451", "card-683", "card-1379", "panel-macro", "panel-arp",
                                          "raised-r7", "raised-r9", "raised-r10", "raised-r12", "badge-A", "badge-B", "badge-C",
                                          "fader-normal", "fader-hover", "fader-drag", "rail", "led-arp-on", "led-arp-off",
                                          "bezel-r8", "bezel-r9", "bezel-r10", "bezel-r12", "keybed" };
            for (const char* role : { "modifier", "layerState", "mode", "eq", "advanced", "preset", "arp", "generic-r4", "generic-r5", "generic-r7", "generic-r9" })
                for (const char* state : { "normal", "hover", "pressed", "active" })
                    n.push_back (juce::String ("key-") + role + "-" + state);
            for (const char* macro : { "life", "drive", "character", "motion", "echo", "space" })
                for (const char* lit : { "lit", "unlit" })
                    n.push_back (juce::String ("led-") + macro + "-" + lit);
            return n;
        }();
        return names;
    }
}

TEST_CASE ("plugin: the ceramic pack is embedded whole: every piece, every level, as declared", "[plugin][ceramic]")
{
    skin::Pieces pieces;
    INFO (pieces.problem());
    REQUIRE (pieces.isValid());
    CHECK (pieces.size() == static_cast<int> (ceramicPieceNames().size()));
    for (const auto& name : ceramicPieceNames())
    {
        INFO (name);
        const auto* p = pieces.find (name);
        REQUIRE (p != nullptr);
        REQUIRE_FALSE (p->levels.empty());
        for (const auto& level : p->levels)
        {
            const auto image = pieces.decode (*p, level);
            REQUIRE (image.isValid());
            CHECK (image.getWidth() == level.width);
            CHECK (image.getHeight() == level.height);
            // The image covers the part and its margin at the level's scale.
            CHECK (std::abs (level.width - (p->rect.getWidth() + 2.0f * p->margin) * level.scale) <= 1.0f);
            CHECK (std::abs (level.height - (p->rect.getHeight() + 2.0f * p->margin) * level.scale) <= 1.0f);
            if (p->kind == skin::Pieces::Kind::opaque)
                CHECK (image.getPixelAt (level.width / 2, level.height / 2).getAlpha() == 255);
            else if (p->kind == skin::Pieces::Kind::alpha)
            {
                // Transparent at the image's edge (no background baked in; the shadow's tail
                // fades out over the margin), solid across the middle (a fader's grip grooves
                // are open: the middle row's most opaque pixel).
                CHECK (image.getPixelAt (0, 0).getAlpha() == 0);
                CHECK (image.getPixelAt (level.width / 2, 0).getAlpha() == 0);
                int solid = 0;
                for (int x = 0; x < level.width; ++x)
                    solid = std::max (solid, static_cast<int> (image.getPixelAt (x, level.height / 2).getAlpha()));
                CHECK (solid >= 250);
            }
        }
        if (p->kind != skin::Pieces::Kind::overlay)
            CHECK (p->face.isOpaque());
    }
    // The chosen design's colours, as rendered (style guide, displayed): the card face, a
    // key and an active key; the window's surround.
    auto near = [] (juce::Colour c, juce::uint32 expected, int tolerance) {
        const juce::Colour e (expected);
        return std::abs (c.getRed() - e.getRed()) <= tolerance && std::abs (c.getGreen() - e.getGreen()) <= tolerance
               && std::abs (c.getBlue() - e.getBlue()) <= tolerance;
    };
    CHECK (near (pieces.find ("card-451")->face, 0xfff9f2e8, 6));
    CHECK (near (pieces.find ("key-modifier-normal")->face, 0xfff2ece4, 6));
    CHECK (near (pieces.find ("key-modifier-active")->face, 0xfff8e0cf, 8));
    CHECK (pieces.find ("housing")->edge.isOpaque());
}

TEST_CASE ("plugin: a ceramic piece drawn at its rendered size is the render itself; other sizes keep its corners", "[plugin][ceramic]")
{
    skin::Pieces pieces;
    REQUIRE (pieces.isValid());
    for (const char* name : { "card-451", "key-modifier-normal", "badge-A", "fader-normal" })
    {
        INFO (name);
        const auto* p = pieces.find (name);
        REQUIRE (p != nullptr);
        const auto& level = p->levels.front();
        const auto stored = pieces.decode (*p, level);
        const auto same = pieces.composed (*p, level.width, level.height, level.scale, {});
        REQUIRE (same.isValid());
        CHECK (maxDifference (stored, same) <= 1);
    }
    // A card drawn wider (the two-layer width from the three-layer render): the left and
    // right corners are the render's, the middle stretches along.
    const auto* card = pieces.find ("card-451");
    const auto& level = card->levels.front();
    const auto stored = pieces.decode (*card, level);
    const int wide = juce::roundToInt ((683.0f + 2.0f * card->margin) * level.scale);
    const auto stretched = pieces.composed (*card, wide, level.height, level.scale, {});
    REQUIRE (stretched.getWidth() == wide);
    const int corner = juce::roundToInt ((card->margin + card->insets[0]) * level.scale) - 2;
    const auto left = stored.getClippedImage ({ 0, 0, corner, level.height });
    const auto right = stored.getClippedImage ({ level.width - corner, 0, corner, level.height });
    CHECK (maxDifference (left, stretched.getClippedImage ({ 0, 0, corner, level.height })) <= 1);
    CHECK (maxDifference (right, stretched.getClippedImage ({ wide - corner, 0, corner, level.height })) <= 1);
    // A face colour scales the light and shading; the shadow stays a shadow.
    const auto darker = pieces.composed (*card, level.width, level.height, level.scale, card->face.darker (0.2f));
    const auto mid = juce::Point<int> (level.width / 2, level.height / 2);
    CHECK (darker.getPixelAt (mid.x, mid.y).getBrightness() < stored.getPixelAt (mid.x, mid.y).getBrightness());
    CHECK (darker.getPixelAt (0, 0).getAlpha() == stored.getPixelAt (0, 0).getAlpha());
}

TEST_CASE ("plugin: ceramic area resampling: flat stays flat, shrinking averages, growing interpolates", "[plugin][ceramic]")
{
    juce::Image flat (juce::Image::ARGB, 9, 5, false);
    flat.clear (flat.getBounds(), juce::Colour (0xff8a6f52));
    for (const auto size : { juce::Point<int> (3, 2), juce::Point<int> (20, 11), juce::Point<int> (9, 5) })
    {
        const auto out = skin::resampleArea (flat, flat.getBounds().toFloat(), size.x, size.y);
        REQUIRE (out.getWidth() == size.x);
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
                CHECK (out.getPixelAt (x, y).getARGB() == 0xff8a6f52);
    }
    // A fractional area: half of a black and half of a white pixel make mid grey.
    juce::Image pair (juce::Image::ARGB, 2, 1, false);
    pair.setPixelAt (0, 0, juce::Colours::black);
    pair.setPixelAt (1, 0, juce::Colours::white);
    const auto grey = skin::resampleArea (pair, { 0.5f, 0.0f, 1.0f, 1.0f }, 1, 1);
    CHECK (std::abs (static_cast<int> (grey.getPixelAt (0, 0).getRed()) - 128) <= 1);
}

TEST_CASE ("plugin: a damaged ceramic pack is refused and the classic look draws", "[plugin][ceramic]")
{
    const char junk[] = "not a zip at all";
    skin::Pieces garbage (junk, sizeof (junk));
    CHECK_FALSE (garbage.isValid());
    CHECK (garbage.problem().isNotEmpty());
    CHECK (garbage.find ("housing") == nullptr);
    juce::MemoryBlock storage, manifest;
    const char* text = "{\"schemaVersion\":1,\"pieces\":[{\"name\":\"x\",\"kind\":\"alpha\",\"rect\":[0,0,10,10],\"margin\":2,"
                       "\"insets\":[3,3,3,3],\"levels\":[{\"scale\":2,\"file\":\"pieces/x@2x.png\",\"width\":28,\"height\":28}]}]}";
    manifest.append (text, std::strlen (text));
    const auto data = zipOf ({ { "manifest.json", manifest } }, storage);
    skin::Pieces missing (data.front().first, data.front().second);
    CHECK_FALSE (missing.isValid());
    CHECK (missing.problem().contains ("missing"));
    const char* future = "{\"schemaVersion\":2,\"pieces\":[]}";
    juce::MemoryBlock storage2, manifest2;
    manifest2.append (future, std::strlen (future));
    const auto data2 = zipOf ({ { "manifest.json", manifest2 } }, storage2);
    skin::Pieces newer (data2.front().first, data2.front().second);
    CHECK_FALSE (newer.isValid());
    // Classic: nothing is active, the drawing is the vector one (a raised card is painted).
    const ScopedLook classic (skin::Look::classic);
    skin::Pieces::Holder holder;
    CHECK (skin::active() == nullptr);
    juce::Image image (juce::Image::ARGB, 120, 120, true);
    {
        juce::Graphics g (image);
        osp::plugin::design::draw::raised (g, { 10.0f, 10.0f, 100.0f, 100.0f }, 16.0f, osp::plugin::design::colour::cardTop,
                                           osp::plugin::design::colour::cardBottom);
    }
    CHECK (image.getPixelAt (60, 60).getAlpha() == 255);
}

TEST_CASE ("plugin: ceramic repaints reuse composed pieces; the cache stays bounded", "[plugin][ceramic]")
{
    const ScopedLook ceramic (skin::Look::ceramic);
    skin::Pieces::Holder holder;
    auto* pieces = skin::active();
    REQUIRE (pieces != nullptr);
    pieces->release();
    auto paint = [&] (float scale, juce::Rectangle<float> target) {
        juce::Image image (juce::Image::ARGB, juce::roundToInt (500 * scale), juce::roundToInt (300 * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        REQUIRE (pieces->draw (g, "card-451", target));
        REQUIRE (pieces->draw (g, "key-modifier-hover", { 30.0f, 30.0f, 98.0f, 45.0f }));
    };
    paint (2.0f, { 20.0f, 20.0f, 451.0f, 260.0f });
    const auto first = pieces->usage();
    CHECK (first.composedImages == 2);
    for (int i = 0; i < 10; ++i)
        paint (2.0f, { 20.0f, 20.0f, 451.0f, 260.0f });
    CHECK (pieces->usage().composedImages == first.composedImages);
    CHECK (pieces->usage().bytes == first.bytes);
    // A resizing window passes through many sizes: memory stays bounded.
    for (int s = 0; s < 40; ++s)
        paint (1.0f + 0.05f * static_cast<float> (s), { 20.0f, 20.0f, 451.0f, 260.0f });
    CHECK (pieces->usage().bytes <= 192u * 1024u * 1024u);
    pieces->release();
    CHECK (pieces->usage().bytes == 0);
}

TEST_CASE ("plugin: ceramic keys are drawn by their surface and follow their state", "[plugin][ceramic]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    const auto file = writeSource (tmp.dir, "Steady Pluck C4.wav", testsignals::pluck (261.6256, 1.2, 48000.0, 21));
    OspAudioProcessor p;
    p.prepareToPlay (48000.0, 256);
    loadAndWait (p, file);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    ui->refreshNow();
    REQUIRE (skin::active() != nullptr);

    // Every key of the main GUI is a KeyFace whose surface draws it.
    std::vector<juce::Component*> keys;
    std::function<void (juce::Component&)> collect = [&] (juce::Component& c) {
        if (dynamic_cast<skin::KeyFace*> (&c) != nullptr && c.isVisible())
            keys.push_back (&c);
        for (auto* child : c.getChildren())
            collect (*child);
    };
    collect (*editor);
    CHECK (keys.size() >= 4 + 2 + 1 + 1 + 3);   // modifiers, M/S, mode, EQ, preset, ARP, Advanced
    auto* modifier = [&]() -> osp::plugin::ModifierButton* {
        for (auto* k : keys)
            if (auto* m = dynamic_cast<osp::plugin::ModifierButton*> (k))
                return m;
        return nullptr;
    }();
    REQUIRE (modifier != nullptr);
    const bool wasOn = modifier->isOn();
    CHECK (modifier->keyState() == (wasOn ? skin::KeyState::active : skin::KeyState::normal));
    // On: the warm face, drawn by the card (the button itself draws only its icon).
    auto faceAt = [&] {
        ui->refreshNow();
        const auto area = editor->getLocalArea (modifier, modifier->getLocalBounds());
        const auto shot = editor->createComponentSnapshot (area, true, 1.0f);
        return shot.getPixelAt (6, shot.getHeight() / 2);
    };
    const auto before = faceAt();
    modifier->mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { 5.0f, 5.0f }, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                         modifier, modifier, juce::Time::getCurrentTime(), { 5.0f, 5.0f }, juce::Time::getCurrentTime(), 1, false));
    p.pollLoads();
    CHECK (modifier->isOn() != wasOn);
    const auto after = faceAt();
    // The active face is peach: redder than it is blue, against the neutral ivory.
    auto warmth = [] (juce::Colour c) { return static_cast<int> (c.getRed()) - static_cast<int> (c.getBlue()); };
    CHECK (std::abs (warmth (after) - warmth (before)) >= 10);
    CHECK (warmth (modifier->isOn() ? after : before) > warmth (modifier->isOn() ? before : after));
}

TEST_CASE ("plugin: the look changes looks only: parameters, state and sound are untouched", "[plugin][ceramic]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook restore (skin::Look::ceramic);
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
    juce::MemoryBlock before;
    p.getStateInformation (before);
    std::map<int, juce::Image> shots;
    const auto busy = renderQaChord (p, 1.5, [&] (int block) {
        if (block % 25 != 0)
            return;
        const auto l = (block / 25) % 2 == 0 ? skin::Look::ceramic : skin::Look::classic;
        skin::setLook (l);
        ui->refreshNow();
        shots[static_cast<int> (l)] = editor->createComponentSnapshot ({ 0, 0, 480, 200 }, true, 1.0f);
    });
    REQUIRE (quiet.size() == busy.size());
    CHECK (std::memcmp (quiet.data(), busy.data(), quiet.size() * sizeof (float)) == 0);   // bit for bit
    juce::MemoryBlock after;
    p.getStateInformation (after);
    CHECK (before == after);
    REQUIRE (shots.size() == 2);
    CHECK (maxDifference (shots[0], shots[1]) > 20);   // the looks really differ
}

TEST_CASE ("plugin: the ceramic editor paints at every scale, one, two and three layers, arpeggiator open", "[plugin][ceramic]")
{
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    const auto a = writeSource (tmp.dir, "A C4.wav", testsignals::pluck (261.6256, 1.0, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "B E4.wav", testsignals::pluck (329.63, 1.0, 48000.0, 4));
    const auto c = writeSource (tmp.dir, "C G4.wav", testsignals::pluck (392.0, 1.0, 48000.0, 5));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    for (const auto& files : std::vector<juce::Array<juce::File>> { { a }, { b }, { c } })
    {
        p.addLayers (files);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        for (const auto scale : { 1.0f, 1.5f, 2.0f })
            for (const int width : { 1086, 1448 })
            {
                editor->setSize (width, juce::roundToInt (width * 1086.0f / 1448.0f));
                ui->refreshNow();
                const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
                REQUIRE (shot.isValid());
                // The teak cheek at the left edge, the housing beside it: rendered, not empty.
                const int y = shot.getHeight() / 2;
                CHECK (shot.getPixelAt (juce::roundToInt (0.008f * shot.getWidth()), y).getAlpha() == 255);
                const auto teak = shot.getPixelAt (juce::roundToInt (0.009f * shot.getWidth()), y);
                CHECK (teak.getRed() > teak.getBlue() + 30);
            }
    }
    p.setParameterValue ("arp.enabled", 1.0f);
    ui->refreshNow();
}

TEST_CASE ("plugin: ceramic vs classic screenshots (full GUI, close-ups, states)", "[.][ceramic-shots]")
{
    const char* dir = std::getenv ("OSP_SNAPSHOT_DIR");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook restore (skin::Look::ceramic);
    const auto a = writeSource (tmp.dir, "MMZT_one_shot_sailboat_C.wav", testsignals::vowel (midiToHz (69), 2.1, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "B A4.wav", testsignals::pluck (440.0, 3.2, 48000.0, 4));
    const auto c = writeSource (tmp.dir, "C E4.wav", testsignals::pluck (329.63, 2.4, 48000.0, 5));
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    p.addLayers ({ a, b, c });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    p.setParameterValue ("layerA.loop", 1.0f);
    p.setParameterValue ("character", 78.0f);
    auto save = [&] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    for (const auto look : { skin::Look::ceramic, skin::Look::classic })
    {
        skin::setLook (look);
        const juce::String tag = look == skin::Look::ceramic ? "ceramic" : "classic";
        for (const auto scale : { 1.0f, 2.0f })
        {
            editor->setSize (1448, 1086);
            ui->refreshNow();
            save (editor->createComponentSnapshot (editor->getLocalBounds(), true, scale), "gui-" + tag + "@" + juce::String (juce::roundToInt (scale)) + "x.png");
        }
        editor->setSize (1086, 815);
        ui->refreshNow();
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), "gui-" + tag + "-1086@2x.png");
        // The arpeggiator's editor open (the window taller, the housing stretched).
        ui->setArpExpanded (true);
        editor->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
        ui->refreshNow();
        save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), "gui-" + tag + "-arp@1x.png");
        ui->setArpExpanded (false);
    }
    // One and two layers (the cards at their own rendered widths).
    skin::setLook (skin::Look::ceramic);
    for (const int count : { 1, 2 })
    {
        OspAudioProcessor q;
        std::unique_ptr<juce::AudioProcessorEditor> e (q.createEditorIfNeeded());
        auto* u = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (u != nullptr);
        q.addLayers (count == 1 ? juce::Array<juce::File> { a } : juce::Array<juce::File> { a, b });
        REQUIRE (q.waitForLoads (30000));
        q.pollLoads();
        e->setSize (1448, 1086);
        u->refreshNow();
        save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), "gui-ceramic-" + juce::String (count) + "-layer@1x.png");
    }
}
