// Before/after render comparison (Library Stage 11.8): fixed scenes rendered through the whole
// processor and written as raw float32 (interleaved stereo) to $OSP_RENDER_DIR. The same file
// is compiled into the baseline build (commit 1537f7e, before the Library) and into this one;
// identical state and audio must give identical bytes. Hidden: [.][render-dump].
// It uses only processor calls that exist in both builds.

TEST_CASE ("plugin: render dump for the before/after Library comparison", "[.][render-dump]")
{
    const char* dir = std::getenv ("OSP_RENDER_DIR");
    REQUIRE (dir != nullptr);
    TempDir tmp;
    const auto pluck = writeSource (tmp.dir, "Render Pluck C4.wav", testsignals::pluck (261.6256, 1.0, 48000.0, 11));
    const auto vowel = writeSource (tmp.dir, "Render Vowel A3.wav", testsignals::vowel (220.0, 1.5, 48000.0, 12));
    struct Scene
    {
        const char* name;
        bool twoLayers;
        std::vector<std::pair<const char*, float>> parameters;
        std::vector<int> notes;
    };
    const std::vector<Scene> scenes {
        { "pluck-default", false, {}, { 60, 64, 67 } },
        { "pluck-macros", false, { { "character", 70.0f }, { "space", 40.0f }, { "life", 30.0f }, { "dynamics", 60.0f } }, { 48, 60, 72 } },
        { "two-layers", true, { { "space", 25.0f } }, { 48, 55, 60, 64 } },
    };
    for (const auto& scene : scenes)
    {
        OspAudioProcessor p;
        p.prepareToPlay (48000.0, 256);
        p.loadFile (pluck, 0);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        if (scene.twoLayers)
        {
            p.loadFile (vowel, 1);
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
        }
        for (const auto& [id, value] : scene.parameters)
            p.setParameterValue (id, value);
        const int block = 256;
        const int total = static_cast<int> (3.0 * 48000.0);
        juce::AudioBuffer<float> buffer (2, block);
        std::vector<float> out;
        out.reserve (static_cast<std::size_t> (total) * 2);
        for (int pos = 0; pos < total; pos += block)
        {
            juce::MidiBuffer midi;
            for (const int note : scene.notes)
            {
                if (pos == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, static_cast<juce::uint8> (100)), 0);
                if (pos == 72000)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            }
            if (pos == 48000)
                p.setParameterValue ("character", 30.0f);   // a change while it plays
            buffer.clear();
            p.processBlock (buffer, midi);
            for (int i = 0; i < block; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    out.push_back (buffer.getSample (ch, i));
        }
        juce::FileOutputStream f (juce::File (dir).getChildFile (juce::String (scene.name) + ".f32"));
        f.setPosition (0);
        f.truncate();
        REQUIRE (f.write (out.data(), out.size() * sizeof (float)));
    }
}
