// CPU of a real patch (hidden): a saved preset (OSP_CPU_PRESET, else a built-in copy of the
// three-layer "Keys - Dreamy Mangled Keys" settings is not stored: the preset is required),
// three stand-in sounds shaped like the preset's (a short piano-like pluck at D5, a long
// flute-like tone at A5, a harp-like pluck at C5), played as 5-note chords that overlap the
// way a player's do. Every block is timed: the mean, the 99th percentile and the worst, and the
// blocks with note-ons apart from the rest, since a stutter is one late block.
//   OSP_CPU_PRESET=<file.osppreset> [OSP_CPU_BLOCK=256] [OSP_CPU_SECONDS=20] ./osp_plugin_tests "[cpu-preset]"
// Included at the end of PluginProcessorTests.cpp.

#include <algorithm>
#include <chrono>

TEST_CASE ("plugin: CPU of a saved three-layer preset, block by block", "[.][cpu-preset]")
{
    const char* presetPath = std::getenv ("OSP_CPU_PRESET");
    if (presetPath == nullptr)
    {
        WARN ("OSP_CPU_PRESET not set");
        return;
    }
    const juce::File presetFile (presetPath);
    REQUIRE (presetFile.existsAsFile());
    auto preset = juce::XmlDocument::parse (presetFile);
    REQUIRE (preset != nullptr);

    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const double rate = 48000.0;
    const auto a = writeSource (tmp.dir, "piano D5.wav", testsignals::pluck (midiToHz (74), 0.75, rate, 3, 0.7, 2));
    const auto b = writeSource (tmp.dir, "flute A5.wav", testsignals::vowel (midiToHz (81), 10.0, rate, 4, 0.5, 2));
    const auto c = writeSource (tmp.dir, "harp C5.wav", testsignals::pluck (midiToHz (72), 3.0, rate, 5, 0.7, 2));

    const int block = std::getenv ("OSP_CPU_BLOCK") != nullptr ? std::atoi (std::getenv ("OSP_CPU_BLOCK")) : 256;
    const double seconds = std::getenv ("OSP_CPU_SECONDS") != nullptr ? std::atof (std::getenv ("OSP_CPU_SECONDS")) : 20.0;

    OspAudioProcessor p;
    REQUIRE (p.addLayers ({ a, b, c }) == 3);
    REQUIRE (p.waitForLoads (1800000));   // generous: valgrind runs it too
    p.pollLoads();
    // The preset with these sounds in its layers: its own Instrument trees replaced by the ones
    // just loaded (same tags), everything else as saved (its version, routing, curves).
    {
        juce::MemoryBlock data;
        p.getStateInformation (data);
        auto now = juce::AudioProcessor::getXmlFromBinary (data.getData(), static_cast<int> (data.getSize()));
        REQUIRE (now != nullptr);
        for (const char* tag : { "Instrument", "InstrumentB", "InstrumentC" })
        {
            if (auto* old = preset->getChildByName (tag))
                preset->removeChildElement (old, true);
            if (auto* loaded = now->getChildByName (tag))
                preset->addChildElement (new juce::XmlElement (*loaded));
        }
        juce::MemoryBlock next;
        juce::AudioProcessor::copyXmlToBinary (*preset, next);
        p.setStateInformation (next.getData(), static_cast<int> (next.getSize()));
        REQUIRE (p.waitForLoads (1800000));   // generous: valgrind runs it too
        p.pollLoads();
    }
    // OSP_CPU_SET="id=value,id=value": settings changed after the preset (to see what costs what).
    juce::String label = "as saved";
    if (const char* set = std::getenv ("OSP_CPU_SET"))
    {
        label = set;
        for (const auto& pair : juce::StringArray::fromTokens (set, ",", ""))
            p.setParameterValue (pair.upToFirstOccurrenceOf ("=", false, false).trim(), pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
    }
    p.prepareToPlay (rate, block);

    // Chords (5 notes, the low-middle register) every 1.5 s, held 1.2 s, the next one moving.
    const std::vector<std::vector<int>> chords { { 48, 55, 60, 64, 71 }, { 45, 52, 57, 60, 67 }, { 50, 57, 62, 65, 72 }, { 43, 50, 55, 59, 65 } };
    const int total = static_cast<int> (seconds * rate / block);
    const int period = static_cast<int> (1.5 * rate), hold = static_cast<int> (1.2 * rate);
    juce::AudioBuffer<float> buffer (2, block);
    std::vector<double> plain, withOn;
    int maxVoices = 0;
    double sum = 0.0;
    for (int i = 0; i < total; ++i)
    {
        juce::MidiBuffer midi;
        const int start = i * block, end = start + block;
        if (i == 0 && std::getenv ("OSP_CPU_PEDAL") != nullptr)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);   // the pedal down throughout: notes pile up
        for (int t = (start / period) * period; t < end; t += period)
        {
            const auto& chord = chords[static_cast<std::size_t> ((t / period) % static_cast<int> (chords.size()))];
            if (t >= start)
                for (int n : chord)
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (96)), t - start);
            if (t + hold >= start && t + hold < end)
                for (int n : chord)
                    midi.addEvent (juce::MidiMessage::noteOff (1, n), t + hold - start);
        }
        const bool hasOn = std::any_of (midi.begin(), midi.end(), [] (const auto& m) { return m.getMessage().isNoteOn(); });
        buffer.clear();
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock (buffer, midi);
        const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
        if (i < 20)
            continue;   // warm-up
        sum += us;
        (hasOn ? withOn : plain).push_back (us);
        maxVoices = std::max (maxVoices, p.activeVoices.load());
    }
    const double budget = 1.0e6 * block / rate;
    auto pct = [budget] (double us) { return juce::String (100.0 * us / budget, 1) + " %"; };
    auto quantile = [] (std::vector<double> v, double q) {
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        return v[static_cast<std::size_t> (std::min<double> (static_cast<double> (v.size() - 1), q * static_cast<double> (v.size())))];
    };
    std::vector<double> all = plain;
    all.insert (all.end(), withOn.begin(), withOn.end());
    std::cout << "preset " << presetFile.getFileName() << " [" << label << "], block " << block << ", " << seconds << " s, up to " << maxVoices << " voices\n"
              << "  all blocks: mean " << pct (sum / static_cast<double> (all.size())) << ", median " << pct (quantile (all, 0.5)) << ", p99 "
              << pct (quantile (all, 0.99)) << ", worst " << pct (quantile (all, 1.0)) << "\n"
              << "  blocks without note-ons: median " << pct (quantile (plain, 0.5)) << ", p99 " << pct (quantile (plain, 0.99)) << ", worst "
              << pct (quantile (plain, 1.0)) << "\n"
              << "  blocks with note-ons (" << withOn.size() << "): median " << pct (quantile (withOn, 0.5)) << ", worst " << pct (quantile (withOn, 1.0))
              << "\n";

    // OSP_CPU_UI=1: the editor while the last chord sounds - how many read heads each card
    // shows and what one display frame costs to paint (1x and 2x), heads included.
    if (std::getenv ("OSP_CPU_UI") != nullptr)
    {
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditorIfNeeded());
        auto* editor = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (base.get());
        REQUIRE (editor != nullptr);
        // Hold a chord and let it sound a little (heads move), then refresh the editor.
        juce::MidiBuffer midi;
        for (int n : chords.front())
            midi.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (96)), 0);
        for (int i = 0; i < static_cast<int> (0.6 * rate / block); ++i)
        {
            buffer.clear();
            p.processBlock (buffer, midi);
            midi.clear();
        }
        editor->refreshNow();
        auto& instrument = *editor->modulationOverlay().getParentComponent();
        for (auto* child : instrument.getChildren())
            if (auto* card = dynamic_cast<osp::plugin::EngineCard*> (child); card != nullptr && card->isVisible())
            {
                auto& display = card->display();
                const int layer = card->layer();
                const auto& snap = p.grainSnapshot (layer);
                const auto r = editor->getLocalArea (&display, display.getLocalBounds());
                for (float scale : { 1.0f, 2.0f })
                {
                    (void) editor->createComponentSnapshot (r, true, scale);
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int k = 0; k < 10; ++k)
                        (void) editor->createComponentSnapshot (r, true, scale);
                    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 10.0;
                    // One head's strip (4 px, the display's height): a frame repaints two per head.
                    const auto strip = r.withX (r.getCentreX()).withWidth (4);
                    (void) editor->createComponentSnapshot (strip, true, scale);
                    const auto t1 = std::chrono::steady_clock::now();
                    for (int k = 0; k < 20; ++k)
                        (void) editor->createComponentSnapshot (strip, true, scale);
                    const double stripMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t1).count() / 20.0;
                    // The display alone (its own paint: the cached waveform and the marks), no parents.
                    (void) display.createComponentSnapshot (display.getLocalBounds(), true, scale);
                    const auto t2 = std::chrono::steady_clock::now();
                    for (int k = 0; k < 10; ++k)
                        (void) display.createComponentSnapshot (display.getLocalBounds(), true, scale);
                    const double ownMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t2).count() / 10.0;
                    std::cout << "  card " << OspAudioProcessor::layerName (layer) << " display alone: " << juce::String (ownMs, 2) << " ms\n";
                    // The same pixels as the display painted the old way (transparent, its parents
                    // behind it): the backdrop must reproduce them.
                    {
                        const auto now = editor->createComponentSnapshot (r, true, scale);
                        display.setOpaque (false);
                        display.invalidateBackdrop();
                        const auto old = editor->createComponentSnapshot (r, true, scale);
                        display.setOpaque (true);
                        int worst = 0;
                        for (int y = 0; y < now.getHeight(); ++y)
                            for (int x = 0; x < now.getWidth(); ++x)
                            {
                                const auto a = now.getPixelAt (x, y), b = old.getPixelAt (x, y);
                                worst = std::max ({ worst, std::abs (a.getRed() - b.getRed()), std::abs (a.getGreen() - b.getGreen()),
                                                    std::abs (a.getBlue() - b.getBlue()) });
                            }
                        std::cout << "  card " << OspAudioProcessor::layerName (layer) << " opaque vs old pixels, worst difference: " << worst << " of 255\n";
                    }
                    std::cout << "  card " << OspAudioProcessor::layerName (layer) << ": " << snap.playheads.load() << " read heads, " << snap.count.load()
                              << " grains; one full display frame at " << scale << "x: " << juce::String (ms, 2) << " ms, one head's strip "
                              << juce::String (stripMs, 2) << " ms\n";
                }
            }
    }
}

TEST_CASE ("plugin: a card shows at most eight read heads, the loudest; the display is opaque", "[plugin][cpu]")
{
    using namespace osp::plugin;
    SourceDisplay display;
    display.setSize (600, 360);
    CHECK (display.isOpaque());
    std::vector<SourceDisplay::Dot> dots;
    for (int i = 0; i < 20; ++i)
        dots.push_back ({ static_cast<float> (i) / 20.0f, static_cast<float> (i) / 19.0f, 0.5f });
    display.setPlayheads (dots.data(), static_cast<int> (dots.size()));
    CHECK (display.headsShown() == SourceDisplay::maxHeadsShown);
    display.setPlayheads (dots.data(), 5);
    CHECK (display.headsShown() == 5);   // a few: all of them
    // Painting it alone (no parents) is safe: the backdrop is then just its own well.
    juce::Image image (juce::Image::RGB, 600, 360, true);
    juce::Graphics g (image);
    display.paint (g);
}
