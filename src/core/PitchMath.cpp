#include "core/PitchMath.h"

#include <cctype>
#include <charconv>
#include <cstdlib>

namespace osp
{

std::string midiNoteName (int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pitchClass = ((midiNote % 12) + 12) % 12;
    const int octave = (midiNote - pitchClass) / 12 - 1;
    return std::string (names[pitchClass]) + std::to_string (octave);
}

std::optional<double> parseNoteSpec (std::string_view text)
{
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.front())))
        text.remove_prefix (1);
    while (! text.empty() && std::isspace (static_cast<unsigned char> (text.back())))
        text.remove_suffix (1);

    if (text.empty())
        return std::nullopt;

    const char first = static_cast<char> (std::toupper (static_cast<unsigned char> (text.front())));

    if (first >= 'A' && first <= 'G')
    {
        static constexpr int classes[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        int pitchClass = classes[first - 'A'];
        std::size_t i = 1;

        while (i < text.size() && (text[i] == '#' || text[i] == 'b'))
        {
            pitchClass += text[i] == '#' ? 1 : -1;
            ++i;
        }

        int octave = 0;
        const auto rest = text.substr (i);
        const auto* begin = rest.data();
        const auto* end = rest.data() + rest.size();
        const auto [ptr, ec] = std::from_chars (begin, end, octave);

        if (ec != std::errc() || ptr != end || rest.empty())
            return std::nullopt;

        return static_cast<double> ((octave + 1) * 12 + pitchClass);
    }

    // Numeric MIDI value. std::from_chars for double is not available on every
    // supported Apple toolchain, so use strtod on a bounded copy.
    const std::string copy (text);
    char* endPtr = nullptr;
    const double value = std::strtod (copy.c_str(), &endPtr);

    if (endPtr != copy.c_str() + copy.size())
        return std::nullopt;

    if (value < -1.0 || value > 128.0)
        return std::nullopt;

    return value;
}

} // namespace osp
