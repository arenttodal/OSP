#include "research/RenderMetrics.h"

#include "analysis/AnalysisFrames.h"
#include "analysis/pitch/PitchAnalyzer.h"
#include "analysis/spectrum/SpectralAnalyzer.h"
#include "core/PitchMath.h"
#include "io/ContentHash.h"
#include "io/JsonUtil.h"

#include <algorithm>
#include <cmath>

namespace osp::research
{

std::string RenderMetrics::status() const
{
    bool warning = false;
    for (const auto& issue : issues)
    {
        if (issue.severity == "error")
            return "error";
        warning = true;
    }
    return warning ? "warning" : "ok";
}

namespace
{
    struct PitchEstimate
    {
        double hz = 0.0;
        double confidence = 0.0;
    };

    PitchEstimate estimatePitch (const std::vector<float>& mono, double sampleRate, std::size_t start, std::size_t length)
    {
        AudioData segment;
        segment.sampleRate = sampleRate;
        const auto end = std::min (mono.size(), start + length);
        if (start >= end)
            return {};
        segment.channels.emplace_back (mono.begin() + static_cast<std::ptrdiff_t> (start), mono.begin() + static_cast<std::ptrdiff_t> (end));

        AnalysisOptions options;
        options.maxAnalysisSeconds = 120.0;
        const auto frames = AnalysisFrames::build (segment, options);
        PitchFrames pitchFrames;
        const auto pitch = PitchAnalyzer::analyse (frames, options, pitchFrames);
        if (pitch.midiNote < 0)
            return {};
        return { pitch.fundamentalHz, pitch.confidence };
    }

    void addIssue (RenderMetrics& m, const char* severity, const char* code, const std::string& message)
    {
        m.issues.push_back ({ severity, code, message });
    }
}

RenderMetrics computeMetrics (const AudioData& audio, const MetricsContext& context)
{
    RenderMetrics m;
    m.sampleRate = audio.sampleRate;
    m.channels = audio.numChannels();
    m.frames = audio.numFrames();
    m.durationSeconds = audio.durationSeconds();

    if (! (audio.sampleRate > 0.0) || (context.expectedSampleRate > 0.0 && std::abs (audio.sampleRate - context.expectedSampleRate) > 0.5))
        addIssue (m, "error", "sample-rate", "invalid or unexpected sample rate " + std::to_string (audio.sampleRate));
    if (context.expectedChannels > 0 && audio.numChannels() != context.expectedChannels)
        addIssue (m, "error", "channel-count", "expected " + std::to_string (context.expectedChannels) + " channels, got "
                                                   + std::to_string (audio.numChannels()));
    if (audio.numFrames() == 0)
    {
        addIssue (m, "error", "empty", "render produced no audio");
        return m;
    }

    // Raw sample hash before any sanitising.
    {
        std::string combined;
        for (const auto& ch : audio.channels)
            combined += io::sha256OfBytes (ch.data(), ch.size() * sizeof (float));
        m.sampleHash = io::sha256OfBytes (combined.data(), combined.size());
    }

    // Sanitised copy for statistics (NaN/Inf counted, then treated as 0).
    double peak = 0.0;
    double sumSquares = 0.0;
    std::vector<float> mono (static_cast<std::size_t> (audio.numFrames()), 0.0f);
    const float channelScale = 1.0f / static_cast<float> (std::max (1, audio.numChannels()));

    for (const auto& ch : audio.channels)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < ch.size(); ++i)
        {
            float s = ch[i];
            if (std::isnan (s)) { ++m.nanCount; s = 0.0f; }
            else if (std::isinf (s)) { ++m.infCount; s = 0.0f; }
            const double a = std::abs (static_cast<double> (s));
            peak = std::max (peak, a);
            if (a >= 1.0)
                ++m.clippedSamples;
            sumSquares += static_cast<double> (s) * s;
            sum += s;
            mono[i] += s * channelScale;
        }
        m.dcOffset.push_back (sum / static_cast<double> (ch.size()));
    }

    m.peakDbfs = gainToDb (peak);
    m.rmsDbfs = gainToDb (std::sqrt (sumSquares / (static_cast<double> (audio.numFrames()) * std::max (1, audio.numChannels()))));

    if (m.nanCount > 0 || m.infCount > 0)
        addIssue (m, "error", "non-finite", std::to_string (m.nanCount) + " NaN and " + std::to_string (m.infCount) + " Inf samples");
    if (m.clippedSamples > 0)
        addIssue (m, "warning", "clipping", std::to_string (m.clippedSamples) + " samples at or above 0 dBFS");
    if (m.peakDbfs < -60.0)
        addIssue (m, "warning", "near-silent", "peak below -60 dBFS");
    for (std::size_t c = 0; c < m.dcOffset.size(); ++c)
        if (std::abs (m.dcOffset[c]) > 0.01)
            addIssue (m, "warning", "dc-offset", "channel " + std::to_string (c) + " DC offset " + std::to_string (m.dcOffset[c]));

    m.centroidMeanHz = SpectralAnalyzer::meanCentroid (mono, audio.sampleRate, &m.centroidStdHz);

    // A whole-render pitch only means something when a single pitch is played.
    bool singlePitch = true;
    if (context.sequence != nullptr)
    {
        int pitch = -1;
        for (const auto& e : context.sequence->events)
            if (e.type == MidiEvent::Type::noteOn)
            {
                singlePitch = singlePitch && (pitch < 0 || pitch == e.note);
                pitch = e.note;
            }
    }
    if (singlePitch)
    {
        const auto whole = estimatePitch (mono, audio.sampleRate, 0, mono.size());
        m.outputPitchHz = whole.hz;
        m.outputPitchConfidence = whole.confidence;
    }

    // Isolated-note pitch checks.
    if (context.sequence != nullptr && context.sourceF0Hz > 0.0)
    {
        const auto& events = context.sequence->events;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const auto& on = events[i];
            if (on.type != MidiEvent::Type::noteOn)
                continue;

            double off = on.timeSeconds + 1.0;
            for (std::size_t k = i + 1; k < events.size(); ++k)
                if (events[k].type == MidiEvent::Type::noteOff && events[k].note == on.note)
                {
                    off = events[k].timeSeconds;
                    break;
                }

            const double windowStart = on.timeSeconds + 0.06;
            const double windowEnd = std::min (off, windowStart + 0.5);
            if (windowEnd - windowStart < 0.1)
                continue;

            // Skip notes that overlap any other note-on (chords).
            bool isolated = true;
            for (const auto& other : events)
                if (&other != &on && other.type == MidiEvent::Type::noteOn
                    && other.timeSeconds > on.timeSeconds - 0.25 && other.timeSeconds < windowEnd)
                    isolated = false;
            if (! isolated)
                continue;

            NoteCheck check;
            check.timeSeconds = on.timeSeconds;
            check.note = on.note;
            check.expectedHz = context.sourceF0Hz * semitonesToRatio (on.note - context.rootMidi);

            const auto start = static_cast<std::size_t> (std::max (0.0, windowStart * audio.sampleRate));
            const auto length = static_cast<std::size_t> ((windowEnd - windowStart) * audio.sampleRate);
            const auto estimate = estimatePitch (mono, audio.sampleRate, start, length);
            check.detectedHz = estimate.hz;
            check.confidence = estimate.confidence;
            if (estimate.hz > 0.0)
                check.centsError = centsBetween (check.expectedHz, estimate.hz);

            // The detector covers 30 Hz .. 4 kHz; only judge notes comfortably inside it.
            check.evaluated = check.expectedHz >= 35.0 && check.expectedHz <= 3500.0;
            if (check.evaluated && estimate.hz > 0.0 && estimate.confidence >= 0.4)
            {
                m.maxAbsCentsError = std::max (m.maxAbsCentsError, std::abs (check.centsError));
                if (std::abs (check.centsError) > context.pitchToleranceCents)
                    addIssue (m, "warning", "pitch-mismatch",
                              "note " + std::to_string (on.note) + " at " + std::to_string (on.timeSeconds) + " s is "
                                  + std::to_string (static_cast<int> (std::lround (check.centsError))) + " cents from expected");
            }
            m.notes.push_back (check);
        }
    }

    return m;
}

juce::var metricsToJson (const RenderMetrics& m)
{
    using json::number;
    using json::set;
    auto root = json::object();
    set (root, "schemaVersion", RenderMetrics::schemaVersion);
    set (root, "status", json::str (m.status()));
    set (root, "sampleRate", number (m.sampleRate, 2));
    set (root, "channels", m.channels);
    set (root, "frames", static_cast<juce::int64> (m.frames));
    set (root, "durationSeconds", number (m.durationSeconds, 4));
    set (root, "peakDbfs", number (m.peakDbfs, 2));
    set (root, "rmsDbfs", number (m.rmsDbfs, 2));
    auto dc = json::array();
    for (double v : m.dcOffset)
        dc.append (number (v, 6));
    set (root, "dcOffset", dc);
    set (root, "nanCount", static_cast<juce::int64> (m.nanCount));
    set (root, "infCount", static_cast<juce::int64> (m.infCount));
    set (root, "clippedSamples", static_cast<juce::int64> (m.clippedSamples));
    set (root, "centroidMeanHz", number (m.centroidMeanHz, 1));
    set (root, "centroidStdHz", number (m.centroidStdHz, 1));
    set (root, "outputPitchHz", number (m.outputPitchHz, 3));
    set (root, "outputPitchConfidence", number (m.outputPitchConfidence, 3));
    set (root, "maxAbsCentsError", number (m.maxAbsCentsError, 2));
    auto notes = json::array();
    for (const auto& n : m.notes)
    {
        auto item = json::object();
        set (item, "timeSeconds", number (n.timeSeconds, 4));
        set (item, "note", n.note);
        set (item, "expectedHz", number (n.expectedHz, 3));
        set (item, "detectedHz", number (n.detectedHz, 3));
        set (item, "centsError", number (n.centsError, 2));
        set (item, "confidence", number (n.confidence, 3));
        set (item, "evaluated", n.evaluated);
        notes.append (item);
    }
    set (root, "notes", notes);
    auto issues = json::array();
    for (const auto& i : m.issues)
    {
        auto item = json::object();
        set (item, "severity", json::str (i.severity));
        set (item, "code", json::str (i.code));
        set (item, "message", json::str (i.message));
        issues.append (item);
    }
    set (root, "issues", issues);
    set (root, "sampleHash", json::str (m.sampleHash));
    return root;
}

} // namespace osp::research
