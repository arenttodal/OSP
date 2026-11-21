#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

namespace osp
{

/**
    Paths and text. Every name OSP keeps (the catalog, JSON, presets, the interface, JUCE's
    Strings, SQLite's file names) is UTF-8. `std::filesystem::path` reads and writes a narrow
    std::string in the system's ANSI code page on Windows, which garbles every letter outside
    ASCII (a user folder named with an ø, a sample called "Kölner Glocke") - so OSP never
    converts with the narrow forms; it goes through these. On macOS and Linux they are the same
    bytes either way.
*/
inline std::filesystem::path pathFromUtf8 (std::string_view utf8)
{
    return std::filesystem::path (std::u8string (utf8.begin(), utf8.end()));
}

/** The path as UTF-8 (the native separators: backslashes on Windows). */
inline std::string utf8Of (const std::filesystem::path& path)
{
    const auto u = path.u8string();
    return std::string (u.begin(), u.end());
}

/** The path as UTF-8 with forward slashes (relative names in archives, packs, backups). */
inline std::string utf8GenericOf (const std::filesystem::path& path)
{
    const auto u = path.generic_u8string();
    return std::string (u.begin(), u.end());
}

/** An environment variable naming a folder (empty if unset). Windows: read in UTF-16, so a
    user folder with any letter in it comes through whole. */
inline std::filesystem::path pathFromEnvironment (const char* name)
{
#if defined(_WIN32)
    std::wstring wide (name, name + std::char_traits<char>::length (name));
    const wchar_t* value = ::_wgetenv (wide.c_str());
    return value != nullptr && *value != 0 ? std::filesystem::path (value) : std::filesystem::path();
#else
    const char* value = std::getenv (name);
    return value != nullptr && *value != 0 ? std::filesystem::path (value) : std::filesystem::path();
#endif
}

} // namespace osp
