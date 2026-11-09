// The Polyphonic Instrument Extraction Lab's import route (research/polyphonic-lab; reports in
// docs/research/polyphonic-lab/). The lab writes an ordinary Note Atlas: one WAV per extracted
// note in samples/, found through relativePath, identified by the SHA-256 of its bytes. These
// tests hold OSP to reading exactly that layout, with no change to OSP itself:
//   [poly-lab]         a hand-written atlas in the exporter's exact JSON layout plays, fills the
//                      keys between, and survives a portable-instrument export and import;
//   [.poly-lab-import] (hidden) the lab's real output: OSP_POLY_ATLAS names an .andoratlas,
//                      OSP_POLY_OUT a folder for the renders and a JSON summary. Run by
//                      `osp-poly build ... --plugin-check` or by hand:
//                      OSP_POLY_ATLAS=... OSP_POLY_OUT=... osp_plugin_tests "[poly-lab-import]"
// Included at the end of PluginProcessorTests.cpp (uses Phase 2/3 helpers).

namespace
{
    /** What the lab's exporter writes for one note (polyphonic_lab/mapping/atlas.py, atlas_note). */
    juce::String polyLabNoteJson (int i, int key, std::int64_t frames, std::int64_t onset, std::int64_t release)
    {
        const auto hz = midiToHz (key);
        return juce::String (R"({"id": "n000)") + juce::String (i) + R"(", "source": "src)" + juce::String (i)
             + R"(", "sourceStart": 0, "sourceEnd": )" + juce::String (frames)
             + R"(, "attackStart": )" + juce::String (onset - 240)
             + R"(, "sustain": {"start": )" + juce::String (std::max<std::int64_t> (onset + 4800, release - 26400))
             + R"(, "end": )" + juce::String (release - 2400)
             + R"(}, "releaseStart": )" + juce::String (release)
             + R"(, "fadeIn": 96, "fadeOut": 480, "pitchHz": )" + juce::String (hz, 4)
             + R"(, "midiPitch": )" + juce::String (key) + R"(.0, "rootMidi": )" + juce::String (key)
             + R"(, "cents": 0.0, "stabilityCents": 0.0, "vibratoCents": 0.0, "vibratoHz": 0.0, "voicing": 1.0, "confidence": 0.8,)"
             + R"( "peakDb": -18.0, "rmsDb": -30.0, "articulation": "sustained", "origin": "analysis", "method": "osp-poly/C", "methodVersion": 1,)"
             + R"( "corrections": {"pitch": false, "boundaries": false, "key": false, "group": false, "split": false, "merged": false, "velocity": false},)"
             + R"( "included": true, "assignedKey": -1, "velocityLayer": 0, "group": "", "variation": 0})";
    }

    /** A folder as `osp-poly export-atlas` leaves it: samples/ + instrument.andoratlas, three
        decaying single-note recordings (C4, E4, G4) with a 20 ms pre-roll. */
    juce::File polyLabFolder (const juce::File& dir)
    {
        const double rate = 48000.0;
        const auto folder = dir.getChildFile ("instrument");
        folder.getChildFile ("samples").createDirectory();
        juce::String sources, notes;
        const int keys[] = { 60, 64, 67 };
        for (int i = 0; i < 3; ++i)
        {
            const auto onset = static_cast<std::int64_t> (0.02 * rate);
            const auto frames = static_cast<std::int64_t> (2.4 * rate);
            const auto release = static_cast<std::int64_t> (2.02 * rate);
            auto audio = AudioData::allocate (1, frames, rate);
            const auto tone = testsignals::pluck (midiToHz (keys[i]), 2.4, rate, 40 + i);
            for (std::int64_t f = onset; f < frames; ++f)
                audio.channels[0][static_cast<std::size_t> (f)] = 0.5f * tone.channels[0][static_cast<std::size_t> (f - onset)];
            const auto name = "note-000" + juce::String (i + 1) + "_" + juce::String (std::vector<const char*> { "C4", "E4", "G4" }[static_cast<std::size_t> (i)]) + ".wav";
            const auto file = folder.getChildFile ("samples").getChildFile (name);
            std::string error;
            REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::float32, error));
            const auto hash = io::contentId (*io::sha256OfFile (std::filesystem::path (file.getFullPathName().toStdString())));
            sources << (i ? ", " : "") << R"({"id": "src)" << i << R"(", "contentHash": ")" << juce::String (hash)
                    << R"(", "origin": "fileImport", "fileName": ")" << name << R"(", "relativePath": "samples/)" << name
                    << R"(", "lastKnownPath": "/nowhere/)" << name << R"(", "managedCopy": "", "sampleRate": 48000, "numChannels": 1, "numFrames": )" << juce::String (frames) << "}";
            notes << (i ? ", " : "") << polyLabNoteJson (i, keys[i], frames, onset, release);
        }
        const auto json = juce::String (R"({"format": "andor.noteAtlas", "schemaVersion": 2, "atlasId": "osp-poly-instrument-test", "createdBy": "osp-poly 0.1.0, research lab",)")
                        + R"( "analysis": {"method": "osp-poly/C", "version": 1, "a4Hz": 440.0, "tuningOffsetCents": 0.0, "settingsDigest": "0123456789abcdef"},)"
                        + R"( "mapping": {"fill": true, "maxTransposeSemitones": 12.0, "correctTuning": true}, "sources": [)" + sources + "], \"notes\": [" + notes + "]}";
        REQUIRE (folder.getChildFile ("instrument.andoratlas").replaceWithText (json));
        return folder;
    }

    double windowRmsDb (const AudioData& audio, double from, double to)
    {
        double sum = 0.0;
        std::int64_t n = 0;
        for (const auto& ch : audio.channels)
            for (auto i = static_cast<std::size_t> (from * audio.sampleRate); i < std::min (ch.size(), static_cast<std::size_t> (to * audio.sampleRate)); ++i, ++n)
                sum += static_cast<double> (ch[i]) * ch[i];
        return 10.0 * std::log10 (sum / static_cast<double> (std::max<std::int64_t> (1, n)) + 1.0e-30);
    }
}

TEST_CASE ("plugin: a polyphonic-lab atlas (one WAV per extracted note, by relative path) plays, fills, and travels", "[plugin][poly-lab]")
{
    TempDir tmp;
    const auto folder = polyLabFolder (tmp.dir);
    const auto package = tmp.dir.getChildFile ("poly.ospinstrument");
    std::string error;
    const auto atlasFile = std::filesystem::path (folder.getChildFile ("instrument.andoratlas").getFullPathName().toStdString());
    auto a = io::readNoteAtlas (atlasFile, error);
    REQUIRE (a.has_value());
    CHECK (a->notes.size() == 3);
    CHECK (atlas::validate (*a).ok());
    {
        const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-a").getFullPathName());
        OspAudioProcessor p;
        p.loadAtlas (*a, -1, folder.getFullPathName().toStdString());
        settle (p);
        REQUIRE (p.loadState() == OspAudioProcessor::LoadState::ready);
        REQUIRE (p.currentInstrument() != nullptr);
        CHECK (p.currentInstrument()->set->members.size() == 3);
        CHECK (p.missingSources().empty());
        for (int key : { 60, 64, 67 })      // the extracted notes
            CHECK (heldLevelDb (p, key) > -50.0);
        for (int key : { 62, 65, 69, 72 })  // FILL: keys never recorded borrow the nearest note
            CHECK (heldLevelDb (p, key) > -50.0);
        CHECK (heldLevelDb (p, 90) < -100.0);
        juce::String exportError;
        REQUIRE (p.exportInstrument (package, exportError));
    }
    REQUIRE (folder.deleteRecursively());
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-b").getFullPathName());
    OspAudioProcessor q;
    juce::String importError;
    REQUIRE (q.importInstrument (package, importError));
    settle (q);
    REQUIRE (q.currentInstrument() != nullptr);
    CHECK (q.currentInstrument()->atlas != nullptr);
    CHECK (heldLevelDb (q, 64) > -50.0);
    CHECK (windowRmsDb (renderHeld (q, { 60, 64, 67 }, 2.0), 0.0, 1.0) > -50.0);
}

TEST_CASE ("plugin: the polyphonic lab's own atlas through the plugin (OSP_POLY_ATLAS, OSP_POLY_OUT)", "[.poly-lab-import]")
{
    const char* atlasEnv = std::getenv ("OSP_POLY_ATLAS");
    const char* outEnv = std::getenv ("OSP_POLY_OUT");
    if (atlasEnv == nullptr || outEnv == nullptr)
    {
        WARN ("set OSP_POLY_ATLAS and OSP_POLY_OUT");
        return;
    }
    const juce::File atlasFile (juce::String::fromUTF8 (atlasEnv)), out (juce::String::fromUTF8 (outEnv));
    out.createDirectory();
    TempDir tmp;
    std::string error;
    auto a = io::readNoteAtlas (std::filesystem::path (atlasFile.getFullPathName().toStdString()), error);
    REQUIRE (a.has_value());
    const auto report = atlas::validate (*a);
    std::vector<int> keys;
    for (const auto& n : a->notes)
        if (n.included)
            keys.push_back (n.playKey());
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());

    auto save = [&out] (const juce::String& name, const AudioData& audio)
    {
        std::string e;
        REQUIRE (io::writeAudioFile (std::filesystem::path (out.getChildFile (name).getFullPathName().toStdString()), audio, io::SampleFormat::float32, e));
    };
    auto* summary = new juce::DynamicObject();
    summary->setProperty ("atlas", atlasFile.getFullPathName());
    summary->setProperty ("validationErrors", static_cast<int> (report.errors.size()));
    summary->setProperty ("validationWarnings", static_cast<int> (report.warnings.size()));
    const auto package = tmp.dir.getChildFile ("lab.ospinstrument");
    juce::String chordHash;
    {
        const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-a").getFullPathName());
        OspAudioProcessor p;
        p.loadAtlas (*a, -1, atlasFile.getParentDirectory().getFullPathName().toStdString());
        settle (p);
        REQUIRE (p.loadState() == OspAudioProcessor::LoadState::ready);
        REQUIRE (p.currentInstrument() != nullptr);
        summary->setProperty ("members", static_cast<int> (p.currentInstrument()->set->members.size()));
        summary->setProperty ("missingSources", static_cast<int> (p.missingSources().size()));
        juce::Array<juce::var> levels;
        for (int key : keys)
        {
            const auto audio = renderHeld (p, { key }, 2.5);
            save ("plugin-key-" + juce::String (key) + ".wav", audio);
            auto* o = new juce::DynamicObject();
            o->setProperty ("key", key);
            o->setProperty ("rmsDbFirstSecond", windowRmsDb (audio, 0.0, 1.0));
            levels.add (juce::var (o));
            CHECK (windowRmsDb (audio, 0.0, 1.0) > -60.0);
        }
        summary->setProperty ("keys", levels);
        const auto chord = renderHeld (p, keys, 3.0);
        chordHash = audioHash (chord);
        save ("plugin-recorded-chord.wav", chord);
        save ("plugin-F-major.wav", renderHeld (p, { 65, 69, 72 }, 3.0));
        save ("plugin-A-minor.wav", renderHeld (p, { 57, 60, 64 }, 3.0));
        summary->setProperty ("chordRmsDb", windowRmsDb (chord, 0.0, 2.0));
        juce::String exportError;
        REQUIRE (p.exportInstrument (package, exportError));
    }
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store-b").getFullPathName());
    OspAudioProcessor q;
    juce::String importError;
    REQUIRE (q.importInstrument (package, importError));
    settle (q);
    REQUIRE (q.currentInstrument() != nullptr);
    const auto again = renderHeld (q, keys, 3.0);
    save ("plugin-reimported-chord.wav", again);
    summary->setProperty ("reimportedIdentical", audioHash (again) == chordHash);
    CHECK (audioHash (again) == chordHash);
    REQUIRE (out.getChildFile ("plugin-import.json").replaceWithText (juce::JSON::toString (juce::var (summary))));
}
