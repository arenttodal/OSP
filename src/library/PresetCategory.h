#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace osp::library
{

/**
    The preset categories, one list for the quick preset menu, the Save sheet's CATEGORY and the
    Library's filter (docs/research/preset-navigation). A preset's category is its own stored
    string, never rewritten here: what is shown is derived when read, so the migration of older
    presets is non-destructive and repeatable.
    - A stored category that is one of these is that one.
    - An older name maps through the aliases (Basses -> Bass, Textured -> Textures, ...).
    - With nothing stored (or an unknown one), a name prefix suggests it ("Arp - Bright" ->
      Arpeggios), the original name kept whole.
    - Anything else is Uncategorized.
*/
inline constexpr std::array<std::string_view, 14> presetCategories {
    "Arpeggios", "Bass", "Bells & Mallets", "Keys", "Leads", "Pads", "Percussion", "Plucks", "Strings", "Synths", "Textures", "Voices", "FX", "Uncategorized"
};

namespace detail
{
    inline std::string lowered (std::string_view s)
    {
        std::string out (s);
        std::transform (out.begin(), out.end(), out.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return out;
    }

    inline std::string trimmed (std::string_view s)
    {
        const auto first = s.find_first_not_of (" \t");
        if (first == std::string_view::npos)
            return {};
        const auto last = s.find_last_not_of (" \t");
        return std::string (s.substr (first, last - first + 1));
    }
}

/** The category a stored value names (exactly, any case, or an older alias), or "" when none. */
inline std::string_view categoryNamed (std::string_view stored)
{
    const auto key = detail::lowered (detail::trimmed (stored));
    if (key.empty())
        return {};
    for (const auto c : presetCategories)
        if (detail::lowered (c) == key)
            return c;
    // The Save sheet's earlier names, and close spellings.
    static constexpr std::pair<std::string_view, std::string_view> aliases[] {
        { "basses", "Bass" },        { "bass", "Bass" },         { "arp", "Arpeggios" },     { "arps", "Arpeggios" },     { "arpeggio", "Arpeggios" },
        { "bells", "Bells & Mallets" }, { "mallets", "Bells & Mallets" }, { "bell", "Bells & Mallets" },
        { "lead", "Leads" },         { "pad", "Pads" },          { "pluck", "Plucks" },      { "string", "Strings" },     { "synth", "Synths" },
        { "texture", "Textures" },   { "textured", "Textures" }, { "voice", "Voices" },      { "vocal", "Voices" },       { "vocals", "Voices" },
        { "choir", "Voices" },       { "drums", "Percussion" },  { "perc", "Percussion" },   { "fx", "FX" },              { "sfx", "FX" },
        { "key", "Keys" },           { "piano", "Keys" },
    };
    for (const auto& [alias, category] : aliases)
        if (alias == key)
            return category;
    return {};
}

/** The prefix a name starts with ("Arp - ", "Keys: ", "Lead / "), as a category, or "". */
inline std::string_view categoryFromName (std::string_view name)
{
    const auto text = detail::trimmed (name);
    for (const auto separator : { " \xe2\x80\x93 ", " \xe2\x80\x94 ", " - ", ": ", " / " })   // en dash, em dash, hyphen, colon, slash
        if (const auto at = text.find (separator); at != std::string::npos && at > 0)
            if (const auto c = categoryNamed (text.substr (0, at)); ! c.empty())
                return c;
    return {};
}

/** What a preset is listed under: its stored category, else its name's prefix, else Uncategorized. */
inline std::string_view canonicalCategory (std::string_view stored, std::string_view name)
{
    if (const auto c = categoryNamed (stored); ! c.empty())
        return c;
    if (const auto c = categoryFromName (name); ! c.empty())
        return c;
    return presetCategories.back();
}

} // namespace osp::library
