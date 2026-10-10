#include "library/Backup.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/Archive.h"
#include "library/SoundImport.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <set>

namespace osp::library
{

namespace
{
    constexpr int backupSchemaVersion = 1;
    constexpr std::int64_t maxManifestBytes = 64LL << 20;   // a large Library lists many files

    struct FileEntry
    {
        std::string path;        ///< relative, '/'-separated
        std::int64_t bytes = 0;
        std::string sha256;      ///< hex (store files: their name)
    };

    bool isHex64 (const std::string& s)
    {
        return s.size() == 64 && std::all_of (s.begin(), s.end(), [] (unsigned char c) { return std::isdigit (c) || (c >= 'a' && c <= 'f'); });
    }

    std::string lowerExtension (const std::filesystem::path& p)
    {
        auto ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return ext;
    }

    bool cancelled (const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }

    /** A real file (not a link, not a folder). */
    bool plainFile (const std::filesystem::path& p)
    {
        std::error_code ec;
        return std::filesystem::symlink_status (p, ec).type() == std::filesystem::file_type::regular;
    }

    bool sameBytes (const std::filesystem::path& a, const std::string& sha256)
    {
        const auto hash = io::sha256OfFile (a);
        return hash && *hash == sha256;
    }

    /** Copies, then checks the copy (`sha256` empty: computed from the source first). */
    bool copyVerified (const std::filesystem::path& from, const std::filesystem::path& to, std::string& sha256, std::string& why)
    {
        std::error_code ec;
        if (sha256.empty())
        {
            const auto hash = io::sha256OfFile (from);
            if (! hash)
            {
                why = "cannot read " + from.filename().string();
                return false;
            }
            sha256 = *hash;
        }
        std::filesystem::create_directories (to.parent_path(), ec);
        auto temp = to;
        temp += ".partial";
        std::filesystem::copy_file (from, temp, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec || ! sameBytes (temp, sha256))
        {
            std::filesystem::remove (temp, ec);
            why = ec ? "cannot write " + to.filename().string() + " (disk full or no permission?)"
                     : from.filename().string() + " does not match its checksum (damaged)";
            return false;
        }
        std::filesystem::rename (temp, to, ec);
        if (ec)
        {
            std::filesystem::remove (temp, ec);
            why = "cannot place " + to.filename().string();
            return false;
        }
        return true;
    }

    /** Files under `folder` with one of `extensions` (any when empty), by their path below it. */
    std::vector<std::pair<std::filesystem::path, std::string>> filesBelow (const std::filesystem::path& folder, const std::vector<std::string>& extensions)
    {
        std::vector<std::pair<std::filesystem::path, std::string>> list;
        std::error_code ec;
        if (! std::filesystem::is_directory (folder, ec))
            return list;
        for (std::filesystem::recursive_directory_iterator it (folder, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end && ! ec;
             it.increment (ec))
        {
            const auto leaf = it->path().filename().string();
            if (! leaf.empty() && leaf[0] == '.')
            {
                if (it->is_directory (ec))
                    it.disable_recursion_pending();
                continue;
            }
            if (! plainFile (it->path()))
                continue;
            if (! extensions.empty() && std::find (extensions.begin(), extensions.end(), lowerExtension (it->path())) == extensions.end())
                continue;
            list.emplace_back (it->path(), std::filesystem::relative (it->path(), folder, ec).generic_string());
        }
        std::sort (list.begin(), list.end(), [] (const auto& x, const auto& y) { return x.second < y.second; });
        return list;
    }

    std::filesystem::path besideName (const std::filesystem::path& target)
    {
        std::error_code ec;
        for (int n = 1; n < 1000; ++n)
        {
            const auto candidate = target.parent_path() / (target.stem().string() + (n == 1 ? " (restored)" : " (restored " + std::to_string (n) + ")") + target.extension().string());
            if (! std::filesystem::exists (candidate, ec))
                return candidate;
        }
        return target.parent_path() / (target.stem().string() + " (restored " + std::to_string (std::chrono::system_clock::now().time_since_epoch().count()) + ")"
                                       + target.extension().string());
    }

    struct Manifest
    {
        std::string created, pluginVersion, presetsRoot, templatesRoot, packsRoot;
        int catalogVersion = 0, records = 0, sounds = 0, presets = 0, templates = 0;
        std::int64_t bytes = 0;
        std::vector<FileEntry> files;
    };

    bool allowedTop (const std::string& path)
    {
        for (const char* top : { "store/", "presets/", "templates/", "packs/" })
            if (path.rfind (top, 0) == 0)
                return true;
        return path == "settings.json" || path == "favourites.txt";
    }

    std::optional<Manifest> readManifest (const std::filesystem::path& folder, std::string& why)
    {
        std::error_code ec;
        if (std::filesystem::symlink_status (folder, ec).type() != std::filesystem::file_type::directory)
        {
            why = "it is not a backup folder";
            return std::nullopt;
        }
        const auto file = folder / "manifest.json";
        if (! plainFile (file) || std::filesystem::file_size (file, ec) > static_cast<std::uintmax_t> (maxManifestBytes))
        {
            why = "it has no manifest (an incomplete backup?)";
            return std::nullopt;
        }
        juce::var v;
        if (juce::JSON::parse (juce::File (juce::String (file.string())).loadFileAsString(), v).failed() || v.getProperty ("format", "") != "ospbackup")
        {
            why = "its manifest is not readable";
            return std::nullopt;
        }
        if (static_cast<int> (v.getProperty ("schemaVersion", 0)) > backupSchemaVersion)
        {
            why = "it was made by a newer version of the plugin";
            return std::nullopt;
        }
        Manifest m;
        m.created = cleanText (v.getProperty ("created", "").toString().toStdString(), 64);
        m.pluginVersion = cleanText (v.getProperty ("pluginVersion", "").toString().toStdString(), 32);
        m.catalogVersion = static_cast<int> (v.getProperty ("catalogVersion", 0));
        const auto roots = v.getProperty ("roots", juce::var());
        m.presetsRoot = roots.getProperty ("presets", "").toString().toStdString();
        m.templatesRoot = roots.getProperty ("templates", "").toString().toStdString();
        m.packsRoot = roots.getProperty ("packs", "").toString().toStdString();
        const auto counts = v.getProperty ("counts", juce::var());
        m.records = static_cast<int> (counts.getProperty ("records", 0));
        m.sounds = static_cast<int> (counts.getProperty ("sounds", 0));
        m.presets = static_cast<int> (counts.getProperty ("presets", 0));
        m.templates = static_cast<int> (counts.getProperty ("templates", 0));
        if (m.catalogVersion > Catalog::schemaVersion)
        {
            why = "its Library was made by a newer version of the plugin";
            return std::nullopt;
        }
        const auto* files = v.getProperty ("files", juce::var()).getArray();
        if (files == nullptr)
        {
            why = "its manifest lists no files";
            return std::nullopt;
        }
        std::set<std::string> seen;
        for (const auto& f : *files)
        {
            FileEntry e;
            e.path = f.getProperty ("path", "").toString().toStdString();
            e.bytes = static_cast<std::int64_t> (static_cast<juce::int64> (f.getProperty ("bytes", -1)));
            e.sha256 = f.getProperty ("sha256", "").toString().toLowerCase().toStdString();
            std::string problem;
            if (! safeRelativePath (e.path, 12, problem) || ! allowedTop (e.path) || e.bytes < 0 || ! isHex64 (e.sha256) || ! seen.insert (e.path).second)
            {
                why = "it lists a file that is not valid (" + cleanText (e.path, 200) + ")";
                return std::nullopt;
            }
            if (e.path.rfind ("store/", 0) == 0 && std::filesystem::path (e.path).filename().string().rfind (e.sha256 + ".", 0) != 0)
            {
                why = "a stored sound's name does not match its checksum (" + cleanText (e.path, 200) + ")";
                return std::nullopt;
            }
            m.bytes += e.bytes;
            m.files.push_back (std::move (e));
        }
        if (! plainFile (folder / "catalog.db"))
        {
            why = "it has no catalog";
            return std::nullopt;
        }
        return m;
    }
}

//==============================================================================

BackupResult exportBackup (Catalog& catalog, const BackupPlaces& places, const std::filesystem::path& out, const BackupOptions& options)
{
    BackupResult result;
    std::error_code ec;
    if (std::filesystem::exists (out, ec))
    {
        result.error = out.filename().string() + " is there already: choose another name";
        return result;
    }
    auto partial = out;
    partial += ".partial";
    std::filesystem::remove_all (partial, ec);
    if (! std::filesystem::create_directories (partial, ec))
    {
        result.error = "cannot write the backup there (no permission, or the drive is full)";
        return result;
    }
    struct Abandon
    {
        std::filesystem::path dir;
        bool keep = false;
        ~Abandon()
        {
            std::error_code e;
            if (! keep)
                std::filesystem::remove_all (dir, e);
        }
    } abandon { partial };

    // 1. The catalog: a consistent snapshot, whatever other instances are writing.
    if (! catalog.snapshotTo (partial / "catalog.db"))
    {
        result.error = "the Library could not be copied (" + catalog.lastError() + ")";
        return result;
    }
    // 2. What goes with it.
    struct Job
    {
        std::filesystem::path from;
        std::string to, sha256;
    };
    std::vector<Job> jobs;
    std::set<std::string> storeNames;
    for (const auto& hash : catalog.neededContent())
    {
        const auto hex = hexOf (hash);
        if (const auto stored = findStored (places.storeDir, hex))
        {
            if (storeNames.insert (stored->filename().string()).second)
                jobs.push_back ({ *stored, "store/" + stored->filename().string(), hex });
            continue;
        }
        const auto where = resolveSound (catalog, places.storeDir, hash);
        std::string name = hex.substr (0, 12);
        for (const auto& id : catalog.soundsWithContent (hash))
            if (const auto a = catalog.asset (id))
            {
                name = a->name;
                break;
            }
        if (where.file && options.includeExternal)
        {
            const auto leaf = hex + lowerExtension (*where.file);
            if (storeNames.insert (leaf).second)
                jobs.push_back ({ *where.file, "store/" + leaf, hex });
            ++result.external;
        }
        else
            result.notIncluded.push_back (name + (where.file ? " (outside the Library, at " + where.file->string() + ": choose \"include external files\")"
                                                            : " (missing: not found anywhere)"));
    }
    if (options.includeUntracked)
        for (const auto& entry : std::filesystem::directory_iterator (places.storeDir, ec))
        {
            const auto leaf = entry.path().filename().string();
            const auto stem = leaf.substr (0, leaf.find ('.'));
            if (isHex64 (stem) && io::isSupportedAudioExtension (entry.path()) && leaf.find (".partial") == std::string::npos && plainFile (entry.path())
                && storeNames.insert (leaf).second)
                jobs.push_back ({ entry.path(), "store/" + leaf, stem });
        }
    for (const auto& [file, rel] : filesBelow (places.presetsDir, { ".osppreset" }))
        jobs.push_back ({ file, "presets/" + rel, {} }), ++result.presets;
    for (const auto& [file, rel] : filesBelow (places.templatesDir, { ".ospstate" }))
        jobs.push_back ({ file, "templates/" + rel, {} }), ++result.templates;
    for (const auto& [file, rel] : filesBelow (places.packsDir, {}))
        jobs.push_back ({ file, "packs/" + rel, {} }), ++result.packFiles;
    if (plainFile (places.settingsFile))
        jobs.push_back ({ places.settingsFile, "settings.json", {} });
    if (plainFile (places.favouritesFile))
        jobs.push_back ({ places.favouritesFile, "favourites.txt", {} });

    // 3. Copied and checked, one by one (cancel between files).
    std::vector<FileEntry> files;
    const int total = static_cast<int> (jobs.size());
    for (int i = 0; i < total; ++i)
    {
        if (cancelled (options.cancel))
        {
            result.error = "cancelled (nothing was kept)";
            return result;
        }
        auto& job = jobs[static_cast<std::size_t> (i)];
        std::string why;
        if (! copyVerified (job.from, partial / job.to, job.sha256, why))
        {
            result.error = why;
            return result;
        }
        const auto bytes = static_cast<std::int64_t> (std::filesystem::file_size (partial / job.to, ec));
        files.push_back ({ job.to, bytes, job.sha256 });
        result.bytes += bytes;
        if (job.to.rfind ("store/", 0) == 0)
            ++result.sounds;
        if (options.progress)
            options.progress (i + 1, total);
    }
    // 4. The manifest, last: a folder without one is an incomplete backup.
    SearchQuery everything;
    result.records = catalog.count (everything);
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "ospbackup");
    root->setProperty ("schemaVersion", backupSchemaVersion);
    root->setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true));
    root->setProperty ("pluginVersion", juce::String (places.pluginVersion));
    root->setProperty ("catalogVersion", catalog.version());
    auto* roots = new juce::DynamicObject();
    roots->setProperty ("presets", juce::String (places.presetsDir.string()));
    roots->setProperty ("templates", juce::String (places.templatesDir.string()));
    roots->setProperty ("packs", juce::String (places.packsDir.string()));
    root->setProperty ("roots", juce::var (roots));
    auto* counts = new juce::DynamicObject();
    counts->setProperty ("records", result.records);
    counts->setProperty ("sounds", result.sounds);
    counts->setProperty ("presets", result.presets);
    counts->setProperty ("templates", result.templates);
    counts->setProperty ("external", result.external);
    root->setProperty ("counts", juce::var (counts));
    root->setProperty ("includesExternal", options.includeExternal);
    juce::Array<juce::var> list;
    for (const auto& f : files)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", juce::String (f.path));
        o->setProperty ("bytes", static_cast<juce::int64> (f.bytes));
        o->setProperty ("sha256", juce::String (f.sha256));
        list.add (juce::var (o));
    }
    root->setProperty ("files", list);
    juce::StringArray missing;
    for (const auto& n : result.notIncluded)
        missing.add (juce::String (n));
    root->setProperty ("notIncluded", missing);
    if (! juce::File (juce::String ((partial / "manifest.json").string())).replaceWithText (juce::JSON::toString (juce::var (root))))
    {
        result.error = "cannot write the backup's manifest (disk full?)";
        return result;
    }
    std::filesystem::rename (partial, out, ec);
    if (ec)
    {
        result.error = "cannot name the backup " + out.filename().string();
        return result;
    }
    abandon.keep = true;
    result.ok = true;
    return result;
}

BackupInfo inspectBackup (const std::filesystem::path& folder)
{
    BackupInfo info;
    std::string why;
    const auto m = readManifest (folder, why);
    if (! m)
    {
        info.error = "this backup cannot be used: " + why;
        return info;
    }
    for (const auto& f : m->files)
        if (! plainFile (folder / f.path) || static_cast<std::int64_t> (std::filesystem::file_size (folder / f.path)) != f.bytes)
        {
            info.error = "this backup is incomplete or damaged: " + f.path + " is missing, changed or a link";
            return info;
        }
    info.ok = true;
    info.created = m->created;
    info.pluginVersion = m->pluginVersion;
    info.catalogVersion = m->catalogVersion;
    info.records = m->records;
    info.sounds = m->sounds;
    info.presets = m->presets;
    info.templates = m->templates;
    info.bytes = m->bytes;
    return info;
}

RestoreReport restoreBackup (Catalog& catalog, const BackupPlaces& places, const std::filesystem::path& folder, const RestoreOptions& options)
{
    RestoreReport report;
    const auto look = inspectBackup (folder);
    if (! look.ok)
    {
        report.error = look.error;
        return report;
    }
    std::string why;
    const auto m = readManifest (folder, why);
    std::error_code ec;
    // 1. The backup's catalog, opened on its own (an older one is migrated in the copy).
    std::filesystem::create_directories (places.storeDir, ec);
    const auto temp = places.storeDir / (".restore-" + std::to_string (std::chrono::steady_clock::now().time_since_epoch().count()) + ".db.partial");
    struct Remove
    {
        std::filesystem::path file;
        ~Remove()
        {
            std::error_code e;
            for (const char* suffix : { "", "-wal", "-shm" })
                std::filesystem::remove (file.string() + suffix, e);
        }
    } removeTemp { temp };
    std::filesystem::copy_file (folder / "catalog.db", temp, ec);
    if (ec)
    {
        report.error = "cannot read the backup's catalog";
        return report;
    }
    std::map<std::string, std::string> idOfFile;
    {
        std::string error;
        auto other = Catalog::open (temp, error);
        if (other == nullptr)
        {
            report.error = "the backup's catalog cannot be used: " + error;
            return report;
        }
        if (other->integrityCheck() != "ok")
        {
            report.error = "the backup's catalog is damaged";
            return report;
        }
        for (const auto& [id, file] : other->presetFiles())
            idOfFile[file] = id;
    }
    // 2. The sounds (each checked against its name, the hash).
    for (const auto& f : m->files)
    {
        if (cancelled (options.cancel))
        {
            report.error = "cancelled";
            return report;
        }
        if (f.path.rfind ("store/", 0) != 0)
            continue;
        if (findStored (places.storeDir, f.sha256))
        {
            ++report.soundsPresent;
            continue;
        }
        std::string problem;
        if (storeContent (places.storeDir, folder / f.path, f.sha256, problem))
            ++report.soundsAdded;
        else
            report.notes.push_back (f.path + ": " + problem);
    }
    // 3. The records, merged.
    const auto merged = catalog.mergeFrom (temp, options.newerWins,
                                           { { m->presetsRoot, places.presetsDir.string() },
                                             { m->templatesRoot, places.templatesDir.string() },
                                             { m->packsRoot, places.packsDir.string() } });
    if (! merged)
    {
        report.error = "the Library could not take the backup's records (" + catalog.lastError() + "); nothing was changed";
        return report;
    }
    report.added = merged->added;
    report.kept = merged->kept;
    report.updated = merged->updated;
    report.tags = merged->tags;
    report.collections = merged->collections;
    const std::set<std::string> updated (merged->updatedIds.begin(), merged->updatedIds.end());
    const std::set<std::string> kept (merged->keptIds.begin(), merged->keptIds.end());
    // 4. Preset, template and pack files.
    for (const auto& f : m->files)
    {
        if (cancelled (options.cancel))
        {
            report.error = "cancelled (what was restored so far stays)";
            return report;
        }
        std::filesystem::path target, oldRoot, newRoot;
        std::string rest;
        for (const auto& [top, from, to] : { std::tuple { std::string ("presets/"), m->presetsRoot, places.presetsDir },
                                              std::tuple { std::string ("templates/"), m->templatesRoot, places.templatesDir },
                                              std::tuple { std::string ("packs/"), m->packsRoot, places.packsDir } })
            if (f.path.rfind (top, 0) == 0)
                rest = f.path.substr (top.size()), oldRoot = from, newRoot = to;
        if (rest.empty())
            continue;
        target = newRoot / std::filesystem::path (rest);
        const auto oldPath = (oldRoot / std::filesystem::path (rest)).string();
        const auto owner = idOfFile.count (oldPath) > 0 ? idOfFile[oldPath] : std::string();
        auto sha = f.sha256;
        std::string problem;
        const bool present = std::filesystem::exists (target, ec);
        if (present && sameBytes (target, f.sha256))
        {
            ++report.filesSame;
            continue;
        }
        if (! present)
        {
            if (copyVerified (folder / f.path, target, sha, problem))
                ++report.filesAdded;
            else
                report.notes.push_back (f.path + ": " + problem);
            continue;
        }
        // A different file is there.
        if (! owner.empty() && kept.count (owner) > 0)
        {
            ++report.filesSame;   // the record here is kept, and so is its file
            continue;
        }
        if (! owner.empty() && updated.count (owner) > 0 && options.moveToTrash && options.moveToTrash (target))
        {
            if (copyVerified (folder / f.path, target, sha, problem))
                ++report.filesReplaced;
            else
                report.notes.push_back (f.path + ": " + problem);
            continue;
        }
        const auto beside = besideName (target);
        if (! copyVerified (folder / f.path, beside, sha, problem))
        {
            report.notes.push_back (f.path + ": " + problem);
            continue;
        }
        ++report.filesBeside;
        if (! owner.empty())
            if (const auto a = catalog.asset (owner))
                catalog.movePreset (owner, beside.string(), a->name);
    }
    // 5. Settings (only where there are none) and favourites (both kept).
    for (const auto& f : m->files)
    {
        auto sha = f.sha256;
        std::string problem;
        if (f.path == "settings.json" && ! std::filesystem::exists (places.settingsFile, ec))
            copyVerified (folder / f.path, places.settingsFile, sha, problem);
        if (f.path == "favourites.txt")
        {
            auto here = juce::StringArray::fromLines (juce::File (juce::String (places.favouritesFile.string())).loadFileAsString());
            here.removeEmptyStrings();
            auto theirs = juce::StringArray::fromLines (juce::File (juce::String ((folder / f.path).string())).loadFileAsString());
            for (const auto& name : theirs)
                if (name.isNotEmpty() && here.addIfNotAlreadyThere (juce::String (cleanText (name.toStdString(), 200))))
                    ++report.favourites;
            if (report.favourites > 0)
            {
                std::filesystem::create_directories (places.favouritesFile.parent_path(), ec);
                juce::File (juce::String (places.favouritesFile.string())).replaceWithText (here.joinIntoString ("\n") + "\n");
            }
        }
    }
    if (report.filesBeside > 0)
        report.notes.push_back (std::to_string (report.filesBeside) + " files differed from ones already here: the backup's were saved beside them, named \"(restored)\"");
    if (report.kept > 0)
        report.notes.push_back (std::to_string (report.kept) + " records were already here: they were kept as they are");
    report.ok = true;
    return report;
}

} // namespace osp::library
