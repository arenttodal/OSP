#pragma once

#include <map>
#include <set>
#include <string>

namespace osp::andorcli
{

/** ANDOR Lab commands of research-renderer (docs/andor-lab/test-instructions.md). */
extern const char* usageText;

/** Options that take no value. */
const std::set<std::string>& flagOptions();

/** True when the arguments ask for an ANDOR Lab command. */
bool handles (const std::map<std::string, std::string>& values, const std::set<std::string>& flags);

/** Runs it; returns the process exit code (0 ok, 1 usage, 2 input, 3 processing). */
int run (const std::map<std::string, std::string>& values, const std::set<std::string>& flags);

} // namespace osp::andorcli
