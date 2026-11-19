#include "model/ArpPatternModel.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace osp::arppattern
{

int Pattern::lengthTicks() const noexcept
{
    long long t = 0;
    for (const auto& e : events)
        t += e.duration;
    return static_cast<int> (std::min<long long> (t, 1LL << 30));
}

int Pattern::onsetOf (int index) const noexcept
{
    long long t = 0;
    for (int i = 0; i < index && i < static_cast<int> (events.size()); ++i)
        t += events[static_cast<std::size_t> (i)].duration;
    return static_cast<int> (t);
}

int Pattern::notes() const noexcept
{
    return static_cast<int> (std::count_if (events.begin(), events.end(), [] (const Event& e) { return ! e.rest; }));
}

int Pattern::highestRank() const noexcept
{
    int r = 0;
    for (const auto& e : events)
        if (! e.rest)
            r = std::max (r, e.rank);
    return r;
}

bool Pattern::valid() const noexcept
{
    return compile (*this).valid();
}

double captureLengthQuarters (const std::vector<CapturedNote>& capture)
{
    if (capture.empty())
        return 0.0;
    double first = capture.front().onset, last = 0.0;
    for (const auto& n : capture)
        first = std::min (first, n.onset);
    for (const auto& n : capture)
        last = std::max (last, n.onset + std::max (0.0, n.duration));
    return std::max (0.0, last - first);
}

namespace
{
    struct Step
    {
        CapturedNote note;
        int rank = 0;
    };

    /** The capture's attacks in time order (a chord's notes reduced to one by the policy), each
        with its rank among the distinct pitches chosen. */
    std::vector<Step> attacksOf (std::vector<CapturedNote> notes, ChordPolicy policy, InterpretReport& report, int& maxRank)
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const CapturedNote& a, const CapturedNote& b) { return a.onset < b.onset; });
        const double origin = notes.empty() ? 0.0 : notes.front().onset;
        constexpr double together = 1.0 / 32.0;   // quarters: one attack
        std::vector<Step> steps;
        for (std::size_t i = 0; i < notes.size();)
        {
            std::size_t j = i + 1;
            while (j < notes.size() && notes[j].onset - notes[i].onset <= together)
                ++j;
            auto chosen = notes[i];
            for (std::size_t k = i + 1; k < j; ++k)
                if ((policy == ChordPolicy::highest && notes[k].pitch > chosen.pitch) || (policy == ChordPolicy::lowest && notes[k].pitch < chosen.pitch))
                    chosen = notes[k];
            if (j - i > 1)
            {
                ++report.chordAttacks;
                report.chordNotesDropped += static_cast<int> (j - i - 1);
            }
            chosen.onset -= origin;
            steps.push_back ({ chosen, 0 });
            i = j;
        }
        std::set<int> distinct;
        for (const auto& s : steps)
            distinct.insert (s.note.pitch);
        const std::vector<int> pitches (distinct.begin(), distinct.end());
        for (auto& s : steps)
            s.rank = static_cast<int> (std::lower_bound (pitches.begin(), pitches.end(), s.note.pitch) - pitches.begin());
        maxRank = std::clamp (pitches.empty() ? 0 : static_cast<int> (pitches.size()) - 1, 0, maxRankLimit);
        return steps;
    }
}

Pattern interpret (const std::vector<CapturedNote>& capture, const Interpretation& how, InterpretReport* reportOut)
{
    InterpretReport report;
    report.capturedNotes = static_cast<int> (capture.size());
    Pattern p;
    p.mapping = how.mapping;
    auto steps = attacksOf (capture, how.chord, report, p.maxRank);
    for (auto& s : steps)
        s.rank = std::min (s.rank, maxRankLimit);
    const int capacityTicks = arp::maxPatternSteps * arp::ticksPerStep;

    if (how.timing == Interpretation::Timing::steps)
    {
        for (const auto& s : steps)
        {
            if (static_cast<int> (p.events.size()) >= std::min (arp::maxPatternEvents, arp::maxPatternSteps))
            {
                ++report.beyondCapacity;
                continue;
            }
            p.events.push_back ({ s.rank, false, arp::ticksPerStep, 0 });
        }
        if (how.lengthQuarters > 0.0 && how.stepQuarters > 0.0)
        {
            const int ticks = std::clamp (static_cast<int> (std::llround (how.lengthQuarters / how.stepQuarters)) * arp::ticksPerStep, arp::ticksPerStep, capacityTicks);
            if (ticks > p.lengthTicks())
                edit::setLength (p, ticks);
        }
    }
    else
    {
        const double stepQuarters = how.stepQuarters > 0.0 ? how.stepQuarters : 0.25;
        const int quantum = std::clamp (how.quantumTicks, arp::minEventTicks, arp::ticksPerStep);
        const auto toTicks = [&] (double quarters) {
            const double ticks = quarters / stepQuarters * arp::ticksPerStep;
            return static_cast<int> (std::llround (ticks / quantum)) * quantum;
        };
        double lengthQuarters = how.lengthQuarters;
        if (lengthQuarters <= 0.0)
        {
            // The capture's own length, to whole steps (at least one).
            std::vector<CapturedNote> shifted;
            for (const auto& s : steps)
                shifted.push_back (s.note);
            lengthQuarters = std::max (1.0, std::ceil (captureLengthQuarters (shifted) / stepQuarters - 1.0e-6)) * stepQuarters;
        }
        const int length = std::clamp (std::max (quantum, toTicks (lengthQuarters)), quantum, capacityTicks);

        struct Placed
        {
            int at, end, rank, velocity;
        };
        std::vector<Placed> placed;
        int last = -quantum;
        for (const auto& s : steps)
        {
            // Two notes on one slot: the later moves to the next free slot (none is dropped silently).
            const int at = std::max (toTicks (s.note.onset), last + quantum);
            if (at >= length)
            {
                if (toTicks (s.note.onset) >= capacityTicks || at >= capacityTicks)
                    ++report.beyondCapacity;
                else
                    ++report.beyondLength;
                continue;
            }
            if (static_cast<int> (placed.size()) * 2 + 2 > arp::maxPatternEvents)   // (a rest may precede each note)
            {
                ++report.beyondCapacity;
                continue;
            }
            const double endTicks = (s.note.onset + s.note.duration) / stepQuarters * arp::ticksPerStep;
            const int end = std::max (at + quantum, static_cast<int> (std::ceil (endTicks / quantum - 1.0e-9)) * quantum);
            placed.push_back ({ at, end, s.rank, 0 });
            last = at;
        }
        int t = 0;
        for (std::size_t i = 0; i < placed.size(); ++i)
        {
            if (placed[i].at > t)
                p.events.push_back ({ 0, true, placed[i].at - t, 0 });
            const int next = i + 1 < placed.size() ? placed[i + 1].at : length;
            // A silence longer than half a step is a rest; a shorter one is articulation (GATE's job).
            const int end = next - placed[i].end > arp::ticksPerStep / 2 ? std::min (placed[i].end, next) : next;
            p.events.push_back ({ placed[i].rank, false, end - placed[i].at, 0 });
            t = end;
        }
        if (t < length && ! placed.empty())
            p.events.push_back ({ 0, true, length - t, 0 });
    }
    report.placedNotes = p.notes();
    if (reportOut != nullptr)
        *reportOut = report;
    return p;
}

arp::CustomPattern compile (const Pattern& pattern)
{
    arp::CustomPattern c;
    c.mapping = pattern.mapping;
    c.maxRank = std::clamp (std::max (pattern.maxRank, pattern.highestRank()), 0, arp::maxPatternRank);
    if (static_cast<int> (pattern.events.size()) > arp::maxPatternEvents)
        return {};   // over capacity: invalid (count 0), never truncated
    for (const auto& e : pattern.events)
        c.events[static_cast<std::size_t> (c.count++)] = { static_cast<std::int16_t> (std::clamp (e.rank, 0, arp::maxPatternRank)), e.rest,
                                                           static_cast<std::uint8_t> (std::clamp (e.velocity, 0, 127)), e.duration };
    return c;
}

Pattern decompile (const arp::CustomPattern& compiled)
{
    Pattern p;
    p.mapping = compiled.mapping;
    p.maxRank = compiled.maxRank;
    for (int i = 0; i < std::clamp (compiled.count, 0, arp::maxPatternEvents); ++i)
    {
        const auto& e = compiled.events[static_cast<std::size_t> (i)];
        p.events.push_back ({ e.rank, e.rest, e.duration, e.velocity });
    }
    return p;
}

namespace edit
{
    namespace
    {
        bool inRange (const Pattern& p, int i) { return i >= 0 && i < static_cast<int> (p.events.size()); }
        bool roomForOneMore (const Pattern& p) { return static_cast<int> (p.events.size()) < arp::maxPatternEvents; }
    }

    bool setRank (Pattern& p, int i, int rank)
    {
        if (! inRange (p, i) || rank < 0 || rank > maxRankLimit)
            return false;
        auto& e = p.events[static_cast<std::size_t> (i)];
        if (! e.rest && e.rank == rank)
            return false;
        e.rest = false;
        e.rank = rank;
        p.maxRank = std::max (p.maxRank, rank);
        return true;
    }

    bool split (Pattern& p, int i)
    {
        if (! inRange (p, i) || ! roomForOneMore (p))
            return false;
        auto& e = p.events[static_cast<std::size_t> (i)];
        if (e.duration % 2 != 0 || e.duration / 2 < arp::minEventTicks)
            return false;
        e.duration /= 2;
        const auto copy = e;
        p.events.insert (p.events.begin() + i + 1, copy);
        return true;
    }

    bool merge (Pattern& p, int i)
    {
        if (! inRange (p, i) || ! inRange (p, i + 1))
            return false;
        auto& a = p.events[static_cast<std::size_t> (i)];
        const auto& b = p.events[static_cast<std::size_t> (i + 1)];
        if (a.rest != b.rest || (! a.rest && a.rank != b.rank))
            return false;   // different notes: choose the rank first (nothing discarded silently)
        if (static_cast<long long> (a.duration) + b.duration > static_cast<long long> (arp::maxPatternSteps) * arp::ticksPerStep)
            return false;
        a.duration += b.duration;
        p.events.erase (p.events.begin() + i + 1);
        return true;
    }

    bool extend (Pattern& p, int i, int ticks)
    {
        if (! inRange (p, i) || ! inRange (p, i + 1) || ticks <= 0)
            return false;
        auto& e = p.events[static_cast<std::size_t> (i)];
        auto& next = p.events[static_cast<std::size_t> (i + 1)];
        if (e.rest || ! next.rest || ticks > next.duration)
            return false;
        if (ticks < next.duration && next.duration - ticks < arp::minEventTicks)
            return false;   // the rest left would be shorter than the shortest event
        e.duration += ticks;
        next.duration -= ticks;
        if (next.duration == 0)
            p.events.erase (p.events.begin() + i + 1);
        return true;
    }

    bool toRest (Pattern& p, int i)
    {
        if (! inRange (p, i) || p.events[static_cast<std::size_t> (i)].rest)
            return false;
        p.events[static_cast<std::size_t> (i)].rest = true;
        return true;
    }

    bool remove (Pattern& p, int i) { return toRest (p, i); }

    bool insertStep (Pattern& p, int i)
    {
        if (i < 0 || i > static_cast<int> (p.events.size()) || ! roomForOneMore (p)
            || p.lengthTicks() + arp::ticksPerStep > arp::maxPatternSteps * arp::ticksPerStep)
            return false;
        p.events.insert (p.events.begin() + i, Event { 0, true, arp::ticksPerStep, 0 });
        return true;
    }

    bool deleteEvent (Pattern& p, int i)
    {
        if (! inRange (p, i) || p.events.size() <= 1)
            return false;
        p.events.erase (p.events.begin() + i);
        return true;
    }

    void reverse (Pattern& p) { std::reverse (p.events.begin(), p.events.end()); }

    void mirror (Pattern& p)
    {
        const int top = std::max (p.maxRank, p.highestRank());
        for (auto& e : p.events)
            if (! e.rest)
                e.rank = std::max (0, top - e.rank);
    }

    void rotate (Pattern& p, int k)
    {
        const int n = static_cast<int> (p.events.size());
        if (n == 0)
            return;
        k = ((k % n) + n) % n;
        std::rotate (p.events.begin(), p.events.begin() + k, p.events.end());
    }

    bool setLength (Pattern& p, int ticks)
    {
        const int now = p.lengthTicks();
        if (ticks < arp::minEventTicks || ticks > arp::maxPatternSteps * arp::ticksPerStep || ticks == now)
            return false;
        if (ticks > now)
        {
            if (! p.events.empty() && p.events.back().rest)
                p.events.back().duration += ticks - now;
            else
            {
                if (! roomForOneMore (p))
                    return false;
                p.events.push_back ({ 0, true, ticks - now, 0 });
            }
            return true;
        }
        const int cut = now - ticks;
        if (p.events.empty() || ! p.events.back().rest || p.events.back().duration < cut)
            return false;   // only trailing silence is cut: LENGTH never removes notes
        auto& last = p.events.back();
        if (last.duration - cut != 0 && last.duration - cut < arp::minEventTicks)
            return false;
        last.duration -= cut;
        if (last.duration == 0)
            p.events.pop_back();
        return ! p.events.empty();
    }

    void setMapping (Pattern& p, arp::CustomMapping mapping) { p.mapping = mapping; }
}

void History::reset (const Pattern& now)
{
    states.assign (1, now);
    position = 0;
}

void History::push (const Pattern& now)
{
    if (states.empty())
    {
        reset (now);
        return;
    }
    states.resize (static_cast<std::size_t> (position + 1));   // a new edit drops the redo branch
    states.push_back (now);
    if (static_cast<int> (states.size()) > capacity)
        states.erase (states.begin());
    position = static_cast<int> (states.size()) - 1;
}

const Pattern& History::undo()
{
    if (canUndo())
        --position;
    return current();
}

const Pattern& History::redo()
{
    if (canRedo())
        ++position;
    return current();
}

} // namespace osp::arppattern
