#include "MemoSources.h"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>

namespace osp::memolab
{

namespace fs = std::filesystem;

juce::File appDataFolder()
{
    if (const char* overridePath = std::getenv ("MEMO_LAB_DATA_DIR"); overridePath != nullptr && *overridePath != 0)
        return juce::File (juce::String::fromUTF8 (overridePath));
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ANDOR").getChildFile ("Memo Lab");
}

juce::File voiceMemosFolder()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("Library/Group Containers/group.com.apple.VoiceMemos.shared/Recordings");
}

//==============================================================================
SourceSettings::SourceSettings (juce::File settingsFile) : file (std::move (settingsFile))
{
    load();
}

void SourceSettings::load()
{
    approved.clear();
    const auto parsed = juce::JSON::parse (file);
    if (auto* list = parsed.getProperty ("folders", {}).getArray())
        for (const auto& path : *list)
            if (path.isString() && juce::File::isAbsolutePath (path.toString()))
                approved.addIfNotAlreadyThere (juce::File (path.toString()));
    voiceMemosOffered = static_cast<bool> (parsed.getProperty ("voiceMemosOffered", false));
}

void SourceSettings::save() const
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> list;
    for (const auto& f : approved)
        list.add (f.getFullPathName());
    root->setProperty ("schemaVersion", 1);
    root->setProperty ("folders", list);
    root->setProperty ("voiceMemosOffered", voiceMemosOffered);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

void SourceSettings::addFolder (const juce::File& folder)
{
    approved.addIfNotAlreadyThere (folder);
    save();
}

void SourceSettings::removeFolder (const juce::File& folder)
{
    approved.removeAllInstancesOf (folder);
    save();
}

//==============================================================================
namespace
{
    constexpr int maxDepth = 4;
    constexpr double stillWritingSeconds = 3.0;

    bool isPackage (const fs::path& p)
    {
        static const std::vector<std::string> packages { ".app", ".bundle", ".photoslibrary", ".logicx", ".band", ".framework", ".plugin", ".component", ".vst3" };
        auto ext = p.extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return std::find (packages.begin(), packages.end(), ext) != packages.end();
    }

    bool inside (const fs::path& child, const fs::path& root)
    {
        const auto c = child.lexically_normal().string(), r = root.lexically_normal().string();
        return c.size() > r.size() && c.compare (0, r.size(), r) == 0 && (r.back() == '/' || c[r.size()] == '/');
    }

    /** "name.m4a" from an iCloud placeholder ".name.m4a.icloud", else empty. */
    juce::String placeholderTarget (const juce::String& name)
    {
        if (name.startsWithChar ('.') && name.endsWithIgnoreCase (".icloud"))
            return name.substring (1, name.length() - 7);
        return {};
    }

    void walk (const fs::path& dir, const fs::path& root, const juce::File& approved, int depth, ScanResult& result, const std::map<juce::String, juce::String>& titles,
               const juce::Time& now, const std::atomic<bool>& cancelled)
    {
        std::error_code ec;
        fs::directory_iterator it (dir, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            return;
        for (const auto& entry : it)
        {
            if (cancelled.load())
                return;
            const auto path = entry.path();
            const auto name = juce::String::fromUTF8 (path.filename().string().c_str());
            // A link is followed only while it stays inside the approved folder.
            fs::path real = path;
            if (entry.is_symlink (ec))
            {
                real = fs::weakly_canonical (path, ec);
                if (ec || ! inside (real, root))
                    continue;
            }
            const bool isDir = fs::is_directory (real, ec);
            if (isDir)
            {
                if (name.startsWithChar ('.') || isPackage (real) || depth >= maxDepth)
                    continue;
                walk (real, root, approved, depth + 1, result, titles, now, cancelled);
                continue;
            }
            if (const auto target = placeholderTarget (name); target.isNotEmpty())
            {
                const juce::File placeholder (juce::String::fromUTF8 (path.string().c_str()));
                if (! isSupported (placeholder.getSiblingFile (target)))
                    continue;
                Recording r;
                r.file = placeholder.getSiblingFile (target);
                r.title = titles.count (target) > 0 ? titles.at (target) : titleFromFileName (juce::File::createFileWithoutCheckingPath (target).getFileNameWithoutExtension());
                const auto named = dateFromFileName (juce::File::createFileWithoutCheckingPath (target).getFileNameWithoutExtension());
                r.date = named ? *named : placeholder.getLastModificationTime();
                r.state = Recording::State::notDownloaded;
                r.problem = "Not downloaded to this Mac yet. Open it once in Voice Memos (or Finder) to download it.";
                r.folder = approved;
                result.recordings.push_back (std::move (r));
                continue;
            }
            if (name.startsWithChar ('.'))
                continue;
            const juce::File file (juce::String::fromUTF8 (real.string().c_str()));
            if (! isSupported (file))
                continue;
            Recording r;
            r.file = file;
            r.folder = approved;
            r.title = titles.count (name) > 0 ? titles.at (name) : titleFromFileName (file.getFileNameWithoutExtension());
            // The moment in a Voice Memos name is when it was recorded (a copy's file time is not).
            if (const auto named = dateFromFileName (file.getFileNameWithoutExtension()))
                r.date = *named;
            else
            {
                r.date = file.getCreationTime();
                if (r.date.toMilliseconds() <= 0)
                    r.date = file.getLastModificationTime();
            }
            if ((now - file.getLastModificationTime()).inSeconds() < stillWritingSeconds)
            {
                r.state = Recording::State::changing;
                r.problem = "Still being written or synced. Refresh in a moment.";
            }
            else
            {
                AudioInfo info;
                juce::String error;
                if (probe (file, info, error))
                {
                    r.seconds = info.seconds();
                    r.sampleRate = info.sampleRate;
                    r.channels = info.channels;
                }
                else
                {
                    r.state = Recording::State::unreadable;
                    r.problem = "Cannot be read: " + error + ".";
                }
            }
            result.recordings.push_back (std::move (r));
        }
    }
}

FolderReport checkFolder (const juce::File& folder)
{
    FolderReport report;
    report.folder = folder;
    report.exists = folder.isDirectory();
    if (! report.exists)
    {
        report.message = "Not found on this Mac.";
        return report;
    }
    std::error_code ec;
    fs::directory_iterator it (fs::path (folder.getFullPathName().toStdString()), ec);
    report.readable = ! ec;
    if (! report.readable)
        report.message = "macOS does not let Memo Lab read this folder. Allow it in System Settings > Privacy & Security, or choose another folder.";
    return report;
}

ScanResult scanFolders (const juce::Array<juce::File>& roots, const std::atomic<bool>& cancelled, juce::Time now)
{
    ScanResult result;
    for (const auto& folder : roots)
    {
        auto report = checkFolder (folder);
        if (report.readable)
        {
            const auto titles = folder == voiceMemosFolder() ? voiceMemosTitles (folder) : std::map<juce::String, juce::String> {};
            const auto before = result.recordings.size();
            std::error_code ec;
            const auto root = fs::weakly_canonical (fs::path (folder.getFullPathName().toStdString()), ec);
            walk (root, root, folder, 0, result, titles, now, cancelled);
            report.recordings = static_cast<int> (result.recordings.size() - before);
            report.message = report.recordings == 0 ? juce::String ("No recordings here (or none this Mac can read).")
                                                    : juce::String (report.recordings) + (report.recordings == 1 ? " recording" : " recordings");
        }
        result.folders.push_back (report);
        if (cancelled.load())
            break;
    }
    std::stable_sort (result.recordings.begin(), result.recordings.end(), [] (const Recording& a, const Recording& b) { return a.date > b.date; });
    return result;
}

std::optional<juce::Time> dateFromFileName (const juce::String& name)
{
    // Voice Memos names its files by the moment they were made: "YYYYMMDD HHMMSS[-ID]".
    if (name.length() >= 15 && name.substring (0, 8).containsOnly ("0123456789") && name[8] == ' ' && name.substring (9, 15).containsOnly ("0123456789"))
    {
        const int y = name.substring (0, 4).getIntValue(), mo = name.substring (4, 6).getIntValue(), d = name.substring (6, 8).getIntValue();
        const int h = name.substring (9, 11).getIntValue(), mi = name.substring (11, 13).getIntValue(), sec = name.substring (13, 15).getIntValue();
        if (y >= 2000 && mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h < 24 && mi < 60 && sec < 60)
            return juce::Time (y, mo - 1, d, h, mi, sec, 0, true);
    }
    return std::nullopt;
}

juce::String titleFromFileName (const juce::String& name)
{
    // Voice Memos names its files by the moment they were made: "YYYYMMDD HHMMSS[-ID]".
    if (name.length() >= 15 && name.substring (0, 8).containsOnly ("0123456789") && name[8] == ' ' && name.substring (9, 15).containsOnly ("0123456789"))
    {
        const int y = name.substring (0, 4).getIntValue(), mo = name.substring (4, 6).getIntValue(), d = name.substring (6, 8).getIntValue();
        const int h = name.substring (9, 11).getIntValue(), mi = name.substring (11, 13).getIntValue();
        if (mo >= 1 && mo <= 12 && d >= 1 && d <= 31 && h < 24 && mi < 60)
        {
            static const char* months[] { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
            return juce::String (d) + " " + months[mo - 1] + " " + juce::String (y) + ", " + juce::String (h).paddedLeft ('0', 2) + ":" + juce::String (mi).paddedLeft ('0', 2);
        }
    }
    return name;
}

std::map<juce::String, juce::String> voiceMemosTitles (const juce::File& recordingsFolder)
{
    // Optional and read-only: "immutable" opens the file without locks or journals, so Voice
    // Memos is never disturbed; any surprise (another schema, no file) leaves the titles out.
    std::map<juce::String, juce::String> titles;
    const auto db = recordingsFolder.getChildFile ("CloudRecordings.db");
    if (! db.existsAsFile())
        return titles;
    sqlite3* handle = nullptr;
    const auto uri = "file:" + db.getFullPathName() + "?mode=ro&immutable=1";
    if (sqlite3_open_v2 (uri.toRawUTF8(), &handle, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK)
    {
        sqlite3_close (handle);
        return titles;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2 (handle, "SELECT ZPATH, ZENCRYPTEDTITLE FROM ZCLOUDRECORDING", -1, &statement, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step (statement) == SQLITE_ROW)
        {
            const auto* path = reinterpret_cast<const char*> (sqlite3_column_text (statement, 0));
            const auto* title = reinterpret_cast<const char*> (sqlite3_column_text (statement, 1));
            if (path == nullptr || title == nullptr || *title == 0)
                continue;
            const auto fileName = juce::File::createFileWithoutCheckingPath (juce::String::fromUTF8 (path)).getFileName();
            titles[fileName] = juce::String::fromUTF8 (title);
        }
    }
    sqlite3_finalize (statement);
    sqlite3_close (handle);
    return titles;
}

} // namespace osp::memolab
