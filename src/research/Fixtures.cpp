#include "research/Fixtures.h"

#include "midi/MidiFileIO.h"

#include <algorithm>
#include <cmath>

namespace osp::research
{

bool writeFixtureFiles (const std::filesystem::path& directory, std::vector<std::string>& written, std::string& error)
{
    for (const auto& name : fixtures::allNames())
    {
        const auto sequence = fixtures::byName (name, 60);
        const auto path = directory / (name + ".mid");
        if (! sequence || ! io::writeMidiFile (path, *sequence, error))
            return false;
        written.push_back (path.string());
    }
    return true;
}

int fixtureReferenceNote (double rootMidi)
{
    return std::clamp (static_cast<int> (std::lround (rootMidi)), 12, 108);
}

} // namespace osp::research
