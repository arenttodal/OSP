#include "research/ArpPatternLab.h"

#include "audio/utility/TestSignals.h"
#include "io/AudioFileIO.h"
#include "midi/MidiFileIO.h"
#include "research/RenderConfig.h"
#include "research/RenderSession.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace osp::research::arplab
{

namespace
{
    /** Notes on a grid of `step` quarters: pitch 0 is a rest (no note). */
    std::vector<CapturedNote> line (std::initializer_list<int> pitches, double step, double gate = 0.9, int velocity = 100)
    {
        std::vector<CapturedNote> r;
        double t = 0.0;
        for (const int p : pitches)
        {
            if (p > 0)
                r.push_back ({ t, step * gate, p, velocity });
            t += step;
        }
        return r;
    }
}

std::vector<Gesture> referenceGestures()
{
    // MIDI numbers: C3 48, C4 60, C5 72. Written for the lab (no existing melody).
    std::vector<Gesture> g;
    g.push_back ({ "five-up", "a five-note ascending motif (odd length)", line ({ 60, 62, 64, 67, 69 }, 0.25), 1.25 });
    g.push_back ({ "bass-pedal", "a bass line returning to its pedal note", line ({ 48, 60, 48, 55, 48, 64, 48, 55 }, 0.25), 2.0 });
    g.push_back ({ "thumb-leaps", "a thumb-like anchor under irregular leaps", line ({ 48, 67, 64, 48, 71, 67, 74 }, 0.25), 1.75 });
    g.push_back ({ "two-pitch", "a repeated two-pitch rhythm", line ({ 64, 64, 71, 64, 71, 71 }, 0.25), 1.5 });
    g.push_back ({ "arch", "an ascending then descending gesture", line ({ 60, 64, 67, 72, 76, 72, 67, 64 }, 0.25), 2.0 });
    g.push_back ({ "wide-jumps", "register jumps of two octaves", line ({ 48, 72, 55, 79, 52, 76 }, 0.25), 1.5 });
    {
        // Rests: D . F A . . D' . A F . D (12 sixteenths, 7 notes).
        Gesture rests { "rests", "a rhythmic phrase with rests (Stage B keeps them; Stage A plays its notes in order)",
                        line ({ 62, 0, 65, 69, 0, 0, 74, 0, 69, 65, 0, 62 }, 0.25), 3.0 };
        g.push_back (rests);
    }
    g.push_back ({ "short-three", "a short three-note cell", line ({ 60, 67, 64 }, 0.25), 0.75 });
    g.push_back ({ "long-32", "a long 32-step line",
                   line ({ 57, 60, 64, 67, 69, 67, 64, 60, 62, 65, 69, 72, 74, 72, 69, 65,
                           64, 67, 71, 74, 76, 74, 71, 67, 65, 69, 72, 76, 77, 72, 69, 60 }, 0.25), 8.0 });
    g.push_back ({ "single", "one pitch, repeated", line ({ 69, 69, 69, 69 }, 0.25), 1.0 });
    {
        // Legato: each note starts before the one before it ends.
        Gesture legato { "legato", "overlapping legato notes (still one step each)", {}, 1.5 };
        const int pitches[] = { 60, 64, 62, 67, 65, 72 };
        for (int i = 0; i < 6; ++i)
            legato.notes.push_back ({ 0.25 * i, 0.4, pitches[i], 96 });
        g.push_back (legato);
    }
    {
        // A chord attack inside the phrase (the policy picks one note of it).
        Gesture chordy { "chord-attack", "a phrase with a three-note chord attack on its third step", line ({ 60, 64, 0, 67, 72 }, 0.25), 1.25 };
        chordy.notes.push_back ({ 0.5, 0.2, 55, 90 });
        chordy.notes.push_back ({ 0.5 + 0.01, 0.2, 62, 90 });
        chordy.notes.push_back ({ 0.5 + 0.005, 0.2, 69, 90 });
        g.push_back (chordy);
    }
    return g;
}

Contour extractContour (const std::vector<CapturedNote>& notesIn, ChordPolicy policy, double together)
{
    auto notes = notesIn;
    std::stable_sort (notes.begin(), notes.end(), [] (const CapturedNote& a, const CapturedNote& b) { return a.onset < b.onset; });
    Contour c;
    std::vector<CapturedNote> steps;
    for (std::size_t i = 0; i < notes.size();)
    {
        // An attack: every note starting within `together` of the first.
        std::size_t j = i + 1;
        while (j < notes.size() && notes[j].onset - notes[i].onset <= together)
            ++j;
        auto chosen = notes[i];
        for (std::size_t k = i + 1; k < j; ++k)
            if ((policy == ChordPolicy::highest && notes[k].pitch > chosen.pitch) || (policy == ChordPolicy::lowest && notes[k].pitch < chosen.pitch))
                chosen = notes[k];
        if (j - i > 1)
        {
            ++c.chordAttacks;
            c.notesDropped += static_cast<int> (j - i - 1);
        }
        steps.push_back (chosen);
        i = j;
    }
    std::set<int> distinct;
    for (const auto& s : steps)
        distinct.insert (s.pitch);
    c.pitches.assign (distinct.begin(), distinct.end());
    for (const auto& s : steps)
    {
        c.ranks.push_back (static_cast<int> (std::lower_bound (c.pitches.begin(), c.pitches.end(), s.pitch) - c.pitches.begin()));
        c.onsets.push_back (s.onset);
        c.velocities.push_back (s.velocity);
        c.durations.push_back (s.duration);
    }
    c.maxRank = c.pitches.empty() ? 0 : static_cast<int> (c.pitches.size()) - 1;
    return c;
}

const char* mappingName (Mapping m) noexcept
{
    switch (m)
    {
        case Mapping::adaptiveContour: return "adaptive-contour";
        case Mapping::degreeCycle: return "degree-cycle";
        case Mapping::contourFit: return "contour-fit";
        case Mapping::fitOctaveCycles: return "fit-octave-cycles";
        case Mapping::hybrid: return "hybrid";
    }
    return "?";
}

int mapRank (Mapping m, int r, int R, int N) noexcept
{
    if (N <= 0)
        return -1;
    r = std::max (0, r);
    auto adaptive = [] (int rank, int top, int size) {
        if (top <= 0 || size == 1)
            return 0;
        // round (rank / top * (size - 1)), half up, in integers.
        const long long num = 2LL * rank * (size - 1) + top;
        return static_cast<int> (std::min<long long> (size - 1, num / (2LL * top)));
    };
    switch (m)
    {
        case Mapping::adaptiveContour: return adaptive (std::min (r, R), R, N);
        case Mapping::degreeCycle: return r % N;
        case Mapping::contourFit:
        case Mapping::fitOctaveCycles:
        case Mapping::hybrid: return N > R ? std::min (r, N - 1) : adaptive (std::min (r, R), R, N);
    }
    return 0;
}

std::vector<int> notePool (std::vector<int> held, int octaves)
{
    std::sort (held.begin(), held.end());
    std::vector<int> pool;
    for (int o = 0; o < std::clamp (octaves, 1, 4); ++o)
        for (const int n : held)
            if (n + 12 * o <= 127)
                pool.push_back (n + 12 * o);
    return pool;
}

std::vector<int> interpret (const Contour& contour, const std::vector<int>& pool, Mapping mapping)
{
    std::vector<int> out;
    if (pool.empty())
        return out;
    for (const int r : contour.ranks)
        out.push_back (pool[static_cast<std::size_t> (mapRank (mapping, r, contour.maxRank, static_cast<int> (pool.size())))]);
    return out;
}

namespace
{
    std::vector<int> distinct (std::vector<int> v)
    {
        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
        return v;
    }
}

HybridPlan planHybrid (const std::vector<int>& held, int octaves, int maxRank)
{
    HybridPlan plan;
    octaves = std::clamp (octaves, 1, 4);
    const int needed = maxRank + 1;
    const auto full = distinct (notePool (held, octaves));
    if (full.empty())
        return plan;
    // The fewest octaves whose distinct notes hold every rank.
    plan.baseOctaves = octaves;
    for (int b = 1; b <= octaves; ++b)
        if (static_cast<int> (distinct (notePool (held, b)).size()) >= needed)
        {
            plan.baseOctaves = b;
            break;
        }
    plan.base = distinct (notePool (held, plan.baseOctaves));
    plan.compressed = static_cast<int> (plan.base.size()) < needed;
    // The notes the gesture uses in the base pool, then every octave shift where all of them
    // are in the full pool.
    std::vector<int> used;
    for (int r = 0; r <= maxRank; ++r)
        used.push_back (plan.base[static_cast<std::size_t> (mapRank (Mapping::contourFit, r, maxRank, static_cast<int> (plan.base.size())))]);
    for (int shift = 0; shift <= 12 * 3; shift += 12)
    {
        const bool fits = std::all_of (used.begin(), used.end(), [&] (int n) { return std::binary_search (full.begin(), full.end(), n + shift); });
        if (! fits)
            break;   // positions are contiguous from the base upwards
        plan.positions.push_back (shift);
    }
    if (plan.positions.empty())
        plan.positions.push_back (0);
    return plan;
}

int noteFor (Mapping mapping, int rank, int maxRank, const std::vector<int>& held, int octaves, long long cycle)
{
    if (held.empty())
        return -1;
    cycle = std::max (0LL, cycle);
    switch (mapping)
    {
        case Mapping::adaptiveContour:
        case Mapping::degreeCycle:
        case Mapping::contourFit:
        {
            const auto pool = notePool (held, octaves);
            return pool[static_cast<std::size_t> (mapRank (mapping, rank, maxRank, static_cast<int> (pool.size())))];
        }
        case Mapping::fitOctaveCycles:
        {
            const auto pool = notePool (held, 1);
            int n = pool[static_cast<std::size_t> (mapRank (mapping, rank, maxRank, static_cast<int> (pool.size())))]
                    + 12 * static_cast<int> (cycle % std::clamp (octaves, 1, 4));
            while (n > 127)
                n -= 12;
            return n;
        }
        case Mapping::hybrid:
        {
            const auto plan = planHybrid (held, octaves, maxRank);
            if (plan.base.empty())
                return -1;
            const int n = plan.base[static_cast<std::size_t> (mapRank (mapping, rank, maxRank, static_cast<int> (plan.base.size())))];
            return n + plan.positions[static_cast<std::size_t> (cycle % static_cast<long long> (plan.positions.size()))];
        }
    }
    return -1;
}

std::vector<int> playLine (const Contour& contour, const std::vector<int>& held, int octaves, Mapping mapping)
{
    std::vector<int> out;
    if (held.empty() || contour.ranks.empty())
        return out;
    long long cycles = 1;
    if (mapping == Mapping::fitOctaveCycles)
        cycles = std::clamp (octaves, 1, 4);
    else if (mapping == Mapping::hybrid)
        cycles = static_cast<long long> (planHybrid (held, octaves, contour.maxRank).positions.size());
    for (long long c = 0; c < cycles; ++c)
        for (const int r : contour.ranks)
            out.push_back (noteFor (mapping, r, contour.maxRank, held, octaves, c));
    return out;
}

Fidelity measure (const Contour& contour, const std::vector<int>& played)
{
    Fidelity f;
    f.distinctCaptured = static_cast<int> (contour.pitches.size());
    if (played.size() != contour.ranks.size() || played.empty())
        return f;
    f.distinctPlayed = static_cast<int> (std::set<int> (played.begin(), played.end()).size());
    std::map<int, std::set<int>> notesOfRank;
    std::map<int, std::set<int>> ranksOfNote;
    for (std::size_t i = 0; i < played.size(); ++i)
    {
        notesOfRank[contour.ranks[i]].insert (played[i]);
        ranksOfNote[played[i]].insert (contour.ranks[i]);
    }
    for (const auto& [note, ranks] : ranksOfNote)
        if (ranks.size() > 1)
            f.collapsedRanks += static_cast<int> (ranks.size());
    const auto sign = [] (int d) { return (d > 0) - (d < 0); };
    int agree = 0, pairs = 0;
    // Around the loop: the step from the last note back to the first counts too.
    const std::size_t n = played.size();
    for (std::size_t i = 0; i < n && n > 1; ++i)
    {
        const std::size_t j = (i + 1) % n;
        const int capturedStep = contour.pitches[static_cast<std::size_t> (contour.ranks[j])] - contour.pitches[static_cast<std::size_t> (contour.ranks[i])];
        const int playedStep = played[j] - played[i];
        agree += sign (capturedStep) == sign (playedStep) ? 1 : 0;
        ++pairs;
        f.maxLeapCaptured = std::max (f.maxLeapCaptured, std::abs (capturedStep));
        f.maxLeapPlayed = std::max (f.maxLeapPlayed, std::abs (playedStep));
        if (capturedStep == 0 && playedStep != 0)
            f.repeatsKept = false;
    }
    f.directionAgreement = pairs > 0 ? static_cast<double> (agree) / pairs : 1.0;
    const auto [lo, hi] = std::minmax_element (played.begin(), played.end());
    f.spanPlayed = *hi - *lo;
    return f;
}

MidiSequence capturedSequence (const Gesture& g, double bpm)
{
    MidiSequence s;
    s.name = g.name;
    const double q = 60.0 / bpm;
    for (int loop = 0; loop < 2; ++loop)
        for (const auto& n : g.notes)
        {
            const double t = (n.onset + loop * g.lengthQuarters) * q;
            s.events.push_back ({ t, MidiEvent::Type::noteOn, n.pitch, n.velocity, 1 });
            s.events.push_back ({ t + n.duration * q, MidiEvent::Type::noteOff, n.pitch, 0, 1 });
        }
    s.sort();
    return s;
}

std::vector<HeldChord> listeningProgression()
{
    return { { "one note (C4)", { 60 }, 1, 1 },
             { "interval (C4 G4)", { 60, 67 }, 1, 1 },
             { "major triad (C E G)", { 60, 64, 67 }, 1, 1 },
             { "minor triad (A C E)", { 57, 60, 64 }, 1, 1 },
             { "seventh (D F A C)", { 50, 53, 57, 60 }, 1, 1 },
             { "extended (G B D F A)", { 55, 59, 62, 65, 69 }, 1, 1 },
             { "maj7 over two octaves (C E G B)", { 48, 52, 55, 59 }, 2, 2 } };
}

std::vector<HeldChord> octavesProgression()
{
    return { { "major triad over 4 octaves", { 60, 64, 67 }, 4, 2 },
             { "one note over 4 octaves", { 57 }, 4, 2 },
             { "seventh over 2 octaves", { 50, 53, 57, 60 }, 2, 2 },
             { "six-note voicing over 3 octaves", { 48, 52, 55, 59, 62, 66 }, 3, 2 } };
}

MidiSequence progressionSequence (const Contour& contour, const std::vector<HeldChord>& chords, Mapping mapping, double bpm)
{
    MidiSequence s;
    s.name = mappingName (mapping);
    if (contour.ranks.empty())
        return s;
    const double step = 0.25 * 60.0 / bpm;   // 1/16
    const double gate = 0.75 * step;
    long long stepIndex = 0;                  // the phase runs on across chords
    double t = 0.0;
    const auto length = static_cast<long long> (contour.ranks.size());
    for (const auto& chord : chords)
    {
        for (int k = 0; k < 16 * chord.bars; ++k, ++stepIndex, t += step)
        {
            const auto i = static_cast<std::size_t> (stepIndex % length);
            // The cycle advances at the pattern's own loop boundary.
            const int note = noteFor (mapping, contour.ranks[i], contour.maxRank, chord.notes, chord.octaves, stepIndex / length);
            if (note < 0)
                continue;
            s.events.push_back ({ t, MidiEvent::Type::noteOn, note, 100, 1 });
            s.events.push_back ({ t + gate, MidiEvent::Type::noteOff, note, 0, 1 });
        }
    }
    s.sort();
    return s;
}


// ---------------------------------------------------------------------------------------------
// Rhythmic patterns

int RhythmPattern::lengthTicks() const noexcept
{
    int t = 0;
    for (const auto& e : events)
        t += e.duration;
    return t;
}

int RhythmPattern::onsetOf (int index) const noexcept
{
    int t = 0;
    for (int i = 0; i < index && i < static_cast<int> (events.size()); ++i)
        t += events[static_cast<std::size_t> (i)].duration;
    return t;
}

int RhythmPattern::notes() const noexcept
{
    return static_cast<int> (std::count_if (events.begin(), events.end(), [] (const PatternEvent& e) { return ! e.rest; }));
}

bool RhythmPattern::valid() const noexcept
{
    if (events.empty() || static_cast<int> (events.size()) > maxPatternEvents || lengthTicks() > maxPatternSteps * ticksPerStep)
        return false;
    return std::all_of (events.begin(), events.end(), [this] (const PatternEvent& e) {
        return e.duration >= minEventTicks && (e.rest || (e.rank >= 0 && e.rank <= std::max (maxRank, e.rank)));
    });
}

RhythmPattern stageA (const Contour& contour)
{
    RhythmPattern p;
    p.maxRank = contour.maxRank;
    for (const int r : contour.ranks)
        p.events.push_back ({ r, false, ticksPerStep, 0 });
    return p;
}

RhythmPattern stageB (const std::vector<CapturedNote>& notes, double stepQuarters, int quantum, double lengthQuarters, ChordPolicy policy)
{
    const auto contour = extractContour (notes, policy);
    RhythmPattern p;
    p.maxRank = contour.maxRank;
    quantum = std::max (minEventTicks, quantum);
    const auto toTicks = [&] (double quarters) {
        const double ticks = quarters / stepQuarters * ticksPerStep;
        return static_cast<int> (std::llround (ticks / quantum)) * quantum;
    };
    const int length = std::max (quantum, toTicks (lengthQuarters));
    // Quantised onsets; two notes landing on one slot keep the first (the policy already chose
    // within chords), later ones move to the next free slot so no note is dropped silently.
    struct Placed
    {
        int at, end, rank;
    };
    std::vector<Placed> placed;
    int last = -quantum;
    for (std::size_t i = 0; i < contour.ranks.size(); ++i)
    {
        const int at = std::max (toTicks (contour.onsets[i]), last + quantum);
        if (at >= length)
            break;   // past the loop: reported by the caller as fewer notes than captured
        // The captured end, rounded up to the quantum (at least one quantum long).
        const double endTicks = (contour.onsets[i] + contour.durations[i]) / stepQuarters * ticksPerStep;
        const int end = std::max (at + quantum, static_cast<int> (std::ceil (endTicks / quantum - 1.0e-9)) * quantum);
        placed.push_back ({ at, end, contour.ranks[i] });
        last = at;
    }
    int t = 0;
    for (std::size_t i = 0; i < placed.size(); ++i)
    {
        if (placed[i].at > t)
            p.events.push_back ({ 0, true, placed[i].at - t, 0 });
        const int next = i + 1 < placed.size() ? placed[i + 1].at : length;
        // A silence longer than half a step is a rest; a shorter one is articulation (GATE's job).
        const int end = next - placed[i].end > ticksPerStep / 2 ? std::min (placed[i].end, next) : next;
        p.events.push_back ({ placed[i].rank, false, end - placed[i].at, 0 });
        t = end;
    }
    if (t < length && ! placed.empty())
        p.events.push_back ({ 0, true, length - t, 0 });
    if (p.events.empty())
        p.events.push_back ({ 0, true, length, 0 });
    return p;
}

namespace edit
{
    namespace
    {
        bool inRange (const RhythmPattern& p, int i) { return i >= 0 && i < static_cast<int> (p.events.size()); }
    }

    bool split (RhythmPattern& p, int i)
    {
        if (! inRange (p, i) || static_cast<int> (p.events.size()) >= maxPatternEvents)
            return false;
        auto& e = p.events[static_cast<std::size_t> (i)];
        if (e.duration % 2 != 0 || e.duration / 2 < minEventTicks)
            return false;
        e.duration /= 2;
        const auto copy = e;
        p.events.insert (p.events.begin() + i + 1, copy);
        return true;
    }

    bool merge (RhythmPattern& p, int i)
    {
        if (! inRange (p, i) || ! inRange (p, i + 1))
            return false;
        auto& a = p.events[static_cast<std::size_t> (i)];
        const auto& b = p.events[static_cast<std::size_t> (i + 1)];
        if (a.rest != b.rest || (! a.rest && a.rank != b.rank))
            return false;   // different notes: choose the rank first (nothing discarded silently)
        a.duration += b.duration;
        p.events.erase (p.events.begin() + i + 1);
        return true;
    }

    bool extend (RhythmPattern& p, int i, int ticks)
    {
        if (! inRange (p, i) || ! inRange (p, i + 1) || ticks <= 0)
            return false;
        auto& e = p.events[static_cast<std::size_t> (i)];
        auto& next = p.events[static_cast<std::size_t> (i + 1)];
        if (e.rest || ! next.rest || ticks > next.duration)
            return false;
        if (ticks < next.duration && next.duration - ticks < minEventTicks)
            return false;   // the rest left would be shorter than the shortest event
        e.duration += ticks;
        next.duration -= ticks;
        if (next.duration == 0)
            p.events.erase (p.events.begin() + i + 1);
        return true;
    }

    bool toRest (RhythmPattern& p, int i)
    {
        if (! inRange (p, i))
            return false;
        p.events[static_cast<std::size_t> (i)].rest = true;
        return true;
    }

    bool toNote (RhythmPattern& p, int i, int rank)
    {
        if (! inRange (p, i) || rank < 0)
            return false;
        auto& e = p.events[static_cast<std::size_t> (i)];
        e.rest = false;
        e.rank = rank;
        p.maxRank = std::max (p.maxRank, rank);
        return true;
    }

    bool remove (RhythmPattern& p, int i) { return toRest (p, i); }

    void reverse (RhythmPattern& p) { std::reverse (p.events.begin(), p.events.end()); }

    void mirror (RhythmPattern& p)
    {
        for (auto& e : p.events)
            if (! e.rest)
                e.rank = std::max (0, p.maxRank - e.rank);
    }

    void rotate (RhythmPattern& p, int k)
    {
        const int n = static_cast<int> (p.events.size());
        if (n == 0)
            return;
        k = ((k % n) + n) % n;
        std::rotate (p.events.begin(), p.events.begin() + k, p.events.end());
    }

    bool setLength (RhythmPattern& p, int ticks)
    {
        const int now = p.lengthTicks();
        if (ticks < minEventTicks || ticks > maxPatternSteps * ticksPerStep)
            return false;
        if (ticks > now)
        {
            if (! p.events.empty() && p.events.back().rest)
                p.events.back().duration += ticks - now;
            else
                p.events.push_back ({ 0, true, ticks - now, 0 });
            return true;
        }
        int cut = now - ticks;
        if (cut == 0)
            return true;
        if (p.events.empty() || ! p.events.back().rest || p.events.back().duration < cut)
            return false;   // only trailing silence is cut: notes are never removed by LENGTH
        auto& last = p.events.back();
        if (last.duration - cut != 0 && last.duration - cut < minEventTicks)
            return false;
        last.duration -= cut;
        if (last.duration == 0)
            p.events.pop_back();
        return ! p.events.empty();
    }
}

double swungTick (long long tick, double swing) noexcept
{
    const auto start = [swing] (long long step) { return static_cast<double> (step) * ticksPerStep + ((step % 2 + 2) % 2 == 1 ? swing * 0.5 * ticksPerStep : 0.0); };
    const long long step = tick >= 0 ? tick / ticksPerStep : -((-tick + ticksPerStep - 1) / ticksPerStep);
    const double frac = static_cast<double> (tick - step * ticksPerStep) / ticksPerStep;
    return start (step) + frac * (start (step + 1) - start (step));
}

MidiSequence rhythmSequence (const RhythmPattern& pattern, const std::vector<HeldChord>& chords, Mapping mapping, double bpm, double gate, double swing)
{
    MidiSequence s;
    s.name = mappingName (mapping);
    const int length = pattern.lengthTicks();
    if (length <= 0)
        return s;
    const double secondsPerTick = 0.25 * 60.0 / bpm / ticksPerStep;   // a step is 1/16
    long long barStart = 0;
    long long cycleStart = 0;
    for (const auto& chord : chords)
    {
        const long long barEnd = barStart + 16LL * chord.bars * ticksPerStep;
        // Every event whose onset falls in this chord's bars (the phase runs on across chords).
        while (true)
        {
            long long t = cycleStart;
            for (const auto& e : pattern.events)
            {
                const long long onset = t;
                t += e.duration;
                if (onset < barStart)
                    continue;
                if (onset >= barEnd)
                    break;
                if (e.rest)
                    continue;
                const int note = noteFor (mapping, e.rank, pattern.maxRank, chord.notes, chord.octaves, cycleStart / length);
                if (note < 0)
                    continue;
                const double on = swungTick (onset, swing) * secondsPerTick;
                const double off = on + gate * e.duration * secondsPerTick;
                s.events.push_back ({ on, MidiEvent::Type::noteOn, note, e.velocity > 0 ? e.velocity : 100, 1 });
                s.events.push_back ({ off, MidiEvent::Type::noteOff, note, 0, 1 });
            }
            if (cycleStart + length > barEnd)
                break;
            cycleStart += length;
        }
        barStart = barEnd;
    }
    s.sort();
    return s;
}

namespace
{
    std::string noteName (int n)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return std::string (names[(n % 12 + 12) % 12]) + std::to_string (n / 12 - 1);
    }

    std::string lineText (const std::vector<int>& notes, std::size_t limit = 16)
    {
        std::ostringstream o;
        for (std::size_t i = 0; i < notes.size() && i < limit; ++i)
            o << (i ? " " : "") << noteName (notes[i]);
        if (notes.size() > limit)
            o << " ...";
        return o.str();
    }

    void writeMidi (const std::filesystem::path& file, const MidiSequence& s, int& count)
    {
        std::string error;
        if (io::writeMidiFile (file, s, error))
            ++count;
    }
}

LabResult runArpPatternLab (const std::filesystem::path& outDir, bool writeAudio)
{
    LabResult result;
    std::filesystem::create_directories (outDir / "fixtures");
    std::filesystem::create_directories (outDir / "midi");
    if (writeAudio)
        std::filesystem::create_directories (outDir / "audio");

    const Mapping mappings[] = { Mapping::adaptiveContour, Mapping::degreeCycle, Mapping::contourFit, Mapping::fitOctaveCycles, Mapping::hybrid };
    const std::vector<std::pair<std::string, std::vector<int>>> holds {
        { "1 note", { 60 } }, { "interval", { 60, 67 } }, { "major", { 60, 64, 67 } }, { "minor", { 57, 60, 64 } },
        { "seventh", { 50, 53, 57, 60 } }, { "extended", { 55, 59, 62, 65, 69 } }, { "six", { 48, 52, 55, 59, 62, 66 } } };

    // The listening source: a synthetic plucked string through baseline A (the plain resampler).
    RenderConfig config;
    config.sampleRate = 44100.0;
    const auto source = testsignals::pluck (261.6256, 2.5, 44100.0, 7);

    std::ostringstream md;
    md << std::fixed << std::setprecision (2);
    md << "# Custom ARP pattern lab (CP1)\n\n"
       << "Generated by `research-renderer --arp-lab <dir>` (src/research/ArpPatternLab.cpp). Stage A: one captured\n"
       << "note-on is one step; the lab steps a 1/16 grid at 120 BPM, GATE 75 %, and the pattern's phase runs on\n"
       << "across chord changes. Mappings: **adaptive-contour** (the spec's default, `round (r / R * (N - 1))`),\n"
       << "**degree-cycle** (`r mod N`), and the lab's candidate **contour-fit** (`r` while the pool has more notes\n"
       << "than the gesture has ranks, otherwise adaptive), and **fit-octave-cycles** (contour-fit over the held notes\n"
       << "only; OCTAVES repeats the whole pattern an octave higher per cycle, as the built-in styles climb), and the\n"
       << "**hybrid** (contour-fit over the fewest octaves the gesture needs, then the whole gesture at each higher octave\n"
       << "position that fits in OCTAVES, back to the start after the top; the position moves at the pattern's loop).\n\n"
       << "Columns: direction = share of steps (around the loop) moving the same way as captured; collapsed =\n"
       << "captured ranks that share a note with another; leap = largest step played (captured); span = range played.\n\n";

    std::map<Mapping, std::vector<double>> agreementByMapping;
    std::map<Mapping, int> repeatBreaks;
    for (const auto& g : referenceGestures())
    {
        const auto contour = extractContour (g.notes);
        writeMidi (outDir / "fixtures" / (g.name + ".mid"), capturedSequence (g), result.midiFiles);
        std::vector<int> capturedPitches;
        for (const int r : contour.ranks)
            capturedPitches.push_back (contour.pitches[static_cast<std::size_t> (r)]);
        md << "## " << g.name << "\n\n" << g.description << ". " << contour.ranks.size() << " steps, " << contour.pitches.size()
           << " distinct pitches (R = " << contour.maxRank << ")";
        if (contour.chordAttacks > 0)
            md << "; " << contour.chordAttacks << " chord attack (highest note kept, " << contour.notesDropped << " dropped)";
        md << ".\n\nCaptured: `" << lineText (capturedPitches, 32) << "`; ranks: `";
        for (std::size_t i = 0; i < contour.ranks.size(); ++i)
            md << (i ? " " : "") << contour.ranks[i];
        md << "`\n\n| held | oct | mapping | played (one cycle) | direction | collapsed | leap | span |\n|---|---|---|---|---|---|---|---|\n";
        for (const auto& [holdName, held] : holds)
            for (const int octaves : { 1, 2, 4 })
                for (const auto m : mappings)
                {
                    const auto line = playLine (contour, held, octaves, m);
                    const auto played = std::vector<int> (line.begin(), line.begin() + static_cast<std::ptrdiff_t> (std::min (line.size(), contour.ranks.size())));
                    const auto f = measure (contour, played);
                    agreementByMapping[m].push_back (f.directionAgreement);
                    repeatBreaks[m] += f.repeatsKept ? 0 : 1;
                    md << "| " << holdName << " | " << octaves << " | " << mappingName (m) << " | `" << lineText (line, 12) << "` | "
                       << f.directionAgreement << " | " << f.collapsedRanks << " | " << f.maxLeapPlayed << " (" << f.maxLeapCaptured << ") | "
                       << f.spanPlayed << " |\n";
                }
        md << "\n";

        for (const auto m : mappings)
        {
            const auto seq = progressionSequence (contour, listeningProgression(), m);
            const auto stem = g.name + "-" + mappingName (m);
            writeMidi (outDir / "midi" / (stem + ".mid"), seq, result.midiFiles);
            if (writeAudio)
            {
                const auto out = renderSequence (source, 60.0, seq, config);
                std::string error;
                if (io::writeAudioFile (outDir / "audio" / (stem + ".wav"), out.audio, io::SampleFormat::pcm16, error))
                    ++result.audioFiles;
            }
        }
        for (const auto m : { Mapping::contourFit, Mapping::fitOctaveCycles, Mapping::hybrid })
        {
            const auto seq = progressionSequence (contour, octavesProgression(), m);
            const auto stem = g.name + "-octaves-" + mappingName (m);
            writeMidi (outDir / "midi" / (stem + ".mid"), seq, result.midiFiles);
            if (writeAudio)
            {
                const auto out = renderSequence (source, 60.0, seq, config);
                std::string error;
                if (io::writeAudioFile (outDir / "audio" / (stem + ".wav"), out.audio, io::SampleFormat::pcm16, error))
                    ++result.audioFiles;
            }
        }
        if (writeAudio)
        {
            const auto out = renderSequence (source, 60.0, capturedSequence (g), config);
            std::string error;
            if (io::writeAudioFile (outDir / "audio" / (g.name + "-captured.wav"), out.audio, io::SampleFormat::pcm16, error))
                ++result.audioFiles;
        }
    }

    // Rhythm (Part II): Stage B from the captured onsets, and an edited pattern.
    auto describe = [] (const RhythmPattern& p) {
        std::ostringstream o;
        for (std::size_t i = 0; i < p.events.size(); ++i)
        {
            const auto& e = p.events[i];
            o << (i ? " · " : "") << (e.rest ? std::string ("rest") : "r" + std::to_string (e.rank)) << " ";
            const int d = e.duration;
            if (d % ticksPerStep == 0)
                o << d / ticksPerStep;
            else
                o << d << "/" << ticksPerStep;
        }
        return o.str();
    };
    md << "## Rhythm: variable-duration events (Part II)\n\n"
       << "Ticks: " << ticksPerStep << " per step (one ARP RATE), shortest event " << minEventTicks << ". Durations below in steps\n"
       << "(or ticks/" << ticksPerStep << "). Renders use contour-fit (and `-hybrid`: the hybrid), GATE 75 % of each event's own duration.\n\n";
    struct RhythmDemo
    {
        std::string name, how;
        RhythmPattern pattern;
        double swing = 0.0;
    };
    std::vector<RhythmDemo> demos;
    const auto gestures = referenceGestures();
    auto gesture = [&gestures] (const std::string& name) {
        return *std::find_if (gestures.begin(), gestures.end(), [&] (const Gesture& g) { return g.name == name; });
    };
    {
        const auto g = gesture ("rests");
        demos.push_back ({ "rests-stage-b", "Stage B: the captured onsets on a 1/16 grid, rests kept, loop 12 steps",
                           stageB (g.notes, 0.25, ticksPerStep / 4, g.lengthQuarters) });
    }
    {
        auto p = stageA (extractContour (gesture ("five-up").notes));
        edit::split (p, 1);            // the second note into two halves
        edit::split (p, 4);            // the fourth original note too
        edit::split (p, 5);            // and its second half again (quarter steps)
        edit::toRest (p, 6);           // a short rest inside
        edit::setLength (p, p.lengthTicks() + ticksPerStep);   // a step of silence at the end
        edit::extend (p, static_cast<int> (p.events.size()) - 2, ticksPerStep / 2);   // the last note sustains into it
        demos.push_back ({ "five-up-edited", "five-up, edited: SPLIT, SPLIT, SPLIT, REST, LENGTH +1, EXTEND", p });
        demos.push_back ({ "five-up-edited-swing", "the same with SWING 50 % (subdivisions keep their place inside the swung step)", p, 0.5 });
    }
    {
        auto p = stageA (extractContour (gesture ("two-pitch").notes));
        edit::merge (p, 0);            // the two first Es: one longer note
        edit::merge (p, 3);            // the two last Bs
        demos.push_back ({ "two-pitch-merged", "two-pitch, MERGE of each repeated pair: they sustain instead of retriggering", p });
    }
    md << "| demo | how | events | length |\n|---|---|---|---|\n";
    for (const auto& d : demos)
    {
        md << "| " << d.name << " | " << d.how << " | `" << describe (d.pattern) << "` | " << static_cast<double> (d.pattern.lengthTicks()) / ticksPerStep << " steps |\n";
        for (const auto m : { Mapping::contourFit, Mapping::hybrid })
        {
            const auto seq = rhythmSequence (d.pattern, listeningProgression(), m, 120.0, 0.75, d.swing);
            const auto stem = d.name + (m == Mapping::hybrid ? "-hybrid" : "");
            writeMidi (outDir / "midi" / (stem + ".mid"), seq, result.midiFiles);
            if (writeAudio)
            {
                const auto out = renderSequence (source, 60.0, seq, config);
                std::string error;
                if (io::writeAudioFile (outDir / "audio" / (stem + ".wav"), out.audio, io::SampleFormat::pcm16, error))
                    ++result.audioFiles;
            }
        }
    }
    md << "\n";

    md << "## Summary\n\n| mapping | mean direction agreement | cases where a repeated note moved |\n|---|---|---|\n";
    for (const auto m : mappings)
    {
        const auto& v = agreementByMapping[m];
        double sum = 0.0;
        for (const double x : v)
            sum += x;
        md << "| " << mappingName (m) << " | " << (v.empty() ? 0.0 : sum / v.size()) << " | " << repeatBreaks[m] << " |\n";
    }
    md << "\nListening renders (audio/): each gesture as captured, then over the progression\n";
    for (const auto& c : listeningProgression())
        md << "- " << c.name << (c.octaves > 1 ? ", OCTAVES " + std::to_string (c.octaves) : std::string()) << ", " << c.bars << " bar" << (c.bars > 1 ? "s" : "") << "\n";
    md << "\nfor each mapping (`<gesture>-<mapping>.wav`), and over the OCTAVES progression\n";
    for (const auto& c : octavesProgression())
        md << "- " << c.name << ", " << c.bars << " bars\n";
    md << "\nfor contour-fit, fit-octave-cycles and the hybrid (`<gesture>-octaves-<mapping>.wav`).\n";
    result.markdown = md.str();
    return result;
}

} // namespace osp::research::arplab
