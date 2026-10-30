#include "library/Classifier.h"

#include "analysis/Analyzer.h"
#include "library/Slicer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace osp::library
{

const char* toString (SoundClass c) noexcept
{
    switch (c)
    {
        case SoundClass::likelyOneShot: return "likely-one-shot";
        case SoundClass::possibleOneShot: return "possible-one-shot";
        case SoundClass::multipleEvents: return "multiple-events";
        case SoundClass::likelyLoop: return "likely-loop";
        case SoundClass::longRecording: return "long-recording";
    }
    return "possible-one-shot";
}

const char* displayName (SoundClass c) noexcept
{
    switch (c)
    {
        case SoundClass::likelyOneShot: return "Likely One-Shot";
        case SoundClass::possibleOneShot: return "Possible One-Shot";
        case SoundClass::multipleEvents: return "Multiple Events";
        case SoundClass::likelyLoop: return "Likely Loop";
        case SoundClass::longRecording: return "Likely Long Recording";
    }
    return "Possible One-Shot";
}

std::optional<SoundClass> soundClassFrom (const std::string& s) noexcept
{
    for (auto c : { SoundClass::likelyOneShot, SoundClass::possibleOneShot, SoundClass::multipleEvents, SoundClass::likelyLoop, SoundClass::longRecording })
        if (s == toString (c))
            return c;
    return std::nullopt;
}

namespace
{
    /** Share of onset intervals that sit on one grid (1-4 steps of the median interval). */
    double regularity (const std::vector<std::int64_t>& onsets, double sampleRate)
    {
        if (onsets.size() < 4)
            return 0.0;
        std::vector<double> intervals;
        for (std::size_t i = 1; i < onsets.size(); ++i)
            intervals.push_back (static_cast<double> (onsets[i] - onsets[i - 1]) / sampleRate);
        auto sorted = intervals;
        std::sort (sorted.begin(), sorted.end());
        const double step = sorted[sorted.size() / 4];   // the shorter intervals define the grid
        if (step < 0.05)
            return 0.0;
        int onGrid = 0;
        for (const double t : intervals)
        {
            const double r = t / step;
            const double nearest = std::round (r);
            if (nearest >= 1.0 && nearest <= 4.0 && std::abs (r - nearest) < 0.06 * nearest)
                ++onGrid;
        }
        return static_cast<double> (onGrid) / static_cast<double> (intervals.size());
    }
}

int heldNotes (const TimeSeries& track, const TimeSeries& confidence, double minHoldSeconds, bool leapsAreNotes)
{
    if (track.hopSeconds <= 0.0 || track.values.empty())
        return 0;
    const auto minFrames = std::max<std::size_t> (2, static_cast<std::size_t> (std::ceil (minHoldSeconds / track.hopSeconds)));
    std::vector<double> held;   // each held stretch's pitch, cents re A4
    std::size_t run = 0;
    double runStart = 0.0;
    for (std::size_t i = 0; i < track.values.size(); ++i)
    {
        const float hz = track.values[i];
        const bool sure = i < confidence.values.size() ? confidence.values[i] >= 0.75f : true;
        if (hz <= 0.0f || ! sure)
        {
            run = 0;
            continue;
        }
        const double cents = 1200.0 * std::log2 (static_cast<double> (hz) / 440.0);
        if (run == 0 || std::abs (cents - runStart) > 40.0)
        {
            run = 1;
            runStart = cents;
            continue;
        }
        if (++run == minFrames)
            held.push_back (runStart);
    }
    if (held.empty())
        return 0;
    auto related = [leapsAreNotes] (double interval) {
        if (leapsAreNotes)
            return false;
        const double a = std::abs (interval);
        for (const double r : { 700.0, 1200.0, 1900.0, 2400.0 })
            if (std::abs (a - r) < 50.0)
                return true;
        return false;
    };
    int notes = 1;
    double current = held.front();
    for (std::size_t i = 1; i < held.size(); ++i)
    {
        const double move = held[i] - current;
        if (std::abs (move) > 90.0 && ! related (move))
        {
            ++notes;
            current = held[i];
        }
    }
    return notes;
}

Classification classifySound (const AudioData& audio, const NameEvidence* hints)
{
    Classification c;
    auto& f = c.features;
    f.durationSeconds = audio.durationSeconds();
    const auto mono = monoOf (audio);
    float peak = 0.0f;
    for (const float v : mono)
        peak = std::max (peak, std::abs (v));
    f.peakDbfs = peak > 0.0f ? 20.0 * std::log10 (static_cast<double> (peak)) : -200.0;
    if (mono.size() < 64 || audio.sampleRate <= 0.0 || f.peakDbfs < -80.0)
    {
        c.soundClass = SoundClass::possibleOneShot;
        c.confidence = 0.1;
        c.reason = f.peakDbfs < -80.0 ? "silent or nearly silent" : "too short to judge";
        return c;
    }

    // Events: what the slicer finds (attacks that die into the floor), and how separated.
    SliceOptions sliceOptions;
    sliceOptions.maxSliceSeconds = 600.0;
    const auto slices = sliceRecording (audio, sliceOptions);
    const auto onsets = detectOnsets (mono, audio.sampleRate, sliceOptions.sensitivity);
    f.onsets = static_cast<int> (onsets.size());
    f.events = static_cast<int> (slices.size());
    const auto gap = static_cast<std::int64_t> (0.12 * audio.sampleRate);
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        const auto next = i + 1 < slices.size() ? slices[i + 1].start : static_cast<std::int64_t> (mono.size());
        if (i + 1 < slices.size() && next - slices[i].end >= gap)
            ++f.separatedEvents;
    }
    if (f.separatedEvents > 0)
        ++f.separatedEvents;   // n gaps separate n + 1 events
    {
        std::vector<double> energy;
        for (const auto& s : slices)
        {
            double e = 0.0;
            for (auto i = s.start; i < s.end; ++i)
                e += static_cast<double> (mono[static_cast<std::size_t> (i)]) * mono[static_cast<std::size_t> (i)];
            energy.push_back (e);
        }
        const double total = std::accumulate (energy.begin(), energy.end(), 0.0);
        f.mainEventShare = total > 0.0 ? *std::max_element (energy.begin(), energy.end()) / total : 0.0;
    }
    {
        const auto envelope = energyEnvelope (mono);
        if (! envelope.empty())
        {
            const float floor = noiseFloorDb (envelope);
            const float top = *std::max_element (envelope.begin(), envelope.end());
            const float quiet = std::max (floor + 6.0f, top - 50.0f);
            f.silenceFraction = static_cast<double> (std::count_if (envelope.begin(), envelope.end(), [quiet] (float d) { return d < quiet; }))
                              / static_cast<double> (envelope.size());
        }
    }
    f.onsetRegularity = regularity (onsets, audio.sampleRate);

    // The analysis: envelope, pitch, spectrum.
    AnalysisOptions options;
    options.maxAnalysisSeconds = 60.0;
    const auto analysis = Analyzer::analyse (audio, options);
    f.leadingSilenceSeconds = analysis.envelope.leadingSilenceSeconds;
    f.attackSeconds = analysis.envelope.attackSeconds;
    f.decaySeconds = analysis.envelope.estimatedDecaySeconds;
    f.sustainLevelDb = analysis.envelope.sustainLevelDb;
    f.endsWhileSounding = analysis.envelope.endsWhileSounding;
    f.pitched = analysis.pitch.detected && analysis.pitch.confidence >= 0.5;
    f.fundamentalHz = analysis.pitch.fundamentalHz;
    f.pitchStabilityCents = analysis.pitch.stabilityCents;
    f.notes = heldNotes (analysis.pitch.trackHz, analysis.pitch.trackConfidence);
    f.flatness = analysis.spectral.meanFlatness;
    f.centroidHz = analysis.spectral.meanCentroidHz;
    f.flux = analysis.spectral.meanFlux;
    f.stereo = ! analysis.stereo.isMono && ! analysis.stereo.isDualMono && analysis.stereo.width > 0.08;
    if (f.pitched && analysis.pitch.confidence >= 0.6 && f.fundamentalHz > 0.0)
        c.rootMidi = 69.0 + 12.0 * std::log2 (f.fundamentalHz / 440.0);

    const bool namedLoop = hints != nullptr && hints->saysLoop;
    const bool namedShot = hints != nullptr && hints->saysOneShot;
    const double d = f.durationSeconds;

    // The decision: the strongest pattern wins, in this order.
    if (f.onsets >= 4 && d <= 90.0 && (f.onsetRegularity >= 0.85 || (namedLoop && f.onsetRegularity >= 0.6)))
    {
        c.soundClass = SoundClass::likelyLoop;
        c.confidence = std::min (0.95, 0.45 + 0.4 * f.onsetRegularity + (namedLoop ? 0.1 : 0.0));
        c.reason = std::to_string (f.onsets) + " attacks on a regular grid";
    }
    else if (d > 120.0 || (d > 45.0 && (f.onsets >= 6 || f.separatedEvents >= 4)))
    {
        c.soundClass = SoundClass::longRecording;
        c.confidence = d > 120.0 ? 0.8 : 0.6;
        c.reason = "long, with " + std::to_string (f.onsets) + " attacks";
    }
    else if (f.separatedEvents >= 2 && f.mainEventShare < 0.9)
    {
        c.soundClass = SoundClass::multipleEvents;
        c.confidence = std::min (0.9, 0.5 + 0.08 * f.separatedEvents);
        c.reason = std::to_string (f.separatedEvents) + " sounds separated by silence";
    }
    else if (f.notes >= 3 && d <= 45.0)
    {
        // A phrase sung or played legato: one breath, several notes.
        c.soundClass = SoundClass::multipleEvents;
        c.confidence = std::min (0.8, 0.45 + 0.08 * f.notes);
        c.reason = std::to_string (f.notes) + " notes in a row";
    }
    else if (f.onsets <= 1 || f.mainEventShare >= 0.85 || (f.pitched && f.pitchStabilityCents < 35.0 && f.onsets <= 3))
    {
        // One sound, however long it sustains.
        c.soundClass = SoundClass::likelyOneShot;
        c.confidence = 0.6 + (f.onsets <= 1 ? 0.2 : 0.05) + (f.leadingSilenceSeconds < 0.3 ? 0.1 : 0.0) + (namedShot ? 0.05 : 0.0);
        c.confidence = std::min (0.95, c.confidence);
        c.reason = f.onsets <= 1 ? "one attack" : "one main sound";
        if (d > 45.0)
            c.confidence -= 0.2, c.reason += ", long";
        if (f.leadingSilenceSeconds >= 1.0)
        {
            c.soundClass = SoundClass::possibleOneShot;
            c.reason += ", after a long silence (trim it)";
        }
    }
    else
    {
        c.soundClass = SoundClass::possibleOneShot;
        c.confidence = 0.45;
        c.reason = std::to_string (f.onsets) + " attacks without silence between";
        if (namedLoop)
            c.soundClass = SoundClass::likelyLoop, c.reason += " (named a loop)";
    }

    // Audio-derived tags and a suggested Type.
    const bool percussive = f.attackSeconds < 0.015 && f.decaySeconds < 1.2 && ! f.endsWhileSounding;
    const bool sustained = f.endsWhileSounding || f.sustainLevelDb > -12.0;
    const bool noisy = f.flatness > 0.35;
    auto tag = [&c] (const char* t, double confidence) { c.audioTags.push_back ({ t, confidence }); };
    if (percussive)
        tag ("percussive", 0.6);
    if (sustained)
        tag ("sustained", 0.6);
    if (f.pitched)
        tag ("tonal", 0.6);
    if (noisy)
        tag ("noisy", 0.5);
    if (f.centroidHz > 3500.0)
        tag ("bright", 0.5);
    else if (f.centroidHz > 0.0 && f.centroidHz < 600.0)
        tag ("dark", 0.5);
    if (f.stereo)
        tag ("stereo", 0.6);
    if (percussive && (noisy || ! f.pitched))
        c.suggestedType = "Percussion";
    else if (f.pitched && f.fundamentalHz > 0.0 && f.fundamentalHz < 130.0)
        c.suggestedType = "Bass";
    else if (f.pitched && f.attackSeconds < 0.03 && ! sustained && f.decaySeconds < 4.0)
        c.suggestedType = "Pluck";
    else if (f.pitched && sustained)
        c.suggestedType = "Pad";
    else if (noisy && sustained)
        c.suggestedType = "Texture";
    return c;
}

std::string toJson (const Classification& c)
{
    const auto& f = c.features;
    std::string tags;
    for (const auto& t : c.audioTags)
        tags += (tags.empty() ? "\"" : ",\"") + t.tag + "\"";
    char buffer[1400];
    std::snprintf (buffer, sizeof (buffer),
                   "{\"version\":%d,\"class\":\"%s\",\"confidence\":%.3f,\"duration\":%.3f,\"peakDbfs\":%.1f,\"onsets\":%d,\"events\":%d,"
                   "\"separatedEvents\":%d,\"mainEventShare\":%.3f,\"silenceFraction\":%.3f,\"onsetRegularity\":%.3f,\"leadingSilence\":%.3f,"
                   "\"attack\":%.4f,\"decay\":%.3f,\"sustainDb\":%.1f,\"endsWhileSounding\":%s,\"pitched\":%s,\"f0\":%.2f,\"stabilityCents\":%.1f,\"notes\":%d,"
                   "\"flatness\":%.3f,\"centroid\":%.0f,\"flux\":%.4f,\"stereo\":%s,\"type\":\"%s\",\"tags\":[%s],\"reason\":\"%s\"}",
                   classifierVersion, toString (c.soundClass), c.confidence, f.durationSeconds, f.peakDbfs, f.onsets, f.events, f.separatedEvents,
                   f.mainEventShare, f.silenceFraction, f.onsetRegularity, f.leadingSilenceSeconds, f.attackSeconds, f.decaySeconds, f.sustainLevelDb,
                   f.endsWhileSounding ? "true" : "false", f.pitched ? "true" : "false", f.fundamentalHz, f.pitchStabilityCents, f.notes, f.flatness,
                   f.centroidHz, f.flux, f.stereo ? "true" : "false", c.suggestedType.c_str(), tags.c_str(), c.reason.c_str());
    return buffer;
}

} // namespace osp::library
