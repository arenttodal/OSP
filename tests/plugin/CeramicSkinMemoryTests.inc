// The ceramic skin's composed-piece cache: budget, stale size variants, and the memory
// investigation (docs/reports/ceramic-skin-memory.md).

TEST_CASE ("plugin: the ceramic cache honours its budget and drops the sizes a resize passed through", "[plugin][ceramic]")
{
    const ScopedLook ceramic (skin::Look::ceramic);
    skin::Pieces::Holder holder;
    auto* pieces = skin::active();
    REQUIRE (pieces != nullptr);
    pieces->release();
    const auto defaultBudget = pieces->budgetBytes();
    CHECK (defaultBudget == 96u * 1024u * 1024u);   // Phase 5 default (OSP_SKIN_CACHE_MB overrides it)
    auto paint = [&] (float scale) {
        juce::Image image (juce::Image::ARGB, juce::roundToInt (500 * scale), juce::roundToInt (300 * scale), true);
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        REQUIRE (pieces->draw (g, "card-451", { 20.0f, 20.0f, 451.0f, 260.0f }));
    };
    // A smaller budget is honoured while a window passes through many sizes.
    pieces->setBudget (8u * 1024u * 1024u, 160);
    for (int s = 0; s < 40; ++s)
        paint (1.0f + 0.05f * static_cast<float> (s));
    CHECK (pieces->usage().bytes <= 8u * 1024u * 1024u);
    CHECK (pieces->usage().evictions > 0);
    // Stale variants: after the resize settles, only the size now shown stays.
    pieces->setBudget (defaultBudget, 160);
    pieces->release();
    for (int s = 0; s < 10; ++s)
        paint (1.0f + 0.1f * static_cast<float> (s));
    CHECK (pieces->usage().composedImages == 10);
    const auto gen = pieces->beginGeneration();
    paint (2.0f);
    const auto before = pieces->usage();
    const auto released = pieces->evictUnusedSince (gen);
    CHECK (released > 0);
    CHECK (pieces->usage().composedImages == 1);
    CHECK (pieces->usage().bytes == before.bytes - released);
    // The size still shown is a hit, not composed again.
    const auto misses = pieces->usage().misses;
    paint (2.0f);
    CHECK (pieces->usage().misses == misses);
    pieces->release();
}

#if JUCE_LINUX
 #include <malloc.h>
#endif

namespace skinmemory
{
    /** Resident memory of this process (MB): Linux /proc/self/statm; 0 elsewhere. */
    inline double residentMB()
    {
       #if JUCE_LINUX
        long pages = 0, resident = 0;
        if (FILE* f = std::fopen ("/proc/self/statm", "r"))
        {
            if (std::fscanf (f, "%ld %ld", &pages, &resident) != 2)
                resident = 0;
            std::fclose (f);
        }
        return static_cast<double> (resident) * static_cast<double> (sysconf (_SC_PAGESIZE)) / (1024.0 * 1024.0);
       #else
        return 0.0;
       #endif
    }
    inline void trim()
    {
       #if JUCE_LINUX
        malloc_trim (0);   // give freed heap back, to separate allocator retention from live data
       #endif
    }
    inline double mb (size_t b) { return static_cast<double> (b) / (1024.0 * 1024.0); }
}

TEST_CASE ("plugin: ceramic skin memory investigation (OSP_SKIN_MEMORY_REPORT)", "[.][skin-memory]")
{
    const auto reportPath = juce::SystemStats::getEnvironmentVariable ("OSP_SKIN_MEMORY_REPORT", {});
    REQUIRE (reportPath.isNotEmpty());
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    juce::Array<juce::File> src;
    src.add (writeSource (tmp.dir, "A.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3)));
    src.add (writeSource (tmp.dir, "B.wav", testsignals::pluck (220.0, 2.6, 48000.0, 4)));
    src.add (writeSource (tmp.dir, "C.wav", testsignals::vowel (midiToHz (48), 2.4, 48000.0, 5)));
    using Editor = osp::plugin::OspAudioProcessorEditor;

    juce::String out;
    auto line = [&out] (const juce::String& s) { out << s << "\n"; };
    line ("budget_mb,evict_on_settle,phase,images,cache_mb,overlay_mb,peak_mb,hits,misses,evictions,compose_ms,rss_mb,rss_trimmed_mb,ms");

    const std::vector<int> widths { 869, 1086, 1269, 1448, 1629, 1810 };
    for (const int budgetMB : { 192, 128, 96, 64 })
        for (const bool evictOnSettle : { false, true })
        {
            skinmemory::trim();
            const double rss0 = skinmemory::residentMB();
            OspAudioProcessor p;
            p.addLayers (src);
            REQUIRE (p.waitForLoads (30000));
            p.pollLoads();
            const auto open0 = juce::Time::getMillisecondCounterHiRes();
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
            auto* ui = dynamic_cast<Editor*> (e.get());
            REQUIRE (ui != nullptr);
            auto* pieces = skin::active();
            REQUIRE (pieces != nullptr);
            pieces->release();
            pieces->setBudget (static_cast<size_t> (budgetMB) * 1024u * 1024u, 160);
            const auto base = pieces->usage();
            auto paintAt = [&] (int width) {
                e->setSize (width, juce::roundToInt (ui->instrumentHeight() * width / 1448.0));
                ui->refreshNow();
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                auto image = e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
                juce::ignoreUnused (image);
                return juce::Time::getMillisecondCounterHiRes() - t0;
            };
            auto record = [&] (const juce::String& phase, double ms) {
                const auto u = pieces->usage();
                const double rss = skinmemory::residentMB();
                skinmemory::trim();
                const double rssTrim = skinmemory::residentMB();
                line (juce::String (budgetMB) + "," + (evictOnSettle ? "1" : "0") + "," + phase + "," + juce::String (u.composedImages) + ","
                      + juce::String (skinmemory::mb (u.bytes), 1) + "," + juce::String (skinmemory::mb (u.overlayBytes), 1) + ","
                      + juce::String (skinmemory::mb (u.peakBytes), 1) + "," + juce::String ((juce::int64) (u.hits - base.hits)) + ","
                      + juce::String ((juce::int64) (u.misses - base.misses)) + "," + juce::String ((juce::int64) (u.evictions - base.evictions)) + ","
                      + juce::String ((u.composeSeconds - base.composeSeconds) * 1000.0, 1) + "," + juce::String (rss - rss0, 1) + ","
                      + juce::String (rssTrim - rss0, 1) + "," + juce::String (ms, 1));
            };
            // Cold open at the default width (first paint composes everything).
            record ("open_cold", paintAt (1448) + (juce::Time::getMillisecondCounterHiRes() - open0) * 0.0);
            // Warm: the same size again (all hits).
            double warm = 0.0;
            for (int i = 0; i < 5; ++i)
                warm += paintAt (1448);
            record ("repaint_warm_avg", warm / 5.0);
            // Sustained resizing: three rounds through six widths, plus in-between sizes.
            double sweep = 0.0;
            int frames = 0;
            for (int round = 0; round < 3; ++round)
                for (int w = 869; w <= 1810; w += 47)
                {
                    sweep += paintAt (w);
                    ++frames;
                }
            record ("resize_sweep_avg", sweep / frames);
            // Settled at 1448: optionally drop what is no longer shown.
            std::uint64_t gen = 0;
            if (evictOnSettle)
                gen = pieces->beginGeneration();
            const double settled = paintAt (1448);
            if (evictOnSettle)
                pieces->evictUnusedSince (gen);
            record ("settled_1448", settled);
            // Back to a size seen during the sweep: hit (kept) or recomposed (evicted)?
            record ("revisit_1086", paintAt (1086));
            record ("revisit_1448", paintAt (1448));
            // Normal use: 60 repaints at one size with notes playing (meters, the keyboard).
            p.prepareToPlay (48000.0, 480);
            juce::AudioBuffer<float> buffer (2, 480);
            double play = 0.0;
            for (int i = 0; i < 60; ++i)
            {
                juce::MidiBuffer midi;
                if (i % 15 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 48 + i % 24, static_cast<juce::uint8> (96)), 0);
                buffer.clear();
                p.processBlock (buffer, midi);
                play += paintAt (1448);
            }
            record ("normal_use_avg", play / 60.0);
            // A second editor in the same process (another instance) shares the pieces.
            {
                OspAudioProcessor p2;
                std::unique_ptr<juce::AudioProcessorEditor> e2 (p2.createEditorIfNeeded());
                CHECK (skin::active() == pieces);
                e2->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
                auto img2 = e2->createComponentSnapshot (e2->getLocalBounds(), true, 2.0f);
                juce::ignoreUnused (img2);
                record ("second_instance", 0.0);
            }
            e.reset();
            // After the last editor closes: the pieces (and their cache) are released.
            const bool released = skin::Pieces::current() == nullptr;
            skinmemory::trim();
            line (juce::String (budgetMB) + "," + (evictOnSettle ? "1" : "0") + ",after_editor_destroyed,"
                  + (released ? "released" : "kept") + ",,,,,,,," + juce::String (skinmemory::residentMB() - rss0, 1) + ",,");
        }
    juce::File (reportPath).replaceWithText (out);
}

TEST_CASE ("plugin: two open editors of different sizes keep their pieces when one resizes and settles", "[plugin][ceramic]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    // These editors stand for two windows on screen (a headless editor never claims or stretches).
    struct OnScreen { OnScreen() { skin::EditorCacheScope::setTreatOffscreenAsOnScreen (true); } ~OnScreen() { skin::EditorCacheScope::setTreatOffscreenAsOnScreen (false); } } onScreen;
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor pa, pb;
    std::unique_ptr<juce::AudioProcessorEditor> ea (pa.createEditorIfNeeded()), eb (pb.createEditorIfNeeded());
    auto* ua = dynamic_cast<Editor*> (ea.get());
    auto* ub = dynamic_cast<Editor*> (eb.get());
    REQUIRE (ua != nullptr);
    REQUIRE (ub != nullptr);
    auto* pieces = skin::active();
    REQUIRE (pieces != nullptr);
    pieces->release();
    auto size = [] (juce::AudioProcessorEditor& e, Editor& u, int w) { e.setSize (w, juce::roundToInt (u.instrumentHeight() * w / 1448.0)); u.refreshNow(); };
    auto paint = [] (juce::AudioProcessorEditor& e) { auto img = e.createComponentSnapshot (e.getLocalBounds(), true, 2.0f); juce::ignoreUnused (img); };
    size (*ea, *ua, 1448);
    size (*eb, *ub, 1086);
    paint (*ea);
    paint (*eb);
    const auto both = pieces->usage();
    REQUIRE (both.composedImages > 0);
    // B again: everything it shows is cached.
    paint (*eb);
    CHECK (pieces->usage().misses == both.misses);
    // A is resized: while it is being dragged its pieces are stretched, no new size composed.
    size (*ea, *ua, 1629);
    CHECK (pieces->isResizing (ea.get()));
    const auto beforeDrag = pieces->usage();
    paint (*ea);
    const auto dragged = pieces->usage();
    // (the stretch composes at most each piece's rendered size once; never one per window size)
    size (*ea, *ua, 1700);
    paint (*ea);
    CHECK (pieces->usage().misses == dragged.misses);
    juce::ignoreUnused (beforeDrag);
    // A settles: it repaints exactly at 1700 and drops what only it used (its 1448 pieces).
    // (the editor's settle timer does these three steps; there is no message loop in a test)
    pieces->setResizing (ea.get(), false);
    pieces->beginGeneration (ea.get());
    paint (*ea);
    const auto released = pieces->evictStale (ea.get());
    CHECK (released > 0);
    CHECK (! pieces->isResizing (ea.get()));
    // B is untouched: nothing it shows was evicted.
    const auto before = pieces->usage().misses;
    paint (*eb);
    CHECK (pieces->usage().misses == before);
    // A closes: its claims go, B's pieces stay.
    const auto beforeClose = pieces->usage().composedImages;
    ea.reset();
    CHECK (pieces->usage().composedImages < beforeClose);
    const auto missesAfter = pieces->usage().misses;
    paint (*eb);
    CHECK (pieces->usage().misses == missesAfter);
}

TEST_CASE ("plugin: ceramic settle behaviour measured (OSP_SKIN_SETTLE_REPORT)", "[.][skin-settle]")
{
    const auto reportPath = juce::SystemStats::getEnvironmentVariable ("OSP_SKIN_SETTLE_REPORT", {});
    REQUIRE (reportPath.isNotEmpty());
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    using Editor = osp::plugin::OspAudioProcessorEditor;
    juce::String out;
    struct OnScreen { OnScreen() { skin::EditorCacheScope::setTreatOffscreenAsOnScreen (true); } ~OnScreen() { skin::EditorCacheScope::setTreatOffscreenAsOnScreen (false); } } onScreen;
    out << "mode,phase,paints,avg_paint_ms,max_paint_ms,misses,compose_ms,cache_mb,peak_mb,rss_trimmed_mb\n";
    // Three modes: "lru" is the cache before this change (no per-editor claims, the plain LRU
    // budget, every passing size composed); "compose-every-size" keeps the claims but never
    // settles (a stress case: nothing may be evicted while the open editor claims it);
    // "settle" is the change (stretch while resizing, exact after settling, stale sizes evicted).
    for (const int mode : { 0, 1, 2 })
    {
        const bool live = mode == 2;
        skin::EditorCacheScope::setTreatOffscreenAsOnScreen (mode != 0);
        const juce::String modeName = mode == 0 ? "lru" : mode == 1 ? "compose-every-size" : "settle";
        skinmemory::trim();
        const double rss0 = skinmemory::residentMB();
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        auto* pieces = skin::active();
        REQUIRE (pieces != nullptr);
        pieces->release();
        auto paintAt = [&] (int w) {
            e->setSize (w, juce::roundToInt (ui->instrumentHeight() * w / 1448.0));
            if (! live)
                pieces->setResizing (e.get(), false);   // the old behaviour: compose every size
            ui->refreshNow();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
            juce::ignoreUnused (img);
            return juce::Time::getMillisecondCounterHiRes() - t0;
        };
        auto row = [&] (const juce::String& phase, int n, double avg, double mx, const skin::Pieces::Usage& u0) {
            const auto u = pieces->usage();
            skinmemory::trim();
            out << modeName << "," << phase << "," << n << "," << juce::String (avg, 1) << ","
                << juce::String (mx, 1) << "," << juce::String ((juce::int64) (u.misses - u0.misses)) << ","
                << juce::String ((u.composeSeconds - u0.composeSeconds) * 1000.0, 1) << "," << juce::String (skinmemory::mb (u.bytes), 1) << ","
                << juce::String (skinmemory::mb (u.peakBytes), 1) << "," << juce::String (skinmemory::residentMB() - rss0, 1) << "\n";
        };
        auto u0 = pieces->usage();
        const double open = paintAt (1448);
        row ("open_1448", 1, open, open, u0);
        u0 = pieces->usage();
        double sum = 0, mx = 0;
        int n = 0;
        for (int round = 0; round < 3; ++round)
            for (int w = 869; w <= 1810; w += 47)
            {
                const double t = paintAt (w);
                sum += t; mx = std::max (mx, t); ++n;
            }
        row ("live_resize_sweep", n, sum / n, mx, u0);
        u0 = pieces->usage();
        e->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
        ui->refreshNow();
        if (live)
        {
            // the settle timer's step, then the repaint it asks for
            pieces->setResizing (e.get(), false);
            pieces->beginGeneration (e.get());
        }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto settled = e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
        const double st = juce::Time::getMillisecondCounterHiRes() - t0;
        if (live)
            pieces->evictStale (e.get());
        row ("settled_1448", 1, st, st, u0);
        // Visual quality: the stretched frame against the exact one at the same size.
        if (live)
        {
            pieces->setResizing (e.get(), true);
            auto stretched = e->createComponentSnapshot (e->getLocalBounds(), true, 2.0f);
            pieces->setResizing (e.get(), false);
            int maxDiff = 0;
            double sumDiff = 0;
            for (int y = 0; y < settled.getHeight(); y += 2)
                for (int x = 0; x < settled.getWidth(); x += 2)
                {
                    const auto a = settled.getPixelAt (x, y), b = stretched.getPixelAt (x, y);
                    const int d = std::max ({ std::abs (a.getRed() - b.getRed()), std::abs (a.getGreen() - b.getGreen()), std::abs (a.getBlue() - b.getBlue()) });
                    maxDiff = std::max (maxDiff, d);
                    sumDiff += d;
                }
            out << "settle,stretched_vs_exact_1448,1," << juce::String (sumDiff / ((settled.getWidth() / 2) * (settled.getHeight() / 2)), 3)
                << "," << maxDiff << ",,,,,\n";
        }
        e.reset();
        skinmemory::trim();
        out << modeName << ",after_close,0,,,,,," << ",," << juce::String (skinmemory::residentMB() - rss0, 1) << "\n";
    }
    juce::File (reportPath).replaceWithText (out);
}
