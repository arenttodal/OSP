// ANDOR Drums key maps: PAD and EXPANDED resolution and every editing rule.
#include "drums/DrumMapping.h"

#include <catch2/catch_test_macros.hpp>

using namespace osp::drums;

TEST_CASE ("PAD mode: the GM-style defaults, one note one pad", "[drums][mapping]")
{
    const auto map = KeyMap::defaults();
    std::array<Hit, maxPads> hits {};
    const std::array<int, maxPads> expected { 36, 38, 42, 46, 39, 41, 37, 43, 49 };
    for (int p = 0; p < maxPads; ++p)
    {
        REQUIRE (resolve (map, KeyboardMode::pad, expected[static_cast<std::size_t> (p)], hits) == 1);
        CHECK (hits[0].pad == p);
        CHECK (hits[0].semitones == 0);
    }
    CHECK (resolve (map, KeyboardMode::pad, 60, hits) == 0);
}

TEST_CASE ("No note in 0..127 plays more than one pad unless configured", "[drums][mapping]")
{
    const auto map = KeyMap::defaults();
    std::array<Hit, maxPads> hits {};
    for (auto mode : { KeyboardMode::pad, KeyboardMode::expanded })
        for (int note = -5; note <= 132; ++note)
            CHECK (resolve (map, mode, note, hits) <= 1);
    CHECK (resolve (map, KeyboardMode::pad, -1, hits) == 0);
    CHECK (resolve (map, KeyboardMode::expanded, 128, hits) == 0);
}

TEST_CASE ("EXPANDED: the lowest key is the natural pitch; a root override moves it", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    std::array<Hit, maxPads> hits {};
    REQUIRE (applyZonePreset (map, ZonePreset::oneOctave, { 0, 1 }, 36).applied);
    REQUIRE (resizeZone (map, 0, 36, 48).applied == false);   // 48 belongs to pad 1 now
    REQUIRE (moveZone (map, 1, 60).applied);
    REQUIRE (resizeZone (map, 0, 36, 48).applied);             // kick on 36..48, root 36

    REQUIRE (resolve (map, KeyboardMode::expanded, 36, hits) == 1);
    CHECK (hits[0].pad == 0);
    CHECK (hits[0].semitones == 0);
    resolve (map, KeyboardMode::expanded, 37, hits);
    CHECK (hits[0].semitones == 1);
    resolve (map, KeyboardMode::expanded, 48, hits);
    CHECK (hits[0].semitones == 12);
    CHECK (resolve (map, KeyboardMode::expanded, 49, hits) == 0);

    REQUIRE (setZoneRoot (map, 0, 42).applied);   // centred: down and up from the middle
    resolve (map, KeyboardMode::expanded, 36, hits);
    CHECK (hits[0].semitones == -6);
    resolve (map, KeyboardMode::expanded, 48, hits);
    CHECK (hits[0].semitones == 6);
    CHECK_FALSE (setZoneRoot (map, 0, 50).applied);   // outside the zone
}

TEST_CASE ("Zones move as a whole and keep their root's place", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    REQUIRE (applyZonePreset (map, ZonePreset::oneOctave, { 1 }, 36).applied);
    REQUIRE (setZoneRoot (map, 1, 40).applied);
    REQUIRE (moveZone (map, 1, 72).applied);
    CHECK (map.zone[1] == Zone { 72, 83, 76 });
    CHECK_FALSE (moveZone (map, 1, 120).applied);   // would leave 0..127
    CHECK (map.zone[1] == Zone { 72, 83, 76 });     // untouched
}

TEST_CASE ("Collisions are refused, swapped or (deliberately) overlapped - never silent", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    REQUIRE (applyZonePreset (map, ZonePreset::oneOctave, { 0, 1, 2 }, 36).applied);   // 36-47, 48-59, 60-71
    const auto before = map;

    auto r = moveZone (map, 0, 50);   // 50..61 reaches into pads 1 and 2
    CHECK_FALSE (r.applied);
    CHECK (r.collidingPads == std::vector<int> { 1, 2 });
    CHECK (map == before);

    r = moveZone (map, 0, 48, Collision::swap);   // onto pad 1 exactly: the two exchange places
    CHECK (r.applied);
    CHECK (map.zone[0] == Zone { 48, 59, noNote });
    CHECK (map.zone[1] == Zone { 36, 47, noNote });

    map = before;
    r = moveZone (map, 0, 54, Collision::swap);   // across two zones: refused
    CHECK_FALSE (r.applied);
    CHECK (map == before);

    r = moveZone (map, 0, 50, Collision::overlap);   // overlap not enabled
    CHECK_FALSE (r.applied);
    map.allowOverlap = true;
    r = moveZone (map, 0, 50, Collision::overlap);
    CHECK (r.applied);
    std::array<Hit, maxPads> hits {};
    CHECK (resolve (map, KeyboardMode::expanded, 52, hits) == 2);   // visible, deliberate layering
}

TEST_CASE ("Resizing either edge; a root outside the new range falls back to the lowest key", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    REQUIRE (applyZonePreset (map, ZonePreset::oneOctave, { 0 }, 36).applied);
    REQUIRE (setZoneRoot (map, 0, 46).applied);
    REQUIRE (resizeZone (map, 0, 30, 40).applied);
    CHECK (map.zone[0] == Zone { 30, 40, noNote });
    CHECK_FALSE (resizeZone (map, 0, 41, 40).applied);
    CHECK_FALSE (resizeZone (map, 0, -1, 40).applied);
    CHECK_FALSE (resizeZone (map, 0, 100, 128).applied);
}

TEST_CASE ("Zone presets fit in MIDI or explain why not", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    const std::vector<int> all { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    auto r = applyZonePreset (map, ZonePreset::twoOctaves, all);
    CHECK_FALSE (r.applied);
    CHECK (r.message.find ("14 keys per pad") != std::string::npos);
    CHECK (map == KeyMap::defaults());

    r = applyZonePreset (map, ZonePreset::oneOctave, all, 36);   // 108 keys: moved down to fit
    REQUIRE (r.applied);
    CHECK (map.zone[8].high <= 127);
    CHECK (map.zone[0].low == 20);
    for (int p = 0; p + 1 < maxPads; ++p)
        CHECK (map.zone[static_cast<std::size_t> (p)].high + 1 == map.zone[static_cast<std::size_t> (p + 1)].low);

    REQUIRE (applyZonePreset (map, ZonePreset::compact, { 2, 0 }, 36).applied);   // display order
    CHECK (map.zone[2] == Zone { 36, 36, noNote });
    CHECK (map.zone[0] == Zone { 37, 37, noNote });
    CHECK_FALSE (map.zone[1].enabled());

    REQUIRE (applyZonePreset (map, ZonePreset::twoOctaves, { 0, 1, 2, 3, 4 }, 0).applied);
    CHECK (map.zone[4].high == 119);
}

TEST_CASE ("PAD notes: swapping, and no accidental double assignment", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    REQUIRE (swapPadNotes (map, 0, 1).applied);
    CHECK (map.padNote[0] == 38);
    CHECK (map.padNote[1] == 36);
    auto r = setPadNote (map, 2, 36);
    CHECK_FALSE (r.applied);
    CHECK (r.collidingPads == std::vector<int> { 1 });
    CHECK (setPadNote (map, 2, 36, true).applied);   // explicit layering
    std::array<Hit, maxPads> hits {};
    CHECK (resolve (map, KeyboardMode::pad, 36, hits) == 2);
    CHECK_FALSE (setPadNote (map, 2, 200).applied);
    CHECK (setPadNote (map, 2, noNote).applied);
}

TEST_CASE ("Swapping zones exchanges bounds and roots; PAD and EXPANDED maps stay independent", "[drums][mapping]")
{
    auto map = KeyMap::defaults();
    REQUIRE (setZoneRoot (map, 0, 18).applied);
    const auto zone0 = map.zone[0], zone1 = map.zone[1];
    const auto notes = map.padNote;
    REQUIRE (swapZones (map, 0, 1).applied);
    CHECK (map.zone[0] == zone1);
    CHECK (map.zone[1] == zone0);
    CHECK (map.padNote == notes);
    CHECK (keyboardModeFromString (toString (KeyboardMode::expanded)) == KeyboardMode::expanded);
    CHECK (keyboardModeFromString ("nonsense", KeyboardMode::pad) == KeyboardMode::pad);
}
