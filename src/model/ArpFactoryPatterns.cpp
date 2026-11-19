#include "model/ArpFactoryPatterns.h"

namespace osp::arppattern
{

namespace
{
    constexpr int T = arp::ticksPerStep;

    Document make (const char* id, const char* name, const char* description, std::initializer_list<std::pair<int, int>> rankTicks,
                   arp::CustomMapping mapping = arp::CustomMapping::contour)
    {
        Document d;
        d.id = std::string ("factory.") + id;
        d.name = name;
        d.description = description;
        d.factory = true;
        d.pattern.mapping = mapping;
        d.interpretation.mapping = mapping;
        for (const auto& [rank, ticks] : rankTicks)
            d.pattern.events.push_back ({ rank < 0 ? 0 : rank, rank < 0, ticks, 0 });
        d.pattern.maxRank = d.pattern.highestRank();
        return d;
    }
}

const std::vector<Document>& factoryPatterns()
{
    static const std::vector<Document> patterns {
        make ("five-climb", "Five Climb", "Five rising steps: the shape climbs an octave each cycle where there is room",
              { { 0, T }, { 1, T }, { 2, T }, { 3, T }, { 4, T } }),
        make ("pedal-bass", "Pedal Bass", "A low anchor under a moving line",
              { { 0, T }, { 2, T }, { 0, T }, { 3, T }, { 0, T }, { 4, T }, { 0, T }, { 2, T } }),
        make ("thumb-leaps", "Thumb Leaps", "The lowest note under irregular leaps",
              { { 0, T }, { 3, T }, { 0, T / 2 }, { 0, T / 2 }, { 4, T }, { 2, T }, { 0, T }, { 5, T } }),
        make ("gallop", "Gallop", "Two pitches in a long-short-short rhythm",
              { { 0, T }, { 1, T / 2 }, { 1, T / 2 }, { 0, T }, { 1, T / 2 }, { 1, T / 2 } }),
        make ("arch", "Arch", "Up and back down, odd length",
              { { 0, T }, { 1, T }, { 2, T }, { 3, T }, { 4, T }, { 3, T }, { 2, T }, { 1, T }, { 0, T } }),
        make ("sparse-rests", "Sparse", "Notes and silences: rests keep time without repeating",
              { { 0, T }, { -1, T }, { 2, T / 2 }, { 1, T / 2 }, { -1, T }, { 3, 2 * T }, { -1, T } }),
        make ("triplet-roll", "Triplet Roll", "Thirds of a step rolling up and down",
              { { 0, T / 3 }, { 1, T / 3 }, { 2, T / 3 }, { 3, T / 3 }, { 2, T / 3 }, { 1, T / 3 } }),
        make ("long-short", "Long Short", "A held note and quick answers, seven steps",
              { { 0, 2 * T }, { 2, T / 2 }, { 1, T / 2 }, { 3, 2 * T }, { 1, T }, { 0, T } }),
        make ("degree-walk", "Degree Walk", "DEGREES: rank 1 the lowest held note, then up the chord",
              { { 0, T }, { 2, T }, { 1, T }, { 3, T } }, arp::CustomMapping::degrees),
    };
    return patterns;
}

} // namespace osp::arppattern
