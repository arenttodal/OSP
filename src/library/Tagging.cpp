#include "library/Tagging.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace osp::library
{

namespace
{
    struct Term
    {
        const char* tag;     ///< the tag it gives
        const char* type;    ///< the sound Type it suggests ("" = none)
    };

    /** Words musicians put in sample names, and what they say. Plural and short forms map to
        one tag, so "Pads", "pad" and "PAD" are the same evidence. */
    const std::map<std::string, Term>& dictionary()
    {
        static const std::map<std::string, Term> terms {
            { "bass", { "bass", "Bass" } }, { "basses", { "bass", "Bass" } }, { "sub", { "bass", "Bass" } }, { "808", { "bass", "Bass" } },
            { "pad", { "pad", "Pad" } }, { "pads", { "pad", "Pad" } }, { "atmos", { "pad", "Pad" } },
            { "pluck", { "pluck", "Pluck" } }, { "plucks", { "pluck", "Pluck" } }, { "plk", { "pluck", "Pluck" } }, { "pizz", { "pluck", "Pluck" } },
            { "harp", { "pluck", "Pluck" } }, { "guitar", { "guitar", "Pluck" } }, { "kalimba", { "pluck", "Pluck" } },
            { "string", { "strings", "" } }, { "strings", { "strings", "" } }, { "violin", { "strings", "" } }, { "viola", { "strings", "" } },
            { "cello", { "strings", "" } }, { "bow", { "bowed", "" } }, { "bowed", { "bowed", "" } },
            { "voice", { "vocal", "Vocal" } }, { "vox", { "vocal", "Vocal" } }, { "vocal", { "vocal", "Vocal" } }, { "vocals", { "vocal", "Vocal" } },
            { "choir", { "choir", "Vocal" } }, { "sing", { "vocal", "Vocal" } }, { "hum", { "vocal", "Vocal" } }, { "vowel", { "vocal", "Vocal" } },
            { "synth", { "synth", "" } }, { "lead", { "lead", "" } }, { "saw", { "synth", "" } },
            { "texture", { "texture", "Texture" } }, { "textures", { "texture", "Texture" } }, { "atmo", { "texture", "Texture" } },
            { "ambience", { "texture", "Texture" } }, { "ambient", { "texture", "Texture" } }, { "drone", { "drone", "Texture" } },
            { "noise", { "noise", "Texture" } }, { "field", { "field recording", "Texture" } }, { "foley", { "foley", "Texture" } },
            { "flux", { "flux", "" } },
            { "kick", { "kick", "Percussion" } }, { "bd", { "kick", "Percussion" } }, { "snare", { "snare", "Percussion" } }, { "sd", { "snare", "Percussion" } },
            { "hat", { "hat", "Percussion" } }, { "hihat", { "hat", "Percussion" } }, { "hh", { "hat", "Percussion" } }, { "clap", { "clap", "Percussion" } },
            { "tom", { "tom", "Percussion" } }, { "perc", { "percussion", "Percussion" } }, { "percussion", { "percussion", "Percussion" } },
            { "drum", { "drum", "Percussion" } }, { "drums", { "drum", "Percussion" } }, { "cymbal", { "cymbal", "Percussion" } },
            { "crash", { "cymbal", "Percussion" } }, { "ride", { "cymbal", "Percussion" } }, { "shaker", { "shaker", "Percussion" } },
            { "rim", { "rim", "Percussion" } }, { "snap", { "snap", "Percussion" } },
            { "piano", { "piano", "Keys" } }, { "keys", { "keys", "Keys" } }, { "key", { "keys", "Keys" } }, { "rhodes", { "keys", "Keys" } },
            { "organ", { "organ", "Keys" } }, { "epiano", { "keys", "Keys" } }, { "ep", { "keys", "Keys" } }, { "bell", { "bell", "Keys" } },
            { "bells", { "bell", "Keys" } }, { "mallet", { "mallet", "Keys" } }, { "marimba", { "mallet", "Keys" } }, { "vibes", { "mallet", "Keys" } },
            { "brass", { "brass", "" } }, { "horn", { "brass", "" } }, { "flute", { "flute", "" } }, { "wind", { "wind", "" } },
            { "fx", { "fx", "" } }, { "sfx", { "fx", "" } }, { "riser", { "fx", "" } }, { "impact", { "fx", "" } },
        };
        return terms;
    }

    bool isSeparator (char c)
    {
        return ! std::isalnum (static_cast<unsigned char> (c)) && c != '#';
    }

    /** Raw tokens: split on separators only ("F#1" stays whole). */
    std::vector<std::string> rawTokens (const std::string& name)
    {
        std::vector<std::string> out;
        std::string current;
        for (const char c : name)
        {
            if (isSeparator (c))
            {
                if (! current.empty())
                    out.push_back (current);
                current.clear();
            }
            else
                current += c;
        }
        if (! current.empty())
            out.push_back (current);
        return out;
    }

    std::string stem (const std::string& fileName)
    {
        const auto dot = fileName.find_last_of ('.');
        return dot == std::string::npos || dot == 0 ? fileName : fileName.substr (0, dot);
    }

    void addTag (std::vector<InferredTag>& tags, const std::string& tag, double confidence)
    {
        for (auto& t : tags)
            if (t.tag == tag)
            {
                t.confidence = std::max (t.confidence, confidence);
                return;
            }
        tags.push_back ({ tag, confidence });
    }

    void collect (NameEvidence& e, const std::string& text, double confidence)
    {
        std::map<std::string, int> typeVotes;
        for (const auto& token : rawTokens (text))
            if (const auto note = noteFromWord (token); note && ! e.rootMidi)
                e.rootMidi = static_cast<double> (*note);
        const auto words = nameWords (text);
        for (std::size_t i = 0; i < words.size(); ++i)
        {
            const auto& w = words[i];
            if (w == "loop" || w == "loops" || w == "bpm" || (w.size() > 3 && w.compare (w.size() - 3, 3, "bpm") == 0))
                e.saysLoop = true;
            if (w == "oneshot" || w == "hit" || w == "shot" || (w == "one" && i + 1 < words.size() && words[i + 1] == "shot"))
                e.saysOneShot = true;
            auto it = dictionary().find (w);
            if (it == dictionary().end() && w.size() > 3 && w.back() == 's')
                it = dictionary().find (w.substr (0, w.size() - 1));   // "Snares", "Kicks"
            if (it != dictionary().end())
            {
                addTag (e.tags, it->second.tag, confidence);
                if (*it->second.type != 0)
                    ++typeVotes[it->second.type];
            }
        }
        if (e.saysLoop)
            addTag (e.tags, "loop", confidence);
        if (e.saysOneShot)
            addTag (e.tags, "one-shot", confidence);
        // The Type most words point to; a tie (a "bass pad") is left to the audio.
        int best = 0;
        bool tie = false;
        for (const auto& [type, votes] : typeVotes)
        {
            if (votes > best)
            {
                best = votes;
                e.type = type;
                tie = false;
            }
            else if (votes == best)
                tie = true;
        }
        if (tie)
            e.type.clear();
        e.typeConfidence = e.type.empty() ? 0.0 : confidence;
    }
}

std::optional<int> noteFromWord (const std::string& word)
{
    // A letter, an optional sharp or flat, an octave digit: C3, F#1, Bb4, A-1 (with a minus
    // only when the word is exactly that). "Take2" or "B52" are not notes.
    if (word.size() < 2 || word.size() > 4)
        return std::nullopt;
    const char letter = static_cast<char> (std::toupper (static_cast<unsigned char> (word[0])));
    static const int offsets[] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    if (letter < 'A' || letter > 'G')
        return std::nullopt;
    std::size_t i = 1;
    int accidental = 0;
    if (word[i] == '#' || word[i] == 's')
        accidental = 1, ++i;
    else if (word[i] == 'b' && i + 1 < word.size())
        accidental = -1, ++i;
    bool negative = false;
    if (i < word.size() && word[i] == '-')
        negative = true, ++i;
    if (i + 1 != word.size() || ! std::isdigit (static_cast<unsigned char> (word[i])))
        return std::nullopt;
    const int octave = (word[i] - '0') * (negative ? -1 : 1);
    if (octave > 8)
        return std::nullopt;
    const int midi = (octave + 1) * 12 + offsets[letter - 'A'] + accidental;
    return midi >= 0 && midi <= 127 ? std::optional<int> (midi) : std::nullopt;
}

std::vector<std::string> nameWords (const std::string& name)
{
    std::vector<std::string> out;
    std::string current;
    auto flush = [&] {
        if (! current.empty())
        {
            std::string lower;
            for (const char c : current)
                lower += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
            out.push_back (lower);
        }
        current.clear();
    };
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        if (! std::isalnum (static_cast<unsigned char> (c)))
        {
            flush();
            continue;
        }
        if (! current.empty())
        {
            const char prev = current.back();
            const bool caseChange = std::islower (static_cast<unsigned char> (prev)) && std::isupper (static_cast<unsigned char> (c));
            const bool kindChange = std::isdigit (static_cast<unsigned char> (prev)) != std::isdigit (static_cast<unsigned char> (c));
            // "120bpm" stays one word (a tempo), "Pad120" splits.
            const bool tempo = std::isdigit (static_cast<unsigned char> (prev)) && name.compare (i, 3, "bpm") == 0;
            if ((caseChange || kindChange) && ! tempo)
                flush();
        }
        current += c;
    }
    flush();
    return out;
}

NameEvidence evidenceFromName (const std::string& fileName)
{
    NameEvidence e;
    collect (e, stem (fileName), 0.75);
    return e;
}

NameEvidence evidenceFromFolders (const std::filesystem::path& file, const std::filesystem::path& root)
{
    NameEvidence e;
    std::error_code ec;
    const auto relative = std::filesystem::relative (file.parent_path(), root, ec);
    if (ec || relative.empty() || relative == ".")
        return e;
    std::vector<std::string> folders;
    for (const auto& part : relative)
        if (part != "." && part != "..")
            folders.push_back (part.string());
    // The folder nearest the file weighs most (0.55), each one further out a little less.
    for (std::size_t i = 0; i < folders.size(); ++i)
    {
        NameEvidence one;
        const double confidence = 0.55 - 0.1 * static_cast<double> (folders.size() - 1 - i);
        collect (one, folders[i], std::max (0.25, confidence));
        for (const auto& t : one.tags)
            addTag (e.tags, t.tag, t.confidence);
        if (! one.type.empty() && one.typeConfidence >= e.typeConfidence)
        {
            e.type = one.type;
            e.typeConfidence = one.typeConfidence;
        }
        e.saysLoop = e.saysLoop || one.saysLoop;
        e.saysOneShot = e.saysOneShot || one.saysOneShot;
    }
    return e;
}

} // namespace osp::library
