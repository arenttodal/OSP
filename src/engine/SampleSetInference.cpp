#include "engine/SampleSetInference.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>

namespace osp
{

const char* toString (SampleRole role) noexcept
{
    switch (role)
    {
        case SampleRole::pitchAnchor: return "pitch";
        case SampleRole::velocityLayer: return "velocity";
        case SampleRole::roundRobin: return "round-robin";
        case SampleRole::articulation: return "articulation";
    }
    return "pitch";
}

std::optional<double> dynamicsRankFromName (const std::string& filename)
{
    // Tokens are split on anything that is not a letter, so "Violin_mf_C4" and
    // "violin-FF-02" both work; whole tokens only ("pp" inside "apple" is not a match).
    static const std::map<std::string, double> words = {
        { "ppp", 0.5 }, { "pp", 1.0 }, { "p", 2.0 }, { "mp", 3.0 }, { "mf", 4.0 }, { "f", 5.0 }, { "ff", 6.0 }, { "fff", 6.5 },
        { "pianissimo", 1.0 }, { "piano", 2.0 }, { "mezzopiano", 3.0 }, { "mezzoforte", 4.0 }, { "forte", 5.0 }, { "fortissimo", 6.0 },
        { "soft", 1.5 }, { "quiet", 1.5 }, { "gentle", 2.0 }, { "light", 2.0 }, { "med", 3.5 }, { "medium", 3.5 }, { "mid", 3.5 },
        { "normal", 3.5 }, { "hard", 5.5 }, { "loud", 5.5 }, { "strong", 5.5 }, { "heavy", 6.0 }
    };
    std::string stem = filename;
    if (const auto dot = stem.find_last_of ('.'); dot != std::string::npos)
        stem = stem.substr (0, dot);
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : stem)
    {
        if (std::isalpha (static_cast<unsigned char> (ch)))
            current += static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
        else if (! current.empty())
        {
            tokens.push_back (current);
            current.clear();
        }
    }
    if (! current.empty())
        tokens.push_back (current);
    // Single letters ("p", "f") are only trusted when they stand alone between separators
    // and the name has other content; still the weakest evidence, so check longer words first.
    std::optional<double> best;
    for (const auto& t : tokens)
        if (t.size() > 1)
            if (auto it = words.find (t); it != words.end())
                best = it->second;
    if (! best && tokens.size() > 1)
        for (const auto& t : tokens)
            if (t.size() == 1)
                if (auto it = words.find (t); it != words.end())
                    best = it->second;
    return best;
}

InstrumentSet inferSampleSet (const std::vector<SetInput>& inputs, const std::vector<SetAssignment>& assignments)
{
    InstrumentSet set;
    for (const auto& in : inputs)
    {
        if (in.model == nullptr || ! in.model->isValid())
            continue;
        SetMember m;
        m.model = in.model;
        m.filename = in.filename;
        m.loudnessDb = in.model->analysis.envelope.maxRmsDbfs;
        set.members.push_back (std::move (m));
    }
    if (set.members.empty())
        return set;

    // 1. Pitch groups (greedy over sorted roots; user roots override).
    std::vector<double> roots;
    for (const auto& m : set.members)
    {
        double root = m.model->rootMidi;
        for (const auto& a : assignments)
            if (a.filename == m.filename && a.rootMidi)
                root = *a.rootMidi;
        roots.push_back (root);
    }
    std::vector<int> order (set.members.size());
    std::iota (order.begin(), order.end(), 0);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return roots[static_cast<std::size_t> (a)] < roots[static_cast<std::size_t> (b)]; });
    for (int index : order)
    {
        const double root = roots[static_cast<std::size_t> (index)];
        if (set.groups.empty() || std::abs (root - set.groups.back().rootMidi) > 0.5)
        {
            PitchGroup g;
            g.rootMidi = root;
            set.groups.push_back (g);
        }
        set.groups.back().members.push_back (index);
        set.members[static_cast<std::size_t> (index)].pitchGroup = static_cast<int> (set.groups.size()) - 1;
    }

    // 2. Roles inside each group.
    for (auto& group : set.groups)
    {
        auto& ids = group.members;
        if (ids.size() == 1)
        {
            auto& m = set.members[static_cast<std::size_t> (ids[0])];
            m.role = SampleRole::pitchAnchor;
            m.confidence = 1.0;
            continue;
        }
        // Alternate articulations: much shorter or longer than the group's median.
        std::vector<double> durations;
        for (int id : ids)
            durations.push_back (set.members[static_cast<std::size_t> (id)].model->analysis.source.durationSeconds);
        auto sortedDur = durations;
        std::sort (sortedDur.begin(), sortedDur.end());
        const double medianDur = sortedDur[sortedDur.size() / 2];
        std::vector<int> playable;
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            auto& m = set.members[static_cast<std::size_t> (ids[i])];
            const double ratio = medianDur > 0.0 ? durations[i] / medianDur : 1.0;
            if (ids.size() >= 3 && (ratio > 3.0 || ratio < 1.0 / 3.0))
            {
                m.role = SampleRole::articulation;
                m.confidence = 0.6;
            }
            else
                playable.push_back (ids[i]);
        }
        if (playable.empty())
            continue;

        // Velocity layers: names first, then loudness gaps.
        std::vector<std::pair<double, int>> ranked; // (rank or loudness, member)
        int named = 0;
        for (int id : playable)
        {
            const auto& m = set.members[static_cast<std::size_t> (id)];
            if (dynamicsRankFromName (m.filename))
                ++named;
        }
        const bool useNames = named == static_cast<int> (playable.size());
        for (int id : playable)
        {
            const auto& m = set.members[static_cast<std::size_t> (id)];
            ranked.emplace_back (useNames ? *dynamicsRankFromName (m.filename) : m.loudnessDb, id);
        }
        std::sort (ranked.begin(), ranked.end());
        int layer = 0;
        std::vector<int> layerOf (ranked.size(), 0);
        for (std::size_t i = 1; i < ranked.size(); ++i)
        {
            const double gap = ranked[i].first - ranked[i - 1].first;
            if ((useNames && gap > 0.25) || (! useNames && gap >= 4.5))
                ++layer;
            layerOf[i] = layer;
        }
        group.layers = layer + 1;
        std::map<int, int> takes;
        double spread = ranked.back().first - ranked.front().first;
        for (std::size_t i = 0; i < ranked.size(); ++i)
        {
            auto& m = set.members[static_cast<std::size_t> (ranked[i].second)];
            m.layer = layerOf[i];
            m.take = takes[layerOf[i]]++;
            m.role = group.layers > 1 ? SampleRole::velocityLayer : SampleRole::roundRobin;
            m.confidence = useNames ? 0.9 : (group.layers > 1 ? 0.6 : (spread < 3.0 ? 0.8 : 0.5));
        }
    }

    // User corrections: role and layer.
    for (const auto& a : assignments)
        for (auto& m : set.members)
            if (m.filename == a.filename)
            {
                m.role = a.role;
                if (a.layer)
                    m.layer = std::max (0, *a.layer);
                m.userAssigned = true;
                m.confidence = 1.0;
                auto& g = set.groups[static_cast<std::size_t> (m.pitchGroup)];
                g.layers = std::max (g.layers, m.layer + 1);
            }
    // Re-number takes within each (group, layer) after corrections.
    for (auto& group : set.groups)
    {
        std::map<int, int> takes;
        for (int id : group.members)
        {
            auto& m = set.members[static_cast<std::size_t> (id)];
            if (m.role != SampleRole::articulation)
                m.take = takes[m.layer]++;
        }
    }

    // 3. Register model over pitched groups.
    std::vector<double> xs, ys;
    for (const auto& group : set.groups)
    {
        const auto& m = set.members[static_cast<std::size_t> (group.members.front())].model;
        const auto& a = m->analysis;
        if (a.pitch.detected && a.spectral.meanCentroidHz > 0.0)
        {
            xs.push_back (group.rootMidi);
            ys.push_back (12.0 * std::log2 (a.spectral.meanCentroidHz / a.pitch.fundamentalHz));
        }
    }
    if (xs.size() >= 2 && (*std::max_element (xs.begin(), xs.end()) - *std::min_element (xs.begin(), xs.end())) >= 3.0)
    {
        const double mx = std::accumulate (xs.begin(), xs.end(), 0.0) / static_cast<double> (xs.size());
        const double my = std::accumulate (ys.begin(), ys.end(), 0.0) / static_cast<double> (ys.size());
        double num = 0.0, den = 0.0;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            num += (xs[i] - mx) * (ys[i] - my);
            den += (xs[i] - mx) * (xs[i] - mx);
        }
        // Cautious: the slope is limited so extrapolation cannot run away, and shrunk
        // towards "no register effect" when few pitches support it (n / (n + 2)).
        const double n = static_cast<double> (xs.size());
        set.brightnessSlope = den > 0.0 ? std::clamp (num / den, -1.0, 1.0) * n / (n + 2.0) : 0.0;
        set.brightnessIntercept = my - set.brightnessSlope * mx;
        set.hasRegisterModel = true;
    }

    // Primary: the loudest take of the group closest to the middle of the range.
    const double middle = 0.5 * (set.groups.front().rootMidi + set.groups.back().rootMidi);
    const PitchGroup* centre = &set.groups.front();
    for (const auto& g : set.groups)
        if (std::abs (g.rootMidi - middle) < std::abs (centre->rootMidi - middle))
            centre = &g;
    set.primary = centre->members.front();
    for (int id : centre->members)
        if (set.members[static_cast<std::size_t> (id)].loudnessDb > set.members[static_cast<std::size_t> (set.primary)].loudnessDb)
            set.primary = id;
    return set;
}

} // namespace osp
