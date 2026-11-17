// NOTE ATLAS refinements in ANDOR OSP (docs/research/note-atlas-refinements.md): recorded round
// robins with LOOP off, per-note FADE IN / FADE OUT, live NOTES highlighting and the footer's
// LIBRARY key. Included after UnifiedPhase5Tests.inc (its helpers: settleAnalysis, settleEdits).

namespace
{
    /** A bass phrase of eight plucked C2 takes (each its own pluck and level), 0.4 s each with
        0.1 s between: what a musician drops. */
    juce::File writeRepeatedBass (const juce::File& dir)
    {
        const double rate = 48000.0;
        const auto note = static_cast<std::int64_t> (0.4 * rate), gap = static_cast<std::int64_t> (0.1 * rate);
        auto audio = AudioData::allocate (2, 8 * (note + gap) + gap, rate);
        for (int t = 0; t < 8; ++t)
        {
            const auto take = testsignals::pluck (midiToHz (36), 0.4, rate, static_cast<std::uint64_t> (0xba55 + t), 0.4 + 0.03 * t, 2);
            const auto start = gap + t * (note + gap);
            for (int c = 0; c < 2; ++c)
                std::copy (take.channels[static_cast<std::size_t> (c)].begin(), take.channels[static_cast<std::size_t> (c)].begin() + note,
                           audio.channels[static_cast<std::size_t> (c)].begin() + start);
        }
        const auto file = dir.getChildFile ("repeated-bass.wav");
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::float32, error));
        return file;
    }

    void setLayerParameter (OspAudioProcessor& p, int layer, const char* field, float value)
    {
        auto* param = p.parameters.getParameter (osp::plugin::InstrumentHost::layerParameterId (layer, field));
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** The member of the layer's set the newest held voice plays (-1: none). */
    int heldMember (const OspAudioProcessor& p, int layer, int key)
    {
        const auto instrument = p.currentInstrument (layer);
        if (instrument == nullptr || instrument->set == nullptr)
            return -1;
        const auto& set = *instrument->set;
        const auto* engine = p.telemetryEngine();
        for (int i = 0; i < InstrumentEngine::voiceSlots(); ++i)
        {
            const auto& v = engine->voiceAt (i);
            if (! v.isActive() || v.isFading() || v.isReleased() || v.note() != key)
                continue;
            for (std::size_t m = 0; m < set.members.size(); ++m)
                if (set.members[m].model.get() == v.model())
                    return static_cast<int> (m);
        }
        return -1;
    }

    /** `count` presses of `key` through processBlock: the member each one played. */
    std::vector<int> pressKey (OspAudioProcessor& p, int key, int count)
    {
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        std::vector<int> out;
        for (int i = 0; i < count; ++i)
        {
            juce::MidiBuffer on;
            on.addEvent (juce::MidiMessage::noteOn (1, key, static_cast<juce::uint8> (100)), 0);
            p.processBlock (buffer, on);
            juce::MidiBuffer none;
            for (int b = 0; b < 4; ++b)
                p.processBlock (buffer, none);
            out.push_back (heldMember (p, 0, key));
            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, key), 0);
            p.processBlock (buffer, off);
            for (int b = 0; b < 8; ++b)
                p.processBlock (buffer, none);
        }
        return out;
    }
} // namespace

TEST_CASE ("note atlas: a dropped bass phrase of eight C2 takes round-robins through every take with LOOP off, as with LOOP on", "[plugin][atlas][round-robin]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    p.analyseRecording (writeRepeatedBass (tmp.dir), 0);
    settleAnalysis (p);
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    REQUIRE (instrument->set != nullptr);
    std::set<int> c2;
    for (std::size_t m = 0; m < instrument->set->members.size(); ++m)
        if (std::lround (instrument->set->members[m].model->rootMidi) == 36)
            c2.insert (static_cast<int> (m));
    INFO (c2.size() << " C2 takes of " << instrument->set->members.size() << " members");
    REQUIRE (c2.size() == 8);
    for (const bool loop : { false, true })
    {
        INFO ("LOOP " << (loop ? "on" : "off"));
        setLayerParameter (p, 0, "loop", loop ? 1.0f : 0.0f);
        const auto played = pressKey (p, 36, 24);
        std::set<int> heard;
        for (const int m : played)
        {
            CHECK (c2.count (m) == 1);
            heard.insert (m);
        }
        for (std::size_t i = 1; i < played.size(); ++i)
            CHECK (played[i] != played[i - 1]);
        if (! loop)
            CHECK (heard == c2);   // every take, loopable or not
        else
            CHECK (heard.size() >= 2);
    }
}
