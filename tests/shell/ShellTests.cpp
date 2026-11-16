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

#include <array>
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
        bool hasKeyDna() const noexcept override { return true; }   // the Granular strip's KEY DNA key, as ANDOR OSP's
        bool hasSynthesize() const noexcept override { return true; }   // the cards' SYNTHESIZE knob, as ANDOR OSP's
    };

    /** A test-only engine that logs the notes it is given (sample, note; velocity 0: off),
        binding `bound`, and shows grains when given some. */
    struct NoteLogEngine final : andor::shell::Engine
    {
        explicit NoteLogEngine (juce::StringArray b) : boundIds (std::move (b)) {}
        Description describe() const override { return { "Note log (test only)", false, {} }; }
        juce::StringArray boundParameters() const override { return boundIds; }
        void prepare (double, int, juce::AudioProcessorValueTreeState&) override { clock = 0; }
        void setSource (int, const osp::plugin::LoadedInstrument*) noexcept override {}
        void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance&) noexcept override
        {
            for (const auto m : midi)
                if (const auto msg = m.getMessage(); msg.isNoteOnOrOff())
                    log.push_back ({ clock + m.samplePosition, msg.getNoteNumber(), msg.isNoteOn() ? msg.getVelocity() : 0 });
            clock += buffer.getNumSamples();
        }
        const osp::InstrumentEngine::GrainSnapshot* grainSnapshot (int layer) const noexcept override { return layer == 0 ? &grains : nullptr; }
        struct Event
        {
            std::int64_t sample;
            int note, velocity;
        };
        juce::StringArray boundIds;
        std::vector<Event> log;
        std::int64_t clock = 0;
        osp::InstrumentEngine::GrainSnapshot grains;
    };

    /** A chord held from the start for `seconds`, rendered in blocks of 256 at 48 kHz. */
    void holdChord (ShellProcessor& p, double seconds)
    {
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        for (int pos = 0; pos < static_cast<int> (seconds * 48000.0); pos += 256)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (const int note : { 60, 64, 67 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 10);
            p.processBlock (buffer, midi);
        }
    }

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
    // A product's own modulation destinations follow ANDOR OSP's in the routes' destination
    // choices (in its order): those are checked, and the rest of the choice as ANDOR OSP's.
    const auto prefix = andor::shell::productInfo().slug + ".";
    const auto& productDests = andor::shell::productInfo().modulationDestinations;
    CHECK (osp::mod::productDestCount() == static_cast<int> (productDests.size()));
    juce::StringArray base, own;
    bool ownStarted = false, orderKept = true, vibratoSeen = false;
    for (auto* parameter : p.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter);
        REQUIRE (ranged != nullptr);
        auto line = parameterschema::describe (ranged);
        const auto id = ranged->getParameterID();
        if (id.startsWith ("mod.route") && id.endsWith (".dest"))
        {
            auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged);
            REQUIRE (choice != nullptr);
            REQUIRE (choice->choices.size() == osp::mod::hostDestCount + static_cast<int> (productDests.size()));
            for (std::size_t k = 0; k < productDests.size(); ++k)
                CHECK (choice->choices[osp::mod::hostDestCount + static_cast<int> (k)] == juce::String (productDests[k].name));
            juce::StringArray ospChoices (choice->choices);
            ospChoices.removeRange (osp::mod::hostDestCount, static_cast<int> (productDests.size()));
            juce::AudioParameterChoice asOsp ({ id, ranged->getVersionHint() }, ranged->getName (128), ospChoices, 0);
            line = parameterschema::describe (&asOsp);
        }
        if (id.startsWith ("mod.route") && id.endsWith (".source"))
        {
            // A product's own sources (C RHYTHM, when it asks for it) follow ANDOR OSP's twelve:
            // those are checked, and the rest of the choice as ANDOR OSP's.
            auto* choice = dynamic_cast<juce::AudioParameterChoice*> (ranged);
            REQUIRE (choice != nullptr);
            REQUIRE (choice->choices.size() == osp::mod::offeredSourceCount() + 1);
            for (int i = osp::mod::baseSourceCount + 1; i < choice->choices.size(); ++i)
                CHECK (choice->choices[i] == osp::mod::sourceName (static_cast<osp::mod::Source> (i)));
            juce::StringArray ospChoices (choice->choices);
            ospChoices.removeRange (osp::mod::baseSourceCount + 1, choice->choices.size() - osp::mod::baseSourceCount - 1);
            juce::AudioParameterChoice asOsp ({ id, ranged->getVersionHint() }, ranged->getName (128), ospChoices, 0);
            line = parameterschema::describe (&asOsp);
        }
        const bool isOwn = line.startsWith (prefix);
        // ANDOR OSP's parameters added after products shipped (the MOD WHEEL's vibrato.*) come
        // after the product's own, so none of a product's moves; the rest of ANDOR OSP's before.
        const bool appendedAfterProducts = id.startsWith ("vibrato.");
        orderKept = orderKept && (isOwn || ! ownStarted || appendedAfterProducts);
        if (appendedAfterProducts)
            vibratoSeen = true;
        else
            orderKept = orderKept && ! vibratoSeen;   // (and those really last)
        ownStarted = ownStarted || isOwn;
        (isOwn ? own : base).add (line);
    }
    CHECK (orderKept);
    // ANDOR OSP's schema, less its own per-card arpeggiator (arp.link, arp.cardA ..): a product
    // that has one keeps it in its own namespace (InstrumentHost::addCardArpParameters). Less
    // its spectral imprint too (layerA.synth.mode / .imprint / .motion): ANDOR OSP's SYNTH page.
    auto ospBase = parameterschema::ospGolden ("osp-parameter-schema.txt");
    auto ospOnly = [] (const juce::String& line) {
        if (line.startsWith ("arp.link ") || line.startsWith ("arp.card"))
            return true;
        // The FX library's parameters (fx.<type>.<n>.*): a product that has the library keeps
        // them in its own namespace (ANDOR ANALOG: analog.fx.*).
        if (line.startsWith ("fx."))
            return true;
        // SATELLITE (layerA.satellite.* ..): a product that has it keeps it in its own namespace
        // (ANDOR ANALOG: analog.satellite.*).
        if (line.startsWith ("layer") && line.upToFirstOccurrenceOf (" ", false, false).contains (".satellite."))
            return true;
        // SUB (sub.*): a product that has it keeps it in its own namespace (ANDOR ANALOG: analog.sub.*).
        if (line.startsWith ("sub."))
            return true;
        // ANDOR OSP's PEDAL LOOP (layerA.pedalLoop ..): its One Shot cards' sustain-pedal loop.
        if (line.startsWith ("layer") && line.upToFirstOccurrenceOf (" ", false, false).endsWith (".pedalLoop"))
            return true;
        for (const auto& name : osp::plugin::InstrumentHost::imprintParameterNames())
            if (line.startsWith ("layer") && line.upToFirstOccurrenceOf (" ", false, false).endsWith (".synth." + name))
                return true;
        return false;
    };
    for (int i = ospBase.size(); --i >= 0;)
        if (ospOnly (ospBase[i]))
            ospBase.remove (i);
    parameterschema::compareListings (base.joinIntoString ("\n") + "\n", ospBase);
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
    // (Three engines: the empty hybrid is a stand-in card, its engine's own: occupied.)
    const bool standIn = p.product().threeEngines.enabled && p.hasHybridLayer();
    CHECK (p.slotCount() == (standIn ? 2 : 1));
    CHECK (p.isLayerOccupied (0));
    CHECK (p.isLayerOccupied (1) == standIn);
    CHECK (p.loadState (0) == osp::plugin::InstrumentHost::LoadState::ready);
    osp::plugin::InstrumentHost::GeneratedSource source;
    REQUIRE (p.generatedSource (0, source));
    CHECK (source.name.isNotEmpty());
    CHECK (source.cycle.size() >= 2);
    CHECK (p.generatedSource (1, source) == standIn);
    const auto file = writeSource (tmp.dir, "pluck.wav", osp::testsignals::pluck (220.0, 0.5, 48000.0, 1));
    if (p.hasHybridLayer())
    {
        // A hybrid generator (ProductInfo::hybridSampleName) takes one recording on layer B,
        // beside its card; layer A stays the generator, layer C is never used.
        CHECK (p.addLayers ({ file, file }) == 1);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        CHECK (p.currentInstrument (0) == nullptr);
        CHECK (p.currentInstrument (1) != nullptr);
        CHECK (p.loadState (1) == osp::plugin::InstrumentHost::LoadState::ready);
        CHECK (p.slotCount() == 2);
        CHECK_FALSE (p.isLayerOccupied (2));
        CHECK_FALSE (p.generatedSource (1, source));
        CHECK (p.statusMessage().isNotEmpty());   // (the second file was not loaded: said so)
        p.removeLayer (1);
        CHECK (p.slotCount() == (standIn ? 2 : 1));
        CHECK (p.currentInstrument (1) == nullptr);
        return;
    }
    // A dropped recording is refused, said so, and nothing changes.
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

TEST_CASE ("shell: an engine that binds arp.* is played by ANDOR OSP's arpeggiator; others get the keys as played", "[shell][engine][arp]")
{
    auto onsets = [] (const NoteLogEngine& e) {
        std::vector<NoteLogEngine::Event> on;
        for (const auto& ev : e.log)
            if (ev.velocity > 0)
                on.push_back (ev);
        return on;
    };
    {
        // Not bound: the chord arrives as played, even with the arpeggiator switched on.
        auto engine = std::make_unique<NoteLogEngine> (juce::StringArray { "layerA.level" });
        auto& log = *engine;
        ShellProcessor p (std::move (engine));
        CHECK_FALSE (p.runsArpeggiator());
        p.parameters.getParameter ("arp.enabled")->setValueNotifyingHost (1.0f);
        holdChord (p, 1.0);
        const auto on = onsets (log);
        REQUIRE (on.size() == 3);
        CHECK (on[0].sample == 10);
        CHECK_FALSE (p.arpView().active);
    }
    {
        // Bound, off: as played.
        auto engine = std::make_unique<NoteLogEngine> (juce::StringArray { "arp.*" });
        auto& log = *engine;
        ShellProcessor p (std::move (engine));
        CHECK (p.runsArpeggiator());
        holdChord (p, 1.0);
        CHECK (onsets (log).size() == 3);
    }
    std::vector<NoteLogEngine::Event> first;
    for (int run = 0; run < 2; ++run)
    {
        // Bound, on: UP in eighths at 120 BPM (no host: free-running), a step every 0.25 s,
        // each note alone and held for the gate (75 %); the display runs.
        auto engine = std::make_unique<NoteLogEngine> (juce::StringArray { "arp.*" });
        auto& log = *engine;
        ShellProcessor p (std::move (engine));
        p.parameters.getParameter ("arp.enabled")->setValueNotifyingHost (1.0f);
        holdChord (p, 1.6);
        const auto on = onsets (log);
        REQUIRE (on.size() >= 6);
        const int expected[] { 60, 64, 67, 60, 64, 67 };
        for (std::size_t i = 0; i < 6; ++i)
        {
            INFO ("step " << i);
            CHECK (on[i].note == expected[i]);
            CHECK (on[i].sample == Approx (10.0 + 12000.0 * static_cast<double> (i)).margin (1.0));
        }
        for (std::size_t i = 0; i + 1 < log.log.size(); ++i)
            if (log.log[i].velocity > 0)
                CHECK ((log.log[i + 1].velocity == 0 && log.log[i + 1].note == log.log[i].note));   // ends before the next starts
        CHECK (p.arpView().active);
        CHECK (p.arpView().current >= 0);
        if (run == 0)
            first = log.log;
        else
        {
            REQUIRE (log.log.size() == first.size());   // the same every time
            for (std::size_t i = 0; i < first.size(); ++i)
                CHECK ((log.log[i].sample == first[i].sample && log.log[i].note == first[i].note && log.log[i].velocity == first[i].velocity));
        }
    }
}

TEST_CASE ("shell: a generator engine's grains reach the card", "[shell][engine]")
{
    auto engine = std::make_unique<NoteLogEngine> (juce::StringArray { "arp.*" });
    auto& e = *engine;
    ShellProcessor p (std::move (engine));
    CHECK (p.grainSnapshot (0).count.load() == 0);
    e.grains.position[0] = 0.25f;
    e.grains.count = 1;
    CHECK (&p.grainSnapshot (0) == &e.grains);
    CHECK (p.grainSnapshot (0).position[0].load() == Approx (0.25f));
    CHECK (p.grainSnapshot (1).count.load() == 0);   // (the engine has none there)
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
        // (Documents is the real one: a file an earlier run left would already be listed)
        preset.withFileExtension (andor::shell::productInfo().presetExtension).deleteFile();
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
    preset.deleteFile();   // (the musician's folder as it was)
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
    // (a hybrid generator: its card and the recording's; three engines: every card)
    CHECK (ui->visibleCardCount() == (p.product().threeEngines.enabled && p.hasHybridLayer() ? 3 : (p.hasHybridLayer() ? 2 : 1)));

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

TEST_CASE ("shell: the AUX strip is a product's opt-in view; without it nothing changes", "[shell][editor][aux]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    ui->refreshNow();
    const float height = ui->instrumentHeight();
    const int modY = ui->modulationPanel().getY();
    juce::MemoryBlock session;
    p.getStateInformation (session);
    const auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), static_cast<int> (session.getSize()));
    REQUIRE (xml != nullptr);
    const auto* view = xml->getChildByName ("View");
    REQUIRE (view != nullptr);
    if (! p.hasAuxStrip())
    {
        // (ANDOR OSP and every product without one: no strip, no view setting, no room)
        CHECK_FALSE (view->hasAttribute ("auxShown"));
        ui->setAuxStripShown (true);
        CHECK (ui->auxStripComponent() == nullptr);
        CHECK (ui->auxShift() == 0.0f);
        CHECK (ui->sourceTrim() == ui->cardTrim());
        CHECK (ui->instrumentHeight() == Approx (height));
        return;
    }
    // A product with one: hidden by default; shown, everything under the cards moves down by its
    // room and the window grows by as much; hidden, every place returns.
    CHECK (view->hasAttribute ("auxShown"));
    CHECK_FALSE (p.auxStripShown());
    const auto card = ui->engineCard (0).getBounds();
    ui->setAuxStripShown (true);
    REQUIRE (ui->auxStripComponent() != nullptr);
    CHECK (ui->auxShift() > 0.0f);
    CHECK (ui->engineCard (0).getBounds() == card);
    CHECK (ui->modulationPanel().getY() == modY + juce::roundToInt (ui->auxShift()));
    CHECK (ui->instrumentHeight() == Approx (height + ui->auxShift()));
    ui->setAuxStripShown (false);
    CHECK (ui->modulationPanel().getY() == modY);
    CHECK (ui->instrumentHeight() == Approx (height));
}

TEST_CASE ("shell: the compact bottom edge - the housing ends 22 px under the keys, no footer line; a failed load is a brief notice", "[shell][editor][bottom]")
{
    namespace design = osp::plugin::design;
    namespace layout = osp::plugin::design::layout;
    // The canvas: the keys' foot + 22 px of housing + the 14 px surround.
    CHECK (design::height == Approx (1050.0f));
    CHECK (layout::keyboard.getBottom() == Approx (1014.0f));
    CHECK (layout::housing.getBottom() == Approx (layout::keyboard.getBottom() + 22.0f));
    CHECK (design::height - layout::housing.getBottom() == Approx (14.0f));

    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    ui->setArpExpanded (false);
    ui->setAuxStripShown (false);
    ui->refreshNow();
    // In the product's own content (its cards may be trimmed: everything below moves up as one).
    const float h = ui->instrumentHeight();
    const float foot = h - (design::height - layout::housing.getBottom());   // the housing's foot
    const auto keys = ui->keyboardComponent().getBoundsInParent().toFloat();
    CHECK (h - keys.getBottom() == Approx (design::height - layout::keyboard.getBottom()));
    CHECK (keys.getHeight() == Approx (layout::keyboard.getHeight()));   // (the keys never stretched)
    // PITCH and MOD: their captions (and MOD's socket) whole, at least 15 px above the foot.
    for (auto* wheel : { &ui->pitchBendWheel(), &ui->modulationWheel() })
    {
        // (the caption's box: under the 88 px wheel, 18 px tall, MOD's socket centred in it)
        const float captionBottom = static_cast<float> (wheel->getY()) + osp::plugin::Wheel::wheelHeight + 18.0f;
        INFO ("caption foot " << captionBottom << " housing foot " << foot);
        CHECK (captionBottom <= foot - 15.0f);
        CHECK (static_cast<float> (wheel->getBottom()) <= foot);
    }
    // The keys beneath ARP clear of the foot.
    CHECK (static_cast<float> (ui->keyboardActions().getBottom() - osp::plugin::KeyboardUtilityActions::margin) <= foot - 20.0f);
    // No footer line: nothing that shows text lies below the keys.
    for (auto* child : ui->contentForTest().getChildren())
        if (child->isVisible() && dynamic_cast<juce::Label*> (child) != nullptr)
        {
            INFO (child->getName() << " " << child->getBounds().toString());
            CHECK (static_cast<float> (child->getY()) < keys.getBottom());
        }

    // A file that cannot load: said once, in the overlay's brief notice (no footer to say it in).
    TempDir tmp;
    const auto bogus = tmp.dir.getChildFile ("not-audio.wav");
    bogus.replaceWithText ("this is not audio");
    CHECK (ui->modulationOverlay().currentNotice().isEmpty());
    p.loadFile (bogus, 0);
    p.waitForLoads (10000);
    p.pollLoads();
    ui->refreshNow();
    CHECK (ui->modulationOverlay().currentNotice().isNotEmpty());
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
    // OSP's macro names and materials: a product's own are its identity, like the header's;
    // the shipped render below shows them).
    {
        const osp::plugin::design::ScopedMaterials ospMaterials (osp::plugin::design::Materials::osp());
        visualstates::render<ShellProcessor, ShellEditor> (output.getChildFile ("parity"), tmp.dir.getChildFile ("parity"), [] {
            auto p = std::make_unique<ShellProcessor> (std::make_unique<ParityEngine>());
            p->showReferenceMacros();
            return p;
        });
    }
    // As shipped: the product's own engine (its unbound controls disabled), for the gallery.
    visualstates::render<ShellProcessor, ShellEditor> (output.getChildFile ("shipped"), tmp.dir.getChildFile ("shipped"),
                                                       [] { return std::make_unique<ShellProcessor>(); });

    // Tolerance: none. The shell draws with the same code and the same rendered assets, so
    // every pixel outside the masks must be exactly ANDOR OSP's.
    constexpr int tolerance = 0;
    juce::StringArray report { "# Visual regression: " + andor::shell::productInfo().name + " vs ANDOR OSP", "",
                               "Tolerance: " + juce::String (tolerance) + " (exact), masks: identity, preset-name, fx-routing, keyboard-actions (and, with the ARP panel open, arp-cards and arp-satellite). Rendered with ANDOR OSP's materials (a product's own finish is its identity: see shipped/).", "",
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
        // (From 03 on, three cards: shorter by EngineCard::footTrim, their modifiers in the
        // displays, ANDOR OSP's and so the parity render's.)
        const bool threeCards = ! name.startsWith ("00-") && ! name.startsWith ("01-") && ! name.startsWith ("02-");
        const float trim = threeCards ? osp::plugin::EngineCard::footTrim : 0.0f;
        const auto masks = name == "09-arp-open" ? visualstates::masks (osp::plugin::design::layout::arpShift, true, trim) : visualstates::masks (0.0f, false, trim);
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

TEST_CASE ("shell: opening the arpeggiator (a taller instrument) never changes the window's width; past the screen it stops growing and the instrument scrolls", "[shell][editor][window]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    // Off screen (headless, or not shown yet): the height that goes with the width, the width kept.
    for (const int width : { 1448, 1200, 900 })
    {
        INFO (width);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width));
        ui->setArpExpanded (true);
        CHECK (editor->getWidth() == width);
        CHECK (editor->getHeight() == juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width));
        ui->setArpExpanded (false);
        CHECK (editor->getWidth() == width);
        CHECK (editor->getHeight() == juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width));
    }
    // On a screen, the rule: the height that goes with the width, but never past the screen's
    // room (the instrument then scrolls, at the same scale); the width is never asked.
    using osp::plugin::frame::fittedWindowHeight;
    const float tall = ui->instrumentHeight() + 400.0f;   // (say, a rack and the arpeggiator open)
    const int natural = juce::roundToInt (1448.0f * tall / osp::plugin::design::width);
    CHECK (fittedWindowHeight (1448, 900, tall) == 900);        // (capped at the screen)
    CHECK (fittedWindowHeight (1448, 4000, tall) == natural);   // (a tall screen: the natural height)
    CHECK (fittedWindowHeight (1448, 0, tall) == natural);      // (no screen to ask: the natural height)
}

TEST_CASE ("shell: an external audio input is a product's opt-in; without one the plugin is output-only, as ever", "[shell][buses]")
{
    ShellProcessor p;
    if (andor::shell::productInfo().externalInputName.isEmpty())
    {
        // (ANDOR OSP's products: no input bus, no VST3 extension, no input layout taken)
        CHECK (p.getBusCount (true) == 0);
        CHECK (p.getVST3ClientExtensions() == nullptr);
        CHECK_FALSE (p.hasExternalInput());
        juce::AudioProcessor::BusesLayout withInput;
        withInput.outputBuses.add (juce::AudioChannelSet::stereo());
        withInput.inputBuses.add (juce::AudioChannelSet::stereo());
        CHECK_FALSE (p.checkBusesLayoutSupported (withInput));
        return;
    }
    // A product with one (ANDOR ANALOG): one input, off by default; an auxiliary VST3 bus.
    REQUIRE (p.getBusCount (true) == 1);
    CHECK_FALSE (p.getBus (true, 0)->isEnabledByDefault());
    REQUIRE (p.getVST3ClientExtensions() != nullptr);
    CHECK_FALSE (p.getVST3ClientExtensions()->getPluginHasMainInput());
}

namespace
{
    /** Where the instrument's main parts sit in the editor (pixels: the window's own). */
    struct Geometry
    {
        int editorWidth = 0, editorHeight = 0;
        std::array<juce::Rectangle<int>, 3> cards {};
        juce::Rectangle<int> macro, keyboard;
    };

    Geometry geometryOf (ShellEditor& ui)
    {
        Geometry g;
        g.editorWidth = ui.getWidth();
        g.editorHeight = ui.getHeight();
        auto onEditor = [&ui] (juce::Component& c) { return ui.getLocalArea (&c, c.getLocalBounds()); };
        for (int i = 0; i < 3; ++i)
            g.cards[static_cast<std::size_t> (i)] = onEditor (ui.engineCard (i));
        if (auto* knob = ui.macroKnob (0))
            g.macro = onEditor (*knob);
        g.keyboard = onEditor (ui.keyboardComponent());
        return g;
    }

    bool near (const juce::Rectangle<int>& a, const juce::Rectangle<int>& b, int tolerance = 1)
    {
        return std::abs (a.getX() - b.getX()) <= tolerance && std::abs (a.getY() - b.getY()) <= tolerance
               && std::abs (a.getWidth() - b.getWidth()) <= tolerance && std::abs (a.getHeight() - b.getHeight()) <= tolerance;
    }

    /** A host's window around the editor, as a plugin wrapper holds it: each resize the editor
        asks for is answered by `policy` - granted, answered first with the old size and granted
        when asked again, or always refused (the window kept at its old height). */
    struct TestHost final : juce::Component
    {
        enum class Policy { grant, oldSizeFirst, refuse };

        explicit TestHost (juce::Component& e) : editor (e)
        {
            addAndMakeVisible (editor);
            granted = editor.getHeight();
        }
        ~TestHost() override { removeChildComponent (&editor); }

        void childBoundsChanged (juce::Component*) override
        {
            if (answering)
                return;
            const int asked = editor.getHeight();
            if (asked == granted)
                return;
            ++requests;
            bool give = policy == Policy::grant;
            if (policy == Policy::oldSizeFirst)
            {
                give = asked == answeredOnce;   // (the second request for the same height)
                answeredOnce = asked;
            }
            if (give)
            {
                granted = asked;
                return;
            }
            const juce::ScopedValueSetter<bool> inside (answering, true);
            editor.setSize (editor.getWidth(), granted);   // (the old size reported back)
        }

        juce::Component& editor;
        Policy policy = Policy::grant;
        int granted = 0, requests = 0, answeredOnce = -1;
        bool answering = false;
    };
}

TEST_CASE ("shell: vertical-only expansion - racks, the arpeggiator and a strip change only the height; nothing above moves, nothing shrinks", "[shell][editor][window][geometry]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    using osp::plugin::rack::Kind;
    const int width = 1200;
    auto fitted = [&] { return juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width); };
    editor->setSize (width, fitted());
    const auto base = geometryOf (*ui);
    const float scale = static_cast<float> (width) / osp::plugin::design::width;

    // Each inline section: the width, the cards and the macros exactly where they were; the
    // keyboard as wide, lower by the section's height only; closed, everything back.
    // (`macroShift`: a section inserted above the macro row - a strip under the cards - moves
    // the macros down by its height alone; every other section sits under them.)
    auto checkOpen = [&] (const juce::String& what, float shiftReference, float macroShift = 0.0f) {
        INFO (what);
        const auto now = geometryOf (*ui);
        CHECK (now.editorWidth == width);
        CHECK (now.editorHeight == fitted());
        for (std::size_t i = 0; i < 3; ++i)
            CHECK (near (now.cards[i], base.cards[i]));
        CHECK (near (now.macro, base.macro.translated (0, juce::roundToInt (macroShift * scale))));
        CHECK (std::abs (now.keyboard.getX() - base.keyboard.getX()) <= 1);
        CHECK (std::abs (now.keyboard.getWidth() - base.keyboard.getWidth()) <= 1);
        CHECK (std::abs (static_cast<float> (now.keyboard.getY() - base.keyboard.getY()) - shiftReference * scale) <= 1.5f);
        CHECK_FALSE (ui->instrumentOverflow().overflowing());
    };
    auto checkBase = [&] (const juce::String& what) {
        INFO (what);
        const auto now = geometryOf (*ui);
        CHECK (now.editorWidth == base.editorWidth);
        CHECK (now.editorHeight == base.editorHeight);
        for (std::size_t i = 0; i < 3; ++i)
            CHECK (near (now.cards[i], base.cards[i]));
        CHECK (near (now.macro, base.macro));
        CHECK (near (now.keyboard, base.keyboard));
    };

    const float baseHeight = ui->instrumentHeight();
    for (const auto kind : { Kind::movement, Kind::echo, Kind::space, Kind::filter, Kind::morph })
    {
        ui->setRackExpanded (kind, true);
        checkOpen ("rack " + osp::plugin::rack::name (kind), ui->instrumentHeight() - baseHeight);
        ui->setRackExpanded (kind, false);
        checkBase ("closed " + osp::plugin::rack::name (kind));
    }
    // One rack for another: only the rack's own height difference (none for equal heights).
    ui->setRackExpanded (Kind::echo, true);
    const float echoHeight = ui->instrumentHeight();
    const int echoWindow = editor->getHeight();
    ui->setRackExpanded (Kind::space, true);
    if (std::abs (ui->instrumentHeight() - echoHeight) < 0.5f)
        CHECK (editor->getHeight() == echoWindow);
    checkOpen ("echo -> space", ui->instrumentHeight() - baseHeight);
    ui->setRackExpanded (Kind::space, false);
    ui->setRackExpanded (Kind::echo, false);
    checkBase ("after switching");

    // The arpeggiator, with and without a rack; a strip (a product's), with both.
    ui->setArpExpanded (true);
    checkOpen ("arpeggiator", ui->instrumentHeight() - baseHeight);
    ui->setRackExpanded (Kind::space, true);
    checkOpen ("arpeggiator + space", ui->instrumentHeight() - baseHeight);
    if (p.hasAuxStrip())
    {
        const float without = ui->instrumentHeight();
        ui->setAuxStripShown (true);
        checkOpen ("arpeggiator + space + strip", ui->instrumentHeight() - baseHeight, ui->instrumentHeight() - without);
        ui->setAuxStripShown (false);
    }
    ui->setRackExpanded (Kind::space, false);
    checkOpen ("arpeggiator again", ui->instrumentHeight() - baseHeight);
    ui->setArpExpanded (false);
    checkBase ("all closed");

    // A hundred quick cycles: no drift in width, scale, height or places.
    for (int i = 0; i < 100; ++i)
    {
        ui->setRackExpanded (i % 2 == 0 ? Kind::space : Kind::echo, true);
        ui->setArpExpanded (i % 3 == 0);
        ui->setRackExpanded (i % 2 == 0 ? Kind::space : Kind::echo, false);
    }
    ui->setArpExpanded (false);
    checkBase ("after 100 cycles");
}

TEST_CASE ("shell: a window shorter than the instrument (a host that refused, the screen's room) scrolls; the instrument is never scaled down or moved sideways", "[shell][editor][window][geometry]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    const int width = 1200;
    editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width));
    const auto base = geometryOf (*ui);
    TestHost host (*editor);
    host.policy = TestHost::Policy::refuse;   // (the host keeps the old height)
    ui->setArpExpanded (true);
    ui->setRackExpanded (osp::plugin::rack::Kind::space, true);
    // Negotiated first (asked again), and only then taken as a refusal: the bar appears.
    CHECK (editor->getHeight() == base.editorHeight);
    CHECK_FALSE (ui->instrumentOverflow().overflowing());
    ui->instrumentOverflow().settleNow();
    ui->instrumentOverflow().settleNow();
    auto now = geometryOf (*ui);
    for (std::size_t i = 0; i < 3; ++i)
        CHECK (near (now.cards[i], base.cards[i]));   // same scale, same places, at the top
    CHECK (std::abs (now.keyboard.getWidth() - base.keyboard.getWidth()) <= 1);   // (same scale; a pixel of rounding)
    CHECK (now.keyboard.getX() == base.keyboard.getX());
    CHECK (ui->instrumentOverflow().overflowing());
    CHECK (now.keyboard.getBottom() > editor->getHeight());   // (below: reached by scrolling)
    // Scrolled to the bottom: the keyboard comes into view, still as wide, not moved sideways.
    juce::MouseWheelDetails down;
    down.deltaX = 0.0f;
    down.deltaY = -1.0f;
    down.isReversed = false;
    down.isSmooth = false;
    down.isInertial = false;
    for (int i = 0; i < 20; ++i)
        ui->instrumentOverflow().scrollBy (down);
    now = geometryOf (*ui);
    CHECK (now.keyboard.getBottom() <= editor->getHeight() + 1);
    CHECK (now.keyboard.getX() == base.keyboard.getX());
    CHECK (std::abs (now.keyboard.getWidth() - base.keyboard.getWidth()) <= 1);   // (same scale; a pixel of rounding)
    CHECK (std::abs (now.cards[0].getWidth() - base.cards[0].getWidth()) <= 1);   // (same scale; a pixel of rounding)
    // The host gives the height after all: no scroll bar, back at the top.
    host.policy = TestHost::Policy::grant;
    editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width));
    CHECK_FALSE (ui->instrumentOverflow().overflowing());
    CHECK_FALSE (ui->instrumentOverflow().settling());
    CHECK (near (geometryOf (*ui).cards[0], base.cards[0]));

    // The window's shape: whatever is asked - a new width or a height alone - is answered with
    // the height that goes with the width (the whole instrument); no aspect ratio is imposed.
    auto* shape = editor->getConstrainer();
    REQUIRE (shape != nullptr);
    CHECK (shape->getFixedAspectRatio() == 0.0);
    const auto before = editor->getBounds();
    auto zoomed = before.withWidth (1000);
    shape->checkBounds (zoomed, before, {}, false, false, true, true);
    CHECK (zoomed.getWidth() == 1000);
    CHECK (zoomed.getHeight() == juce::roundToInt (1000.0f * ui->instrumentHeight() / osp::plugin::design::width));
    auto shorter = before.withHeight (before.getHeight() - 200);
    shape->checkBounds (shorter, before, {}, false, false, true, false);
    CHECK (shorter.getWidth() == before.getWidth());
    CHECK (shorter.getHeight() == before.getHeight());   // (never capped short: the full height)
    auto taller = before.withHeight (before.getHeight() + 300);
    shape->checkBounds (taller, before, {}, false, false, true, false);
    CHECK (taller.getHeight() == before.getHeight());   // (no empty space under the instrument)
    ui->setRackExpanded (osp::plugin::rack::Kind::space, false);
    ui->setArpExpanded (false);
}

TEST_CASE ("shell: SPACE + ARP + a strip opened together grow the window itself; only a host's negotiated refusal or the screen makes it scroll", "[shell][editor][window][geometry][host]")
{
    ShellProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<ShellEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    const int width = 1200;
    auto fitted = [&] { return juce::roundToInt (static_cast<float> (width) * ui->instrumentHeight() / osp::plugin::design::width); };
    editor->setSize (width, fitted());
    const auto base = geometryOf (*ui);
    TestHost host (*editor);
    auto& overflow = ui->instrumentOverflow();
    auto openAll = [&] {
        ui->setRackExpanded (osp::plugin::rack::Kind::space, true);
        ui->setArpExpanded (true);
        if (p.hasAuxStrip())
            ui->setAuxStripShown (true);
    };
    auto closeAll = [&] {
        if (p.hasAuxStrip())
            ui->setAuxStripShown (false);
        ui->setArpExpanded (false);
        ui->setRackExpanded (osp::plugin::rack::Kind::space, false);
    };
    auto sameWidthAndScale = [&] (const char* what) {
        INFO (what);
        const auto now = geometryOf (*ui);
        CHECK (now.editorWidth == width);
        for (std::size_t i = 0; i < 3; ++i)
            CHECK (near (now.cards[i], base.cards[i]));
        CHECK (std::abs (now.keyboard.getWidth() - base.keyboard.getWidth()) <= 1);   // (same scale; a pixel of rounding)
    };

    SECTION ("a host that grants: the window grows to the whole instrument, never a scroll bar")
    {
        openAll();
        CHECK (editor->getHeight() == fitted());
        CHECK (editor->getHeight() > base.editorHeight);
        CHECK (host.granted == fitted());
        CHECK_FALSE (overflow.overflowing());
        CHECK_FALSE (overflow.settling());
        sameWidthAndScale ("granted");
        closeAll();
        CHECK (editor->getHeight() == base.editorHeight);   // (closing shrinks the window again)
        CHECK_FALSE (overflow.overflowing());
    }
    SECTION ("a host that first reports the old size: no scroll bar while it settles; asked again, it grows")
    {
        host.policy = TestHost::Policy::oldSizeFirst;
        openAll();
        CHECK (editor->getHeight() < fitted());   // (the old size, for now)
        CHECK (overflow.settling());
        CHECK_FALSE (overflow.overflowing());     // (not taken as a refusal)
        overflow.settleNow();                     // (the negotiation goes on: asked again)
        CHECK (editor->getHeight() == fitted());
        CHECK_FALSE (overflow.overflowing());
        CHECK_FALSE (overflow.settling());
        sameWidthAndScale ("after negotiating");
        closeAll();   // (a smaller window, answered first with the old, taller size ...)
        CHECK (overflow.settling());
        CHECK_FALSE (overflow.overflowing());
        overflow.settleNow();   // (... asked again: it shrinks)
        CHECK (editor->getHeight() == base.editorHeight);
        CHECK_FALSE (overflow.settling());
    }
    SECTION ("a host that refuses: asked twice, then scrolled - the width and scale kept")
    {
        host.policy = TestHost::Policy::refuse;
        openAll();
        CHECK (overflow.settling());
        CHECK_FALSE (overflow.overflowing());
        overflow.settleNow();
        CHECK (host.requests >= 2);               // (asked again before giving up)
        CHECK_FALSE (overflow.overflowing());
        overflow.settleNow();
        CHECK (overflow.overflowing());
        CHECK (editor->getHeight() == base.editorHeight);
        sameWidthAndScale ("refused");
        host.policy = TestHost::Policy::grant;
        closeAll();
        CHECK_FALSE (overflow.overflowing());
    }
    SECTION ("a screen too short for all of it: the window stops at the screen, the instrument scrolls at once")
    {
        struct Screen
        {
            explicit Screen (int room) { osp::plugin::frame::overrideScreenRoom (room); }
            ~Screen() { osp::plugin::frame::overrideScreenRoom (0); }
        } screen (base.editorHeight + 100);
        openAll();
        CHECK (editor->getHeight() == base.editorHeight + 100);
        CHECK (overflow.overflowing());
        CHECK_FALSE (overflow.settling());
        sameWidthAndScale ("screen");
        closeAll();
        CHECK (editor->getHeight() == base.editorHeight);
        CHECK_FALSE (overflow.overflowing());
    }
}

TEST_CASE ("shell: PITCH BEND, MOD WHEEL and ADVANCED as every product shows them (ANDOR_SHELL_POPOVER_SHOTS)", "[.][shell-popover-shots]")
{
    // The same shared popovers ANDOR ANALOG and every shell product open (ShellEditor's own
    // anchors), at 1086 and 1448 px.
    const auto dirName = juce::SystemStats::getEnvironmentVariable ("ANDOR_SHELL_POPOVER_SHOTS", {});
    REQUIRE (dirName.isNotEmpty());
    const juce::File dir (dirName);
    dir.createDirectory();
    for (const int width : { 1086, 1448 })
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<ShellEditor&> (*editor);
        editor->setSize (width, juce::roundToInt (static_cast<float> (width) * ui.instrumentHeight() / osp::plugin::design::width));
        ui.refreshNow();
        for (const auto& [which, name] : { std::pair<int, const char*> { ShellEditor::pitchWheelPopup, "pitch" },
                                           { ShellEditor::modWheelPopup, "mod" }, { ShellEditor::advancedPopup, "advanced" } })
        {
            ui.openPopup (which);
            ui.refreshNow();
            REQUIRE (ui.openPopupPanel() != nullptr);
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream out (dir.getChildFile ("shell-" + juce::String (width) + "-" + name + ".png"));
            REQUIRE (out.openedOk());
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
            ui.closePopup();
        }
    }
}
