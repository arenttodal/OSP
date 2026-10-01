#include "research/CorpusIndex.h"

#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "io/JsonUtil.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace osp::research
{

std::string CorpusEntry::folderName() const
{
    std::string stem = std::filesystem::path (filename).stem().string();
    std::string clean;
    for (unsigned char c : stem)
        clean += (std::isalnum (c) || c == '-' || c == '_') ? static_cast<char> (c) : '_';
    if (clean.empty())
        clean = "source";
    if (clean.size() > 48)
        clean.resize (48);
    const auto hex = id.size() > 7 ? id.substr (7, 8) : std::string ("nohash00");
    return clean + "__" + hex;
}

CorpusIndex buildCorpusIndex (const std::filesystem::path& root)
{
    CorpusIndex index;
    index.root = root.string();

    std::error_code ec;
    if (! std::filesystem::is_directory (root, ec))
        return index;

    for (auto it = std::filesystem::recursive_directory_iterator (root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment (ec))
    {
        if (ec)
            break;
        const auto& path = it->path();
        const auto name = path.filename().string();
        if (! name.empty() && name.front() == '.')
        {
            if (it->is_directory (ec))
                it.disable_recursion_pending();
            continue;
        }
        if (! it->is_regular_file (ec))
            continue;
        if (name == "index.json" || name.rfind ("README", 0) == 0 || name.rfind ("MANIFEST", 0) == 0)
            continue;

        CorpusEntry entry;
        entry.filename = name;
        entry.relativePath = std::filesystem::relative (path, root, ec).generic_string();
        auto ext = path.extension().string();
        if (! ext.empty())
            ext.erase (0, 1);
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        entry.extension = ext;
        entry.sizeBytes = std::filesystem::file_size (path, ec);
        entry.supported = io::isSupportedAudioExtension (path);

        if (const auto hash = io::sha256OfFile (path))
        {
            entry.id = io::contentId (*hash);
            entry.shortId = hash->substr (0, 12);
        }
        else
            entry.error = "could not read file for hashing";

        index.files.push_back (std::move (entry));
    }

    std::sort (index.files.begin(), index.files.end(), [] (const auto& a, const auto& b) { return a.relativePath < b.relativePath; });

    std::map<std::string, std::string> firstById;
    for (auto& entry : index.files)
    {
        if (entry.id.empty())
            continue;
        const auto [it, inserted] = firstById.emplace (entry.id, entry.relativePath);
        if (! inserted)
            entry.duplicateOf = it->second;
    }
    return index;
}

juce::var corpusIndexToJson (const CorpusIndex& index)
{
    using json::set;
    using json::str;
    auto root = json::object();
    set (root, "schemaVersion", CorpusIndex::schemaVersion);
    set (root, "root", str (index.root));
    auto files = json::array();
    for (const auto& e : index.files)
    {
        auto item = json::object();
        set (item, "id", str (e.id));
        set (item, "path", str (e.relativePath));
        set (item, "filename", str (e.filename));
        set (item, "extension", str (e.extension));
        set (item, "sizeBytes", static_cast<juce::int64> (e.sizeBytes));
        set (item, "supported", e.supported);
        if (! e.duplicateOf.empty())
            set (item, "duplicateOf", str (e.duplicateOf));
        if (! e.error.empty())
            set (item, "error", str (e.error));
        files.append (item);
    }
    set (root, "files", files);
    return root;
}

} // namespace osp::research
