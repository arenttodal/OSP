#pragma once

#include "core/AudioData.h"
#include "midi/MidiEvent.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace osp::andor
{

/**
    Renders a MIDI sequence through an ANDOR engine (PhraseEngine, GrainEngine) headlessly, with
    events applied sample-accurately inside blocks of `blockSize`, exactly as a host would call
    processBlock. The engine must already be prepared at `sampleRate`, with its model and
    parameters set. Rule 4: every algorithm is testable without a GUI.
*/
template <typename Engine>
AudioData renderOffline (Engine& engine, const MidiSequence& sequence, double sampleRate, int blockSize, double tailSeconds)
{
    auto events = sequence.events;
    std::stable_sort (events.begin(), events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.timeSeconds < b.timeSeconds; });
    const double endSeconds = (events.empty() ? 0.0 : events.back().timeSeconds) + tailSeconds;
    const auto total = static_cast<std::int64_t> (std::ceil (endSeconds * sampleRate));
    auto out = AudioData::allocate (2, total, sampleRate);
    std::vector<float> l (static_cast<std::size_t> (blockSize)), r (static_cast<std::size_t> (blockSize));

    std::size_t next = 0;
    std::int64_t position = 0;
    while (position < total)
    {
        const int block = static_cast<int> (std::min<std::int64_t> (blockSize, total - position));
        int done = 0;
        while (done < block)
        {
            // Apply every event due at this sample, then render up to the next event.
            while (next < events.size() && static_cast<std::int64_t> (std::llround (events[next].timeSeconds * sampleRate)) <= position + done)
            {
                const auto& e = events[next++];
                switch (e.type)
                {
                    case MidiEvent::Type::noteOn: engine.noteOn (e.note, static_cast<float> (e.value) / 127.0f); break;
                    case MidiEvent::Type::noteOff: engine.noteOff (e.note); break;
                    case MidiEvent::Type::sustainPedal: engine.setSustainPedal (e.value >= 64); break;
                    case MidiEvent::Type::allNotesOff: engine.allNotesOff(); break;
                }
            }
            int run = block - done;
            if (next < events.size())
            {
                const auto due = static_cast<std::int64_t> (std::llround (events[next].timeSeconds * sampleRate)) - (position + done);
                run = static_cast<int> (std::clamp<std::int64_t> (due, 1, run));
            }
            engine.render (l.data(), r.data(), run);
            for (int i = 0; i < run; ++i)
            {
                out.channels[0][static_cast<std::size_t> (position + done + i)] = l[static_cast<std::size_t> (i)];
                out.channels[1][static_cast<std::size_t> (position + done + i)] = r[static_cast<std::size_t> (i)];
            }
            done += run;
        }
        position += block;
    }
    return out;
}

/** A note helper for tests and demos. */
inline void addNote (MidiSequence& seq, double start, double duration, int key, int velocity = 100)
{
    seq.events.push_back ({ start, MidiEvent::Type::noteOn, key, velocity, 1 });
    seq.events.push_back ({ start + duration, MidiEvent::Type::noteOff, key, 0, 1 });
}

} // namespace osp::andor
