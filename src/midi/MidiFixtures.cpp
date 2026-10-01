#include "midi/MidiFixtures.h"

#include <algorithm>

namespace osp
{

void MidiSequence::sort()
{
    auto rank = [] (const MidiEvent& e) {
        switch (e.type)
        {
            case MidiEvent::Type::noteOff: return 0;
            case MidiEvent::Type::allNotesOff: return 0;
            case MidiEvent::Type::sustainPedal: return e.value >= 64 ? 2 : 1;
            case MidiEvent::Type::noteOn: return 3;
        }
        return 3;
    };
    std::stable_sort (events.begin(), events.end(), [&] (const MidiEvent& a, const MidiEvent& b) {
        if (a.timeSeconds != b.timeSeconds)
            return a.timeSeconds < b.timeSeconds;
        return rank (a) < rank (b);
    });
}

namespace fixtures
{

namespace
{
    int clampNote (int note) { return std::clamp (note, 0, 127); }

    void addNote (MidiSequence& seq, double start, double duration, int note, int velocity)
    {
        seq.events.push_back ({ start, MidiEvent::Type::noteOn, clampNote (note), velocity, 1 });
        seq.events.push_back ({ start + duration, MidiEvent::Type::noteOff, clampNote (note), 0, 1 });
    }

    MidiSequence finish (MidiSequence seq)
    {
        seq.sort();
        return seq;
    }
}

MidiSequence repetition (int ref)
{
    MidiSequence seq { "repetition", {} };
    for (int i = 0; i < 8; ++i)
        addNote (seq, 0.5 * i, 0.4, ref, 90);
    return finish (seq);
}

MidiSequence dynamics (int ref)
{
    MidiSequence seq { "dynamics", {} };
    const int velocities[] = { 20, 40, 60, 80, 100, 127 };
    for (int i = 0; i < 6; ++i)
        addNote (seq, 1.5 * i, 1.2, ref, velocities[i]);
    return finish (seq);
}

MidiSequence registerSweep (int ref)
{
    MidiSequence seq { "register", {} };
    const int offsets[] = { -24, -12, 0, 12, 24 };
    for (int i = 0; i < 5; ++i)
        addNote (seq, 2.5 * i, 2.0, ref + offsets[i], 100);
    return finish (seq);
}

MidiSequence melody (int ref)
{
    // Major-key phrase: steps (0-2-4), thirds (0-4, 5-2), fourth (0-5), fifth (0-7),
    // a repeated note (7-7) and varying velocity.
    struct N { double start, duration; int offset, velocity; };
    const N notes[] = {
        { 0.00, 0.45, 0, 80 },  { 0.50, 0.45, 2, 70 },   { 1.00, 0.45, 4, 90 },  { 1.50, 0.45, 0, 65 },
        { 2.00, 0.95, 7, 105 }, { 3.00, 0.20, 7, 60 },   { 3.25, 0.20, 7, 75 },  { 3.50, 0.45, 5, 85 },
        { 4.00, 0.45, 2, 70 },  { 4.50, 0.45, 5, 95 },   { 5.00, 0.45, 4, 80 },  { 5.50, 0.45, 2, 60 },
        { 6.00, 0.45, -5, 90 }, { 6.50, 1.40, 0, 100 },
    };
    MidiSequence seq { "melody", {} };
    for (const auto& n : notes)
        addNote (seq, n.start, n.duration, ref + n.offset, n.velocity);
    return finish (seq);
}

MidiSequence chords (int ref)
{
    const std::vector<std::vector<int>> voicings = {
        { 0, 4, 7 },          // major, close
        { 0, 3, 7 },          // minor, close
        { 0, 5, 7 },          // sus4, close
        { -12, 7, 16, 24 },   // major, wide
        { -12, 3, 10, 19 },   // minor (m7 add9 colour), wide
    };
    MidiSequence seq { "chords", {} };
    for (std::size_t i = 0; i < voicings.size(); ++i)
        for (int offset : voicings[i])
            addNote (seq, 3.0 * static_cast<double> (i), 2.5, ref + offset, 90);
    return finish (seq);
}

MidiSequence longHold (int ref)
{
    MidiSequence seq { "long-hold", {} };
    addNote (seq, 0.0, 60.0, ref, 100);
    return finish (seq);
}

MidiSequence longChord (int ref)
{
    MidiSequence seq { "long-chord", {} };
    for (int offset : { 0, 3, 7, 10 })
        addNote (seq, 0.0, 60.0, ref + offset, 90);
    return finish (seq);
}

MidiSequence repeatedSustains (int ref)
{
    struct N { double duration; int offset, velocity; };
    const N notes[] = { { 5.0, 0, 90 }, { 8.0, 0, 75 }, { 6.0, 7, 100 }, { 10.0, 0, 85 }, { 7.0, 5, 70 } };
    MidiSequence seq { "repeated-sustains", {} };
    double t = 0.0;
    for (const auto& n : notes)
    {
        addNote (seq, t, n.duration, ref + n.offset, n.velocity);
        t += n.duration + 1.0;
    }
    return finish (seq);
}

const std::vector<std::string>& allNames()
{
    static const std::vector<std::string> names = { "repetition", "dynamics", "register", "melody",
                                                    "chords", "long-hold", "long-chord", "repeated-sustains" };
    return names;
}

std::optional<MidiSequence> byName (const std::string& name, int ref)
{
    if (name == "repetition") return repetition (ref);
    if (name == "dynamics") return dynamics (ref);
    if (name == "register") return registerSweep (ref);
    if (name == "melody") return melody (ref);
    if (name == "chords") return chords (ref);
    if (name == "long-hold") return longHold (ref);
    if (name == "long-chord") return longChord (ref);
    if (name == "repeated-sustains") return repeatedSustains (ref);
    return std::nullopt;
}

std::optional<std::vector<std::string>> profile (const std::string& name)
{
    if (name == "quick") return std::vector<std::string> { "repetition", "register" };
    if (name == "standard") return std::vector<std::string> { "repetition", "dynamics", "register", "melody", "chords" };
    if (name == "sustain") return std::vector<std::string> { "long-hold", "long-chord", "repeated-sustains" };
    if (name == "full") return allNames();
    return std::nullopt;
}

} // namespace fixtures
} // namespace osp
