#pragma once

#include <string>

namespace osp
{

/**
    Where a render was made ("linux-x86_64-gcc", "macos-arm64-clang"). Exact sample hashes
    are only comparable on the platform that stored them: other compilers and FPUs round
    differently (Apple Silicon fuses multiply-adds, for one), so reference renders store it.
*/
inline std::string platformTag()
{
#if defined(__APPLE__)
    const std::string os = "macos";
#elif defined(_WIN32)
    const std::string os = "windows";
#else
    const std::string os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const std::string arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const std::string arch = "x86_64";
#else
    const std::string arch = "other";
#endif
#if defined(__clang__)
    const std::string compiler = "clang";
#elif defined(__GNUC__)
    const std::string compiler = "gcc";
#elif defined(_MSC_VER)
    const std::string compiler = "msvc";
#else
    const std::string compiler = "other";
#endif
    return os + "-" + arch + "-" + compiler;
}

} // namespace osp
