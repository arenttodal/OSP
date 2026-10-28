// The unified engine's Phase 2 in ANDOR OSP (docs/research/unified-andor/phase-2-report.md):
// the new host parameters (appended; nothing before them moves), KEY DNA in the session
// (versioned, exact on reopening), profiles handed to the audio thread through a ModelExchange,
// KEY DNA AMOUNT live under automation, and older sessions opening with the original grains.
// Included at the end of PluginProcessorTests.cpp.

#include "engine/KeyDna.h"
#include "support/AllocationCounter.h"

namespace
{
    mod::CurveTable keyDnaLine (float fromY, float toY)
    {
        mod::Curve c;
        c.count = 2;
        c.points[0] = { 0.0f, fromY, 0.0f };
        c.points[1] = { 1.0f, toY, 0.0f };
        return keydna::curveFrom (c);
    }
    /** The Phase 1 demonstrations' C (expressive) and B (organic) profiles (research/UnifiedScenes.cpp). */
    keydna::Settings expressiveDna()
    {
        keydna::Settings s;
        s.enabled = true;
        s.seed = 23;
        s.individuality = 0.6;
        s[keydna::Param::position].variation = 0.7;
        s[keydna::Param::size].curve = keyDnaLine (1.0f, 0.0f);
        s[keydna::Param::size].curveDepth = 0.55;
        s[keydna::Param::size].variation = 0.35;
        s[keydna::Param::density].curve = keyDnaLine (0.0f, 1.0f);
        s[keydna::Param::density].curveDepth = 0.5;
        s[keydna::Param::density].variation = 0.3;
        s[keydna::Param::spread].variation = 0.3;
        return s;
    }
    keydna::Settings organicDna()
    {
        keydna::Settings s;
        s.enabled = true;
        s.seed = 11;
        s.individuality = 0.15;
        s[keydna::Param::position].variation = 0.12;
        s[keydna::Param::size].curve = keyDnaLine (0.8f, 0.2f);
        s[keydna::Param::size].curveDepth = 0.2;
        s[keydna::Param::size].variation = 0.12;
        s[keydna::Param::density].variation = 0.1;
        s[keydna::Param::spread].variation = 0.1;
        return s;
    }

    std::array<juce::File, 3> phase2Sources (const juce::File& dir)
    {
        return { writeSource (dir, "a-vowel.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 3)),
                 writeSource (dir, "b-pluck.wav", testsignals::pluck (midiToHz (40), 2.5, 48000.0, 8, 0.7, 1)),
                 writeSource (dir, "c-vowel.wav", testsignals::vowel (midiToHz (64), 2.5, 48000.0, 7)) };
    }

    void loadLayers (OspAudioProcessor& p, const juce::File& dir, int layers)
    {
        const auto files = phase2Sources (dir);
        juce::Array<juce::File> sources;
        for (int i = 0; i < layers; ++i)
            sources.add (files[static_cast<std::size_t> (i)]);
        if (layers == 1)
            p.loadFile (sources[0]);
        else
            p.addLayers (sources);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
    }

    juce::String audioHash (const AudioData& audio)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const auto& channel : audio.channels)
            for (float x : channel)
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof bits);
                for (int b = 0; b < 4; ++b)
                    h = (h ^ ((bits >> (8 * b)) & 0xffu)) * 1099511628211ull;
            }
        return juce::String::toHexString (static_cast<juce::int64> (h));
    }

    /** Plays `notes` together for `seconds` (released at 75 %), calling `eachBlock (sample)` before every block. */
    AudioData renderHeld (OspAudioProcessor& p, std::vector<int> notes, double seconds, const std::function<void (int)>& eachBlock = {})
    {
        const double rate = 48000.0;
        const int block = 256, total = static_cast<int> (seconds * rate);
        p.prepareToPlay (rate, block);
        auto audio = AudioData::allocate (2, total, rate);
        juce::AudioBuffer<float> buffer (2, block);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (std::size_t k = 0; k < notes.size(); ++k)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], static_cast<juce::uint8> (100 - k)), 0);
            const int off = static_cast<int> (0.75 * seconds * rate);
            if (off >= pos && off < pos + block)
                midi.addEvent (juce::MidiMessage::allNotesOff (1), off - pos);
            if (eachBlock)
                eachBlock (pos);
            buffer.clear();
            p.processBlock (buffer, midi);
            const int n = std::min (block, total - pos);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, audio.channels[static_cast<std::size_t> (ch)].begin() + pos);
        }
        return audio;
    }

    /** The session as a build before Phase 2 saved it: state version 12, no KEY DNA child and
        none of the version-hint-20 parameters. */
    void asVersion12 (juce::MemoryBlock& state)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        xml->setAttribute ("stateVersion", 12);
        if (auto* dna = xml->getChildByName ("KeyDna"))
            xml->removeChildElement (dna, true);
        juce::StringArray newIds { "grainBudget" };
        for (int l = 0; l < 3; ++l)
            for (const char* n : { "granular.scatter", "granular.pan", "granular.window", "keyDna.amount" })
                newIds.add (osp::plugin::InstrumentHost::layerParameterId (l, n));
        for (auto* child = xml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            if (newIds.contains (child->getStringAttribute ("id")))
                xml->removeChildElement (child, true);
            child = next;
        }
        state.reset();
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
    }

    /** Three dense Granular layers: an eight-note chord asks for about 400 grains. */
    void heavyGranular (OspAudioProcessor& p)
    {
        for (int l = 0; l < 3; ++l)
        {
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "sourceMode"), 1.0f);
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.size"), 400.0f);
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.density"), 40.0f);
            p.setParameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.spread"), 50.0f);
        }
    }
    const std::vector<int> chord8 { 36, 43, 48, 52, 55, 60, 64, 71 };
}

TEST_CASE ("plugin: Phase 2's parameters are appended and neutral; the modulation destinations stay frozen", "[plugin][phase2]")
{
    OspAudioProcessor p;
    const auto& all = p.getParameters();
    // The last thirteen parameters, in order: per layer SCATTER, PAN, WINDOW, KEY DNA AMOUNT; the budget.
    juce::StringArray expected;
    for (int l = 0; l < 3; ++l)
        for (const char* n : { "granular.scatter", "granular.pan", "granular.window", "keyDna.amount" })
            expected.add (osp::plugin::InstrumentHost::layerParameterId (l, n));
    expected.add ("grainBudget");
    REQUIRE (all.size() > expected.size());
    for (int i = 0; i < expected.size(); ++i)
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (all[all.size() - expected.size() + i]);
        REQUIRE (param != nullptr);
        CHECK (param->getParameterID() == expected[i]);
    }
    for (int l = 0; l < 3; ++l)
    {
        CHECK (p.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.scatter")) == 0.0f);
        CHECK (p.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.pan")) == 0.0f);
        CHECK (p.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.window")) == 0.0f);
        CHECK (p.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "keyDna.amount")) == 100.0f);
        CHECK_FALSE (p.keyDnaSettings (l).enabled);
    }
    // A new session starts from the benchmarked budget.
    CHECK (p.parameterValue ("grainBudget") == static_cast<float> (osp::plugin::InstrumentHost::newSessionGrainBudgetIndex));
    CHECK (osp::plugin::InstrumentHost::grainBudgetGrains (p.parameterValue ("grainBudget")) == 256);
    // KEY DNA AMOUNT is an engine destination but not one the plugin's routes offer: the
    // route-destination choice keeps its 75 entries, so its automation range is unchanged.
    auto* dest = dynamic_cast<juce::AudioParameterChoice*> (p.parameters.getParameter (osp::plugin::InstrumentHost::modRouteId (0, "dest")));
    REQUIRE (dest != nullptr);
    CHECK (dest->choices.size() == 75);
    CHECK (osp::plugin::InstrumentHost::modDestinationNames().size() == 75);
}

TEST_CASE ("plugin: sessions saved before Phase 2 open with the original grains", "[plugin][phase2][migration]")
{
    TempDir tmp;
    juce::MemoryBlock modern, old;
    {
        OspAudioProcessor p;
        loadLayers (p, tmp.dir, 3);
        heavyGranular (p);
        p.getStateInformation (modern);   // a new session: the 256-grain budget
    }
    old = modern;
    asVersion12 (old);

    OspAudioProcessor reopened;
    reopened.setStateInformation (old.getData(), static_cast<int> (old.getSize()));
    REQUIRE (reopened.waitForLoads (30000));
    reopened.pollLoads();
    CHECK (reopened.parameterValue ("grainBudget") == 0.0f);   // Unlimited
    for (int l = 0; l < 3; ++l)
    {
        CHECK (reopened.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.scatter")) == 0.0f);
        CHECK (reopened.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.pan")) == 0.0f);
        CHECK (reopened.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "granular.window")) == 0.0f);
        CHECK (reopened.parameterValue (osp::plugin::InstrumentHost::layerParameterId (l, "keyDna.amount")) == 100.0f);
        CHECK_FALSE (reopened.keyDnaSettings (l).enabled);
    }
    const auto migrated = audioHash (renderHeld (reopened, chord8, 1.2));

    // The same session opened as saved (256 grains) is heard differently - the budget binds
    // here - and with the budget set to Unlimited it is the migrated one exactly.
    OspAudioProcessor budgeted;
    budgeted.setStateInformation (modern.getData(), static_cast<int> (modern.getSize()));
    REQUIRE (budgeted.waitForLoads (30000));
    budgeted.pollLoads();
    CHECK (budgeted.parameterValue ("grainBudget") == 3.0f);
    const auto withBudget = audioHash (renderHeld (budgeted, chord8, 1.2));
    CHECK (withBudget != migrated);
    budgeted.setParameterValue ("grainBudget", 0.0f);
    CHECK (audioHash (renderHeld (budgeted, chord8, 1.2)) == migrated);
}

TEST_CASE ("plugin: KEY DNA is saved in the session and reopens exactly", "[plugin][phase2][keydna]")
{
    TempDir tmp;
    auto expressive = expressiveDna();
    expressive[keydna::Param::pan].curve = keyDnaLine (0.5f, 1.0f);
    expressive[keydna::Param::pan].curveDepth = 0.6;
    expressive[keydna::Param::scatter].variation = 0.2;
    const auto organic = organicDna();
    juce::MemoryBlock state;
    juce::String original;
    {
        OspAudioProcessor p;
        loadLayers (p, tmp.dir, 3);
        p.setParameterValue ("layerA.sourceMode", 1.0f);
        p.setParameterValue ("layerC.sourceMode", 1.0f);
        p.setKeyDnaSettings (0, expressive);
        p.setKeyDnaSettings (2, organic);
        p.getStateInformation (state);
        original = audioHash (renderHeld (p, { 48, 55, 64, 72 }, 1.2));
    }
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getIntAttribute ("stateVersion") == OspAudioProcessor::stateVersion);
        const auto* dna = xml->getChildByName ("KeyDna");
        REQUIRE (dna != nullptr);
        CHECK (dna->getIntAttribute ("schemaVersion") == 1);
    }
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    CHECK (q.keyDnaSettings (0) == expressive);
    CHECK (q.keyDnaSettings (1) == keydna::Settings {});
    CHECK (q.keyDnaSettings (2) == organic);
    CHECK (audioHash (renderHeld (q, { 48, 55, 64, 72 }, 1.2)) == original);
    // ...and it is heard: without it the same session sounds different.
    q.setKeyDnaSettings (0, {});
    q.setKeyDnaSettings (2, {});
    CHECK (audioHash (renderHeld (q, { 48, 55, 64, 72 }, 1.2)) != original);

    // A KEY DNA schema this build does not know is not guessed at: no KEY DNA.
    auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
    xml->getChildByName ("KeyDna")->setAttribute ("schemaVersion", 99);
    juce::MemoryBlock future;
    juce::AudioProcessor::copyXmlToBinary (*xml, future);
    OspAudioProcessor r;
    r.setStateInformation (future.getData(), static_cast<int> (future.getSize()));
    CHECK_FALSE (r.keyDnaSettings (0).enabled);
}

TEST_CASE ("plugin: KEY DNA profiles reach the audio thread through the exchange, once per block", "[plugin][phase2][keydna]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    p.setParameterValue ("layerA.sourceMode", 1.0f);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    auto block = [&] { buffer.clear(); midi.clear(); p.processBlock (buffer, midi); };
    block();
    CHECK (p.keyDnaGenerationInUse (0) == 0);   // none yet: exactly the Granulator
    auto dna = expressiveDna();
    p.setKeyDnaSettings (0, dna);
    const auto first = p.keyDnaGenerationInUse (0);
    CHECK (first == 0);   // computed and published, not yet taken
    block();
    const auto taken = p.keyDnaGenerationInUse (0);
    CHECK (taken > 0);
    // Editing fast (a GUI drag): many profiles between two blocks; the block takes the newest,
    // and the older ones are freed on the message thread once it is in use.
    for (int i = 0; i < 20; ++i)
    {
        dna.seed = static_cast<std::uint64_t> (100 + i);
        p.setKeyDnaSettings (0, dna);
    }
    block();
    CHECK (p.keyDnaGenerationInUse (0) == taken + 20);
    CHECK (p.keyDnaProfilesAlive (0) > 1);
    p.pollLoads();   // the message thread's timer
    CHECK (p.keyDnaProfilesAlive (0) == 1);
}

TEST_CASE ("plugin: KEY DNA AMOUNT automation is live on held notes and deterministic", "[plugin][phase2][keydna]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    p.setParameterValue ("layerA.sourceMode", 1.0f);
    p.setKeyDnaSettings (0, expressiveDna());
    auto* amount = p.parameters.getParameter ("layerA.keyDna.amount");
    REQUIRE (amount != nullptr);
    const std::vector<int> chord { 55, 62, 67, 74 };
    auto automated = [&] {
        return renderHeld (p, chord, 2.0, [&] (int pos) {
            // host automation: 0 -> 100 % over the first second, block by block
            amount->setValueNotifyingHost (std::min (1.0f, static_cast<float> (pos) / 48000.0f));
        });
    };
    const auto a = automated(), b = automated();
    CHECK (audioHash (a) == audioHash (b));
    amount->setValueNotifyingHost (0.0f);
    const auto atZero = audioHash (renderHeld (p, chord, 2.0));
    amount->setValueNotifyingHost (1.0f);
    const auto atFull = audioHash (renderHeld (p, chord, 2.0));
    CHECK (audioHash (a) != atZero);
    CHECK (audioHash (a) != atFull);
    // AMOUNT 0 is exactly no KEY DNA.
    p.setKeyDnaSettings (0, {});
    CHECK (audioHash (renderHeld (p, chord, 2.0)) == atZero);
}

TEST_CASE ("plugin: SCATTER, PAN and WINDOW reach the grains; at neutral they are the original", "[plugin][phase2]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 1);
    p.setParameterValue ("layerA.sourceMode", 1.0f);
    const std::vector<int> chord { 57, 64, 69 };
    const auto plain = audioHash (renderHeld (p, chord, 1.0));
    p.setParameterValue ("layerA.granular.scatter", 0.0f);
    p.setParameterValue ("layerA.granular.pan", 0.0f);
    p.setParameterValue ("layerA.granular.window", 0.0f);
    CHECK (audioHash (renderHeld (p, chord, 1.0)) == plain);
    for (const auto& [id, value] : { std::pair<const char*, float> { "layerA.granular.scatter", 5.0f }, { "layerA.granular.pan", 60.0f },
                                     { "layerA.granular.window", 1.0f }, { "layerA.granular.window", 2.0f } })
    {
        p.setParameterValue (id, value);
        INFO (id << " = " << value);
        CHECK (audioHash (renderHeld (p, chord, 1.0)) != plain);
        p.setParameterValue (id, 0.0f);
    }
    CHECK (audioHash (renderHeld (p, chord, 1.0)) == plain);
}

TEST_CASE ("plugin: the grain budget binds only when reached", "[plugin][phase2]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 3);
    heavyGranular (p);
    p.setParameterValue ("grainBudget", 0.0f);
    const auto unlimited = audioHash (renderHeld (p, chord8, 1.0));
    p.setParameterValue ("grainBudget", 1.0f);   // 512: more than the ~400 this chord asks for
    CHECK (audioHash (renderHeld (p, chord8, 1.0)) == unlimited);
    p.setParameterValue ("grainBudget", 7.0f);   // 64
    CHECK (audioHash (renderHeld (p, chord8, 1.0)) != unlimited);
}

TEST_CASE ("plugin: the audio callback allocates nothing while KEY DNA profiles change and AMOUNT is automated", "[plugin][phase2][keydna]")
{
    TempDir tmp;
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 3);
    heavyGranular (p);
    p.setKeyDnaSettings (0, expressiveDna());
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    auto* amount = p.parameters.getParameter ("layerA.keyDna.amount");
    REQUIRE (amount != nullptr);
    // Warm up every path once (first notes, first profile), outside the count.
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
    for (int b = 0; b < 8; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        midi.clear();
    }
    int allocations = 0;
    for (int round = 0; round < 6; ++round)
    {
        auto dna = expressiveDna();
        dna.seed = static_cast<std::uint64_t> (40 + round);
        p.setKeyDnaSettings (round % 3, dna);   // message thread: computed and published here
        amount->setValueNotifyingHost (static_cast<float> (round) / 5.0f);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * round, static_cast<juce::uint8> (90)), 0);
        {
            test::AllocationScope scope;   // the audio thread's blocks
            for (int b = 0; b < 4; ++b)
            {
                buffer.clear();
                p.processBlock (buffer, midi);
                midi.clear();
            }
            allocations += scope.count();
        }
    }
    CHECK (allocations == 0);
    {
        test::AllocationScope scope;   // (the counter does count on this thread)
        auto keep = std::make_unique<std::vector<int>> (16);
        CHECK (scope.count() >= 1);
    }
}
