#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace osp::io
{

/** Lower-case hex SHA-256 of a file's bytes, or nullopt if it cannot be read. */
std::optional<std::string> sha256OfFile (const std::filesystem::path& path);

/** Lower-case hex SHA-256 of a memory block. */
std::string sha256OfBytes (const void* data, std::size_t size);

/** Stable content identifier: "sha256:<hex>". */
inline std::string contentId (const std::string& sha256Hex) { return "sha256:" + sha256Hex; }

} // namespace osp::io
