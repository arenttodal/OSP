#pragma once

#include "midi/MidiFixtures.h"

#include <filesystem>
#include <string>
#include <vector>

namespace osp::research
{

/**
    Writes every standard fixture as a .mid file (reference C4) plus a README. These
    files are committed in research/midi/ so external tools (DAWs) can use them; the
    renderer itself generates fixtures programmatically at the source root.
*/
bool writeFixtureFiles (const std::filesystem::path& directory, std::vector<std::string>& written, std::string& error);

/** Reference note for fixtures rendered against a source: the root rounded and clamped to 12..108. */
int fixtureReferenceNote (double rootMidi);

} // namespace osp::research
