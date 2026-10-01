#include "io/ContentHash.h"

#include <juce_cryptography/juce_cryptography.h>

namespace osp::io
{

std::optional<std::string> sha256OfFile (const std::filesystem::path& path)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec))
        return std::nullopt;

    const auto absolute = std::filesystem::absolute (path, ec);
    juce::File file (juce::String::fromUTF8 (absolute.string().c_str()));
    juce::FileInputStream stream (file);
    if (! stream.openedOk())
        return std::nullopt;

    return juce::SHA256 (stream).toHexString().toLowerCase().removeCharacters (" ").toStdString();
}

std::string sha256OfBytes (const void* data, std::size_t size)
{
    return juce::SHA256 (data, size).toHexString().toLowerCase().removeCharacters (" ").toStdString();
}

} // namespace osp::io
