#include "research/AtlasFixtures.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/ContentHash.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace osp::research::atlasfixtures
{

namespace
{
    constexpr double rate = 48000.0;

    /** Lays segments end to end (optionally with silence between) and records each note's place. */
    struct Recording
    {
        AudioData audio;
        struct Placed
        {
            std::int64_t start, end;
            double midi;
        };
        std::vector<Placed> placed;

        explicit Recording (int channels) { audio = AudioData::allocate (channels, 0, rate); }

        void gap (double seconds)
        {
            const auto n = static_cast<std::size_t> (seconds * rate);
            for (auto& ch : audio.channels)
                ch.insert (ch.end(), n, 0.0f);
        }
        void note (const AudioData& segment, double midi)
        {
            const auto start = audio.numFrames();
            for (std::size_t c = 0; c < audio.channels.size(); ++c)
            {
                const auto& from = segment.channels[std::min (c, segment.channels.size() - 1)];
                audio.channels[c].insert (audio.channels[c].end(), from.begin(), from.end());
            }
            placed.push_back ({ start, audio.numFrames(), midi });
        }
    };

    AudioData vowelNote (double midi, double seconds, std::uint64_t seed, double amplitude, int channels)
    {
        auto a = testsignals::vowel (midiToHz (midi), seconds, rate, seed, amplitude, channels);
        testsignals::applyFades (a, 0.01, 0.06);
        return a;
    }

    /** A bowed string stand-in: a saw with a slow swell, light vibrato-free body and bow noise. */
    AudioData bowedNote (double midi, double seconds, std::uint64_t seed)
    {
        auto a = testsignals::saw (midiToHz (midi), seconds, rate, 0.3, 2);
        const auto noise = testsignals::whiteNoise (seconds, rate, 0.02, seed, 2);
        const auto n = a.numFrames();
        for (int c = 0; c < 2; ++c)
            for (std::int64_t i = 0; i < n; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                const double swell = std::min (1.0, t / 0.12) * std::min (1.0, (seconds - t) / 0.08);
                auto& v = a.channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)];
                v = static_cast<float> ((v + noise.channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)]) * std::max (0.0, swell));
            }
        return a;
    }

    atlas::NoteEvent noteAt (const atlas::NoteAtlas& a, const Recording::Placed& p, double midi)
    {
        atlas::NoteEvent n;
        n.id = a.nextNoteId();
        n.sourceId = "src0";
        n.sourceStart = p.start;
        n.sourceEnd = p.end;
        n.attackStart = p.start;
        const auto len = p.end - p.start;
        n.sustain = { p.start + len / 10, p.end - len / 6 };
        n.releaseStart = n.sustain.end;
        n.midiPitch = midi;
        n.pitchHz = midiToHz (midi);
        const auto r = atlas::roundPitch (midi);
        n.rootMidi = r.key;
        n.centsOffset = r.cents;
        n.confidence = 1.0;
        n.voicing = 1.0;
        n.origin = atlas::NoteOrigin::user;
        return n;
    }

    Fixture finish (std::string name, std::string description, Recording& rec, atlas::NoteAtlas atlas)
    {
        Fixture f;
        f.name = std::move (name);
        f.description = std::move (description);
        atlas.atlasId = "atlas:fixture-" + f.name;
        atlas.createdBy = "OSP atlas fixtures";
        atlas.sources = { assetFor ("src0", f.name + ".wav", rec.audio) };
        auto audio = std::make_shared<AudioData> (std::move (rec.audio));
        f.audio = { { "src0", std::move (audio) } };
        atlas::remapNotes (atlas);
        f.atlas = std::move (atlas);
        return f;
    }

    Fixture simplePhrase (const char* name, const char* description, int channels, const std::vector<std::pair<double, std::uint64_t>>& notes,
                          double seconds, double gapSeconds, double amplitude = 0.5)
    {
        Recording rec (channels);
        rec.gap (0.05);
        for (const auto& [midi, seed] : notes)
        {
            rec.note (vowelNote (midi, seconds, seed, amplitude, channels), midi);
            rec.gap (gapSeconds);
        }
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
            a.notes.push_back (noteAt (a, p, p.midi));
        return finish (name, description, rec, std::move (a));
    }
}

std::string contentHashOf (const AudioData& audio)
{
    std::vector<float> all;
    for (const auto& ch : audio.channels)
        all.insert (all.end(), ch.begin(), ch.end());
    return io::contentId (io::sha256OfBytes (all.data(), all.size() * sizeof (float)));
}

atlas::SourceAsset assetFor (const std::string& id, const std::string& name, const AudioData& audio, atlas::AssetOrigin origin)
{
    atlas::SourceAsset s;
    s.assetId = id;
    s.contentHash = contentHashOf (audio);
    s.origin = origin;
    s.fileName = name;
    s.sampleRate = audio.sampleRate;
    s.numChannels = std::min (audio.numChannels(), 2);
    s.numFrames = audio.numFrames();
    return s;
}

std::vector<std::string> names()
{
    return { "sung-four", "repeated-note", "velocity-layers", "bowed-phrase", "silence-gaps", "legato", "stereo-wide", "out-of-tune",
             "missing-notes", "boundary-probe" };
}

Fixture make (const std::string& name)
{
    if (name == "sung-four")
        return simplePhrase ("sung-four", "a sung phrase: A3, C4, E4, G4 with short gaps", 2, { { 57.0, 3 }, { 60.0, 4 }, { 64.0, 5 }, { 67.0, 6 } }, 0.8, 0.12);
    if (name == "repeated-note")
    {
        // Three takes of D4, each a little different (seed, level): round robin.
        Recording rec (2);
        rec.gap (0.05);
        const double amps[] = { 0.45, 0.5, 0.55 };
        for (int t = 0; t < 3; ++t)
        {
            rec.note (vowelNote (62.0, 0.7, static_cast<std::uint64_t> (11 + t), amps[t], 2), 62.0);
            rec.gap (0.15);
        }
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
            a.notes.push_back (noteAt (a, p, p.midi));
        return finish ("repeated-note", "three recordings of D4", rec, std::move (a));
    }
    if (name == "velocity-layers")
    {
        Recording rec (2);
        rec.gap (0.05);
        const double amps[] = { 0.1, 0.11, 0.5, 0.55 };
        for (int t = 0; t < 4; ++t)
        {
            rec.note (vowelNote (60.0, 0.6, static_cast<std::uint64_t> (21 + t), amps[t], 2), 60.0);
            rec.gap (0.1);
        }
        atlas::NoteAtlas a;
        for (std::size_t i = 0; i < rec.placed.size(); ++i)
        {
            auto n = noteAt (a, rec.placed[i], 60.0);
            n.velocityLayer = i < 2 ? 0 : 1;
            a.notes.push_back (n);
        }
        return finish ("velocity-layers", "C4 soft (two takes) and loud (two takes)", rec, std::move (a));
    }
    if (name == "bowed-phrase")
    {
        Recording rec (2);
        rec.gap (0.03);
        const double notes[] = { 55.0, 59.0, 62.0 };
        for (int i = 0; i < 3; ++i)
        {
            rec.note (bowedNote (notes[i], 0.55, static_cast<std::uint64_t> (31 + i)), notes[i]);
            rec.gap (0.03);
        }
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
            a.notes.push_back (noteAt (a, p, p.midi));
        return finish ("bowed-phrase", "a short bowed phrase: G3, B3, D4", rec, std::move (a));
    }
    if (name == "silence-gaps")
    {
        Recording rec (1);
        rec.gap (0.4);
        const double notes[] = { 48.0, 52.0, 55.0 };
        for (int i = 0; i < 3; ++i)
        {
            rec.note (testsignals::pluck (midiToHz (notes[i]), 0.7, rate, static_cast<std::uint64_t> (41 + i), 0.6, 1), notes[i]);
            rec.gap (0.6);
        }
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
            a.notes.push_back (noteAt (a, p, p.midi));
        return finish ("silence-gaps", "mono plucks C3, E3, G3 with long silences between", rec, std::move (a));
    }
    if (name == "legato")
    {
        // One continuous tone stepping C4, D4, E4, F4: each region ends on the frame the next begins.
        Recording rec (2);
        const double notes[] = { 60.0, 62.0, 64.0, 65.0 };
        for (int i = 0; i < 4; ++i)
        {
            auto seg = testsignals::vowel (midiToHz (notes[i]), 0.4, rate, static_cast<std::uint64_t> (51 + i), 0.5, 2);
            rec.note (seg, notes[i]);
        }
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
        {
            auto n = noteAt (a, p, p.midi);
            n.articulation = atlas::Articulation::legato;
            a.notes.push_back (n);
        }
        return finish ("legato", "C4 D4 E4 F4 joined without gaps", rec, std::move (a));
    }
    if (name == "stereo-wide")
    {
        // Left and right are different performances of the same notes: very wide.
        Recording rec (2);
        rec.gap (0.05);
        const double notes[] = { 57.0, 62.0 };
        for (int i = 0; i < 2; ++i)
        {
            const auto l = vowelNote (notes[i], 0.8, static_cast<std::uint64_t> (61 + i), 0.5, 1);
            const auto r = vowelNote (notes[i] + 0.07, 0.8, static_cast<std::uint64_t> (71 + i), 0.5, 1);
            AudioData seg = AudioData::allocate (2, l.numFrames(), rate);
            seg.channels[0] = l.channels[0];
            seg.channels[1] = r.channels[0];
            rec.note (seg, notes[i]);
            rec.gap (0.1);
        }
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
            a.notes.push_back (noteAt (a, p, p.midi));
        return finish ("stereo-wide", "A3 and D4, left and right decorrelated", rec, std::move (a));
    }
    if (name == "out-of-tune")
        return simplePhrase ("out-of-tune", "C4 +35 cents, E4 -40 cents, G4 +12 cents", 2, { { 60.35, 81 }, { 63.60, 82 }, { 67.12, 83 } }, 0.7, 0.1);
    if (name == "missing-notes")
        return simplePhrase ("missing-notes", "only C3, C4 and C5", 2, { { 48.0, 91 }, { 60.0, 92 }, { 72.0, 93 } }, 0.7, 0.1);
    if (name == "boundary-probe")
    {
        // Two adjacent notes of very different content: a grain read past a boundary would show.
        Recording rec (1);
        auto low = testsignals::sine (440.0, 0.5, rate, 0.5, 1);
        auto high = testsignals::sine (3000.0, 0.5, rate, 0.5, 1);
        rec.note (low, hzToMidi (440.0));
        rec.note (high, hzToMidi (3000.0));
        atlas::NoteAtlas a;
        for (const auto& p : rec.placed)
            a.notes.push_back (noteAt (a, p, p.midi));
        return finish ("boundary-probe", "a 440 Hz note right next to a 3 kHz note", rec, std::move (a));
    }
    return {};
}

Fixture manyRegions (int count, double noteSeconds, int keys, int channels)
{
    Recording rec (channels);
    rec.gap (0.02);
    for (int i = 0; i < count; ++i)
    {
        const double midi = 48.0 + 2.0 * (i % std::max (1, keys));
        rec.note (vowelNote (midi, noteSeconds, static_cast<std::uint64_t> (100 + i), 0.5, channels), midi);
        rec.gap (0.02);
    }
    atlas::NoteAtlas a;
    for (const auto& p : rec.placed)
        a.notes.push_back (noteAt (a, p, p.midi));
    char name[64];
    std::snprintf (name, sizeof (name), "regions-%d-%.1fs", count, noteSeconds);
    return finish (name, "benchmark atlas", rec, std::move (a));
}

atlasset::BuildResult build (const Fixture& fixture, const atlasset::BuildOptions& options)
{
    return atlasset::buildInstrumentSet (fixture.atlas, fixture.audio, options);
}

} // namespace osp::research::atlasfixtures
