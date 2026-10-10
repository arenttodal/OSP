#include "library/FolderScan.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/Classifier.h"
#include "library/Tagging.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <set>
#include <thread>

namespace osp::library
{

void ScanProgress::reset()
{
    for (auto* a : { &found, &indexed, &unchanged, &analysed, &failed, &skipped, &likelyOneShots, &possibleOneShots, &multipleEvents, &loops, &longRecordings, &toAnalyse })
        a->store (0);
    stage.store (0);
    setCurrent ({});
}

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    bool wildcard (const char* pattern, const char* text)
    {
        // '*' any run, '?' one character (iterative, no recursion blow-up).
        const char* star = nullptr;
        const char* resume = nullptr;
        while (*text != 0)
        {
            if (*pattern == '?' || *pattern == *text)
            {
                ++pattern;
                ++text;
            }
            else if (*pattern == '*')
            {
                star = pattern++;
                resume = text;
            }
            else if (star != nullptr)
            {
                pattern = star + 1;
                text = ++resume;
            }
            else
                return false;
        }
        while (*pattern == '*')
            ++pattern;
        return *pattern == 0;
    }

    std::vector<std::string> patterns (const std::string& list)
    {
        std::vector<std::string> out;
        std::string current;
        for (const char c : list + ";")
        {
            if (c == ';')
            {
                const auto from = current.find_first_not_of (' ');
                const auto to = current.find_last_not_of (' ');
                if (from != std::string::npos)
                    out.push_back (lower (current.substr (from, to - from + 1)));
                current.clear();
            }
            else
                current += c;
        }
        return out;
    }

    bool hidden (const std::filesystem::path& relative)
    {
        for (const auto& part : relative)
        {
            const auto s = part.string();
            if (! s.empty() && s[0] == '.' && s != "." && s != "..")
                return true;
        }
        return false;
    }

    std::int64_t modified (const std::filesystem::path& file)
    {
        std::error_code ec;
        const auto t = std::filesystem::last_write_time (file, ec);
        return ec ? 0 : static_cast<std::int64_t> (t.time_since_epoch().count());
    }

    void count (ScanProgress& progress, const std::string& classification)
    {
        const auto c = soundClassFrom (classification);
        if (! c)
            return;
        switch (*c)
        {
            case SoundClass::likelyOneShot: ++progress.likelyOneShots; break;
            case SoundClass::possibleOneShot: ++progress.possibleOneShots; break;
            case SoundClass::multipleEvents: ++progress.multipleEvents; break;
            case SoundClass::likelyLoop: ++progress.loops; break;
            case SoundClass::longRecording: ++progress.longRecordings; break;
        }
    }

    /** Between files: wait while paused; true when cancelled. */
    bool stopHere (ScanControl& control)
    {
        while (control.paused.load() && ! control.cancelled.load())
            std::this_thread::sleep_for (std::chrono::milliseconds (40));
        return control.cancelled.load();
    }
}

bool passesPatterns (const std::filesystem::path& relative, const std::string& include, const std::string& exclude)
{
    const auto name = lower (relative.filename().string());
    const auto path = lower (relative.generic_string());
    auto matches = [&] (const std::string& p) { return wildcard (p.c_str(), name.c_str()) || wildcard (p.c_str(), path.c_str()); };
    for (const auto& p : patterns (exclude))
        if (matches (p))
            return false;
    const auto in = patterns (include);
    return in.empty() || std::any_of (in.begin(), in.end(), matches);
}

ScanSummary scanFolder (Catalog& catalog, const ScanRoot& root, const ScanSettings& settings, ScanControl& control, ScanProgress& progress)
{
    ScanSummary summary;
    progress.reset();
    progress.stage = 1;
    const std::filesystem::path base (root.path);
    std::error_code ec;
    if (! std::filesystem::is_directory (base, ec))
    {
        summary.error = root.path + " is not there (a disconnected drive?). Nothing was changed.";
        progress.stage = 3;
        return summary;
    }

    // Fast stage: what is there, from headers only, in batches of one transaction each.
    std::set<std::string> seen;
    std::vector<ScanFile> batch;
    auto flush = [&] {
        if (batch.empty())
            return;
        catalog.inTransaction ([&] {
            for (const auto& f : batch)
                catalog.putScanFile (f);
            return true;
        });
        batch.clear();
    };
    auto visit = [&] (const std::filesystem::path& file) {
        const auto relative = std::filesystem::relative (file, base, ec);
        if (hidden (relative) || ! io::isSupportedAudioExtension (file) || ! passesPatterns (relative, root.include, root.exclude))
            return;
        ++progress.found;
        ++summary.found;
        const auto path = file.string();
        seen.insert (path);
        const auto size = static_cast<std::int64_t> (std::filesystem::file_size (file, ec));
        const auto time = modified (file);
        const auto known = catalog.scanFile (path);
        // Unchanged (same size and time): nothing to read again; an indexed one still waits for
        // the deep stage below.
        if (known && known->size == size && known->mtime == time)
        {
            ++progress.unchanged;
            ++summary.unchanged;
            if (! known->contentHash.empty())
                count (progress, catalog.classificationOf (known->contentHash));
            return;
        }
        ScanFile f = known.value_or (ScanFile {});
        f.path = path;
        f.rootId = root.id;
        f.size = size;
        f.mtime = time;
        f.contentHash.clear();   // changed: hashed again in the deep stage
        io::AudioFileInfo info;
        std::string error;
        if (io::readAudioFileInfo (file, info, error))
        {
            f.state = "indexed";
            f.format = lower (file.extension().string().substr (1));
            f.sampleRate = info.sampleRate;
            f.channels = info.channels;
            f.durationSeconds = info.sampleRate > 0.0 ? static_cast<double> (info.frames) / info.sampleRate : 0.0;
            f.error.clear();
            ++progress.indexed;
            ++summary.indexed;
        }
        else
        {
            f.state = "failed";
            f.error = error;
            ++progress.failed;
            ++summary.failed;
        }
        batch.push_back (std::move (f));
        if (batch.size() >= 200)
            flush();
    };
    const auto walkOptions = std::filesystem::directory_options::skip_permission_denied;
    if (settings.recursive)
    {
        for (std::filesystem::recursive_directory_iterator it (base, walkOptions, ec), end; it != end && ! ec; it.increment (ec))
        {
            if (stopHere (control))
                break;
            if (it->is_directory (ec) && hidden (std::filesystem::path (it->path().filename())))
            {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file (ec))
                visit (it->path());
        }
    }
    else
        for (std::filesystem::directory_iterator it (base, walkOptions, ec), end; it != end && ! ec; it.increment (ec))
        {
            if (stopHere (control))
                break;
            if (it->is_regular_file (ec))
                visit (it->path());
        }
    flush();
    if (control.cancelled.load())
    {
        summary.cancelled = true;
        progress.stage = 4;
        return summary;
    }
    // Files gone from the folder: their candidates go (the Library's own records stay).
    for (const auto& f : catalog.scanFiles (root.id, {}, true))
        if (seen.count (f.path) == 0)
        {
            catalog.forgetScanFile (f.path);
            ++summary.removed;
        }

    if (! settings.deep)
    {
        progress.stage = 3;
        return summary;
    }
    // Deep stage: identity and classification of each new or changed file.
    progress.stage = 2;
    const auto pending = catalog.scanFiles (root.id, "indexed");
    progress.toAnalyse = static_cast<int> (pending.size());
    for (auto f : pending)
    {
        if (stopHere (control))
        {
            summary.cancelled = true;
            progress.stage = 4;
            return summary;
        }
        progress.setCurrent (f.path);
        if (settings.skipLongerThanSeconds > 0.0 && f.durationSeconds > settings.skipLongerThanSeconds)
        {
            f.state = "skipped";
            f.error = "longer than the scan's limit";
            catalog.putScanFile (f);
            ++progress.skipped;
            ++summary.skipped;
            continue;
        }
        const auto hex = io::sha256OfFile (f.path);
        if (! hex)
        {
            f.state = "failed";
            f.error = "cannot read it to the end";
            catalog.putScanFile (f);
            ++progress.failed;
            ++summary.failed;
            continue;
        }
        f.contentHash = io::contentId (*hex);
        // The same bytes analysed before (elsewhere, or by an earlier scan): reused.
        std::string error;
        if (! analyseContent (catalog, f.path, f.contentHash, error))
        {
            f.state = "failed";
            f.error = error;
            catalog.putScanFile (f);
            ++progress.failed;
            ++summary.failed;
            continue;
        }
        // Already in the Library (imported earlier, or loaded into the instrument): marked so.
        const auto records = catalog.soundsWithContent (f.contentHash);
        f.state = records.empty() ? "analysed" : "imported";
        if (! records.empty() && f.assetId.empty())
            f.assetId = records.front();
        f.error.clear();
        catalog.putScanFile (f);
        count (progress, catalog.classificationOf (f.contentHash));
        ++progress.analysed;
        ++summary.analysed;
    }
    progress.setCurrent ({});
    progress.stage = 3;
    return summary;
}

std::string jsonString (const std::string& json, const std::string& key)
{
    const auto at = json.find ("\"" + key + "\":\"");
    if (at == std::string::npos)
        return {};
    const auto from = at + key.size() + 4;
    const auto to = json.find ('"', from);
    return to == std::string::npos ? std::string() : json.substr (from, to - from);
}

double jsonNumber (const std::string& json, const std::string& key, double fallback)
{
    const auto at = json.find ("\"" + key + "\":");
    if (at == std::string::npos)
        return fallback;
    try
    {
        return std::stod (json.substr (at + key.size() + 3));
    }
    catch (...)
    {
        return fallback;
    }
}

std::vector<std::string> jsonStrings (const std::string& json, const std::string& key)
{
    std::vector<std::string> out;
    const auto at = json.find ("\"" + key + "\":[");
    if (at == std::string::npos)
        return out;
    const auto end = json.find (']', at);
    auto p = at + key.size() + 4;
    while (p < end)
    {
        const auto open = json.find ('"', p);
        if (open == std::string::npos || open > end)
            break;
        const auto close = json.find ('"', open + 1);
        out.push_back (json.substr (open + 1, close - open - 1));
        p = close + 1;
    }
    return out;
}

bool analyseContent (Catalog& catalog, const std::string& path, const std::string& contentHash, std::string& error)
{
    if (const auto known = catalog.analysis (contentHash); known && known->version >= classifierVersion)
        return true;
    io::LoadOptions options;
    options.maxSeconds = 120.0;
    const auto decoded = io::loadAudioFile (path, options);
    if (! decoded.ok)
    {
        error = decoded.error;
        return false;
    }
    const auto name = evidenceFromName (std::filesystem::path (path).filename().string());
    const auto c = classifySound (decoded.audio, &name);
    return catalog.putAnalysis (contentHash, classifierVersion, toString (c.soundClass), toJson (c));
}

ImportRequest requestWithEvidence (const Catalog& catalog, const std::filesystem::path& file, const std::string& contentHash, const std::string& rootPath)
{
    ImportRequest request;
    request.file = file;
    const auto name = evidenceFromName (file.filename().string());
    const auto folders = rootPath.empty() ? NameEvidence {} : evidenceFromFolders (file, rootPath);
    for (const auto& t : name.tags)
        request.tags.push_back ({ t.tag, TagSource::filename, t.confidence });
    for (const auto& t : folders.tags)
        request.tags.push_back ({ t.tag, TagSource::folder, t.confidence });
    std::string audioType;
    std::optional<double> audioRoot;
    if (! contentHash.empty())
        if (const auto a = catalog.analysis (contentHash))
        {
            for (const auto& t : jsonStrings (a->features, "tags"))
                request.tags.push_back ({ t, TagSource::audio, 0.6 });
            audioType = jsonString (a->features, "type");
            const double f0 = jsonNumber (a->features, "f0");
            if (jsonString (a->features, "class").empty() == false && f0 > 0.0 && a->features.find ("\"pitched\":true") != std::string::npos)
                audioRoot = 69.0 + 12.0 * std::log2 (f0 / 440.0);
        }
    // The name's Type first (a person wrote it), then the folders', then the audio's.
    request.category = ! name.type.empty() ? name.type : (! folders.type.empty() ? folders.type : audioType);
    // A root from the name only when the audio does not disagree by more than a semitone.
    if (name.rootMidi && (! audioRoot || std::abs (*audioRoot - *name.rootMidi) <= 1.0))
        request.rootMidi = name.rootMidi;
    else if (audioRoot)
        request.rootMidi = std::round (*audioRoot * 100.0) / 100.0;
    return request;
}

ImportResult importCandidate (Catalog& catalog, const std::filesystem::path& storeDir, const ScanFile& candidate, const std::string& rootPath,
                              const std::string& collectionId, Origin origin)
{
    auto request = requestWithEvidence (catalog, candidate.path, candidate.contentHash, rootPath);
    request.origin = origin;
    request.collectionId = collectionId;
    request.provenance = "unknown";   // found on a drive: whose it is, the user says when sharing
    auto result = importSound (catalog, storeDir, request);
    if (result.ok)
    {
        auto updated = candidate;
        updated.state = "imported";
        updated.assetId = result.assetId;
        updated.contentHash = result.contentHash;
        catalog.putScanFile (updated);
    }
    return result;
}

} // namespace osp::library
