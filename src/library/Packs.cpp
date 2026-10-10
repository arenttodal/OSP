#include "library/Packs.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "library/Archive.h"
#include "library/SoundImport.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace osp::library
{

namespace
{
    constexpr std::int64_t maxManifestBytes = 8LL << 20;

    bool isHex64 (const std::string& s)
    {
        return s.size() == 64 && std::all_of (s.begin(), s.end(), [] (unsigned char c) { return std::isdigit (c) || (c >= 'a' && c <= 'f'); });
    }

    const char* kindName (PackItem::Kind k)
    {
        return k == PackItem::Kind::sound ? "sound" : (k == PackItem::Kind::preset ? "preset" : "template");
    }

    const char* folderOf (PackItem::Kind k)
    {
        return k == PackItem::Kind::sound ? "sounds/" : (k == PackItem::Kind::preset ? "presets/" : "templates/");
    }

    std::string str (const juce::var& v, const char* key, std::size_t maxBytes, bool multiline = false)
    {
        return cleanText (v.getProperty (key, juce::var()).toString().toStdString(), maxBytes, multiline);
    }

    std::string lowerExtension (const std::string& name)
    {
        const auto dot = name.rfind ('.');
        auto ext = dot == std::string::npos ? std::string() : name.substr (dot);
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return ext;
    }

    /** A file name for a preset or template inside a pack folder (no separators, not hidden). */
    std::string safeLeaf (const std::string& name, const std::string& extension)
    {
        std::string leaf;
        for (const unsigned char c : cleanText (name, 120))
            leaf += (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') ? '-' : static_cast<char> (c);
        while (! leaf.empty() && (leaf.front() == '.' || leaf.front() == ' '))
            leaf.erase (leaf.begin());
        if (leaf.empty())
            leaf = "Untitled";
        return leaf + extension;
    }

    std::filesystem::path folderFor (const PackPlaces& places, const PackManifest& m)
    {
        return (m.factory ? places.factoryDir : places.packsDir) / m.id;
    }

    /** Checks the archive against its manifest (every listed file there, at its size). */
    bool archiveMatches (const ArchiveReader& archive, const PackManifest& m, std::string& why)
    {
        for (const auto& item : m.items)
            if (archive.size (item.path) != item.bytes)
            {
                why = item.name + " (" + item.path + ") is " + (archive.has (item.path) ? "not the size the manifest says" : "missing from the pack");
                return false;
            }
        return true;
    }

    /** Version, plugin version, dependencies, presets' sounds: what install needs. */
    bool checkInstallable (const Catalog& catalog, const PackManifest& m, const PackPlaces& places, PackResult& result)
    {
        if (! places.pluginVersion.empty() && ! m.requiresPlugin.empty() && compareVersions (m.requiresPlugin, places.pluginVersion) > 0)
        {
            result.error = m.name + " needs plugin version " + m.requiresPlugin + " or later (this is " + places.pluginVersion + ")";
            return false;
        }
        if (const auto installed = catalog.pack (m.id))
        {
            result.previousVersion = installed->version;
            const int order = compareVersions (m.version, installed->version);
            if (order < 0)
            {
                result.error = "a newer version of " + m.name + " (" + installed->version + ") is installed: nothing was changed";
                return false;
            }
            result.alreadyInstalled = order == 0;
            result.upgraded = order > 0;
        }
        for (const auto& [id, version] : m.dependencies)
        {
            const auto other = catalog.pack (id);
            if (! other || compareVersions (other->version, version) < 0)
            {
                result.error = m.name + " needs the pack " + id + " (version " + version + " or later): install it first";
                return false;
            }
        }
        std::set<std::string> inPack;
        for (const auto& item : m.items)
            if (item.kind == PackItem::Kind::sound)
                inPack.insert (item.sha256);
        for (const auto& item : m.items)
            for (const auto& hex : item.sounds)
                if (inPack.count (hex) == 0 && ! findStored (places.storeDir, hex))
                {
                    result.error = item.name + " needs a sound that is neither in the pack nor in this Library (" + hex.substr (0, 12) + "...)";
                    return false;
                }
        return true;
    }

    PresetInfo presetInfoFor (const std::filesystem::path& file, const PackItem& item, const std::string& root)
    {
        PresetInfo info;
        info.file = file.string();
        info.root = root;
        for (const auto& hex : item.sounds)
            info.soundHashes.push_back (io::contentId (hex));
        if (const auto xml = juce::XmlDocument::parse (juce::File (juce::String (file.string()))))
        {
            info.stateVersion = xml->getIntAttribute ("stateVersion", 0);
            info.layers = item.kind == PackItem::Kind::templateState ? xml->getIntAttribute ("keptSlots", 1) : static_cast<int> (item.sounds.size());
        }
        return info;
    }
}

//==============================================================================

std::optional<PackManifest> parsePackManifest (const std::string& json, std::string& why)
{
    juce::var v;
    if (json.size() > static_cast<std::size_t> (maxManifestBytes) || juce::JSON::parse (juce::String (json), v).failed() || ! v.isObject())
    {
        why = "its manifest is not readable";
        return std::nullopt;
    }
    if (v.getProperty ("format", "").toString() != "osppack")
    {
        why = "it is not an OSP pack";
        return std::nullopt;
    }
    const int version = static_cast<int> (v.getProperty ("schemaVersion", 0));
    if (version < 1)
    {
        why = "its manifest has no version";
        return std::nullopt;
    }
    if (version > PackManifest::schemaVersion)
    {
        why = "it was made for a newer version of the plugin";
        return std::nullopt;
    }
    PackManifest m;
    m.id = v.getProperty ("id", "").toString().toStdString();
    m.name = str (v, "name", 200);
    m.version = v.getProperty ("version", "").toString().toStdString();
    m.creator = str (v, "creator", 200);
    m.description = str (v, "description", 4000, true);
    m.license = str (v, "license", 20000, true);
    m.requiresPlugin = v.getProperty ("requiresPlugin", "").toString().toStdString();
    m.factory = static_cast<bool> (v.getProperty ("factory", false));
    if (! validPackId (m.id))
    {
        why = "its id is not valid (letters a-z, digits, '.', '_' and '-')";
        return std::nullopt;
    }
    if (m.name.empty() || ! validVersion (m.version) || (! m.requiresPlugin.empty() && ! validVersion (m.requiresPlugin)))
    {
        why = "its name or version is missing or not valid";
        return std::nullopt;
    }
    if (const auto* deps = v.getProperty ("requires", juce::var()).getArray())
        for (const auto& d : *deps)
        {
            const auto id = d.getProperty ("id", "").toString().toStdString();
            const auto at = d.getProperty ("version", "0").toString().toStdString();
            if (! validPackId (id) || ! validVersion (at) || id == m.id)
            {
                why = "it names a required pack that is not valid";
                return std::nullopt;
            }
            m.dependencies.emplace_back (id, at);
        }
    const auto* items = v.getProperty ("items", juce::var()).getArray();
    if (items == nullptr || items->isEmpty() || items->size() > 50000)
    {
        why = "it lists no contents (or far too many)";
        return std::nullopt;
    }
    std::set<std::string> uids, paths;
    for (const auto& i : *items)
    {
        PackItem item;
        const auto kind = i.getProperty ("kind", "").toString();
        item.kind = kind == "sound" ? PackItem::Kind::sound : (kind == "preset" ? PackItem::Kind::preset : PackItem::Kind::templateState);
        if (kind != "sound" && kind != "preset" && kind != "template")
        {
            why = "it lists something that is not a sound, preset or template";
            return std::nullopt;
        }
        item.uid = str (i, "uid", 200);
        item.path = i.getProperty ("path", "").toString().toStdString();
        item.sha256 = i.getProperty ("sha256", "").toString().toLowerCase().toStdString();
        item.bytes = static_cast<std::int64_t> (static_cast<juce::int64> (i.getProperty ("bytes", -1)));
        item.name = str (i, "name", 200);
        item.category = str (i, "category", 60);
        if (const auto* tags = i.getProperty ("tags", juce::var()).getArray())
            for (const auto& t : *tags)
                if (const auto tag = cleanText (t.toString().toLowerCase().toStdString(), 60); ! tag.empty() && item.tags.size() < 50)
                    item.tags.push_back (tag);
        if (i.hasProperty ("rootMidi"))
        {
            const double root = static_cast<double> (i.getProperty ("rootMidi", 60.0));
            if (root >= 0.0 && root <= 127.0)
                item.rootMidi = root;
        }
        if (const auto* sounds = i.getProperty ("sounds", juce::var()).getArray())
            for (const auto& s : *sounds)
                item.sounds.push_back (s.toString().toLowerCase().toStdString());
        std::string pathProblem;
        const auto leaf = item.path.size() > std::string (folderOf (item.kind)).size() ? item.path.substr (std::string (folderOf (item.kind)).size()) : std::string();
        const auto ext = lowerExtension (leaf);
        const bool pathOk = safeRelativePath (item.path, 1, pathProblem) && item.path.rfind (folderOf (item.kind), 0) == 0 && ! leaf.empty()
                            && (item.kind != PackItem::Kind::sound || (leaf == item.sha256 + ext && io::isSupportedAudioExtension (leaf)))
                            && (item.kind != PackItem::Kind::preset || ext == ".osppreset")
                            && (item.kind != PackItem::Kind::templateState || ext == ".ospstate");
        if (item.uid.empty() || ! uids.insert (item.uid).second || ! pathOk || ! paths.insert (item.path).second || ! isHex64 (item.sha256) || item.bytes < 0
            || ! std::all_of (item.sounds.begin(), item.sounds.end(), isHex64))
        {
            why = "an entry is not valid (" + cleanText (item.path, 200) + ")";
            return std::nullopt;
        }
        if (item.name.empty())
            item.name = std::filesystem::path (leaf).stem().string();
        m.items.push_back (std::move (item));
    }
    return m;
}

std::string toJson (const PackManifest& m)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "osppack");
    root->setProperty ("schemaVersion", PackManifest::schemaVersion);
    root->setProperty ("id", juce::String (m.id));
    root->setProperty ("name", juce::String (m.name));
    root->setProperty ("version", juce::String (m.version));
    root->setProperty ("creator", juce::String (m.creator));
    root->setProperty ("description", juce::String (m.description));
    root->setProperty ("license", juce::String (m.license));
    root->setProperty ("requiresPlugin", juce::String (m.requiresPlugin));
    root->setProperty ("factory", m.factory);
    juce::Array<juce::var> deps;
    for (const auto& [id, version] : m.dependencies)
    {
        auto* d = new juce::DynamicObject();
        d->setProperty ("id", juce::String (id));
        d->setProperty ("version", juce::String (version));
        deps.add (juce::var (d));
    }
    root->setProperty ("requires", deps);
    juce::Array<juce::var> items;
    for (const auto& item : m.items)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("uid", juce::String (item.uid));
        o->setProperty ("kind", kindName (item.kind));
        o->setProperty ("path", juce::String (item.path));
        o->setProperty ("sha256", juce::String (item.sha256));
        o->setProperty ("bytes", static_cast<juce::int64> (item.bytes));
        o->setProperty ("name", juce::String (item.name));
        if (! item.category.empty())
            o->setProperty ("category", juce::String (item.category));
        juce::Array<juce::var> tags, sounds;
        for (const auto& t : item.tags)
            tags.add (juce::String (t));
        for (const auto& s : item.sounds)
            sounds.add (juce::String (s));
        if (! tags.isEmpty())
            o->setProperty ("tags", tags);
        if (! sounds.isEmpty())
            o->setProperty ("sounds", sounds);
        if (item.rootMidi)
            o->setProperty ("rootMidi", *item.rootMidi);
        items.add (juce::var (o));
    }
    root->setProperty ("items", items);
    return juce::JSON::toString (juce::var (root)).toStdString();
}

//==============================================================================

PackResult inspectPack (const Catalog& catalog, const std::filesystem::path& file, const PackPlaces& places)
{
    PackResult result;
    const ArchiveReader archive (file);
    if (! archive.ok())
    {
        result.error = "the pack cannot be used: " + archive.problem();
        return result;
    }
    const auto json = archive.readText ("manifest.json", maxManifestBytes);
    std::string why;
    const auto manifest = json ? parsePackManifest (*json, why) : std::nullopt;
    if (! manifest)
    {
        result.error = "the pack cannot be used: " + (json ? why : std::string ("it has no manifest"));
        return result;
    }
    result.manifest = *manifest;
    if (! archiveMatches (archive, *manifest, why))
    {
        result.error = "the pack is incomplete or damaged: " + why;
        return result;
    }
    if (! checkInstallable (catalog, *manifest, places, result))
        return result;
    for (const auto& item : manifest->items)
        ++(item.kind == PackItem::Kind::sound ? result.sounds : (item.kind == PackItem::Kind::preset ? result.presets : result.templates));
    if (! manifest->license.empty())
        result.notes.push_back ("Its licence: " + manifest->license.substr (0, 300) + (manifest->license.size() > 300 ? "..." : ""));
    result.ok = true;
    return result;
}

PackResult installPack (Catalog& catalog, const std::filesystem::path& file, const PackPlaces& places)
{
    auto result = inspectPack (catalog, file, places);
    if (! result.ok)
        return result;
    result.ok = false;
    result.sounds = result.presets = result.templates = 0;   // counted again as they are installed
    const auto& m = result.manifest;
    const ArchiveReader archive (file);
    std::error_code ec;
    const auto origin = m.factory ? Origin::factory : Origin::pack;
    const auto root = m.factory ? std::string ("factory") : "pack:" + m.id;
    const auto folder = folderFor (places, m);
    // Everything is unpacked and checked (size and checksum) before anything changes.
    const auto staging = folder.parent_path() / (".staging-" + m.id);
    std::filesystem::remove_all (staging, ec);
    std::filesystem::create_directories (staging, ec);
    struct Cleanup
    {
        std::filesystem::path dir;
        ~Cleanup()
        {
            std::error_code e;
            std::filesystem::remove_all (dir, e);
        }
    } cleanup { staging };
    for (const auto& item : m.items)
    {
        std::string why;
        if (! archive.extract (item.path, staging / item.path, item.bytes, item.sha256, why))
        {
            result.error = "the pack is damaged, nothing was changed: " + why;
            return result;
        }
    }
    std::set<std::string> keys;
    for (const auto& item : m.items)
    {
        keys.insert (item.uid);
        const auto staged = staging / item.path;
        const auto existing = catalog.assetForPackKey (m.id, item.uid);
        if (item.kind == PackItem::Kind::sound)
        {
            const auto place = "(pack:" + m.id + ")/" + item.uid;
            if (existing)
            {
                // An update: the same record (the user's favourite, tags, rating stay), the pack's audio.
                const auto current = catalog.sound (*existing);
                if (! current || current->contentHash != io::contentId (item.sha256))
                {
                    std::string why;
                    io::AudioFileInfo info;
                    if (! storeContent (places.storeDir, staged, item.sha256, why) || ! io::readAudioFileInfo (staged, info, why))
                    {
                        result.error = item.name + ": " + why;
                        return result;
                    }
                    SoundInfo sound;
                    sound.contentHash = io::contentId (item.sha256);
                    sound.format = lowerExtension (item.path).substr (1);
                    sound.sampleRate = info.sampleRate;
                    sound.channels = info.channels;
                    sound.bitDepth = info.bitDepth;
                    sound.frames = info.frames;
                    sound.durationSeconds = info.sampleRate > 0.0 ? static_cast<double> (info.frames) / info.sampleRate : 0.0;
                    sound.fileSize = item.bytes;
                    sound.provenance = "licensed";
                    if (! catalog.replaceSoundContent (*existing, sound, place))
                    {
                        result.error = "the Library could not update " + item.name;
                        return result;
                    }
                }
                catalog.rename (*existing, item.name);
                if (! item.category.empty())
                    catalog.setCategory (*existing, item.category);
                if (const auto a = catalog.asset (*existing); a && a->trashed)
                    catalog.restore (*existing);
            }
            else
            {
                ImportRequest request;
                request.file = staged;
                request.origin = origin;
                request.name = item.name;
                request.provenance = "licensed";   // under the pack's licence
                request.category = item.category;
                request.rootMidi = item.rootMidi;
                request.place = place;
                for (const auto& t : item.tags)
                    request.tags.push_back ({ t, TagSource::user, 1.0 });
                const auto imported = importSound (catalog, places.storeDir, request);
                if (! imported.ok || ! catalog.setPackKey (imported.assetId, m.id, item.uid))
                {
                    result.error = item.name + ": " + (imported.ok ? catalog.lastError() : imported.error);
                    return result;
                }
            }
            ++result.sounds;
            continue;
        }
        // A preset or template: the pack's own folder, its record by its key.
        const auto target = folder / (item.kind == PackItem::Kind::preset ? "Presets" : "Templates") / std::filesystem::path (item.path).filename();
        std::filesystem::create_directories (target.parent_path(), ec);
        std::filesystem::copy_file (staged, target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
        {
            result.error = "cannot write " + target.filename().string() + " (disk full or no permission?)";
            return result;
        }
        const auto info = presetInfoFor (target, item, root);
        std::optional<std::string> id = existing;
        if (existing)
        {
            if (! catalog.updatePreset (*existing, item.name, info))
            {
                result.error = "the Library could not update " + item.name;
                return result;
            }
            if (const auto a = catalog.asset (*existing); a && a->trashed)
                catalog.restore (*existing);
        }
        else
        {
            Asset asset;
            asset.type = item.kind == PackItem::Kind::preset ? AssetType::preset : AssetType::templateState;
            asset.origin = origin;
            asset.name = item.name;
            asset.category = item.category;
            id = catalog.addPreset (asset, info);
            if (! id || ! catalog.setPackKey (*id, m.id, item.uid))
            {
                result.error = "the Library could not record " + item.name;
                return result;
            }
        }
        if (! item.category.empty())
            catalog.setCategory (*id, item.category);
        for (const auto& t : item.tags)
            catalog.addTag (*id, t, TagSource::user, 1.0);
        ++(item.kind == PackItem::Kind::preset ? result.presets : result.templates);
    }
    // What an update no longer has: its records go (the pack's, never the user's); sounds
    // through the trash, so a user preset that uses one still finds its audio.
    for (const auto& [id, key] : catalog.packAssets (m.id))
    {
        if (keys.count (key) > 0)
            continue;
        const auto a = catalog.asset (id);
        if (! a)
            continue;
        catalog.trash (id);
        if (a->type != AssetType::sound)
        {
            if (const auto p = catalog.preset (id))
                std::filesystem::remove (p->file, ec);
            catalog.purge (id);
        }
        ++result.removedItems;
    }
    Catalog::PackRecord record;
    record.id = m.id;
    record.name = m.name;
    record.version = m.version;
    record.creator = m.creator;
    record.manifest = toJson (m);
    if (! catalog.putPack (record))
    {
        result.error = "the Library could not record the pack";
        return result;
    }
    if (result.upgraded)
        result.notes.push_back ("Updated from " + result.previousVersion + "; your favourites, ratings, tags and notes on its content are kept");
    result.ok = true;
    return result;
}

PackResult removePack (Catalog& catalog, const std::string& packId, const PackPlaces& places,
                       const std::function<bool (const std::filesystem::path&)>& moveToTrash)
{
    PackResult result;
    const auto record = catalog.pack (packId);
    if (! record)
    {
        result.error = "that pack is not installed";
        return result;
    }
    std::string why;
    if (const auto m = parsePackManifest (record->manifest, why))
        result.manifest = *m;
    result.manifest.id = packId;
    result.manifest.name = record->name;
    for (const auto& [id, key] : catalog.packAssets (packId))
    {
        const auto a = catalog.asset (id);
        if (! a)
            continue;
        if (! a->trashed)
            catalog.trash (id);
        if (a->type == AssetType::sound)
            ++result.sounds;
        else
        {
            catalog.purge (id);
            ++(a->type == AssetType::preset ? result.presets : result.templates);
        }
    }
    std::error_code ec;
    for (const auto& folder : { places.packsDir / packId, places.factoryDir / packId })
        if (std::filesystem::is_directory (folder, ec) && moveToTrash != nullptr && ! moveToTrash (folder))
            std::filesystem::remove_all (folder, ec);
    catalog.removePackRecord (packId);
    result.ok = true;
    return result;
}

std::vector<PackResult> installFactoryPacks (Catalog& catalog, const std::filesystem::path& folder, const PackPlaces& places)
{
    std::vector<PackResult> results;
    std::error_code ec;
    if (! std::filesystem::is_directory (folder, ec))
        return results;
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator (folder, ec))
        if (entry.is_regular_file (ec) && lowerExtension (entry.path().filename().string()) == ".osppack")
            files.push_back (entry.path());
    std::sort (files.begin(), files.end());
    for (const auto& file : files)
    {
        const auto look = inspectPack (catalog, file, places);
        // Only factory packs, only when new or newer; a broken one is reported, never fatal.
        if (look.ok && (! look.manifest.factory || look.alreadyInstalled))
            continue;
        results.push_back (look.ok ? installPack (catalog, file, places) : look);
    }
    return results;
}

//==============================================================================

PackResult exportPack (Catalog& catalog, const std::filesystem::path& storeDir, const PackExport& request, const std::filesystem::path& out,
                       const std::string& pluginVersion)
{
    PackResult result;
    PackManifest m;
    m.id = request.id;
    m.name = cleanText (request.name, 200);
    m.version = request.version;
    m.creator = cleanText (request.creator, 200);
    m.description = cleanText (request.description, 4000, true);
    m.license = cleanText (request.license, 20000, true);
    m.requiresPlugin = pluginVersion;
    if (! validPackId (m.id) || m.name.empty() || ! validVersion (m.version))
    {
        result.error = "the pack needs a name, an id (a-z, 0-9, '.', '_', '-') and a version (1.0.0)";
        return result;
    }
    // What goes in: the records, and every sound a preset needs (each content once).
    std::map<std::string, std::string> soundNames;   // content hash -> name
    std::vector<std::string> presets;
    for (const auto& id : request.assetIds)
    {
        const auto a = catalog.asset (id);
        if (! a || a->trashed)
            continue;
        if (a->type == AssetType::sound)
        {
            if (const auto s = catalog.sound (id))
                soundNames.emplace (s->contentHash, a->name);
        }
        else
            presets.push_back (id);
    }
    for (const auto& id : presets)
        if (const auto p = catalog.preset (id))
            for (const auto& hash : p->soundHashes)
                if (soundNames.count (hash) == 0)
                {
                    std::string name = hexOf (hash).substr (0, 12);
                    for (const auto& sid : catalog.soundsWithContent (hash))
                        if (const auto a = catalog.asset (sid); a && ! a->trashed)
                        {
                            name = a->name;
                            break;
                        }
                    soundNames.emplace (hash, name);
                }
    if (soundNames.empty() && presets.empty())
    {
        result.error = "there is nothing to put in the pack";
        return result;
    }
    // Rights (as for a portable instrument meant for sharing).
    std::vector<std::string> unknown, unknownHashes, licensed;
    for (const auto& [hash, name] : soundNames)
    {
        const auto rights = catalog.provenanceOf (hash);
        if (rights == "unknown")
            unknown.push_back (name), unknownHashes.push_back (hash);
        else if (rights == "licensed")
            licensed.push_back (name);
    }
    if (! unknown.empty() && ! request.confirmUnknownRights)
    {
        std::string list;
        for (std::size_t i = 0; i < unknown.size() && i < 12; ++i)
            list += (i > 0 ? ", " : "") + unknown[i];
        result.error = "these sounds have no known rights: " + list + (unknown.size() > 12 ? ", ..." : "") + ". Confirm that you may share them to export";
        return result;
    }
    for (const auto& hash : unknownHashes)
        catalog.setProvenance (hash, "cleared");   // the user's confirmation, recorded
    if (! licensed.empty())
        result.notes.push_back (std::to_string (licensed.size()) + " sounds came with other packs: check that their licences allow sharing");

    ArchiveWriter writer;
    std::set<std::string> leaves;
    for (const auto& [hash, name] : soundNames)
    {
        const auto where = resolveSound (catalog, storeDir, hash);
        if (! where.file)
        {
            result.error = name + " cannot be found (Missing sounds can relink it)";
            return result;
        }
        PackItem item;
        item.kind = PackItem::Kind::sound;
        item.sha256 = hexOf (hash);
        item.uid = "sound-" + item.sha256.substr (0, 20);
        item.path = "sounds/" + item.sha256 + lowerExtension (where.file->filename().string());
        std::error_code ec;
        item.bytes = static_cast<std::int64_t> (std::filesystem::file_size (*where.file, ec));
        item.name = name;
        for (const auto& sid : catalog.soundsWithContent (hash))
            if (const auto a = catalog.asset (sid); a && ! a->trashed)
            {
                item.category = a->category;
                for (const auto& t : catalog.tagsOf (sid))
                    if (item.tags.size() < 50)
                        item.tags.push_back (t.name);
                if (const auto s = catalog.sound (sid))
                    item.rootMidi = s->rootMidi;
                break;
            }
        writer.addFile (*where.file, item.path, false);
        m.items.push_back (std::move (item));
        ++result.sounds;
    }
    for (const auto& id : presets)
    {
        const auto a = catalog.asset (id);
        const auto p = catalog.preset (id);
        if (! a || ! p)
            continue;
        const std::filesystem::path file (p->file);
        const auto hex = io::sha256OfFile (file);
        if (! hex)
        {
            result.error = a->name + "'s file cannot be read (" + file.filename().string() + ")";
            return result;
        }
        PackItem item;
        item.kind = a->type == AssetType::templateState ? PackItem::Kind::templateState : PackItem::Kind::preset;
        item.uid = id;
        auto leaf = safeLeaf (a->name, item.kind == PackItem::Kind::preset ? ".osppreset" : ".ospstate");
        for (int n = 2; ! leaves.insert (leaf).second; ++n)
            leaf = safeLeaf (a->name + " " + std::to_string (n), item.kind == PackItem::Kind::preset ? ".osppreset" : ".ospstate");
        item.path = folderOf (item.kind) + leaf;
        item.sha256 = *hex;
        std::error_code ec;
        item.bytes = static_cast<std::int64_t> (std::filesystem::file_size (file, ec));
        item.name = a->name;
        item.category = a->category;
        for (const auto& t : catalog.tagsOf (id))
            if (item.tags.size() < 50)
                item.tags.push_back (t.name);
        for (const auto& hash : p->soundHashes)
            item.sounds.push_back (hexOf (hash));
        writer.addFile (file, item.path, true);
        m.items.push_back (std::move (item));
        ++(a->type == AssetType::templateState ? result.templates : result.presets);
    }
    writer.addText ("manifest.json", toJson (m));
    std::string why;
    if (! writer.writeTo (out, why))
    {
        result.error = why;
        return result;
    }
    result.manifest = m;
    result.ok = true;
    return result;
}

} // namespace osp::library
