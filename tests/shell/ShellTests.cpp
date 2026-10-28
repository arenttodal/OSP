// Headless tests of a product built from the ANDOR Instrument Shell (built once per product,
// with that product's Product.cpp): identity and registry, the shared parameter schema, the
// engine boundary (sources, MIDI, wheels, real-time hand-over), presets and saved state, the
// interface's bindings, and the visual regression against ANDOR OSP ("[visual-regression]",
// run by ctest after osp_visual_reference).

#include "Binding.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include "../support/ParameterSchema.h"
#include "../support/VisualStates.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

using Catch::Approx;
using andor::shell::ShellEditor;
using andor::shell::ShellProcessor;

namespace
{
    void setEnvironment (const char* name, const juce::String& value)
    {
#if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
#else
        setenv (name, value.toRawUTF8(), 1);
#endif
    }

    struct TempDir
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("andor-shell-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        TempDir() { dir.createDirectory(); }
        ~TempDir() { dir.deleteRecursively(); }
    };

    juce::File writeSource (const juce::File& dir, const juce::String& name, const osp::AudioData& audio)
    {
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (osp::io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, osp::io::SampleFormat::pcm24, error));
        return file;
    }

    struct ScopedLook
    {
        explicit ScopedLook (osp::plugin::skin::Look l) : before (osp::plugin::skin::look()) { osp::plugin::skin::setLook (l); }
        ~ScopedLook() { osp::plugin::skin::setLook (before); }
        osp::plugin::skin::Look before;
    };

    /** Renders `seconds` with one note held from the start (and released at `releaseAt`). */
    juce::AudioBuffer<float> play (ShellProcessor& p, int note, double seconds, double releaseAt = 1.0e9, int block = 256)
    {
        constexpr double rate = 48000.0;
        p.prepareToPlay (rate, block);
        const int total = static_cast<int> (seconds * rate);
        juce::AudioBuffer<float> out (2, total), buffer (2, block);
        const int releaseSample = static_cast<int> (releaseAt * rate);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
            if (releaseSample >= pos && releaseSample < pos + block)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), releaseSample - pos);
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    float rms (const juce::AudioBuffer<float>& b, double from, double to)
    {
        const int a = static_cast<int> (from * 48000.0), z = std::min (b.getNumSamples(), static_cast<int> (to * 48000.0));
        return b.getRMSLevel (0, a, std::max (1, z - a));
    }

    /** A test-only engine that reads every parameter and makes no sound: the shell then
        shows every control enabled, as ANDOR OSP does (the visual regression's parity). */
    struct ParityEngine final : andor::shell::Engine
    {
        Description describe() const override { return { "Parity (test only)", false, {} }; }
        juce::StringArray boundParameters() const override { return { "*" }; }
        void prepare (double, int, juce::AudioProcessorValueTreeState&) override {}
        void setSource (int, const osp::plugin::LoadedInstrument*) noexcept override {}
        void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Performance&) noexcept override {}
    };

    /** A four-character code as JUCE's plugin defines give it (0x41657870 for "Aexp"). */
    int fourCharCode (const juce::String& code)
    {
        int value = 0;
        for (int i = 0; i < 4; ++i)
            value = (value << 8) | (static_cast<int> (code[i]) & 0xff);
        return value;
    }

    juce::var registry()
    {
        return juce::JSON::parse (juce::File (OSP_SOURCE_DIR).getChildFile ("andor-design-system/product-registry.json"));
    }
}

TEST_CASE ("shell: the product is itself (name, codes, namespaces) and its identifiers are unique", "[shell]")
{
    const auto& product = andor::shell::productInfo();
    CHECK (product.name == JucePlugin_Name);
    CHECK (product.presetExtension.startsWith ("."));
    CHECK (product.stateType != "OSP");
    CHECK (product.slug != "osp");

    // Its registry entry says the same as the plugin it was built as; no other plugin shares them.
    const auto list = registry()["products"];
    REQUIRE (list.isArray());
    int own = 0;
    for (const auto& entry : *list.getArray())
    {
        const bool mine = entry["slug"].toString() == product.slug;
        own += mine ? 1 : 0;
        // (The bundle ID is checked against the registry when CMake configures the product.)
        if (mine)
        {
            CHECK (fourCharCode (entry["pluginCode"].toString()) == JucePlugin_PluginCode);
            CHECK (entry["name"].toString() == product.name);
            CHECK (entry["stateType"].toString() == product.stateType);
            CHECK (entry["presetExtension"].toString() == product.presetExtension);
        }
        else
        {
            CHECK (fourCharCode (entry["pluginCode"].toString()) != JucePlugin_PluginCode);
            CHECK (entry["name"].toString() != product.name);
            CHECK (entry["stateType"].toString() != product.stateType);
        }
    }
    CHECK (own == 1);
    CHECK (fourCharCode ("Ospx") == JucePlugin_ManufacturerCode);

    // The identity fits the header (it ends before the preset bar).
    using namespace osp::plugin;
    const auto bold = fonts::make (60.0f, fonts::Weight::displayBold).withHorizontalScale (1.07f);
    const auto light = fonts::make (60.0f, fonts::Weight::displayLight).withHorizontalScale (1.07f);
    juce::GlyphArrangement mark, rest;
    mark.addLineOfText (bold, product.identity.mark, design::layout::logo.x, design::layout::logo.y);
    const float w = mark.getBoundingBox (0, -1, true).getRight() - design::layout::logo.x + 1.0f;
    rest.addLineOfText (light, product.identity.product, design::layout::logo.x + w, design::layout::logo.y);
    CHECK (rest.getBoundingBox (0, -1, true).getRight() < design::layout::presetBar.getX() - 12.0f);
}

TEST_CASE ("shell: the parameter schema is ANDOR OSP's, unchanged; the product's own come after, in its namespace", "[shell]")
{
    ShellProcessor p;
    // ANDOR OSP's schema first, exactly; then the product's own parameters (if any), every
    // ID in the product's namespace ("<slug>."), pinned by the product's golden listing.
    const auto prefix = andor::shell::productInfo().slug + ".";
    juce::StringArray base, own;
    bool ownStarted = false, orderKept = true;
    for (const auto& line : juce::StringArray::fromLines (parameterschema::describe (p).trimEnd()))
    {
        const bool isOwn = line.startsWith (prefix);
        orderKept = orderKept && (isOwn || ! ownStarted);
        ownStarted = ownStarted || isOwn;
        (isOwn ? own : base).add (line);
    }
    CHECK (orderKept);
    parameterschema::checkAgainstGolden (base.joinIntoString ("\n") + "\n", "osp-parameter-schema.txt");
#ifdef ANDOR_PRODUCT_SCHEMA_GOLDEN
    REQUIRE_FALSE (own.isEmpty());
    parameterschema::checkAgainstGoldenFile (own.joinIntoString ("\n") + "\n", juce::File (ANDOR_PRODUCT_SCHEMA_GOLDEN));
#else
    CHECK (own.isEmpty());   // a product with parameters of its own pins them (andor_add_instrument_tests ... GOLDEN)
#endif
}

TEST_CASE ("shell: a generator engine has one card, layer A, and never loads a file", "[shell][engine]")
{
    TempDir tmp;
    ShellProcessor p;
    if (! p.isGenerator())
        return;
    CHECK (p.slotCount() == 1);
    CHECK (p.isLayerOccupied (0));
    CHECK_FALSE (p.isLayerOccupied (1));
    CHECK (p.loadState (0) == osp::plugin::InstrumentHost::LoadState::ready);
    osp::plugin::InstrumentHost::GeneratedSource source;
    REQUIRE (p.generatedSource (0, source));
    CHECK (source.name.isNotEmpty());
    CHECK (source.cycle.size() >= 2);
    CHECK_FALSE (p.generatedSource (1, source));
    // A dropped recording is refused, said so, and nothing changes.
    const auto file = writeSource (tmp.dir, "pluck.wav", osp::testsignals::pluck (220.0, 0.5, 48000.0, 1));
    CHECK (p.addLayers ({ file }) == 0);
    p.loadFile (file, 0);
    CHECK (p.waitForLoads (1000));
    p.pollLoads();
    CHECK (p.currentInstrument (0) == nullptr);
    CHECK (p.statusMessage().isNotEmpty());
    CHECK (p.slotCount() == 1);
    // It sounds with nothing loaded.
    CHECK (rms (play (p, 60, 1.0, 0.6), 0.1, 0.5) > 0.003f);
    // The editor shows its card, not the drop zone, and takes no files.
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    ui->refreshNow();
    CHECK (ui->visibleCardCount() == 1);
    CHECK_FALSE (ui->isInterestedInFileDrag ({ file.getFullPathName() }));
}

TEST_CASE ("shell: the diagnostic engine plays the sources it is given, says what it is, binds only what it reads", "[shell][engine]")
{
    TempDir tmp;
    ShellProcessor p;
    const auto engine = p.engineDescription();
    // Products may replace the engine; these checks are the shell's stand-in's.
    if (! engine.diagnostic)
        return;
    CHECK (engine.statusLine.containsIgnoreCase ("diagnostic"));
    CHECK (engine.statusLine.containsIgnoreCase ("not a production instrument"));
    CHECK (p.boundParameters().contains ("layerA.level"));
    CHECK_FALSE (p.boundParameters().contains ("life"));

    // No sound loaded: a held key plays the quiet test tone; released, it fades out.
    {
        const auto out = play (p, 69, 1.5, 0.5);
        CHECK (rms (out, 0.1, 0.4) > 0.01f);
        CHECK (rms (out, 0.1, 0.4) < 0.2f);
        CHECK (rms (out, 1.3, 1.5) < 1.0e-4f);
    }
    // A loaded sound plays through baseline A at the key's pitch: same note, same sound.
    const auto file = writeSource (tmp.dir, "vowel A3.wav", osp::testsignals::vowel (osp::midiToHz (57), 2.5, 48000.0, 3));
    REQUIRE (p.addLayers ({ file }) == 1);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    REQUIRE (p.loadState (0) == osp::plugin::InstrumentHost::LoadState::ready);
    const auto a = play (p, 57, 1.0);
    const auto b = play (p, 57, 1.0);
    CHECK (rms (a, 0.1, 0.8) > 0.01f);
    bool identical = true;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            identical = identical && juce::exactlyEqual (a.getSample (ch, i), b.getSample (ch, i));
    CHECK (identical);   // deterministic
    // MUTE silences the layer; LEVEL moves it.
    p.setParameterValue ("layerA.mute", 1.0f);
    CHECK (rms (play (p, 57, 0.6), 0.1, 0.5) < 1.0e-6f);
    p.setParameterValue ("layerA.mute", 0.0f);
    p.setParameterValue ("layerA.level", -12.0f);
    CHECK (rms (play (p, 57, 0.6), 0.1, 0.5) == Approx (rms (a, 0.1, 0.5) * std::pow (10.0f, -12.0f / 20.0f)).epsilon (0.05));
}

TEST_CASE ("shell: a new sound replaces a layer's on the audio thread without a gap in the hand-over", "[shell][engine]")
{
    TempDir tmp;
    ShellProcessor p;
    if (p.isGenerator())
        return;   // it loads nothing (the generator test above)
    const auto first = writeSource (tmp.dir, "one.wav", osp::testsignals::pluck (220.0, 1.5, 48000.0, 1));
    const auto second = writeSource (tmp.dir, "two.wav", osp::testsignals::pluck (330.0, 1.5, 48000.0, 2));
    p.prepareToPlay (48000.0, 256);
    p.loadFile (first, 0);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    const auto before = p.currentInstrument (0);
    p.loadFile (second, 0);
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    CHECK (p.currentInstrument (0) != before);
    CHECK (p.currentInstrument (0)->filename == "two.wav");
    // Blocks keep rendering while sources change (hand-over through ModelExchange).
    CHECK (rms (play (p, 60, 0.5), 0.05, 0.4) > 0.0f);
    // A file that is not audio fails beautifully: reported, the layer keeps its sound.
    const auto bad = tmp.dir.getChildFile ("not audio.wav");
    bad.replaceWithText ("this is not a wave file");
    p.loadFile (bad, 0);
    p.waitForLoads (30000);
    p.pollLoads();
    CHECK (p.currentInstrument (0) != nullptr);
    CHECK (p.statusMessage().isNotEmpty());
}

TEST_CASE ("shell: presets and sessions save and reload; another product's state is never read", "[shell][presets]")
{
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "pluck E4.wav", osp::testsignals::pluck (329.63, 2.0, 48000.0, 5));
    juce::MemoryBlock session;
    juce::File preset;
    const bool generator = ShellProcessor().isGenerator();   // no sources to save or recall
    {
        ShellProcessor p;
        p.addLayers ({ file });
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        p.setParameterValue ("layerA.level", -6.0f);
        p.setParameterValue ("attack", 120.0f);
        REQUIRE (p.assignModulation (osp::mod::Source::lfo1, osp::mod::Dest::levelA, 40.0f).slot >= 0);
        preset = p.presetFolder().getChildFile ("Soft Pluck");
        const auto factory = p.presetList().size();   // a product's factory presets, if it has any
        REQUIRE (p.savePreset (preset));
        preset = preset.withFileExtension (andor::shell::productInfo().presetExtension);
        CHECK (preset.existsAsFile());
        p.getStateInformation (session);
        CHECK (p.presetList().size() == factory + 1);
        CHECK (p.presetDisplayName() == "Soft Pluck");
    }
    // A session recalls parameters, routes and sources.
    {
        ShellProcessor q;
        q.setStateInformation (session.getData(), static_cast<int> (session.getSize()));
        REQUIRE (q.waitForLoads (30000));
        q.pollLoads();
        CHECK (q.parameterValue ("layerA.level") == Approx (-6.0f).margin (0.05f));
        CHECK (q.parameterValue ("attack") == Approx (120.0f).margin (0.5f));
        CHECK (q.modulationRoutes().size() == 1);
        if (! generator)
        {
            REQUIRE (q.currentInstrument (0) != nullptr);
            CHECK (q.currentInstrument (0)->filename == "pluck E4.wav");
        }
    }
    // A preset file loads the same way (and can be undone with "previous state").
    {
        ShellProcessor r;
        r.setParameterValue ("attack", 5.0f);
        REQUIRE (r.loadPreset (preset));
        REQUIRE (r.waitForLoads (30000));
        r.pollLoads();
        CHECK (r.parameterValue ("attack") == Approx (120.0f).margin (0.5f));
        CHECK (r.canRestorePreviousState());
        REQUIRE (r.restorePreviousState());
        CHECK (r.parameterValue ("attack") == Approx (5.0f).margin (0.5f));
    }
    // ANDOR OSP's (or any other product's) state is refused: nothing changes.
    {
        ShellProcessor s;
        s.setParameterValue ("attack", 33.0f);
        juce::XmlElement osp ("OSP");
        osp.setAttribute ("stateVersion", 12);
        juce::MemoryBlock foreign;
        juce::AudioProcessor::copyXmlToBinary (osp, foreign);
        s.setStateInformation (foreign.getData(), static_cast<int> (foreign.getSize()));
        CHECK (s.parameterValue ("attack") == Approx (33.0f).margin (0.5f));
    }
}

TEST_CASE ("shell: the interface binds what the engine reads and disables the rest", "[shell][editor]")
{
    const ScopedLook ceramic (osp::plugin::skin::Look::ceramic);
    TempDir tmp;
    const auto file = writeSource (tmp.dir, "vowel A4.wav", osp::testsignals::vowel (osp::midiToHz (69), 2.0, 48000.0, 3));
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    p.addLayers ({ file });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    ui->refreshNow();
    CHECK (ui->visibleCardCount() == 1);

    // Each tagged control is enabled exactly when the engine reads one of its parameters.
    int checked = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (const auto tags = osp::plugin::binding::tagsOf (*child); ! tags.isEmpty() && child->isVisible())
            {
                INFO (tags.joinIntoString (" "));
                CHECK (child->isEnabled() == osp::plugin::binding::anyBound (tags, p.boundParameters()));
                ++checked;
            }
            walk (*child);
        }
    };
    walk (*editor);
    CHECK (checked > 20);
    CHECK (ui->disabledControlCount() > 0);
    if (p.engineDescription().diagnostic)
        CHECK (ui->disabledControlCount() > 10);

    // The Advanced popover (ANDOR OSP's, shared) follows the bindings too.
    ui->openPopup (ShellEditor::advancedPopup);
    REQUIRE (ui->openPopupPanel() != nullptr);
    walk (*ui->openPopupPanel());
    ui->closePopup();

    // With an engine that reads everything, nothing is disabled.
    ShellProcessor all (std::make_unique<ParityEngine>());
    std::unique_ptr<juce::AudioProcessorEditor> allEditor (all.createEditorIfNeeded());
    CHECK (dynamic_cast<ShellEditor&> (*allEditor).disabledControlCount() == 0);
}

namespace
{
    /** Pixels that differ (any channel by more than `tolerance`) outside the masks; writes a
        diff image (red: differs, blue tint: masked). */
    struct Difference
    {
        juce::Rectangle<int> bounds; ///< of the differing pixels outside the masks, in image pixels
        int largest = 0;             ///< the largest channel difference among them (0..255)
    };

    int compareImages (const juce::Image& a, const juce::Image& b, const std::vector<visualstates::Mask>& masks, float scale,
                       int tolerance, const juce::File& diffFile, int& differingInMasks, Difference& where)
    {
        differingInMasks = 0;
        where = {};
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
            return a.getWidth() * a.getHeight();
        juce::Image diff (juce::Image::ARGB, a.getWidth(), a.getHeight(), true);
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dd (diff, juce::Image::BitmapData::writeOnly);
        int count = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const juce::Point<float> at (static_cast<float> (x) / scale, static_cast<float> (y) / scale);
                bool masked = false;
                for (const auto& m : masks)
                    masked = masked || m.area.contains (at);
                const auto pa = da.getPixelColour (x, y), pb = db.getPixelColour (x, y);
                const int d = std::max ({ std::abs (pa.getRed() - pb.getRed()), std::abs (pa.getGreen() - pb.getGreen()),
                                          std::abs (pa.getBlue() - pb.getBlue()), std::abs (pa.getAlpha() - pb.getAlpha()) });
                auto shown = pa.withMultipliedSaturation (0.0f).withMultipliedAlpha (0.35f);
                if (masked)
                {
                    shown = shown.interpolatedWith (juce::Colours::royalblue, 0.35f);
                    differingInMasks += d > tolerance ? 1 : 0;
                }
                else if (d > tolerance)
                {
                    shown = juce::Colours::red;
                    ++count;
                    const juce::Rectangle<int> pixel (x, y, 1, 1);
                    where.bounds = where.bounds.isEmpty() ? pixel : where.bounds.getUnion (pixel);
                    where.largest = std::max (where.largest, d);
                }
                dd.setPixelColour (x, y, shown);
            }
        visualstates::savePng (diff, diffFile);
        return count;
    }
}

TEST_CASE ("shell: the interface is ANDOR OSP's, pixel for pixel outside the approved dynamic regions", "[.][visual-regression]")
{
    const char* referenceEnv = std::getenv ("ANDOR_VISUAL_REFERENCE_DIR");
    const char* outputEnv = std::getenv ("ANDOR_VISUAL_OUTPUT_DIR");
    REQUIRE (referenceEnv != nullptr);
    REQUIRE (outputEnv != nullptr);
    const juce::File reference (referenceEnv), output (outputEnv);
    output.deleteRecursively();
    const ScopedLook ceramic (osp::plugin::skin::Look::ceramic);
    TempDir tmp;

    // Parity: the same states rendered by the shell with every control enabled (and ANDOR
    // OSP's macro names: a product's own are its identity, like the header's).
    visualstates::render<ShellProcessor, ShellEditor> (output.getChildFile ("parity"), tmp.dir.getChildFile ("parity"), [] {
        auto p = std::make_unique<ShellProcessor> (std::make_unique<ParityEngine>());
        p->showReferenceMacros();
        return p;
    });
    // As shipped: the product's own engine (its unbound controls disabled), for the gallery.
    visualstates::render<ShellProcessor, ShellEditor> (output.getChildFile ("shipped"), tmp.dir.getChildFile ("shipped"),
                                                       [] { return std::make_unique<ShellProcessor>(); });

    // Tolerance: none. The shell draws with the same code and the same rendered assets, so
    // every pixel outside the masks must be exactly ANDOR OSP's.
    constexpr int tolerance = 0;
    juce::StringArray report { "# Visual regression: " + andor::shell::productInfo().name + " vs ANDOR OSP", "",
                               "Tolerance: " + juce::String (tolerance) + " (exact), masks: identity, preset-name, status-line.", "",
                               "| State | Differing pixels outside the masks | Inside the masks (allowed) |", "|---|---|---|" };
    int failures = 0;
    for (const auto& name : visualstates::names())
    {
        INFO (name);
        const auto a = juce::ImageFileFormat::loadFrom (reference.getChildFile (name + ".png"));
        const auto b = juce::ImageFileFormat::loadFrom (output.getChildFile ("parity/" + name + ".png"));
        REQUIRE (a.isValid());
        REQUIRE (b.isValid());
        const float scale = name.endsWith ("@2x") ? 2.0f : (name.startsWith ("04-window-") ? static_cast<float> (a.getWidth()) / osp::plugin::design::width : 1.0f);
        const auto masks = visualstates::masks (name == "09-arp-open" ? osp::plugin::design::layout::arpShift : 0.0f);
        int inMasks = 0;
        Difference where;
        const int differing = compareImages (a, b, masks, scale, tolerance, output.getChildFile ("diff/" + name + ".png"), inMasks, where);
        // Where and how much, in reference pixels, so a failure reads without the diff image.
        const auto at = where.bounds.toFloat() / scale;
        const juce::String located = differing == 0 ? juce::String()
                                                    : " (x " + juce::String (at.getX(), 1) + ".." + juce::String (at.getRight(), 1) + ", y "
                                                          + juce::String (at.getY(), 1) + ".." + juce::String (at.getBottom(), 1)
                                                          + " reference px; largest channel difference " + juce::String (where.largest) + ")";
        report.add ("| " + name + " | " + juce::String (differing) + located + " | " + juce::String (inMasks) + " |");
        INFO (name << located);
        CHECK (differing == 0);
        failures += differing > 0 ? 1 : 0;
    }
    report.add ("");
    report.add (failures == 0 ? "Result: identical outside the masks in every state." : "Result: " + juce::String (failures) + " states differ (see diff/).");
    output.getChildFile ("report.md").replaceWithText (report.joinIntoString ("\n") + "\n");
}

int main (int argc, char* argv[])
{
    // Isolated folders so tests never touch the user's presets or samples.
    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("andor-shell-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    setEnvironment ("ANDOR_SAMPLE_STORE", base.getChildFile ("samples").getFullPathName());
    setEnvironment ("ANDOR_DOCUMENTS_DIR", base.getChildFile ("documents").getFullPathName());
    juce::ScopedJuceInitialiser_GUI juceInit;
    const int result = Catch::Session().run (argc, argv);
    base.deleteRecursively();
    return result;
}
