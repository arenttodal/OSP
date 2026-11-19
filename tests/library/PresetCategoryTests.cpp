// The preset categories (docs/research/preset-navigation): one list, older names and name
// prefixes mapped when read, nothing ambiguous guessed.

#include "library/PresetCategory.h"

#include <catch2/catch_test_macros.hpp>

using namespace osp::library;

TEST_CASE ("preset categories: the list, the stored value, older names, name prefixes, Uncategorized", "[unit][library][preset-categories]")
{
    REQUIRE (presetCategories.size() == 14);
    CHECK (presetCategories.back() == "Uncategorized");
    // A stored category is itself, whatever its case.
    for (const auto c : presetCategories)
        CHECK (canonicalCategory (c, "Anything") == c);
    CHECK (canonicalCategory ("keys", "x") == "Keys");
    CHECK (canonicalCategory ("  Pads ", "x") == "Pads");
    // The Save sheet's earlier names.
    CHECK (canonicalCategory ("Basses", "x") == "Bass");
    CHECK (canonicalCategory ("Textured", "x") == "Textures");
    CHECK (canonicalCategory ("FX", "x") == "FX");
    // Nothing stored: the name's prefix suggests it (the name itself is never changed).
    CHECK (canonicalCategory ("", "Arp - Glass Run") == "Arpeggios");
    CHECK (canonicalCategory ("", "Keys \xe2\x80\x93 Felt Upright") == "Keys");
    CHECK (canonicalCategory ("", "Lead: Saw Glide") == "Leads");
    CHECK (canonicalCategory ("", "Pad - Slow Bloom") == "Pads");
    CHECK (canonicalCategory ("", "Pluck - Kalimba") == "Plucks");
    CHECK (canonicalCategory ("", "Strings - Night Bow") == "Strings");
    CHECK (canonicalCategory ("", "Synth - Wire") == "Synths");
    // An older category that does not say (Evolving, Rhythmic, Experimental) and no prefix:
    // Uncategorized, never a guess.
    CHECK (canonicalCategory ("Evolving", "Slow Thing") == "Uncategorized");
    CHECK (canonicalCategory ("Rhythmic", "Pulse") == "Uncategorized");
    CHECK (canonicalCategory ("", "Warm Hymn") == "Uncategorized");
    CHECK (canonicalCategory ("", "Arpeggiated Morning") == "Uncategorized");   // (a word, not a prefix)
    // Repeatable: the result of a result is itself.
    for (const auto* name : { "Arp - X", "Warm Hymn", "Keys: Y" })
    {
        const auto once = canonicalCategory ("", name);
        CHECK (canonicalCategory (once, name) == once);
    }
}
