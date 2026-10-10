// Library Stages 6 and 8: tags from names and folders, the slicer (ported from Sample Forge
// with its tests), trim suggestions, rendering a slice, and the one-shot classifier measured on
// the labelled evaluation set.

#include "LabelledSet.h"
#include "library/Classifier.h"
#include "library/Slicer.h"
#include "library/Tagging.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <map>

using namespace osp;
using namespace osp::library;

namespace
{
    bool hasTag (const NameEvidence& e, const std::string& tag)
    {
        for (const auto& t : e.tags)
            if (t.tag == tag)
                return true;
        return false;
    }
}

TEST_CASE ("library tagging: names give tags, a Type and a root; folders give weaker evidence", "[unit][library][tagging]")
{
    CHECK (nameWords ("WarmPad_C3-120bpm v2") == std::vector<std::string> { "warm", "pad", "c", "3", "120bpm", "v", "2" });
    auto e = evidenceFromName ("Warm Pad C3.wav");
    CHECK (hasTag (e, "pad"));
    CHECK (e.type == "Pad");
    REQUIRE (e.rootMidi.has_value());
    CHECK (*e.rootMidi == 48.0);
    e = evidenceFromName ("808_sub_bass_F#1.aif");
    CHECK (e.type == "Bass");
    CHECK (e.rootMidi == 30.0);
    e = evidenceFromName ("Vox_Ahh_Bb4.wav");
    CHECK (e.type == "Vocal");
    CHECK (e.rootMidi == 70.0);
    e = evidenceFromName ("DrumLoop 120bpm.wav");
    CHECK (e.saysLoop);
    CHECK (hasTag (e, "loop"));
    CHECK (e.type == "Percussion");
    e = evidenceFromName ("Kalimba one shot.wav");
    CHECK (e.saysOneShot);
    CHECK (e.type == "Pluck");
    e = evidenceFromName ("Flux texture 3.wav");
    CHECK (hasTag (e, "flux"));
    CHECK (hasTag (e, "texture"));
    CHECK_FALSE (e.rootMidi.has_value());   // "3" alone is no note
    e = evidenceFromName ("Bass pad.wav");   // two Types: the audio decides
    CHECK (e.type.empty());
    e = evidenceFromName ("Take 7.wav");
    CHECK (e.tags.empty());
    CHECK_FALSE (noteFromWord ("B52").has_value());
    CHECK_FALSE (noteFromWord ("H3").has_value());
    CHECK (noteFromWord ("a-1") == 9);
    CHECK (noteFromWord ("Cs4") == 61);

    const auto folders = evidenceFromFolders ("/Samples/Drums/Snares/Tight 01.wav", "/Samples");
    CHECK (hasTag (folders, "snare"));
    CHECK (hasTag (folders, "drum"));
    CHECK (folders.type == "Percussion");
    for (const auto& t : folders.tags)
        CHECK (t.confidence < 0.75);   // weaker than the name's own
    CHECK (evidenceFromFolders ("/Samples/x.wav", "/Samples").tags.empty());   // the root does not count
}

// Sample Forge's slicer tests, translated.
namespace
{
    struct Sequence
    {
        AudioData audio;
        std::vector<std::int64_t> onsets;
        std::vector<std::int64_t> lengths;
    };
    Sequence sequence (const std::vector<AudioData>& hits, double gapMs = 400.0, double noise = 0.0, double rate = 48000.0)
    {
        Sequence s;
        double total = 0.2;
        for (const auto& h : hits)
            total += h.durationSeconds() + gapMs / 1000.0;
        s.audio = labelled::blank (std::max (total, 2.0), rate);
        if (noise > 0.0)
            labelled::addNoise (s.audio, noise, 99);
        double t = 0.1;
        for (const auto& h : hits)
        {
            s.onsets.push_back (static_cast<std::int64_t> (t * rate));
            s.lengths.push_back (h.numFrames());
            labelled::mix (s.audio, h, t);
            t += h.durationSeconds() + gapMs / 1000.0;
        }
        return s;
    }
    std::vector<AudioData> kit (double rate = 48000.0)
    {
        return { labelled::kick (rate), labelled::snare (rate, 1), labelled::hat (rate, 2), labelled::kick (rate, 0.2), labelled::snare (rate, 3, 0.25),
                 labelled::hat (rate, 4, 0.12) };
    }
}

TEST_CASE ("library slicer: every hit, quiet ones too, from the start of its attack (Sample Forge's tests)", "[unit][library][slicer]")
{
    for (const double rate : { 48000.0, 44100.0, 22050.0 })
    {
        INFO ("rate " << rate);
        const auto s = sequence (kit (rate), 400.0, 0.0, rate);
        const auto mono = monoOf (s.audio);
        const auto found = detectOnsets (mono, rate, 0.3);
        REQUIRE (found.size() == s.onsets.size());
        for (std::size_t i = 0; i < found.size(); ++i)
        {
            CHECK (found[i] <= s.onsets[i] + static_cast<std::int64_t> (rate * 0.001));   // never cuts into the attack
            CHECK (s.onsets[i] - found[i] < static_cast<std::int64_t> (rate * 0.01));     // and not far before it
        }
        // Slices end when the hit has died away.
        const auto slices = sliceRecording (s.audio);
        REQUIRE (slices.size() == s.onsets.size());
        for (std::size_t i = 0; i < slices.size(); ++i)
        {
            const auto length = slices[i].end - slices[i].start;
            CHECK (length > static_cast<std::int64_t> (static_cast<double> (s.lengths[i]) * 0.3));
            CHECK (length < s.lengths[i] + static_cast<std::int64_t> (rate * 0.12));
        }
    }
    // Room noise alone is no hit, even at high sensitivity.
    const auto noise = sequence ({}, 2000.0, 0.01);
    CHECK (detectOnsets (monoOf (noise.audio), 48000.0, 0.8).empty());
    // Close hits.
    const auto close = sequence ({ labelled::kick (48000.0), labelled::hat (48000.0, 5), labelled::snare (48000.0, 6), labelled::hat (48000.0, 7) }, 30.0);
    CHECK (detectOnsets (monoOf (close.audio), 48000.0, 0.3).size() == close.onsets.size());
    // Empty and tiny input: nothing, no crash.
    CHECK (sliceRecording (AudioData {}).empty());
    CHECK (sliceRecording (labelled::blank (0.001)).empty());
}

TEST_CASE ("library slicer: a sustained note is one slice, longer than Sample Forge's 1.5 s limit", "[unit][library][slicer]")
{
    auto a = labelled::blank (7.0, 48000.0, 2);
    labelled::mix (a, labelled::held (220.0, 5.0, 48000.0, 0.01, 3), 0.5);
    const auto slices = sliceRecording (a);
    REQUIRE (slices.size() == 1);
    CHECK (static_cast<double> (slices[0].end - slices[0].start) / 48000.0 > 4.5);
}

TEST_CASE ("library trim: silence goes, soft attacks and tails stay; rendering never changes the source", "[unit][library][slicer]")
{
    // A slow, breathy attack (a bowed note): rises from -60 dB over 400 ms.
    auto a = labelled::blank (4.0, 48000.0, 2);
    auto note = labelled::held (196.0, 2.0, 48000.0, 0.4, 9);
    labelled::mix (a, note, 1.0);
    const auto trim = suggestTrim (a);
    const double start = static_cast<double> (trim.start) / 48000.0, end = static_cast<double> (trim.end) / 48000.0;
    CHECK (start > 0.9);
    CHECK (start <= 1.005);
    // The soft beginning is kept: nothing cut off before the start is louder than 55 dB under
    // the peak.
    float peak = 0.0, cut = 0.0;
    for (const auto& channel : a.channels)
        for (std::size_t i = 0; i < channel.size(); ++i)
        {
            peak = std::max (peak, std::abs (channel[i]));
            if (static_cast<std::int64_t> (i) < trim.start)
                cut = std::max (cut, std::abs (channel[i]));
        }
    CHECK (20.0 * std::log10 (cut / peak + 1.0e-12) < -55.0);
    CHECK (end > 2.95);    // the release is kept
    CHECK (end < 3.2);
    // A silent recording: nothing to trim.
    const auto silent = labelled::blank (1.0);
    CHECK (suggestTrim (silent).start == 0);
    CHECK (suggestTrim (silent).end == silent.numFrames());

    const auto before = a.channels;
    RenderOptions options;
    options.snapToZeroCrossing = true;
    const auto rendered = renderSlice (a, trim, options);
    CHECK (a.channels == before);   // the original is intact
    REQUIRE (rendered.numChannels() == 2);   // stereo kept
    CHECK (rendered.numFrames() <= trim.end - trim.start + 48);
    CHECK (rendered.channels[0].front() == 0.0f);   // anti-click fades
    CHECK (std::abs (rendered.channels[1].back()) < 1.0e-6f);
    // Left and right stay aligned: frame k of the slice is frame start + k of both channels.
    const auto plain = renderSlice (a, trim);
    const auto middle = static_cast<std::size_t> (plain.numFrames() / 2);
    for (int c = 0; c < 2; ++c)
        CHECK (plain.channels[static_cast<std::size_t> (c)][middle] == a.channels[static_cast<std::size_t> (c)][static_cast<std::size_t> (trim.start) + middle]);
    // Gain is optional and capped.
    CHECK (peakGainDb (a, trim, -1.0) > 0.0);
    CHECK (peakGainDb (labelled::blank (0.5), { 0, 100 }, -1.0) == 0.0);
    RenderOptions loud;
    loud.gainDb = peakGainDb (a, trim, -1.0);
    const auto normalised = renderSlice (a, trim, loud);
    float loudest = 0.0f;
    for (const auto& c : normalised.channels)
        for (const float v : c)
            loudest = std::max (loudest, std::abs (v));
    CHECK (20.0 * std::log10 (loudest) == Catch::Approx (-1.0).margin (0.05));
}

TEST_CASE ("library slicer: a very quiet take over room noise; slices that play backwards cleanly", "[unit][library][slicer]")
{
    // The same kit 40 dB down (peaks near -41 dBFS) over noise at about -80 dBFS: the levels are
    // relative, so every hit is still found and none of the noise is.
    auto quiet = kit();
    for (auto& hit : quiet)
        for (auto& channel : hit.channels)
            for (auto& v : channel)
                v *= 0.01f;
    const auto s = sequence (quiet, 400.0, 0.0001);
    const auto slices = sliceRecording (s.audio);
    REQUIRE (slices.size() == s.onsets.size());
    for (std::size_t i = 0; i < slices.size(); ++i)
        CHECK (std::abs (slices[i].start - s.onsets[i]) < static_cast<std::int64_t> (48000.0 * 0.01));
    // Reversed (OSP's REVERSE plays a sound from its end): the slice's end fade makes the
    // reversed start silent, and its start fade the reversed end, so neither way clicks.
    for (const auto& slice : slices)
    {
        const auto rendered = renderSlice (s.audio, slice);
        for (const auto& channel : rendered.channels)
        {
            CHECK (std::abs (channel.front()) < 1.0e-6f);
            CHECK (std::abs (channel.back()) < 1.0e-6f);
        }
    }
}

TEST_CASE ("library classifier: the labelled evaluation set (precision and recall per class, speed)", "[unit][library][classifier]")
{
    const auto set = labelled::evaluationSet();
    std::map<SoundClass, int> truePositive, falsePositive, falseNegative;
    std::map<std::string, std::map<std::string, int>> confusion;
    double audioSeconds = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    int correct = 0;
    for (const auto& item : set)
    {
        const auto c = classifySound (item.audio);
        audioSeconds += item.audio.durationSeconds();
        ++confusion[item.kind][displayName (c.soundClass)];
        if (c.soundClass == item.label)
        {
            ++truePositive[item.label];
            ++correct;
        }
        else
        {
            ++falsePositive[c.soundClass];
            ++falseNegative[item.label];
        }
    }
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\nClassifier evaluation: %d of %d correct; %.1f s of audio in %.2f s (%.0fx real time)\n", correct, static_cast<int> (set.size()),
                 audioSeconds, seconds, audioSeconds / seconds);
    std::printf ("%-26s %s\n", "kind", "answers");
    for (const auto& [kind, answers] : confusion)
    {
        std::string line;
        for (const auto& [answer, n] : answers)
            line += answer + " " + std::to_string (n) + "   ";
        std::printf ("%-26s %s\n", kind.c_str(), line.c_str());
    }
    std::printf ("%-24s %9s %9s %4s %4s %4s\n", "class", "precision", "recall", "TP", "FP", "FN");
    std::map<SoundClass, double> precision, recall;
    for (auto c : { SoundClass::likelyOneShot, SoundClass::multipleEvents, SoundClass::likelyLoop, SoundClass::longRecording, SoundClass::possibleOneShot })
    {
        const int tp = truePositive[c], fp = falsePositive[c], fn = falseNegative[c];
        precision[c] = tp + fp > 0 ? static_cast<double> (tp) / (tp + fp) : 1.0;
        recall[c] = tp + fn > 0 ? static_cast<double> (tp) / (tp + fn) : 1.0;
        std::printf ("%-24s %9.2f %9.2f %4d %4d %4d\n", displayName (c), precision[c], recall[c], tp, fp, fn);
    }
    // The bars this set must keep clearing (set from the measured results, a little below
    // them; docs/library/classification.md). Not a claim of perfect detection.
    CHECK (precision[SoundClass::likelyOneShot] >= 0.85);
    CHECK (recall[SoundClass::likelyOneShot] >= 0.8);
    CHECK (precision[SoundClass::likelyLoop] >= 0.75);
    CHECK (recall[SoundClass::likelyLoop] >= 0.75);
    CHECK (recall[SoundClass::multipleEvents] >= 0.6);
    CHECK (recall[SoundClass::longRecording] >= 0.6);
    CHECK (static_cast<double> (correct) / static_cast<double> (set.size()) >= 0.75);
    // Deterministic: the same answer twice.
    CHECK (toJson (classifySound (set[3].audio)) == toJson (classifySound (set[3].audio)));
}

TEST_CASE ("library classifier: the hard cases (measured and reported, not tuned to)", "[unit][library][classifier]")
{
    int correct = 0;
    const auto set = labelled::hardCases();
    std::printf ("\nHard cases:\n");
    for (const auto& item : set)
    {
        const auto c = classifySound (item.audio);
        correct += c.soundClass == item.label ? 1 : 0;
        std::printf ("  %-20s expected %-22s got %-22s (%s)\n", item.kind.c_str(), displayName (item.label), displayName (c.soundClass), c.reason.c_str());
    }
    std::printf ("  %d of %d\n", correct, static_cast<int> (set.size()));
    CHECK (correct >= static_cast<int> (set.size()) / 2);
}

TEST_CASE ("library classifier: odd input never breaks it; names tip close cases; tags from the audio", "[unit][library][classifier]")
{
    CHECK (classifySound (AudioData {}).confidence <= 0.1);
    CHECK (classifySound (labelled::blank (2.0)).reason.find ("silent") != std::string::npos);
    auto noise = labelled::blank (3.0);
    labelled::addNoise (noise, 0.3, 4);
    CHECK_NOTHROW (classifySound (noise));
    const auto kick = classifySound (labelled::kick (48000.0));
    CHECK (kick.soundClass == SoundClass::likelyOneShot);
    const auto bass = classifySound (labelled::held (65.41, 3.0, 48000.0, 0.01, 2, true));
    CHECK (bass.suggestedType == "Bass");
    REQUIRE (bass.rootMidi.has_value());
    CHECK (*bass.rootMidi == Catch::Approx (36.0).margin (0.5));
    const auto pad = classifySound (labelled::pad (110.0, 8.0, 48000.0));
    CHECK (pad.soundClass == SoundClass::likelyOneShot);   // long and sustained is still one sound
    bool sustained = false;
    for (const auto& t : pad.audioTags)
        sustained = sustained || t.tag == "sustained";
    CHECK (sustained);
    CHECK (toJson (pad).find ("\"class\":\"likely-one-shot\"") != std::string::npos);
    CHECK (soundClassFrom ("likely-loop") == SoundClass::likelyLoop);
    CHECK_FALSE (soundClassFrom ("nonsense").has_value());
}

// The user's own recordings (research/corpus): no labels, so it prints what it decides for a
// person to judge. Run: osp_tests "[library-corpus]"
#include "io/AudioFileIO.h"
TEST_CASE ("library classifier: the research corpus, for a person to judge", "[.][library-corpus]")
{
    const std::filesystem::path root = std::filesystem::path (OSP_SOURCE_DIR) / "research" / "corpus";
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::recursive_directory_iterator (root, ec))
        if (e.is_regular_file() && io::isSupportedAudioExtension (e.path()))
            files.push_back (e.path());
    std::sort (files.begin(), files.end());
    std::map<std::string, int> counts;
    for (const auto& file : files)
    {
        const auto loaded = io::loadAudioFile (file);
        if (! loaded.ok)
            continue;
        const auto name = evidenceFromName (file.filename().string());
        const auto c = classifySound (loaded.audio, &name);
        ++counts[displayName (c.soundClass)];
        std::printf ("%-70s %6.1f s  %-22s %-10s %s\n", std::filesystem::relative (file, root).string().c_str(), loaded.audio.durationSeconds(),
                     displayName (c.soundClass), c.suggestedType.c_str(), c.reason.c_str());
    }
    for (const auto& [k, n] : counts)
        std::printf ("%s: %d\n", k.c_str(), n);
}
