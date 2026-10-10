#include "library/Archive.h"

#include "io/ContentHash.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>

namespace osp::library
{

bool safeRelativePath (const std::string& path, int maxDepth, std::string& why)
{
    if (path.empty() || path.size() > 1024)
    {
        why = "an empty or very long name";
        return false;
    }
    for (const unsigned char c : path)
        if (c < 0x20 || c == 0x7f || c == '\\' || c == ':')
        {
            why = "a name with a control character, a backslash or a colon";
            return false;
        }
    if (path.front() == '/')
    {
        why = "an absolute path";
        return false;
    }
    int depth = 0;
    std::size_t start = 0;
    while (start <= path.size())
    {
        const auto end = path.find ('/', start);
        const auto part = path.substr (start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == ".." || part.front() == '.' || part.size() > 255)
        {
            why = "a path with an empty, hidden or parent ('..') part";
            return false;
        }
        if (end == std::string::npos)
            break;
        ++depth;
        start = end + 1;
    }
    if (depth > maxDepth)
    {
        why = "a path nested too deep";
        return false;
    }
    return true;
}

std::string cleanText (const std::string& text, std::size_t maxBytes, bool multiline)
{
    // Valid UTF-8 only (anything else becomes '?'), then no control characters.
    std::string valid;
    valid.reserve (std::min<std::size_t> (text.size(), maxBytes * 2 + 16));
    for (std::size_t i = 0; i < text.size() && valid.size() < maxBytes * 2 + 16;)
    {
        const auto c = static_cast<unsigned char> (text[i]);
        const int length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 0;
        bool ok = length > 0 && i + static_cast<std::size_t> (length) <= text.size();
        for (int k = 1; ok && k < length; ++k)
            ok = (static_cast<unsigned char> (text[i + static_cast<std::size_t> (k)]) & 0xc0) == 0x80;
        if (ok && length == 2)
            ok = c >= 0xc2;   // no overlong forms
        if (ok)
            valid.append (text, i, static_cast<std::size_t> (length)), i += static_cast<std::size_t> (length);
        else
            valid += '?', ++i;
    }
    std::string out;
    out.reserve (std::min (valid.size(), maxBytes));
    for (const unsigned char c : valid)
    {
        if (c == '\n' && multiline)
            out += '\n';
        else if (c == '\t' || c == '\r' || c == '\n')
            out += ' ';
        else if (c >= 0x20 && c != 0x7f)
            out += static_cast<char> (c);
    }
    // Trim, then cut at a character boundary.
    const auto first = out.find_first_not_of (' ');
    const auto last = out.find_last_not_of (' ');
    out = first == std::string::npos ? std::string() : out.substr (first, last - first + 1);
    if (out.size() > maxBytes)
    {
        std::size_t cut = maxBytes;
        while (cut > 0 && (static_cast<unsigned char> (out[cut]) & 0xc0) == 0x80)
            --cut;
        out.resize (cut);
    }
    return out;
}

bool validPackId (const std::string& id)
{
    if (id.empty() || id.size() > 100 || id.front() == '.' || id.front() == '-')
        return false;
    return std::all_of (id.begin(), id.end(), [] (unsigned char c) { return std::islower (c) || std::isdigit (c) || c == '.' || c == '_' || c == '-'; });
}

namespace
{
    std::vector<long> versionNumbers (const std::string& v)
    {
        std::vector<long> numbers;
        std::size_t start = 0;
        while (start <= v.size())
        {
            const auto end = v.find ('.', start);
            const auto part = v.substr (start, end == std::string::npos ? std::string::npos : end - start);
            if (part.empty() || part.size() > 9 || ! std::all_of (part.begin(), part.end(), [] (unsigned char c) { return std::isdigit (c); }))
                return {};
            numbers.push_back (std::stol (part));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return numbers;
    }
}

bool validVersion (const std::string& version)
{
    const auto n = versionNumbers (version);
    return ! n.empty() && n.size() <= 4;
}

int compareVersions (const std::string& a, const std::string& b)
{
    auto x = versionNumbers (a), y = versionNumbers (b);
    const auto n = std::max (x.size(), y.size());
    x.resize (n, 0);
    y.resize (n, 0);
    for (std::size_t i = 0; i < n; ++i)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}

//==============================================================================

ArchiveReader::ArchiveReader (const std::filesystem::path& file, const ArchiveLimits& l) : limits (l)
{
    const juce::File f (juce::String (file.string()));
    if (! f.existsAsFile())
    {
        error = file.filename().string() + " is not there";
        return;
    }
    zip = std::make_unique<juce::ZipFile> (f);
    const int n = zip->getNumEntries();
    if (n == 0)
    {
        error = file.filename().string() + " is not a readable archive (or it is empty)";
        return;
    }
    if (n > limits.maxEntries)
    {
        error = "the archive has too many files (" + std::to_string (n) + ")";
        return;
    }
    std::int64_t total = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto* e = zip->getEntry (i);
        const auto name = e->filename.toStdString();
        if (! name.empty() && name.back() == '/')
            continue;   // a folder entry
        std::string why;
        if (e->isSymbolicLink)
        {
            error = "the archive contains a link (" + cleanText (name, 200) + "), which is never followed";
            return;
        }
        if (! safeRelativePath (name, limits.maxDepth, why))
        {
            error = "the archive contains an unsafe entry (" + cleanText (name, 200) + ": " + why + ")";
            return;
        }
        if (e->uncompressedSize < 0 || e->uncompressedSize > limits.maxEntryBytes)
        {
            error = "the archive contains a file that is too large (" + cleanText (name, 200) + ")";
            return;
        }
        total += e->uncompressedSize;
        if (total > limits.maxTotalBytes)
        {
            error = "the archive unpacks to more than its limit";
            return;
        }
        if (! index.emplace (name, std::make_pair (i, static_cast<std::int64_t> (e->uncompressedSize))).second)
        {
            error = "the archive names a file twice (" + cleanText (name, 200) + ")";
            return;
        }
    }
}

ArchiveReader::~ArchiveReader() = default;

std::int64_t ArchiveReader::size (const std::string& name) const
{
    const auto it = index.find (name);
    return it == index.end() ? -1 : it->second.second;
}

std::vector<std::string> ArchiveReader::names() const
{
    std::vector<std::string> list;
    for (const auto& [name, entry] : index)
        list.push_back (name);
    return list;
}

std::optional<std::string> ArchiveReader::readText (const std::string& name, std::int64_t maxBytes) const
{
    const auto it = index.find (name);
    if (! ok() || it == index.end() || it->second.second > maxBytes)
        return std::nullopt;
    std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (it->second.first));
    if (in == nullptr)
        return std::nullopt;
    juce::MemoryBlock block;
    in->readIntoMemoryBlock (block, static_cast<juce::pointer_sized_int> (maxBytes) + 1);
    if (static_cast<std::int64_t> (block.getSize()) > maxBytes)
        return std::nullopt;
    return block.toString().toStdString();
}

bool ArchiveReader::extract (const std::string& name, const std::filesystem::path& target, std::int64_t expectedBytes, const std::string& expectedSha256,
                             std::string& why) const
{
    const auto it = index.find (name);
    if (! ok() || it == index.end())
    {
        why = cleanText (name, 200) + " is missing from the archive";
        return false;
    }
    if (expectedBytes >= 0 && it->second.second != expectedBytes)
    {
        why = cleanText (name, 200) + " has the wrong size (the archive is damaged or was changed)";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories (target.parent_path(), ec);
    auto temp = target;
    temp += ".partial";
    {
        std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (it->second.first));
        juce::File out (juce::String (temp.string()));
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (in == nullptr || ! stream.openedOk())
        {
            why = "cannot write " + target.filename().string() + " (disk full or no permission?)";
            return false;
        }
        // At most the size the archive declared (a lying header cannot fill the disk).
        const auto written = stream.writeFromInputStream (*in, it->second.second + 1);
        stream.flush();
        if (written != it->second.second || stream.getStatus().failed())
        {
            stream.flush();
            out.deleteFile();
            why = "cannot unpack " + cleanText (name, 200) + " (damaged, or the disk is full)";
            return false;
        }
    }
    if (! expectedSha256.empty())
    {
        const auto hash = io::sha256OfFile (temp);
        if (! hash || *hash != expectedSha256)
        {
            std::filesystem::remove (temp, ec);
            why = cleanText (name, 200) + " does not match its checksum (the archive is damaged or was changed)";
            return false;
        }
    }
    std::filesystem::rename (temp, target, ec);
    if (ec)
    {
        std::filesystem::remove (temp, ec);
        why = "cannot place " + target.filename().string();
        return false;
    }
    return true;
}

//==============================================================================

ArchiveWriter::ArchiveWriter() = default;
ArchiveWriter::~ArchiveWriter() = default;

void ArchiveWriter::addFile (const std::filesystem::path& source, const std::string& name, bool compress)
{
    entries.push_back ({ source, name, {}, compress, false });
}

void ArchiveWriter::addText (const std::string& name, const std::string& text)
{
    entries.push_back ({ {}, name, text, true, true });
}

bool ArchiveWriter::writeTo (const std::filesystem::path& out, std::string& why)
{
    juce::ZipFile::Builder builder;
    for (const auto& e : entries)
    {
        if (e.isText)
            builder.addEntry (new juce::MemoryInputStream (e.text.data(), e.text.size(), true), 9, juce::String (e.name), juce::Time::getCurrentTime());
        else
            builder.addFile (juce::File (juce::String (e.source.string())), e.compress ? 6 : 0, juce::String (e.name));
    }
    auto temp = out;
    temp += ".partial";
    const juce::File tempFile (juce::String (temp.string()));
    tempFile.deleteFile();
    {
        juce::FileOutputStream stream (tempFile);
        if (! stream.openedOk() || ! builder.writeToStream (stream, nullptr))
        {
            tempFile.deleteFile();
            why = "cannot write " + out.filename().string() + " (disk full or no permission?)";
            return false;
        }
    }
    // Read back before it replaces anything.
    {
        juce::ZipFile check (tempFile);
        if (check.getNumEntries() != static_cast<int> (entries.size()))
        {
            tempFile.deleteFile();
            why = "the written archive could not be read back";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename (temp, out, ec);
    if (ec)
    {
        tempFile.deleteFile();
        why = "cannot place " + out.filename().string();
        return false;
    }
    return true;
}

} // namespace osp::library
