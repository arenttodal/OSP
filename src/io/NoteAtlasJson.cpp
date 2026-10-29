#include "io/NoteAtlasJson.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/JsonUtil.h"

#include <algorithm>

namespace osp::io
{

namespace
{
    constexpr const char* formatTag = "andor.noteAtlas";

    juce::var frames (std::int64_t value) { return juce::var (static_cast<juce::int64> (value)); }

    std::int64_t getFrames (const juce::var& obj, const char* key, std::int64_t fallback)
    {
        const auto& v = obj[juce::Identifier (key)];
        if (v.isInt() || v.isInt64() || v.isDouble())
            return static_cast<std::int64_t> (static_cast<juce::int64> (v));
        return fallback;
    }

    juce::var rangeToJson (const atlas::FrameRange& r)
    {
        auto o = json::object();
        json::set (o, "start", frames (r.start));
        json::set (o, "end", frames (r.end));
        return o;
    }

    atlas::FrameRange rangeFromJson (const juce::var& o)
    {
        return { getFrames (o, "start", 0), getFrames (o, "end", 0) };
    }

    juce::var sourceToJson (const atlas::SourceAsset& s)
    {
        auto o = json::object();
        json::set (o, "id", json::str (s.assetId));
        json::set (o, "contentHash", json::str (s.contentHash));
        json::set (o, "origin", json::str (atlas::toString (s.origin)));
        if (! s.captureId.empty())
            json::set (o, "captureId", json::str (s.captureId));
        json::set (o, "fileName", json::str (s.fileName));
        json::set (o, "relativePath", json::str (s.relativePath));
        json::set (o, "lastKnownPath", json::str (s.lastKnownPath));
        json::set (o, "managedCopy", json::str (s.managedCopy));
        json::set (o, "sampleRate", s.sampleRate);
        json::set (o, "numChannels", s.numChannels);
        json::set (o, "numFrames", frames (s.numFrames));
        return o;
    }

    atlas::SourceAsset sourceFromJson (const juce::var& o)
    {
        atlas::SourceAsset s;
        s.assetId = json::getString (o, "id");
        s.contentHash = json::getString (o, "contentHash");
        s.origin = atlas::assetOriginFromString (json::getString (o, "origin")).value_or (atlas::AssetOrigin::fileImport);
        s.captureId = json::getString (o, "captureId");
        s.fileName = json::getString (o, "fileName");
        s.relativePath = json::getString (o, "relativePath");
        s.lastKnownPath = json::getString (o, "lastKnownPath");
        s.managedCopy = json::getString (o, "managedCopy");
        s.sampleRate = json::getDouble (o, "sampleRate", 0.0);
        s.numChannels = json::getInt (o, "numChannels", 0);
        s.numFrames = getFrames (o, "numFrames", 0);
        return s;
    }

    juce::var noteToJson (const atlas::NoteEvent& n)
    {
        auto o = json::object();
        json::set (o, "id", json::str (n.id));
        json::set (o, "source", json::str (n.sourceId));
        json::set (o, "sourceStart", frames (n.sourceStart));
        json::set (o, "sourceEnd", frames (n.sourceEnd));
        json::set (o, "attackStart", frames (n.attackStart));
        json::set (o, "sustain", rangeToJson (n.sustain));
        json::set (o, "releaseStart", frames (n.releaseStart));
        json::set (o, "fadeIn", frames (n.fadeInFrames));
        json::set (o, "fadeOut", frames (n.fadeOutFrames));
        // Everything playback reads is written at full precision: a recalled session must play
        // sample for sample like the one that was saved (a rounded pitch moves every read).
        json::set (o, "pitchHz", n.pitchHz);
        json::set (o, "midiPitch", n.midiPitch);
        json::set (o, "rootMidi", n.rootMidi);
        json::set (o, "cents", n.centsOffset);
        json::set (o, "stabilityCents", json::number (n.pitchStabilityCents, 2));
        json::set (o, "vibratoCents", json::number (n.vibratoDepthCents, 2));
        json::set (o, "vibratoHz", json::number (n.vibratoRateHz, 2));
        json::set (o, "voicing", n.voicing);
        json::set (o, "confidence", n.confidence);
        json::set (o, "peakDb", n.peakDb);
        json::set (o, "rmsDb", n.rmsDb);
        json::set (o, "articulation", json::str (atlas::toString (n.articulation)));
        json::set (o, "origin", json::str (n.origin == atlas::NoteOrigin::analysis ? "analysis" : "user"));
        json::set (o, "method", json::str (n.analysisMethod));
        json::set (o, "methodVersion", n.analysisVersion);
        auto c = json::object();
        json::set (c, "pitch", n.corrections.pitch);
        json::set (c, "boundaries", n.corrections.boundaries);
        json::set (c, "key", n.corrections.key);
        json::set (c, "group", n.corrections.group);
        json::set (c, "split", n.corrections.split);
        json::set (c, "merged", n.corrections.merged);
        json::set (c, "velocity", n.corrections.velocity);
        if (n.corrections.included)
            json::set (c, "included", true);   // Phase 4 (added field)
        json::set (o, "corrections", c);
        if (n.diagnostics.any())
        {
            // Phase 4 (added field): only the doubts, so a reliable note writes nothing.
            juce::Array<juce::var> d;
            const auto& g = n.diagnostics;
            for (const auto& [flag, name] : { std::pair { g.uncertainPitch, "uncertainPitch" }, { g.possibleOctaveError, "possibleOctaveError" },
                                              { g.uncertainBoundary, "uncertainBoundary" }, { g.unvoiced, "unvoiced" },
                                              { g.possibleSplit, "possibleSplit" }, { g.quietAlternate, "quietAlternate" } })
                if (flag)
                    d.add (juce::String (name));
            json::set (o, "diagnostics", juce::var (d));
        }
        json::set (o, "included", n.included);
        json::set (o, "assignedKey", n.assignedKey);
        json::set (o, "velocityLayer", n.velocityLayer);
        json::set (o, "group", json::str (n.variationGroup));
        json::set (o, "variation", n.variationIndex);
        if (n.grainRegion)
            json::set (o, "grainRegion", rangeToJson (*n.grainRegion));
        return o;
    }

    atlas::NoteEvent noteFromJson (const juce::var& o)
    {
        atlas::NoteEvent n;
        n.id = json::getString (o, "id");
        n.sourceId = json::getString (o, "source");
        n.sourceStart = getFrames (o, "sourceStart", 0);
        n.sourceEnd = getFrames (o, "sourceEnd", 0);
        n.attackStart = getFrames (o, "attackStart", n.sourceStart);
        n.sustain = json::has (o, "sustain") ? rangeFromJson (o["sustain"]) : atlas::FrameRange { n.attackStart, n.sourceEnd };
        n.releaseStart = getFrames (o, "releaseStart", n.sustain.end);
        n.fadeInFrames = getFrames (o, "fadeIn", 0);
        n.fadeOutFrames = getFrames (o, "fadeOut", 0);
        n.pitchHz = json::getDouble (o, "pitchHz", 0.0);
        n.midiPitch = json::getDouble (o, "midiPitch", 60.0);
        n.rootMidi = json::getInt (o, "rootMidi", 60);
        n.centsOffset = json::getDouble (o, "cents", 0.0);
        n.pitchStabilityCents = json::getDouble (o, "stabilityCents", 0.0);
        n.vibratoDepthCents = json::getDouble (o, "vibratoCents", 0.0);
        n.vibratoRateHz = json::getDouble (o, "vibratoHz", 0.0);
        n.voicing = json::getDouble (o, "voicing", 0.0);
        n.confidence = json::getDouble (o, "confidence", 0.0);
        n.peakDb = json::getDouble (o, "peakDb", -120.0);
        n.rmsDb = json::getDouble (o, "rmsDb", -120.0);
        n.articulation = atlas::articulationFromString (json::getString (o, "articulation")).value_or (atlas::Articulation::sustained);
        n.analysisMethod = json::getString (o, "method");
        // (version 1 had no origin: a note that names its method came from analysis)
        n.origin = json::has (o, "origin") ? (json::getString (o, "origin") == "analysis" ? atlas::NoteOrigin::analysis : atlas::NoteOrigin::user)
                                           : (n.analysisMethod.empty() ? atlas::NoteOrigin::user : atlas::NoteOrigin::analysis);
        n.analysisVersion = json::getInt (o, "methodVersion", 0);
        const auto& c = o["corrections"];
        n.corrections.pitch = json::getBool (c, "pitch", false);
        n.corrections.boundaries = json::getBool (c, "boundaries", false);
        n.corrections.key = json::getBool (c, "key", false);
        n.corrections.group = json::getBool (c, "group", false);
        n.corrections.split = json::getBool (c, "split", false);
        n.corrections.merged = json::getBool (c, "merged", false);
        n.corrections.velocity = json::getBool (c, "velocity", false);
        n.corrections.included = json::getBool (c, "included", false);
        if (const auto* d = o["diagnostics"].getArray())
            for (const auto& v : *d)
            {
                const auto name = v.toString();
                n.diagnostics.uncertainPitch = n.diagnostics.uncertainPitch || name == "uncertainPitch";
                n.diagnostics.possibleOctaveError = n.diagnostics.possibleOctaveError || name == "possibleOctaveError";
                n.diagnostics.uncertainBoundary = n.diagnostics.uncertainBoundary || name == "uncertainBoundary";
                n.diagnostics.unvoiced = n.diagnostics.unvoiced || name == "unvoiced";
                n.diagnostics.possibleSplit = n.diagnostics.possibleSplit || name == "possibleSplit";
                n.diagnostics.quietAlternate = n.diagnostics.quietAlternate || name == "quietAlternate";
            }
        n.included = json::getBool (o, "included", true);
        n.assignedKey = json::getInt (o, "assignedKey", -1);
        n.velocityLayer = std::clamp (json::getInt (o, "velocityLayer", 0), 0, 15);
        n.variationGroup = json::getString (o, "group");
        n.variationIndex = json::getInt (o, "variation", 0);
        if (json::has (o, "grainRegion"))
            n.grainRegion = rangeFromJson (o["grainRegion"]);
        return n;
    }

    std::filesystem::path fromUtf8 (const std::string& s) { return std::filesystem::path (std::u8string (s.begin(), s.end())); }
    std::string utf8 (const std::u8string& s) { return std::string (s.begin(), s.end()); }
}

juce::var noteAtlasToJson (const atlas::NoteAtlas& a)
{
    auto root = json::object();
    json::set (root, "format", formatTag);
    json::set (root, "schemaVersion", a.schemaVersion);
    json::set (root, "atlasId", json::str (a.atlasId));
    json::set (root, "createdBy", json::str (a.createdBy));

    auto analysis = json::object();
    json::set (analysis, "method", json::str (a.analysis.method));
    json::set (analysis, "version", a.analysis.version);
    json::set (analysis, "a4Hz", a.analysis.a4Hz);
    json::set (analysis, "tuningOffsetCents", a.analysis.tuningOffsetCents);
    json::set (analysis, "settingsDigest", json::str (a.analysis.settingsDigest));
    if (! a.analysis.suitability.empty())
    {
        // Phase 4 (added fields): what the analysis found about the recording as a whole.
        json::set (analysis, "suitability", json::str (a.analysis.suitability));
        json::set (analysis, "analysedSeconds", a.analysis.analysedSeconds);
        json::set (analysis, "pitchedSeconds", a.analysis.pitchedSeconds);
        juce::Array<juce::var> uncovered;
        for (const auto& r : a.analysis.uncovered)
            uncovered.add (rangeToJson (r));
        json::set (analysis, "uncovered", juce::var (uncovered));
    }
    json::set (root, "analysis", analysis);

    auto mapping = json::object();
    json::set (mapping, "fill", a.mapping.fill);
    json::set (mapping, "maxTransposeSemitones", a.mapping.maxTransposeSemitones);
    json::set (mapping, "correctTuning", a.mapping.correctTuning);
    json::set (root, "mapping", mapping);

    auto sources = json::array();
    for (const auto& s : a.sources)
        sources.append (sourceToJson (s));
    json::set (root, "sources", sources);

    auto notes = json::array();
    for (const auto& n : a.notes)
        notes.append (noteToJson (n));
    json::set (root, "notes", notes);
    return root;
}

std::optional<atlas::NoteAtlas> noteAtlasFromJson (const juce::var& json, std::string& error)
{
    if (! json.isObject())
    {
        error = "not a Note Atlas (expected a JSON object)";
        return std::nullopt;
    }
    if (json::getString (json, "format") != formatTag)
    {
        error = "not a Note Atlas (format tag missing)";
        return std::nullopt;
    }
    const int version = json::getInt (json, "schemaVersion", 0);
    if (version <= 0)
    {
        error = "Note Atlas without a schemaVersion";
        return std::nullopt;
    }
    if (version > atlas::currentSchemaVersion)
    {
        error = "Note Atlas schemaVersion " + std::to_string (version) + " is newer than this build understands ("
                + std::to_string (atlas::currentSchemaVersion) + ")";
        return std::nullopt;
    }
    // Version 1 (ANDOR Lab) -> 2: the fields 2 added read their defaults (above and below).

    atlas::NoteAtlas a;
    a.schemaVersion = atlas::currentSchemaVersion;
    a.atlasId = json::getString (json, "atlasId");
    a.createdBy = json::getString (json, "createdBy");
    const auto& analysis = json["analysis"];
    a.analysis.method = json::getString (analysis, "method");
    a.analysis.version = json::getInt (analysis, "version", 0);
    a.analysis.a4Hz = json::getDouble (analysis, "a4Hz", 440.0);
    a.analysis.tuningOffsetCents = json::getDouble (analysis, "tuningOffsetCents", 0.0);
    a.analysis.settingsDigest = json::getString (analysis, "settingsDigest");
    a.analysis.suitability = json::getString (analysis, "suitability");
    a.analysis.analysedSeconds = json::getDouble (analysis, "analysedSeconds", 0.0);
    a.analysis.pitchedSeconds = json::getDouble (analysis, "pitchedSeconds", 0.0);
    if (const auto* list = analysis["uncovered"].getArray())
        for (const auto& r : *list)
            a.analysis.uncovered.push_back (rangeFromJson (r));
    if (json::has (json, "mapping"))
    {
        const auto& m = json["mapping"];
        a.mapping.fill = json::getBool (m, "fill", a.mapping.fill);
        a.mapping.maxTransposeSemitones = json::getDouble (m, "maxTransposeSemitones", a.mapping.maxTransposeSemitones);
        a.mapping.correctTuning = json::getBool (m, "correctTuning", a.mapping.correctTuning);
    }
    if (const auto* list = json["sources"].getArray())
        for (const auto& s : *list)
            a.sources.push_back (sourceFromJson (s));
    if (const auto* list = json["notes"].getArray())
        for (const auto& n : *list)
            a.notes.push_back (noteFromJson (n));
    return a;
}

std::string noteAtlasToString (const atlas::NoteAtlas& a)
{
    // Not json::toString (6 decimals, for reports): doubles round-trip exactly here.
    const auto options = juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::multiLine).withMaxDecimalPlaces (17);
    return juce::JSON::toString (noteAtlasToJson (a), options).toStdString();
}

std::optional<atlas::NoteAtlas> noteAtlasFromString (const std::string& text, std::string& error)
{
    const auto parsed = json::parse (text, error);
    if (! parsed)
        return std::nullopt;
    return noteAtlasFromJson (*parsed, error);
}

bool writeNoteAtlas (const std::filesystem::path& path, const atlas::NoteAtlas& a, std::string& error)
{
    auto copy = a;
    std::error_code ec;
    const auto folder = std::filesystem::absolute (path, ec).parent_path();
    for (auto& s : copy.sources)
    {
        if (s.lastKnownPath.empty())
            continue;
        const auto rel = std::filesystem::relative (fromUtf8 (s.lastKnownPath), folder, ec);
        if (! ec && ! rel.empty())
            s.relativePath = utf8 (rel.generic_u8string());
    }
    std::error_code absoluteError;
    const auto absolute = std::filesystem::absolute (path, absoluteError);
    const juce::File file (juce::String::fromUTF8 (utf8 ((absoluteError ? path : absolute).u8string()).c_str()));
    if (! file.replaceWithText (juce::String::fromUTF8 (noteAtlasToString (copy).c_str()) + "\n"))
    {
        error = "cannot write " + utf8 (path.u8string());
        return false;
    }
    return true;
}

std::optional<atlas::NoteAtlas> readNoteAtlas (const std::filesystem::path& path, std::string& error)
{
    const auto parsed = json::readFile (path, error);
    if (! parsed)
        return std::nullopt;
    return noteAtlasFromJson (*parsed, error);
}

std::optional<ResolvedSource> resolveSource (const atlas::SourceAsset& source, const std::filesystem::path& atlasFolder,
                                             const std::vector<std::filesystem::path>& searchFolders)
{
    std::vector<std::filesystem::path> candidates;
    if (! source.relativePath.empty())
        candidates.push_back (atlasFolder / fromUtf8 (source.relativePath));
    if (! source.lastKnownPath.empty())
        candidates.push_back (fromUtf8 (source.lastKnownPath));
    if (! source.managedCopy.empty())
        candidates.push_back (fromUtf8 (source.managedCopy));
    if (! source.fileName.empty())
    {
        candidates.push_back (atlasFolder / fromUtf8 (source.fileName));
        for (const auto& folder : searchFolders)
            candidates.push_back (folder / fromUtf8 (source.fileName));
    }

    std::optional<ResolvedSource> compatible;
    for (const auto& candidate : candidates)
    {
        std::error_code ec;
        if (! std::filesystem::is_regular_file (candidate, ec))
            continue;
        const auto hash = sha256OfFile (candidate);
        if (hash && ! source.contentHash.empty() && contentId (*hash) == source.contentHash)
            return ResolvedSource { candidate, atlas::RelinkResult::exact };
        AudioFileInfo info;
        std::string error;
        if (! compatible && readAudioFileInfo (candidate, info, error))
        {
            const auto match = atlas::checkRelink (source, hash ? contentId (*hash) : std::string(), info.sampleRate,
                                                   std::min (info.channels, 2), info.frames);
            if (match != atlas::RelinkResult::incompatible)
                compatible = ResolvedSource { candidate, match };
        }
    }
    return compatible;
}

bool relinkSource (atlas::NoteAtlas& a, const std::string& assetId, const std::filesystem::path& file, std::string& error,
                   atlas::RelinkResult* matchOut)
{
    auto it = std::find_if (a.sources.begin(), a.sources.end(), [&] (const auto& s) { return s.assetId == assetId; });
    if (it == a.sources.end())
    {
        error = "no source '" + assetId + "' in the atlas";
        return false;
    }
    AudioFileInfo info;
    if (! readAudioFileInfo (file, info, error))
        return false;
    const auto hash = sha256OfFile (file);
    if (! hash)
    {
        error = "cannot read " + utf8 (file.u8string());
        return false;
    }
    const auto match = atlas::checkRelink (*it, contentId (*hash), info.sampleRate, std::min (info.channels, 2), info.frames);
    if (matchOut != nullptr)
        *matchOut = match;
    if (match == atlas::RelinkResult::incompatible)
    {
        error = "'" + utf8 (file.filename().u8string()) + "' does not match the source (different length, rate or channels)";
        return false;
    }
    std::error_code ec;
    it->lastKnownPath = utf8 (std::filesystem::absolute (file, ec).u8string());
    it->fileName = utf8 (file.filename().u8string());
    if (match == atlas::RelinkResult::compatible)
        it->contentHash = contentId (*hash);   // the new bytes are the fingerprint from now on
    return true;
}

} // namespace osp::io
