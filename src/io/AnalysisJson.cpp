#include "io/AnalysisJson.h"

#include "io/JsonUtil.h"

namespace osp::io
{

using json::number;
using json::set;
using json::str;

namespace
{
    juce::var modulationToJson (const std::optional<Modulation>& m, const char* depthKey)
    {
        if (! m)
            return {};
        auto obj = json::object();
        set (obj, "rateHz", number (m->rateHz, 3));
        set (obj, depthKey, number (m->depth, 2));
        set (obj, "strength", number (m->strength, 3));
        return obj;
    }

    std::optional<Modulation> modulationFromJson (const juce::var& v, const char* depthKey)
    {
        if (! v.isObject())
            return std::nullopt;
        return Modulation { json::getDouble (v, "rateHz", 0.0), json::getDouble (v, depthKey, 0.0), json::getDouble (v, "strength", 0.0) };
    }
}

juce::var analysisToJson (const AnalysisData& d)
{
    auto root = json::object();
    set (root, "schemaVersion", d.schemaVersion);
    set (root, "analyzer", str (d.analyzer));

    auto source = json::object();
    set (source, "filename", str (d.source.filename));
    set (source, "path", str (d.source.path));
    set (source, "contentHash", str (d.source.contentHash));
    set (source, "format", str (d.source.format));
    set (source, "bitDepth", d.source.bitDepth);
    set (source, "isFloatingPoint", d.source.isFloatingPoint);
    set (source, "sampleRate", number (d.source.sampleRate, 2));
    set (source, "channels", d.source.channels);
    set (source, "frames", static_cast<juce::int64> (d.source.frames));
    set (source, "durationSeconds", number (d.source.durationSeconds, 4));
    set (source, "truncated", d.source.truncated);
    set (root, "source", source);

    const auto& p = d.pitch;
    const bool hasEstimate = p.midiNote >= 0;
    auto pitch = json::object();
    set (pitch, "detected", p.detected);
    set (pitch, "fundamentalHz", hasEstimate ? number (p.fundamentalHz, 3) : juce::var());
    set (pitch, "midiNote", hasEstimate ? juce::var (p.midiNote) : juce::var());
    set (pitch, "noteName", hasEstimate ? str (p.noteName) : juce::var());
    set (pitch, "centsOffset", hasEstimate ? number (p.centsOffset, 2) : juce::var());
    set (pitch, "confidence", number (p.confidence, 3));
    set (pitch, "confidenceLevel", str (p.confidenceLevel));
    set (pitch, "voicedFraction", number (p.voicedFraction, 3));
    set (pitch, "stabilityCents", number (p.stabilityCents, 2));
    set (pitch, "rangeCents", number (p.rangeCents, 2));
    set (pitch, "vibrato", modulationToJson (p.vibrato, "depthCents"));
    set (root, "pitch", pitch);

    const auto& e = d.envelope;
    auto env = json::object();
    set (env, "peakDbfs", number (e.peakDbfs, 2));
    set (env, "maxRmsDbfs", number (e.maxRmsDbfs, 2));
    set (env, "leadingSilenceSeconds", number (e.leadingSilenceSeconds, 4));
    set (env, "trailingSilenceSeconds", number (e.trailingSilenceSeconds, 4));
    set (env, "onsetSeconds", number (e.onsetSeconds, 4));
    set (env, "attackSeconds", number (e.attackSeconds, 4));
    set (env, "peakSeconds", number (e.peakSeconds, 4));
    set (env, "estimatedDecaySeconds", number (e.estimatedDecaySeconds, 4));
    set (env, "decaySlopeDbPerSecond", number (e.decaySlopeDbPerSecond, 3));
    set (env, "sustainLevelDb", number (e.sustainLevelDb, 2));
    set (env, "sustainFluctuationDb", number (e.sustainFluctuationDb, 3));
    set (env, "endsWhileSounding", e.endsWhileSounding);
    set (env, "secondaryPeakCount", e.secondaryPeakCount);
    set (env, "tremolo", modulationToJson (e.tremolo, "depthDb"));
    auto onsets = json::array();
    for (const auto& o : e.onsets)
    {
        auto item = json::object();
        set (item, "timeSeconds", number (o.timeSeconds, 4));
        set (item, "strength", number (o.strength, 3));
        onsets.append (item);
    }
    set (env, "onsets", onsets);
    set (root, "envelope", env);

    const auto& s = d.spectral;
    auto spec = json::object();
    set (spec, "fftSize", s.fftSize);
    set (spec, "meanCentroidHz", number (s.meanCentroidHz, 1));
    set (spec, "centroidStdHz", number (s.centroidStdHz, 1));
    set (spec, "meanRolloffHz", number (s.meanRolloffHz, 1));
    set (spec, "meanFlatness", number (s.meanFlatness, 4));
    set (spec, "meanFlux", number (s.meanFlux, 4));
    set (spec, "periodicity", number (s.periodicity, 4));
    set (spec, "harmonicEnergyRatio", number (s.harmonicEnergyRatio, 4));
    set (spec, "highFrequencyEnergyRatio", number (s.highFrequencyEnergyRatio, 4));
    set (spec, "lowFrequencyEnergyRatio", number (s.lowFrequencyEnergyRatio, 4));
    set (root, "spectral", spec);

    const auto& st = d.stereo;
    auto stereo = json::object();
    set (stereo, "isMono", st.isMono);
    set (stereo, "isDualMono", st.isDualMono);
    set (stereo, "correlation", number (st.correlation, 4));
    set (stereo, "width", number (st.width, 4));
    set (stereo, "sideToMidDb", number (st.sideToMidDb, 2));
    set (stereo, "balanceDb", number (st.balanceDb, 2));
    set (stereo, "widthLow", number (st.widthLow, 4));
    set (stereo, "widthMid", number (st.widthMid, 4));
    set (stereo, "widthHigh", number (st.widthHigh, 4));
    set (root, "stereo", stereo);

    set (root, "warnings", json::stringArray (d.warnings));

    // Time series last so the summary stays at the top of the file.
    auto series = json::object();
    set (series, "hopSeconds", number (d.envelope.rmsDb.hopSeconds, 6));
    set (series, "pitchHz", json::floatArray (p.trackHz.values, 2));
    set (series, "pitchConfidence", json::floatArray (p.trackConfidence.values, 3));
    set (series, "rmsDb", json::floatArray (e.rmsDb.values, 1));
    set (series, "centroidHz", json::floatArray (s.centroidHz.values, 0));
    set (series, "flux", json::floatArray (s.flux.values, 3));
    set (series, "flatness", json::floatArray (s.flatness.values, 4));
    set (root, "series", series);

    return root;
}

std::optional<AnalysisData> analysisFromJson (const juce::var& root, std::string& error)
{
    if (! root.isObject())
    {
        error = "analysis report is not a JSON object";
        return std::nullopt;
    }

    const int version = json::getInt (root, "schemaVersion", -1);
    if (version < 1)
    {
        error = "analysis report has no schemaVersion";
        return std::nullopt;
    }
    if (version > analysisSchemaVersion)
    {
        error = "analysis schemaVersion " + std::to_string (version) + " is newer than supported ("
                + std::to_string (analysisSchemaVersion) + ")";
        return std::nullopt;
    }

    AnalysisData d;
    d.schemaVersion = analysisSchemaVersion;
    d.analyzer = json::getString (root, "analyzer", "unknown");

    const auto& source = root["source"];
    d.source.filename = json::getString (source, "filename");
    d.source.path = json::getString (source, "path");
    d.source.contentHash = json::getString (source, "contentHash");
    d.source.format = json::getString (source, "format");
    d.source.bitDepth = json::getInt (source, "bitDepth", 0);
    d.source.isFloatingPoint = json::getBool (source, "isFloatingPoint", false);
    d.source.sampleRate = json::getDouble (source, "sampleRate", 0.0);
    d.source.channels = json::getInt (source, "channels", 0);
    d.source.frames = static_cast<std::int64_t> (json::getDouble (source, "frames", 0.0));
    d.source.durationSeconds = json::getDouble (source, "durationSeconds", 0.0);
    d.source.truncated = json::getBool (source, "truncated", false);

    const auto& pitch = root["pitch"];
    d.pitch.detected = json::getBool (pitch, "detected", false);
    d.pitch.midiNote = json::getInt (pitch, "midiNote", -1);
    d.pitch.fundamentalHz = json::getDouble (pitch, "fundamentalHz", 0.0);
    d.pitch.noteName = json::getString (pitch, "noteName");
    d.pitch.centsOffset = json::getDouble (pitch, "centsOffset", 0.0);
    d.pitch.confidence = json::getDouble (pitch, "confidence", 0.0);
    d.pitch.confidenceLevel = json::getString (pitch, "confidenceLevel", "none");
    d.pitch.voicedFraction = json::getDouble (pitch, "voicedFraction", 0.0);
    d.pitch.stabilityCents = json::getDouble (pitch, "stabilityCents", 0.0);
    d.pitch.rangeCents = json::getDouble (pitch, "rangeCents", 0.0);
    d.pitch.vibrato = modulationFromJson (pitch["vibrato"], "depthCents");

    const auto& env = root["envelope"];
    d.envelope.peakDbfs = json::getDouble (env, "peakDbfs", -200.0);
    d.envelope.maxRmsDbfs = json::getDouble (env, "maxRmsDbfs", -200.0);
    d.envelope.leadingSilenceSeconds = json::getDouble (env, "leadingSilenceSeconds", 0.0);
    d.envelope.trailingSilenceSeconds = json::getDouble (env, "trailingSilenceSeconds", 0.0);
    d.envelope.onsetSeconds = json::getDouble (env, "onsetSeconds", 0.0);
    d.envelope.attackSeconds = json::getDouble (env, "attackSeconds", 0.0);
    d.envelope.peakSeconds = json::getDouble (env, "peakSeconds", 0.0);
    d.envelope.estimatedDecaySeconds = json::getDouble (env, "estimatedDecaySeconds", 0.0);
    d.envelope.decaySlopeDbPerSecond = json::getDouble (env, "decaySlopeDbPerSecond", 0.0);
    d.envelope.sustainLevelDb = json::getDouble (env, "sustainLevelDb", -200.0);
    d.envelope.sustainFluctuationDb = json::getDouble (env, "sustainFluctuationDb", 0.0);
    d.envelope.endsWhileSounding = json::getBool (env, "endsWhileSounding", false);
    d.envelope.secondaryPeakCount = json::getInt (env, "secondaryPeakCount", 0);
    d.envelope.tremolo = modulationFromJson (env["tremolo"], "depthDb");
    if (const auto* onsets = env["onsets"].getArray())
        for (const auto& o : *onsets)
            d.envelope.onsets.push_back ({ json::getDouble (o, "timeSeconds", 0.0), json::getDouble (o, "strength", 0.0) });

    const auto& spec = root["spectral"];
    d.spectral.fftSize = json::getInt (spec, "fftSize", 0);
    d.spectral.meanCentroidHz = json::getDouble (spec, "meanCentroidHz", 0.0);
    d.spectral.centroidStdHz = json::getDouble (spec, "centroidStdHz", 0.0);
    d.spectral.meanRolloffHz = json::getDouble (spec, "meanRolloffHz", 0.0);
    d.spectral.meanFlatness = json::getDouble (spec, "meanFlatness", 0.0);
    d.spectral.meanFlux = json::getDouble (spec, "meanFlux", 0.0);
    d.spectral.periodicity = json::getDouble (spec, "periodicity", 0.0);
    d.spectral.harmonicEnergyRatio = json::getDouble (spec, "harmonicEnergyRatio", 0.0);
    d.spectral.highFrequencyEnergyRatio = json::getDouble (spec, "highFrequencyEnergyRatio", 0.0);
    d.spectral.lowFrequencyEnergyRatio = json::getDouble (spec, "lowFrequencyEnergyRatio", 0.0);

    const auto& st = root["stereo"];
    d.stereo.isMono = json::getBool (st, "isMono", true);
    d.stereo.isDualMono = json::getBool (st, "isDualMono", false);
    d.stereo.correlation = json::getDouble (st, "correlation", 1.0);
    d.stereo.width = json::getDouble (st, "width", 0.0);
    d.stereo.sideToMidDb = json::getDouble (st, "sideToMidDb", -120.0);
    d.stereo.balanceDb = json::getDouble (st, "balanceDb", 0.0);
    d.stereo.widthLow = json::getDouble (st, "widthLow", 0.0);
    d.stereo.widthMid = json::getDouble (st, "widthMid", 0.0);
    d.stereo.widthHigh = json::getDouble (st, "widthHigh", 0.0);

    if (const auto* warnings = root["warnings"].getArray())
        for (const auto& w : *warnings)
            d.warnings.push_back (w.toString().toStdString());

    const auto& series = root["series"];
    const double hop = json::getDouble (series, "hopSeconds", 0.0);
    d.pitch.trackHz = { hop, json::getFloatArray (series, "pitchHz") };
    d.pitch.trackConfidence = { hop, json::getFloatArray (series, "pitchConfidence") };
    d.envelope.rmsDb = { hop, json::getFloatArray (series, "rmsDb") };
    d.spectral.centroidHz = { hop, json::getFloatArray (series, "centroidHz") };
    d.spectral.flux = { hop, json::getFloatArray (series, "flux") };
    d.spectral.flatness = { hop, json::getFloatArray (series, "flatness") };

    return d;
}

bool writeAnalysis (const std::filesystem::path& path, const AnalysisData& data, std::string& error)
{
    return json::writeFile (path, analysisToJson (data), error);
}

std::optional<AnalysisData> readAnalysis (const std::filesystem::path& path, std::string& error)
{
    const auto parsed = json::readFile (path, error);
    if (! parsed)
        return std::nullopt;
    return analysisFromJson (*parsed, error);
}

} // namespace osp::io
