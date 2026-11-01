// ANDOR Memo Lab (docs/research/memo-lab): the app's own parts, headless.
// - Sources: approved folders, the scan's states (ready, not downloaded, still syncing,
//   unreadable), hidden entries, packages, links out of the folder.
// - Decoding: WAV, AIFF, and on macOS real AAC .m4a files made by afconvert.
// - Passages: exactly the frames chosen.
// - ONE SHOT and MAP NOTES through ANDOR OSP's own processor; an unpitched recording falls
//   back to one sample.
// - The saved .ospinstrument opening in a fresh processor with an empty sample store and the
//   recording gone, as the plugin opens it.
// - Playing with no allocations; the window listing, reading, converting.
// Recordings are only read: every test checks that their bytes are unchanged.
// MEMO_LAB_SHOTS=<folder> also writes the window's screenshots.

#include "MemoLabWindow.h"
#include "MemoSession.h"
#include "MemoSources.h"
#include "OspLookAndFeel.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "support/AllocationCounter.h"

#define CATCH_CONFIG_RUNNER
#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace osp;
using namespace osp::memolab;
using Catch::Approx;

namespace
{
    struct TempDir
    {
        TempDir() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("memo-lab-test-" + juce::String (juce::Random::getSystemRandom().nextInt64())))
        {
            dir.createDirectory();
        }
        ~TempDir() { dir.deleteRecursively(); }
        juce::File dir;
    };

    void setEnv (const char* name, const juce::String& value)
    {
#if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
#else
        setenv (name, value.toRawUTF8(), 1);
#endif
    }

    juce::File write (const juce::File& dir, const juce::String& name, const AudioData& audio)
    {
        const auto file = dir.getChildFile (name);
        std::string error;
        REQUIRE (io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error));
        // Recorded an hour ago (a fresh file counts as still syncing).
        file.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (1.0));
        return file;
    }

    std::string hashOf (const juce::File& f)
    {
        return io::contentId (*io::sha256OfFile (std::filesystem::path (f.getFullPathName().toStdString())));
    }

    /** A sung phrase: vowels on `keys`, 0.8 s each with 0.25 s of silence between. */
    AudioData sungPhrase (std::vector<int> keys, double rate = 48000.0, int channels = 1)
    {
        const auto note = static_cast<std::int64_t> (0.8 * rate), gap = static_cast<std::int64_t> (0.25 * rate);
        auto audio = AudioData::allocate (channels, static_cast<std::int64_t> (keys.size()) * (note + gap) + gap, rate);
        for (std::size_t k = 0; k < keys.size(); ++k)
        {
            const auto v = testsignals::vowel (midiToHz (keys[k]), 0.8, rate, 40 + k, 0.5, channels);
            const auto start = gap + static_cast<std::int64_t> (k) * (note + gap);
            for (int c = 0; c < channels; ++c)
                for (std::int64_t i = 0; i < note; ++i)
                    audio.channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (start + i)] = v.channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)];
        }
        return audio;
    }

    /** The processor on 48 kHz blocks: one held note's level (dBFS RMS, 0.4 s). */
    double heldLevelDb (OspAudioProcessor& p, int note)
    {
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        double sum = 0.0;
        std::int64_t n = 0;
        for (int b = 0; b < 48; ++b)
        {
            midi.clear();
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
            block.clear();
            p.processBlock (block, midi);
            if (b >= 4 && b < 44)
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 512; ++i, ++n)
                        sum += static_cast<double> (block.getSample (c, i)) * block.getSample (c, i);
        }
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        for (int b = 0; b < 40; ++b)
        {
            block.clear();
            p.processBlock (block, midi);
            midi.clear();
        }
        return 10.0 * std::log10 (sum / static_cast<double> (std::max<std::int64_t> (1, n)) + 1.0e-30);
    }

    /** Polls the processor and the session until the conversion has an outcome. */
    MemoSession::Status settle (MemoSession& session, int timeoutMs = 60000)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            session.processor().pollLoads();
            const auto s = session.update();
            if (s.stage != MemoSession::Status::Stage::building && s.stage != MemoSession::Status::Stage::playable && ! session.busy())
                return s;
            juce::Thread::sleep (10);
        }
        FAIL ("the conversion did not finish");
        return session.status();
    }

    Recording recordingFor (const juce::File& file)
    {
        Recording r;
        r.file = file;
        r.title = file.getFileNameWithoutExtension();
        return r;
    }

    DecodedAudio decoded (const juce::File& file)
    {
        DecodedAudio audio;
        juce::String error;
        REQUIRE (decode (file, audio, error));
        return audio;
    }
}

//==============================================================================
TEST_CASE ("memo lab: approved folders are remembered, added once and removed", "[memo-lab][unit]")
{
    TempDir tmp;
    const auto file = tmp.dir.getChildFile ("settings.json");
    {
        SourceSettings s (file);
        CHECK (s.folders().isEmpty());
        CHECK_FALSE (s.offeredVoiceMemos());
        s.addFolder (tmp.dir.getChildFile ("a"));
        s.addFolder (tmp.dir.getChildFile ("b"));
        s.addFolder (tmp.dir.getChildFile ("a"));
        s.setOfferedVoiceMemos();
    }
    SourceSettings again (file);
    REQUIRE (again.folders().size() == 2);
    CHECK (again.offeredVoiceMemos());
    again.removeFolder (tmp.dir.getChildFile ("a"));
    CHECK (SourceSettings (file).folders() == juce::Array<juce::File> { tmp.dir.getChildFile ("b") });
    CHECK (juce::JSON::parse (file).getProperty ("schemaVersion", 0) == juce::var (1));
}

TEST_CASE ("memo lab: Voice Memos file names read as their moment", "[memo-lab][unit]")
{
    CHECK (titleFromFileName ("20241030 143012-AB12CD34") == "30 Oct 2024, 14:30");
    CHECK (titleFromFileName ("20240101 000501") == "1 Jan 2024, 00:05");
    CHECK (titleFromFileName ("Cello idea") == "Cello idea");
    CHECK (titleFromFileName ("20241399 143012") == "20241399 143012");   // not a date: as named
    REQUIRE (dateFromFileName ("20241030 143012-AB12CD34").has_value());
    CHECK (dateFromFileName ("20241030 143012-AB12CD34")->formatted ("%Y-%m-%d %H:%M:%S") == "2024-10-30 14:30:12");
    CHECK_FALSE (dateFromFileName ("Cello idea").has_value());
    CHECK (voiceMemosFolder().getFullPathName().endsWith ("Group Containers/group.com.apple.VoiceMemos.shared/Recordings"));
}

TEST_CASE ("memo lab: a scan lists only readable recordings in approved folders, and says why others are not ready", "[memo-lab][unit]")
{
    TempDir tmp, outside;
    const auto root = tmp.dir.getChildFile ("Recordings");
    root.createDirectory();
    const auto memo = write (root, "20241030 143012-AB12CD34.wav", testsignals::vowel (220.0, 1.0, 48000.0, 1, 0.5, 1));
    root.getChildFile ("Older").createDirectory();
    write (root.getChildFile ("Older"), "cello.aiff", testsignals::vowel (130.0, 1.5, 44100.0, 2, 0.5, 2));
    write (root, ".hidden.wav", testsignals::sine (440.0, 0.5, 48000.0));
    root.getChildFile ("Tool.app").createDirectory();
    write (root.getChildFile ("Tool.app"), "inside.wav", testsignals::sine (440.0, 0.5, 48000.0));
    root.getChildFile ("notes.txt").replaceWithText ("not audio");
    root.getChildFile (".later.m4a.icloud").replaceWithText ("placeholder");
    const auto corrupt = root.getChildFile ("broken.wav");
    corrupt.replaceWithText ("RIFF....WAVEjunk");
    corrupt.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (1.0));
    auto fresh = write (root, "arriving.wav", testsignals::sine (440.0, 0.5, 48000.0));
    fresh.setLastModificationTime (juce::Time::getCurrentTime());
    const auto away = write (outside.dir, "elsewhere.wav", testsignals::sine (330.0, 0.5, 48000.0));
#if ! JUCE_WINDOWS
    REQUIRE (away.createSymbolicLink (root.getChildFile ("link-out.wav"), true));
#endif
    const auto memoHash = hashOf (memo);

    const std::atomic<bool> never { false };
    const auto result = scanFolders ({ root, tmp.dir.getChildFile ("missing") }, never);
    std::map<juce::String, Recording> byName;
    for (const auto& r : result.recordings)
        byName[r.file.getFileName()] = r;
    CHECK (byName.size() == 5);
    REQUIRE (byName.count ("20241030 143012-AB12CD34.wav") == 1);
    const auto& ok = byName["20241030 143012-AB12CD34.wav"];
    CHECK (ok.state == Recording::State::ready);
    CHECK (ok.title == "30 Oct 2024, 14:30");
    CHECK (ok.seconds == Approx (1.0).margin (0.01));
    CHECK (ok.folder == root);
    CHECK (byName["cello.aiff"].state == Recording::State::ready);
    CHECK (byName["cello.aiff"].channels == 2);
    CHECK (byName["later.m4a"].state == Recording::State::notDownloaded);
    CHECK (byName["broken.wav"].state == Recording::State::unreadable);
    CHECK (byName["arriving.wav"].state == Recording::State::changing);
    CHECK (byName.count ("inside.wav") == 0);
    CHECK (byName.count (".hidden.wav") == 0);
    CHECK (byName.count ("link-out.wav") == 0);
    CHECK (byName.count ("elsewhere.wav") == 0);
    for (const auto& r : result.recordings)
        if (r.state != Recording::State::ready)
            CHECK (r.problem.isNotEmpty());
    REQUIRE (result.folders.size() == 2);
    CHECK (result.folders[0].readable);
    CHECK (result.folders[0].recordings == 5);
    CHECK_FALSE (result.folders[1].exists);
    CHECK (result.folders[1].message.isNotEmpty());
    // Newest first.
    for (std::size_t i = 1; i < result.recordings.size(); ++i)
        CHECK (result.recordings[i - 1].date >= result.recordings[i].date);
    CHECK (hashOf (memo) == memoHash);
    CHECK_FALSE (root.getChildFile ("CloudRecordings.db").exists());

    const auto empty = scanFolders ({ outside.dir.getChildFile ("none") }, never);
    CHECK (empty.recordings.empty());
}

TEST_CASE ("memo lab: recordings decode exactly, read-only, and refuse what is not audio", "[memo-lab][unit]")
{
    TempDir tmp;
    const auto mono = write (tmp.dir, "mono.wav", testsignals::vowel (220.0, 1.25, 44100.0, 3, 0.5, 1));
    const auto stereo = write (tmp.dir, "stereo.aif", testsignals::vowel (330.0, 0.75, 48000.0, 4, 0.5, 2));
    const auto before = hashOf (mono);
    {
        const auto a = decoded (mono);
        CHECK (a.info.sampleRate == 44100.0);
        CHECK (a.buffer.getNumChannels() == 1);
        CHECK (a.buffer.getNumSamples() == static_cast<int> (std::lround (1.25 * 44100.0)));
        CHECK (a.buffer.getMagnitude (0, a.buffer.getNumSamples()) > 0.1f);
        CHECK_FALSE (a.truncated);
    }
    {
        const auto a = decoded (stereo);
        CHECK (a.buffer.getNumChannels() == 2);
        CHECK (a.buffer.getNumSamples() == 36000);
    }
    DecodedAudio cut;
    juce::String error;
    REQUIRE (decode (mono, cut, error, 0.5));
    CHECK (cut.truncated);
    CHECK (cut.buffer.getNumSamples() == 22050);
    CHECK (hashOf (mono) == before);

    const auto text = tmp.dir.getChildFile ("words.wav");
    text.replaceWithText ("this is not a recording");
    DecodedAudio none;
    CHECK_FALSE (decode (text, none, error));
    CHECK (error.isNotEmpty());
    CHECK_FALSE (decode (tmp.dir.getChildFile ("gone.m4a"), none, error));
    CHECK (error.containsIgnoreCase ("not on this Mac"));
}

#if JUCE_MAC
TEST_CASE ("memo lab: AAC recordings (.m4a, as Voice Memos writes them) decode on macOS", "[memo-lab][mac]")
{
    if (! juce::File ("/usr/bin/afconvert").existsAsFile())
        SKIP ("afconvert is not available");
    TempDir tmp;
    for (const int channels : { 1, 2 })
    {
        const auto wav = write (tmp.dir, "source" + juce::String (channels) + ".wav", testsignals::vowel (220.0, 2.0, 48000.0, 5, 0.5, channels));
        const auto m4a = tmp.dir.getChildFile ("memo" + juce::String (channels) + ".m4a");
        juce::ChildProcess convert;
        REQUIRE (convert.start (juce::StringArray { "/usr/bin/afconvert", "-f", "m4af", "-d", "aac", wav.getFullPathName(), m4a.getFullPathName() }));
        convert.waitForProcessToFinish (30000);
        REQUIRE (m4a.existsAsFile());
        m4a.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::hours (1.0));
        AudioInfo info;
        juce::String error;
        REQUIRE (probe (m4a, info, error));
        CHECK (info.channels == channels);
        CHECK (info.seconds() == Approx (2.0).margin (0.06));
        const auto a = decoded (m4a);
        CHECK (a.buffer.getNumChannels() == channels);
        CHECK (a.buffer.getNumSamples() / a.info.sampleRate == Approx (2.0).margin (0.06));
        CHECK (a.buffer.getRMSLevel (0, 0, a.buffer.getNumSamples()) > 0.05f);
        // The AVFoundation path reads it too (the fallback for .qta and similar).
        DecodedAudio viaAv;
        REQUIRE (decodeWithAVFoundation (m4a, viaAv, error, 900.0));
        CHECK (viaAv.buffer.getNumChannels() == channels);
        CHECK (viaAv.buffer.getNumSamples() / viaAv.info.sampleRate == Approx (2.0).margin (0.06));
        CHECK (viaAv.buffer.getRMSLevel (0, 0, viaAv.buffer.getNumSamples()) > 0.05f);
        const std::atomic<bool> never { false };
        const auto scan = scanFolders ({ tmp.dir }, never);
        bool listed = false;
        for (const auto& r : scan.recordings)
            listed = listed || (r.file == m4a && r.state == Recording::State::ready);
        CHECK (listed);
    }
}
#endif

TEST_CASE ("memo lab: a passage is exactly the frames chosen", "[memo-lab][unit]")
{
    TempDir tmp;
    const auto source = write (tmp.dir, "take.wav", testsignals::vowel (196.0, 2.0, 48000.0, 6, 0.5, 2));
    const auto audio = decoded (source);
    juce::String error;
    const auto file = MemoSession::writePassage (audio, { 12345, 67890 }, "My take", tmp.dir.getChildFile ("Passages"), error);
    REQUIRE (file.existsAsFile());
    CHECK (file.getFileName() == "My take 0.26-1.41 s.wav");
    const auto back = decoded (file);
    REQUIRE (back.buffer.getNumSamples() == 67890 - 12345);
    REQUIRE (back.buffer.getNumChannels() == 2);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < back.buffer.getNumSamples(); i += 97)
            CHECK (back.buffer.getSample (c, i) == audio.buffer.getSample (c, 12345 + i));
    // Whole recording; and a new passage replaces the old one (one conversion at a time).
    const auto whole = MemoSession::writePassage (audio, {}, "My take", tmp.dir.getChildFile ("Passages"), error);
    CHECK (whole.getFileName() == "My take.wav");
    CHECK_FALSE (file.exists());
    CHECK (decoded (whole).buffer.getNumSamples() == audio.buffer.getNumSamples());
    CHECK_FALSE (MemoSession::writePassage (audio, { 0, 100 }, "x", tmp.dir.getChildFile ("Passages"), error).existsAsFile());
    CHECK (error.containsIgnoreCase ("too short"));
}

TEST_CASE ("memo lab: ONE SHOT makes a playable instrument that opens elsewhere", "[memo-lab][integration]")
{
    TempDir tmp;
    const auto source = write (tmp.dir, "Hum.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 7, 0.5, 1));
    const auto before = hashOf (source);
    const auto package = tmp.dir.getChildFile ("Hum.ospinstrument");
    {
        OspAudioProcessor p;
        MemoSession session (p);
        juce::String error;
        REQUIRE (session.convert (recordingFor (source), decoded (source), {}, MemoSession::Mode::oneShot, error));
        const auto s = settle (session);
        CHECK (s.stage == MemoSession::Status::Stage::ready);
        CHECK_FALSE (s.atlas);
        CHECK (s.text.startsWith ("Ready"));
        CHECK (s.pitchRange == "A3");
        CHECK (heldLevelDb (p, 57) > -45.0);
        CHECK (heldLevelDb (p, 69) > -45.0);   // across the keyboard
        CHECK (session.suggestedFileName() == "Hum.ospinstrument");
        REQUIRE (session.save (package, error));
    }
    CHECK (hashOf (source) == before);
    // Another computer: an empty sample store, and neither the recording nor the passage.
    REQUIRE (source.deleteFile());
    MemoSession::passageFolder().deleteRecursively();
    setEnv ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("other-store").getFullPathName());
    OspAudioProcessor q;
    juce::String error;
    REQUIRE (q.importInstrument (package, error));
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    REQUIRE (q.currentInstrument (0) != nullptr);
    CHECK (q.currentInstrument (0)->filename == "Hum.wav");
    CHECK (heldLevelDb (q, 57) > -45.0);
}

TEST_CASE ("memo lab: MAP NOTES maps a sung phrase's notes onto their keys, and the saved instrument keeps them", "[memo-lab][integration]")
{
    TempDir tmp;
    const auto source = write (tmp.dir, "Melody.wav", sungPhrase ({ 57, 62, 66, 69 }));
    const auto before = hashOf (source);
    const auto package = tmp.dir.getChildFile ("Melody (notes).ospinstrument");
    std::size_t notes = 0;
    {
        OspAudioProcessor p;
        MemoSession session (p);
        juce::String error;
        REQUIRE (session.convert (recordingFor (source), decoded (source), {}, MemoSession::Mode::mapNotes, error));
        const auto s = settle (session);
        REQUIRE (s.stage == MemoSession::Status::Stage::ready);
        CHECK (s.atlas);
        CHECK (s.notes >= 3);
        CHECK (s.pitchRange.isNotEmpty());
        const auto instrument = p.currentInstrument (0);
        REQUIRE (instrument != nullptr);
        REQUIRE (instrument->atlas != nullptr);
        notes = instrument->atlas->notes.size();
        bool has62 = false;
        for (const auto& n : instrument->atlas->notes)
            has62 = has62 || (n.included && n.playKey() == 62);
        CHECK (has62);
        // Stage B: every note of 0.1 s or more holds (a loop of its own).
        REQUIRE (instrument->set != nullptr);
        int sustaining = 0;
        for (const auto& m : instrument->set->members)
            sustaining += m.model != nullptr && m.model->original.continuation.canSustain ? 1 : 0;
        CHECK (sustaining == static_cast<int> (instrument->set->members.size()));
        CHECK (heldLevelDb (p, 62) > -45.0);
        REQUIRE (session.save (package, error));
        CHECK (package.existsAsFile());
    }
    CHECK (hashOf (source) == before);
    REQUIRE (source.deleteFile());
    MemoSession::passageFolder().deleteRecursively();
    setEnv ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("fresh-store").getFullPathName());
    OspAudioProcessor q;
    juce::String error;
    REQUIRE (q.importInstrument (package, error));
    REQUIRE (q.waitForLoads (30000));
    q.pollLoads();
    const auto instrument = q.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    CHECK (instrument->atlas->notes.size() == notes);
    CHECK (q.missingSources (0).empty());
    CHECK (heldLevelDb (q, 62) > -45.0);
}

TEST_CASE ("memo lab: a passage of the phrase maps only its own notes", "[memo-lab][integration]")
{
    TempDir tmp;
    const auto source = write (tmp.dir, "Long melody.wav", sungPhrase ({ 57, 62, 66, 69, 72, 74 }));
    const auto audio = decoded (source);
    // The 4th to 6th notes: 0.25 s + 3 * 1.05 s onwards.
    const auto start = static_cast<std::int64_t> ((0.25 + 3 * 1.05 - 0.1) * 48000.0), end = static_cast<std::int64_t> (audio.buffer.getNumSamples());
    OspAudioProcessor p;
    MemoSession session (p);
    juce::String error;
    REQUIRE (session.convert (recordingFor (source), audio, { start, end }, MemoSession::Mode::mapNotes, error));
    const auto s = settle (session);
    REQUIRE (s.atlas);
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument != nullptr);
    for (const auto& n : instrument->atlas->notes)
        if (n.included)
            CHECK (n.playKey() >= 68);
    CHECK (juce::String (instrument->filename).endsWith ("Long melody 3.30-6.55 s.wav"));
}

TEST_CASE ("memo lab: an unpitched recording does not map, and plays as one sample instead", "[memo-lab][integration]")
{
    TempDir tmp;
    const auto source = write (tmp.dir, "Wind.wav", testsignals::whiteNoise (3.0, 48000.0, 0.3, 11));
    OspAudioProcessor p;
    MemoSession session (p);
    juce::String error;
    REQUIRE (session.convert (recordingFor (source), decoded (source), {}, MemoSession::Mode::mapNotes, error));
    const auto s = settle (session);
    CHECK (s.stage == MemoSession::Status::Stage::unsuitable);
    CHECK_FALSE (s.atlas);
    CHECK (s.text.containsIgnoreCase ("not suitable"));
    CHECK (s.canPlay());
    CHECK (heldLevelDb (p, 60) > -60.0);
    CHECK (session.save (tmp.dir.getChildFile ("Wind.ospinstrument"), error));
}

TEST_CASE ("memo lab: nothing to save before an instrument exists; one conversion at a time", "[memo-lab][unit]")
{
    TempDir tmp;
    OspAudioProcessor p;
    MemoSession session (p);
    juce::String error;
    CHECK_FALSE (session.save (tmp.dir.getChildFile ("x.ospinstrument"), error));
    CHECK (error.isNotEmpty());
    const auto source = write (tmp.dir, "Melody.wav", sungPhrase ({ 57, 62, 66 }));
    const auto audio = decoded (source);
    REQUIRE (session.convert (recordingFor (source), audio, {}, MemoSession::Mode::mapNotes, error));
    if (session.busy())
    {
        CHECK_FALSE (session.convert (recordingFor (source), audio, {}, MemoSession::Mode::oneShot, error));
        CHECK (error.containsIgnoreCase ("still"));
    }
    settle (session);
}

TEST_CASE ("memo lab: playing a mapped memo allocates nothing on the audio thread", "[memo-lab][integration]")
{
    TempDir tmp;
    const auto source = write (tmp.dir, "Melody.wav", sungPhrase ({ 57, 62, 66, 69 }));
    OspAudioProcessor p;
    MemoSession session (p);
    juce::String error;
    REQUIRE (session.convert (recordingFor (source), decoded (source), {}, MemoSession::Mode::mapNotes, error));
    REQUIRE (settle (session).atlas);
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> block (2, 256);
    juce::MidiBuffer midi;
    auto run = [&] (int blocks, bool count) {
        int allocations = 0;
        for (int b = 0; b < blocks; ++b)
        {
            midi.clear();
            if (b % 20 == 0)
                for (const int key : { 57, 62, 66, 69 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, key, 0.7f), b % 7);
            if (b % 20 == 13)
                for (const int key : { 57, 62, 66, 69 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, key), 3);
            block.clear();
            test::AllocationScope scope;
            p.processBlock (block, midi);
            if (count)
                allocations += scope.count();
        }
        return allocations;
    };
    run (60, false);   // first notes (voices, tables) warm
    CHECK (run (200, true) == 0);
    midi.clear();
    for (int key = 0; key < 128; ++key)
        midi.addEvent (juce::MidiMessage::noteOff (1, key), 0);
    p.processBlock (block, midi);
}

TEST_CASE ("memo lab: the window lists, reads, maps and shows the instrument", "[memo-lab][ui]")
{
    TempDir tmp;
    const auto folder = tmp.dir.getChildFile ("My recordings");
    folder.createDirectory();
    write (folder, "20241030 143012-AB12CD34.wav", sungPhrase ({ 57, 62, 66, 69, 71 }, 48000.0, 1));
    write (folder, "20241029 091500-FF00AA11.wav", testsignals::pluck (220.0, 1.5, 48000.0, 3, 0.7, 1));
    folder.getChildFile (".20241031 100000-0000.m4a.icloud").replaceWithText ("placeholder");

    SourceSettings settings (tmp.dir.getChildFile ("settings.json"));
    settings.setOfferedVoiceMemos();
    settings.addFolder (folder);
    OspAudioProcessor p;
    osp::plugin::OspLookAndFeel look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    {
        MemoLabComponent ui (p, nullptr, settings);
        ui.setSize (1240, 820);
        REQUIRE (ui.waitForIdle (30000));
        REQUIRE (ui.recordings().size() == 3);
        const char* shots = std::getenv ("MEMO_LAB_SHOTS");
        auto shoot = [&] (const juce::String& name) {
            if (shots == nullptr)
                return;
            ui.tick();
            for (const float scale : { 1.0f, 2.0f })
            {
                juce::FileOutputStream out (juce::File (juce::String::fromUTF8 (shots)).getChildFile (name + (scale > 1.5f ? "@2x.png" : "@1x.png")));
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (ui.createComponentSnapshot (ui.getLocalBounds(), true, scale), out);
            }
        };
        shoot ("01-recordings");
        int melody = -1;
        for (std::size_t i = 0; i < ui.recordings().size(); ++i)
            if (ui.recordings()[i].file.getFileName().startsWith ("20241030"))
                melody = static_cast<int> (i);
        REQUIRE (melody >= 0);
        ui.selectRecording (melody);
        REQUIRE (ui.waitForIdle (30000));
        ui.waveform().setSelection ({ static_cast<std::int64_t> (0.1 * 48000.0), static_cast<std::int64_t> (4.3 * 48000.0) });
        shoot ("02-selected-passage");
        ui.convert (MemoSession::Mode::mapNotes);
        REQUIRE (ui.waitForIdle (60000));
        CHECK (ui.session().status().stage == MemoSession::Status::Stage::ready);
        CHECK (ui.session().status().atlas);
        {
            const auto instrument = p.currentInstrument (0);
            REQUIRE (instrument != nullptr);
            REQUIRE (instrument->set != nullptr);
            int sustaining = 0;
            for (const auto& m : instrument->set->members)
                sustaining += m.model != nullptr && m.model->original.continuation.canSustain ? 1 : 0;
            CHECK (sustaining == static_cast<int> (instrument->set->members.size()));
            CHECK ((instrument->atlasStatus == nullptr || ! instrument->atlasStatus->playableOnly));
        }
        shoot ("03-mapped-instrument");
        // The plucked recording as ONE SHOT.
        for (std::size_t i = 0; i < ui.recordings().size(); ++i)
            if (ui.recordings()[i].file.getFileName().startsWith ("20241029"))
                ui.selectRecording (static_cast<int> (i));
        REQUIRE (ui.waitForIdle (30000));
        ui.convert (MemoSession::Mode::oneShot);
        REQUIRE (ui.waitForIdle (60000));
        CHECK (ui.session().status().stage == MemoSession::Status::Stage::ready);
        CHECK_FALSE (ui.session().status().atlas);
        shoot ("04-one-shot");
        // The placeholder: listed, with what to do about it.
        for (std::size_t i = 0; i < ui.recordings().size(); ++i)
            if (ui.recordings()[i].state == Recording::State::notDownloaded)
                ui.selectRecording (static_cast<int> (i));
        ui.waitForIdle (5000);
        shoot ("05-not-downloaded");
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST_CASE ("memo lab: with no folder it can read, the window says so plainly", "[memo-lab][ui]")
{
    TempDir tmp;
    SourceSettings settings (tmp.dir.getChildFile ("settings.json"));
    settings.setOfferedVoiceMemos();
    settings.addFolder (tmp.dir.getChildFile ("Voice Memos that are not here"));
    OspAudioProcessor p;
    osp::plugin::OspLookAndFeel look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    MemoLabComponent ui (p, nullptr, settings);
    ui.setSize (1240, 820);
    REQUIRE (ui.waitForIdle (10000));
    CHECK (ui.recordings().empty());
    if (const char* shots = std::getenv ("MEMO_LAB_SHOTS"))
    {
        juce::FileOutputStream out (juce::File (juce::String::fromUTF8 (shots)).getChildFile ("00-no-folder@1x.png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f), out);
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST_CASE ("memo lab: how long a typical memo takes (run on demand)", "[.][memo-lab][timing]")
{
    // A 10-second sung melody (ten notes) and a 2.5-second hum, timed from the button to
    // playable (Stage A) and to ready (Stage B).
    TempDir tmp;
    const auto melody = write (tmp.dir, "Melody.wav", sungPhrase ({ 57, 59, 61, 62, 64, 66, 68, 69, 71, 73 }));
    const auto hum = write (tmp.dir, "Hum.wav", testsignals::vowel (midiToHz (57), 2.5, 48000.0, 7, 0.5, 1));
    for (const auto& [file, mode] : { std::pair { melody, MemoSession::Mode::mapNotes }, std::pair { hum, MemoSession::Mode::oneShot } })
    {
        OspAudioProcessor p;
        MemoSession session (p);
        const auto audio = decoded (file);
        juce::String error;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        REQUIRE (session.convert (recordingFor (file), audio, {}, mode, error));
        double playable = -1.0;
        while (true)
        {
            p.pollLoads();
            const auto s = session.update();
            if (playable < 0.0 && s.canPlay())
                playable = juce::Time::getMillisecondCounterHiRes() - t0;
            if (s.stage != MemoSession::Status::Stage::building && s.stage != MemoSession::Status::Stage::playable && ! session.busy())
                break;
            juce::Thread::sleep (2);
        }
        const auto ready = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("%s %s: playable after %.0f ms, ready after %.0f ms (%d notes)\n", file.getFileName().toRawUTF8(),
                     mode == MemoSession::Mode::mapNotes ? "MAP NOTES" : "ONE SHOT", playable, ready, session.status().notes);
    }
}

int main (int argc, char* argv[])
{
    // Every store and folder the processor or Memo Lab would use, isolated for this run.
    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("memo-lab-tests-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    setEnv ("OSP_SAMPLE_STORE", base.getChildFile ("store").getFullPathName());
    setEnv ("OSP_LIBRARY_DIR", base.getChildFile ("library").getFullPathName());
    setEnv ("OSP_DOCUMENTS_DIR", base.getChildFile ("documents").getFullPathName());
    setEnv ("MEMO_LAB_DATA_DIR", base.getChildFile ("memo-lab").getFullPathName());
    juce::ScopedJuceInitialiser_GUI juceInit;
    const int result = Catch::Session().run (argc, argv);
    base.deleteRecursively();
    return result;
}

//==============================================================================
TEST_CASE ("memo lab: audition starts at a recording's first sound", "[memo-lab][unit][audition]")
{
    const double rate = 48000.0;
    auto make = [rate] (double silence, double tone) {
        DecodedAudio a;
        a.info.sampleRate = rate;
        a.info.channels = 1;
        const int n = static_cast<int> ((silence + tone) * rate);
        a.buffer.setSize (1, n);
        juce::Random noise (7);
        for (int i = 0; i < n; ++i)
        {
            const bool sounding = i >= static_cast<int> (silence * rate);
            // handling noise at -70 dBFS before the sound, then a tone at about -12 dBFS
            a.buffer.setSample (0, i, sounding ? 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * static_cast<float> (i) / static_cast<float> (rate))
                                               : 0.0003f * (noise.nextFloat() * 2.0f - 1.0f));
        }
        a.info.frames = n;
        return a;
    };
    const auto late = make (2.5, 1.0);
    CHECK (firstSoundFrame (late) / rate == Approx (2.5 - 0.08).margin (0.025));
    CHECK (firstSoundFrame (make (0.0, 1.0)) == 0);
    CHECK (firstSoundFrame (make (3.0, 0.0)) == 0);   // nothing sounds: from the start
}

TEST_CASE ("memo lab: audition browses with the arrows and plays each recording from its first sound", "[memo-lab][ui][audition]")
{
    TempDir tmp;
    const auto folder = tmp.dir.getChildFile ("Memos");
    folder.createDirectory();
    const double rate = 48000.0;
    // A long memo (read in two steps: its opening first) whose sound starts after 3 s of silence.
    auto longMemo = AudioData::allocate (1, static_cast<std::int64_t> (90.0 * rate), rate);
    const auto tone = testsignals::vowel (midiToHz (60), 87.0, rate, 5, 0.5, 1);
    for (std::size_t i = 0; i < tone.channels[0].size(); ++i)
        longMemo.channels[0][static_cast<std::size_t> (3.0 * rate) + i] = tone.channels[0][i];
    write (folder, "20241030 120000-AAAA0001.wav", longMemo);
    write (folder, "20241029 120000-AAAA0002.wav", sungPhrase ({ 60, 64 }, rate, 1));
    write (folder, "20241028 120000-AAAA0003.wav", testsignals::pluck (220.0, 1.5, rate, 3, 0.7, 1));
    SourceSettings settings (tmp.dir.getChildFile ("settings.json"));
    settings.setOfferedVoiceMemos();
    settings.addFolder (folder);
    OspAudioProcessor p;
    osp::plugin::OspLookAndFeel look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    {
        MemoLabComponent ui (p, nullptr, settings);
        ui.setSize (1240, 820);
        REQUIRE (ui.waitForIdle (30000));
        REQUIRE (ui.recordings().size() == 3);
        ui.audition (true);   // nothing chosen yet: the newest is chosen and heard
        REQUIRE (ui.waitForIdle (30000));
        CHECK (ui.isAuditioning());
        CHECK (ui.preview().isPlaying());
        // (No audio device in the test: the transport stays where it was started.)
        CHECK (ui.preview().position() / rate == Approx (3.0 - 0.08).margin (0.03));
        REQUIRE (ui.currentAudio() != nullptr);
        CHECK_FALSE (ui.currentAudioIsPartial());   // the whole recording followed its opening
        CHECK (ui.currentAudio()->buffer.getNumSamples() == static_cast<int> (90.0 * rate));
        // Down: the next recording, playing at once; Up: back.
        CHECK (ui.keyPressed (juce::KeyPress (juce::KeyPress::downKey)));
        REQUIRE (ui.waitForIdle (30000));
        CHECK (ui.preview().isPlaying());
        CHECK (ui.recordings()[1].file.getFileName().startsWith ("20241029"));
        CHECK (ui.currentAudio()->buffer.getNumSamples() < static_cast<int> (10.0 * rate));
        // Right: 10 s on (clamped to the recording).
        const auto before = ui.preview().position();
        CHECK (ui.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
        CHECK (ui.preview().position() > before);
        CHECK (ui.keyPressed (juce::KeyPress (juce::KeyPress::upKey)));
        REQUIRE (ui.waitForIdle (30000));
        CHECK (ui.currentAudio()->buffer.getNumSamples() == static_cast<int> (90.0 * rate));
        // Off: it stops.
        ui.audition (false);
        CHECK_FALSE (ui.preview().isPlaying());
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST_CASE ("memo lab: Z and X move the Mac keys an octave, and a new instrument centres them", "[memo-lab][ui]")
{
    TempDir tmp;
    const auto folder = tmp.dir.getChildFile ("Memos");
    folder.createDirectory();
    write (folder, "20241030 143012-AB12CD34.wav", sungPhrase ({ 43, 47, 50, 55 }, 48000.0, 1));   // G2 .. G3
    SourceSettings settings (tmp.dir.getChildFile ("settings.json"));
    settings.setOfferedVoiceMemos();
    settings.addFolder (folder);
    OspAudioProcessor p;
    osp::plugin::OspLookAndFeel look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    {
        MemoLabComponent ui (p, nullptr, settings);
        ui.setSize (1240, 820);
        REQUIRE (ui.waitForIdle (30000));
        CHECK (ui.keyOctave() == 5);
        CHECK (ui.keyRangeText() == juce::String (midiNoteName (60)) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (midiNoteName (76)));
        CHECK (ui.keyPressed (juce::KeyPress ('x', 0, 'x')));
        CHECK (ui.keyOctave() == 6);
        CHECK (ui.keyPressed (juce::KeyPress ('z', 0, 'z')));
        CHECK (ui.keyPressed (juce::KeyPress ('z', 0, 'z')));
        CHECK (ui.keyOctave() == 4);
        for (int i = 0; i < 12; ++i)
            ui.keyPressed (juce::KeyPress ('z', 0, 'z'));
        CHECK (ui.keyOctave() == 0);   // clamped
        // A mapped instrument from G2 to G3 puts the Mac keys over it (A = C2 .. ; = E3... in octave 3 or 4).
        ui.selectRecording (0);
        REQUIRE (ui.waitForIdle (30000));
        ui.convert (MemoSession::Mode::mapNotes);
        REQUIRE (ui.waitForIdle (60000));
        const int low = 12 * ui.keyOctave(), high = low + 16;
        CHECK (low <= 49);    // the middle of G2..G3 is within reach
        CHECK (high >= 49);
        CHECK (ui.keyOctave() == 3);
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

//==============================================================================
// The library scan's sorting (MemoClassifier).
#include "MemoClassifier.h"

namespace
{
    MemoProfile profileOf (const AudioData& a)
    {
        std::vector<float> mono (a.channels[0].size(), 0.0f);
        for (const auto& ch : a.channels)
            for (std::size_t i = 0; i < mono.size(); ++i)
                mono[i] += ch[i] / static_cast<float> (a.channels.size());
        return profileRecording (mono.data(), static_cast<std::int64_t> (mono.size()), a.sampleRate);
    }

    std::string describe (const MemoProfile& p)
    {
        std::string s = std::string (soundClassName (p.label)) + " (" + std::to_string (juce::roundToInt (100.0f * p.confidence)) + " %):";
        for (int c = 0; c < soundClassCount; ++c)
            s += std::string (" ") + soundClassId (static_cast<SoundClass> (c)) + " " + std::to_string (juce::roundToInt (100.0f * p.share[static_cast<std::size_t> (c)]));
        return s;
    }
}

TEST_CASE ("memo lab: the library scan profiles every file in OSP_CLASSIFY_DIR (run on demand)", "[.][memo-lab][classify-files]")
{
    const char* dir = std::getenv ("OSP_CLASSIFY_DIR");
    if (dir == nullptr)
        return;
    for (const auto& entry : juce::RangedDirectoryIterator (juce::File (juce::String::fromUTF8 (dir)), false, "*.wav;*.aif;*.aiff;*.m4a;*.flac"))
    {
        DecodedAudio audio;
        juce::String error;
        if (! decode (entry.getFile(), audio, error))
            continue;
        std::vector<float> mono (static_cast<std::size_t> (audio.buffer.getNumSamples()), 0.0f);
        for (int c = 0; c < audio.buffer.getNumChannels(); ++c)
            for (int i = 0; i < audio.buffer.getNumSamples(); ++i)
                mono[static_cast<std::size_t> (i)] += audio.buffer.getSample (c, i) / static_cast<float> (audio.buffer.getNumChannels());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto p = profileRecording (mono.data(), static_cast<std::int64_t> (mono.size()), audio.info.sampleRate);
        std::printf ("%-48s %6.1f s  %5.0f ms  %s  music@%.0f\n", entry.getFile().getFileName().substring (0, 48).toRawUTF8(), p.durationSeconds,
                     juce::Time::getMillisecondCounterHiRes() - t0, describe (p).c_str(), p.firstMusicSeconds);
    }
}

namespace
{
    constexpr double sr = 48000.0;
    AudioData silence (double seconds) { return AudioData::allocate (1, static_cast<std::int64_t> (seconds * sr), sr); }

    /** Speech-like: syllables of 80-220 ms, the pitch gliding down through each (150-220 Hz),
        a few harmonics, short gaps with a little breath noise between them. */
    AudioData talking (double seconds, std::uint64_t seed)
    {
        auto a = silence (seconds);
        Prng rng (seed);
        double t = 0.1, phase = 0.0;
        while (t < seconds - 0.4)
        {
            const double len = rng.uniform (0.08, 0.22), gap = rng.uniform (0.05, 0.18);
            const double f0 = rng.uniform (150.0, 220.0), drop = rng.uniform (0.75, 0.9);
            const auto start = static_cast<std::size_t> (t * sr), n = static_cast<std::size_t> (len * sr);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double u = static_cast<double> (i) / static_cast<double> (n);
                const double hz = f0 * (1.0 - (1.0 - drop) * u);
                phase += 2.0 * std::numbers::pi * hz / sr;
                const double env = std::sin (std::numbers::pi * u);
                a.channels[0][start + i] = static_cast<float> (0.25 * env * (std::sin (phase) + 0.5 * std::sin (2.0 * phase) + 0.3 * std::sin (3.0 * phase)));
            }
            const auto gs = start + n, gn = static_cast<std::size_t> (gap * sr);
            for (std::size_t i = 0; i < gn && gs + i < a.channels[0].size(); ++i)
                a.channels[0][gs + i] = static_cast<float> (0.004 * rng.bipolar());
            t += len + gap;
        }
        return a;
    }

    /** Humming: held dark notes (a fundamental and a soft second harmonic) with a natural
        slow vibrato, one after another. */
    AudioData humming (std::vector<int> keys, double noteSeconds)
    {
        auto a = silence (static_cast<double> (keys.size()) * noteSeconds + 0.4);
        double phase = 0.0;
        for (std::size_t k = 0; k < keys.size(); ++k)
        {
            const auto start = static_cast<std::size_t> ((0.2 + static_cast<double> (k) * noteSeconds) * sr), n = static_cast<std::size_t> (noteSeconds * sr);
            for (std::size_t i = 0; i < n; ++i)
            {
                const double tt = static_cast<double> (i) / sr;
                const double hz = midiToHz (keys[k]) * std::exp2 (0.18 / 12.0 * std::sin (2.0 * std::numbers::pi * 5.2 * tt) + 0.06 / 12.0 * std::sin (2.0 * std::numbers::pi * 0.7 * tt));
                phase += 2.0 * std::numbers::pi * hz / sr;
                const double env = std::min (1.0, std::min (tt / 0.08, (noteSeconds - tt) / 0.08));
                a.channels[0][start + i] = static_cast<float> (0.3 * env * (std::sin (phase) + 0.12 * std::sin (2.0 * phase)));
            }
        }
        return a;
    }

    /** A jam: chords of several plucked strings, restruck four times a second, without a break. */
    AudioData jam (double seconds)
    {
        auto a = silence (seconds);
        const std::vector<std::vector<int>> chords { { 48, 55, 64, 67, 72 }, { 45, 52, 60, 64, 69 }, { 50, 57, 62, 65, 69 }, { 43, 50, 59, 62, 67 } };
        int c = 0;
        for (double t = 0.0; t < seconds - 0.5; t += 0.25, ++c)
            for (const int key : chords[static_cast<std::size_t> ((c / 8) % 4)])
            {
                const auto p = testsignals::pluck (midiToHz (key), 0.6, sr, static_cast<std::uint64_t> (c * 7 + key), 0.18, 1);
                const auto start = static_cast<std::size_t> (t * sr);
                for (std::size_t i = 0; i < p.channels[0].size() && start + i < a.channels[0].size(); ++i)
                    a.channels[0][start + i] += p.channels[0][i];
            }
        return a;
    }
}

TEST_CASE ("memo lab: the library scan tells talking, humming, phrases, jams, noise and quiet apart", "[memo-lab][unit][classify]")
{
    auto check = [] (const AudioData& a, SoundClass expected, const char* what) {
        const auto p = profileOf (a);
        INFO (what << ": " << describe (p));
        CHECK (p.label == expected);
        CHECK (p.confidence >= 0.6f);
        return p;
    };
    check (talking (20.0, 3), SoundClass::talk, "talking");
    const auto hum = check (humming ({ 57, 60, 62, 64, 60, 57 }, 2.0), SoundClass::hum, "humming");
    CHECK (hum.firstMusicSeconds >= 0.0);
    check (sungPhrase ({ 57, 62, 66, 69, 71, 69, 66, 62 }, sr, 1), SoundClass::phrase, "sung phrase (vowels)");
    check (testsignals::pluck (220.0, 1.5, sr, 3, 0.7, 1), SoundClass::phrase, "a plucked note (not a hum: it decays)");
    check (jam (20.0), SoundClass::jam, "jam");
    {
        auto noise = silence (10.0);
        Prng rng (9);
        for (auto& x : noise.channels[0])
            x = static_cast<float> (0.1 * rng.bipolar());
        check (noise, SoundClass::noise, "noise");
    }
    {
        auto quiet = silence (10.0);
        Prng rng (4);
        for (auto& x : quiet.channels[0])
            x = static_cast<float> (0.00002 * rng.bipolar());   // -94 dBFS of hiss
        const auto p = profileOf (quiet);
        CHECK (p.label == SoundClass::quiet);
        CHECK (p.tags() == std::vector<SoundClass> { SoundClass::quiet });
    }
    // Talking, then music: two tags, and the music's first second.
    {
        auto talkFirst = talking (12.0, 5);
        const auto tune = humming ({ 60, 62, 64, 65, 67, 65, 64, 62 }, 1.5);
        talkFirst.channels[0].insert (talkFirst.channels[0].end(), tune.channels[0].begin(), tune.channels[0].end());
        const auto p = profileOf (talkFirst);
        INFO (describe (p));
        const auto tags = p.tags();
        CHECK (std::find (tags.begin(), tags.end(), SoundClass::talk) != tags.end());
        CHECK (std::find (tags.begin(), tags.end(), SoundClass::hum) != tags.end());
        CHECK (p.firstMusicSeconds >= 11.0);
        CHECK (p.firstMusicSeconds <= 14.0);
    }
    // Odd input never fails: nothing, a sliver, another sample rate.
    CHECK (profileRecording (nullptr, 0, 48000.0).label == SoundClass::quiet);
    {
        const std::vector<float> sliver (100, 0.5f);
        CHECK_NOTHROW (profileRecording (sliver.data(), 100, 44100.0));
    }
    {
        const auto hum441 = humming ({ 55, 57, 59 }, 2.0);
        std::vector<float> resampled;
        for (double t = 0.0; t < static_cast<double> (hum441.channels[0].size() - 1); t += sr / 44100.0)
            resampled.push_back (hum441.channels[0][static_cast<std::size_t> (t)]);
        CHECK (profileRecording (resampled.data(), static_cast<std::int64_t> (resampled.size()), 44100.0).label == SoundClass::hum);
    }
}

TEST_CASE ("memo lab: Scan sorts the library on request, filters by kind, remembers it and can stop", "[memo-lab][ui][classify]")
{
    TempDir tmp;
    setEnv ("MEMO_LAB_DATA_DIR", tmp.dir.getChildFile ("data").getFullPathName());   // the scan's cache
    const auto folder = tmp.dir.getChildFile ("Memos");
    folder.createDirectory();
    auto loudJam = jam (20.0);
    for (auto& x : loudJam.channels[0])
        x *= 0.5f;   // no clipping in the file
    write (folder, "20241030 120000-AAAA0001.wav", talking (20.0, 3));
    write (folder, "20241029 120000-AAAA0002.wav", humming ({ 57, 60, 62, 64, 60, 57 }, 2.0));
    write (folder, "20241028 120000-AAAA0003.wav", loudJam);
    SourceSettings settings (tmp.dir.getChildFile ("settings.json"));
    settings.setOfferedVoiceMemos();
    settings.addFolder (folder);
    OspAudioProcessor p;
    osp::plugin::OspLookAndFeel look;
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    auto labelOf = [] (const MemoLabComponent& ui, const juce::String& prefix) {
        for (const auto& r : ui.recordings())
            if (r.file.getFileName().startsWith (prefix))
                if (const auto* profile = ui.profileOf (r))
                    return static_cast<int> (profile->label);
        return -1;
    };
    {
        MemoLabComponent ui (p, nullptr, settings);
        ui.setSize (1240, 820);
        REQUIRE (ui.waitForIdle (30000));
        REQUIRE (ui.recordings().size() == 3);
        // Nothing is analysed until asked.
        for (const auto& r : ui.recordings())
            CHECK (ui.profileOf (r) == nullptr);
        // Stop at once: the scan ends early and nothing breaks.
        ui.startScan();
        CHECK (ui.isScanningLibrary());
        ui.stopScan();
        REQUIRE (ui.waitForIdle (30000));
        CHECK_FALSE (ui.isScanningLibrary());

        ui.startScan();
        REQUIRE (ui.waitForIdle (60000));
        CHECK_FALSE (ui.isScanningLibrary());
        CHECK (labelOf (ui, "20241030") == static_cast<int> (SoundClass::talk));
        CHECK (labelOf (ui, "20241029") == static_cast<int> (SoundClass::hum));
        CHECK (labelOf (ui, "20241028") == static_cast<int> (SoundClass::jam));
        ui.setCategoryFilter (static_cast<int> (SoundClass::hum));
        REQUIRE (ui.recordings().size() == 1);
        CHECK (ui.recordings()[0].file.getFileName().startsWith ("20241029"));
        ui.setCategoryFilter (soundClassCount);   // not scanned yet: none
        CHECK (ui.recordings().empty());
        ui.setCategoryFilter (-1);
        CHECK (ui.recordings().size() == 3);
        if (const char* shots = std::getenv ("MEMO_LAB_SHOTS"))
        {
            ui.selectRecording (0);
            REQUIRE (ui.waitForIdle (30000));
            ui.tick();
            juce::FileOutputStream out (juce::File (juce::String::fromUTF8 (shots)).getChildFile ("07-library-scan@1x.png"));
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f), out);
        }
    }
    CHECK (tmp.dir.getChildFile ("data").getChildFile ("library-scan.json").existsAsFile());
    // A new window remembers the scan; a changed recording is scanned again.
    folder.getChildFile ("20241030 120000-AAAA0001.wav").setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::minutes (30.0));
    {
        MemoLabComponent ui (p, nullptr, settings);
        ui.setSize (1240, 820);
        REQUIRE (ui.waitForIdle (30000));
        CHECK (labelOf (ui, "20241029") == static_cast<int> (SoundClass::hum));
        CHECK (labelOf (ui, "20241028") == static_cast<int> (SoundClass::jam));
        CHECK (labelOf (ui, "20241030") == -1);
        ui.setCategoryFilter (soundClassCount);
        CHECK (ui.recordings().size() == 1);
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}
