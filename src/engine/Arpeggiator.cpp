#include "engine/Arpeggiator.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace osp
{

namespace
{
    constexpr std::int64_t never = std::numeric_limits<std::int64_t>::max();
    /** Offsets are computed from PPQ in double precision; this keeps a boundary that lands
        exactly on a sample on that sample instead of the next one. */
    constexpr double timeEpsilon = 1.0e-6;
}

double arp::rateQuarters (ArpRate rate) noexcept
{
    switch (rate)
    {
        case ArpRate::quarter: return 1.0;
        case ArpRate::eighth: return 0.5;
        case ArpRate::sixteenth: return 0.25;
        case ArpRate::thirtySecond: return 0.125;
        case ArpRate::quarterDotted: return 1.5;
        case ArpRate::eighthDotted: return 0.75;
        case ArpRate::sixteenthDotted: return 0.375;
        case ArpRate::quarterTriplet: return 2.0 / 3.0;
        case ArpRate::eighthTriplet: return 1.0 / 3.0;
        case ArpRate::sixteenthTriplet: return 1.0 / 6.0;
    }
    return 0.5;
}

const char* arp::patternName (ArpPattern pattern) noexcept
{
    switch (pattern)
    {
        case ArpPattern::up: return "UP";
        case ArpPattern::down: return "DOWN";
        case ArpPattern::upDown: return "UP/DOWN";
        case ArpPattern::played: return "PLAYED";
        case ArpPattern::random: return "RANDOM";
        case ArpPattern::chord: return "CHORD";
    }
    return "UP";
}

const char* arp::rateName (ArpRate rate) noexcept
{
    switch (rate)
    {
        case ArpRate::quarter: return "1/4";
        case ArpRate::eighth: return "1/8";
        case ArpRate::sixteenth: return "1/16";
        case ArpRate::thirtySecond: return "1/32";
        case ArpRate::quarterDotted: return "1/4D";
        case ArpRate::eighthDotted: return "1/8D";
        case ArpRate::sixteenthDotted: return "1/16D";
        case ArpRate::quarterTriplet: return "1/4T";
        case ArpRate::eighthTriplet: return "1/8T";
        case ArpRate::sixteenthTriplet: return "1/16T";
    }
    return "1/8";
}

void Arpeggiator::prepare (double rate) noexcept
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    reset();
}

void Arpeggiator::reset() noexcept
{
    enabled = running = synced = hadSync = false;
    held = 0;
    pedal = false;
    sequenceDirty = true;
    sequenceLength = sortedCount = 0;
    blockStart = 0;
    blockSize = cursor = 0;
    gridIndex = 0;
    lastStepExact = phraseStartExact = 0.0;
    stepCount = 0;
    previousIndex = -1;
    phraseCounter = 0;
    for (auto& h : history)
        h = { -1, -1 };
    offTime.fill (never);
    sounding = 0;
    eventCount = 0;
}

void Arpeggiator::restartPattern() noexcept
{
    phraseCounter = 0;
    if (running)
    {
        stepCount = 0;
        previousIndex = -1;
        phraseSeed = Prng::deriveSeed (baseSeed, phraseCounter++, 0x417270);
        for (auto& h : history)
            h = { -1, -1 };
    }
}

void Arpeggiator::beginBlock (const Settings& s, const HostTiming& timing, int numSamples) noexcept
{
    eventCount = 0;
    cursor = 0;
    blockSize = std::max (0, numSamples);

    Settings next = s;
    next.gate = std::clamp (std::isfinite (next.gate) ? next.gate : 0.75, arp::minGate, arp::maxGate);
    next.octaves = std::clamp (next.octaves, arp::minOctaves, arp::maxOctaves);
    if (next.pattern != settings.pattern || next.octaves != settings.octaves)
        sequenceDirty = true;
    const bool wantEnabled = next.enabled;
    next.enabled = enabled;
    settings = next;

    // Time: the host's grid while it plays, otherwise free running at the last known tempo.
    const bool tempoKnown = timing.valid && std::isfinite (timing.bpm) && timing.bpm > 0.0;
    if (tempoKnown)
        lastKnownBpm = std::clamp (timing.bpm, 5.0, 1000.0);
    const bool sync = tempoKnown && timing.playing && std::isfinite (timing.ppq);
    bpm = lastKnownBpm;

    const double q = arp::rateQuarters (settings.rate);
    if (q != gridQuarters)
    {
        // The new rate's grid continues after the last boundary of the old one.
        const double last = static_cast<double> (gridIndex) * gridQuarters;
        gridIndex = static_cast<std::int64_t> (std::floor (last / q + 1.0e-9));
        gridQuarters = q;
    }
    synced = sync;
    if (sync)
    {
        ppqStart = timing.ppq;
        // Anything but the continuation of the previous block (a loop, a locate, the transport
        // starting) resynchronises to the grid from here: the next boundary plays, missed ones
        // are not caught up.
        const double tolerance = std::max (64.0, 0.01 * blockSize) / samplesPerQuarter();
        if (! hadSync || std::abs (ppqStart - expectedPpq) > tolerance)
            syncGridTo (0);
        expectedPpq = ppqStart + blockSize / samplesPerQuarter();
        hadSync = true;
    }
    else
    {
        hadSync = false;
    }

    if (wantEnabled != enabled)
    {
        enabled = wantEnabled;
        settings.enabled = enabled;
        if (enabled)
            handOverToArp();
        else
            handBackToKeys();
    }
}

void Arpeggiator::endBlock() noexcept
{
    advanceTo (blockSize);
    blockStart += blockSize;
    cursor = 0;
}

// ---------------------------------------------------------------------------------------------
// Hand-over between the keys and the arpeggiator

void Arpeggiator::handOverToArp() noexcept
{
    // The engine played the held keys directly (and kept others with the pedal): those end,
    // and the arpeggiator takes over everything held, the pedal included.
    if (pedal)
        emit (blockStart, Event::Kind::sustainOff, 0, 0, 1);
    for (int i = 0; i < held; ++i)
        if (heldNotes[static_cast<std::size_t> (i)].down)
            emit (blockStart, Event::Kind::noteOff, heldNotes[static_cast<std::size_t> (i)].note, 0, heldNotes[static_cast<std::size_t> (i)].channel);
    running = false;
    sequenceDirty = true;
    if (held > 0)
        startPhrase (0);
}

void Arpeggiator::handBackToKeys() noexcept
{
    // Every generated note ends; the keys still held sound again as played, and the pedal is
    // given back to the engine. (Notes only the pedal kept are not restarted.)
    endSounding (blockStart);
    running = false;
    if (pedal)
        emit (blockStart, Event::Kind::sustainOn, 0, 0, 1);
    for (int i = 0; i < held; ++i)
    {
        const auto& h = heldNotes[static_cast<std::size_t> (i)];
        if (h.down)
            emit (blockStart, Event::Kind::noteOn, h.note, h.velocity, h.channel);
    }
}

// ---------------------------------------------------------------------------------------------
// Input

void Arpeggiator::noteOn (int offset, int note, int velocity, int channel) noexcept
{
    if (velocity <= 0)
    {
        noteOff (offset, note, channel);
        return;
    }
    if (note < 0 || note > 127)
        return;
    advanceTo (offset);
    addHeld (note, std::min (velocity, 127), std::clamp (channel, 1, 16));
    heldChanged (offset);
}

void Arpeggiator::noteOff (int offset, int note, int channel) noexcept
{
    if (note < 0 || note > 127)
        return;
    advanceTo (offset);
    releaseHeld (note, std::clamp (channel, 1, 16));
    heldChanged (offset);
}

void Arpeggiator::sustainPedal (int offset, bool down) noexcept
{
    advanceTo (offset);
    pedal = down;
    if (! down)
        dropSustained();
    heldChanged (offset);
}

void Arpeggiator::allNotesOff (int offset) noexcept
{
    advanceTo (offset);
    held = 0;
    pedal = false;
    sequenceDirty = true;
    endSounding (blockStart + cursor);
    running = false;
}

void Arpeggiator::addHeld (int note, int velocity, int channel) noexcept
{
    for (int i = 0; i < held; ++i)
    {
        auto& h = heldNotes[static_cast<std::size_t> (i)];
        if (h.note == note && h.channel == channel)
        {
            h.down = true;   // pressed again (it was kept by the pedal): keeps its place
            h.velocity = static_cast<std::uint8_t> (velocity);
            return;
        }
    }
    if (held == maxHeld)
        return;
    heldNotes[static_cast<std::size_t> (held++)] = { static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (velocity),
                                                     static_cast<std::uint8_t> (channel), true };
}

void Arpeggiator::releaseHeld (int note, int channel) noexcept
{
    int found = -1;
    for (int i = 0; i < held && found < 0; ++i)
        if (heldNotes[static_cast<std::size_t> (i)].note == note && heldNotes[static_cast<std::size_t> (i)].channel == channel)
            found = i;
    for (int i = 0; i < held && found < 0; ++i)   // a note-off on another channel than its note-on
        if (heldNotes[static_cast<std::size_t> (i)].note == note && heldNotes[static_cast<std::size_t> (i)].down)
            found = i;
    if (found < 0)
        return;
    if (pedal)
    {
        heldNotes[static_cast<std::size_t> (found)].down = false;
        return;
    }
    std::move (heldNotes.begin() + found + 1, heldNotes.begin() + held, heldNotes.begin() + found);
    --held;
}

void Arpeggiator::dropSustained() noexcept
{
    int kept = 0;
    for (int i = 0; i < held; ++i)
        if (heldNotes[static_cast<std::size_t> (i)].down)
            heldNotes[static_cast<std::size_t> (kept++)] = heldNotes[static_cast<std::size_t> (i)];
    held = kept;
}

void Arpeggiator::heldChanged (int offset) noexcept
{
    sequenceDirty = true;
    if (! enabled)
        return;
    if (held > 0 && ! running)
        startPhrase (offset);
    else if (held == 0)
        running = false;   // the notes still sounding finish their gate
}

// ---------------------------------------------------------------------------------------------
// The pattern

void Arpeggiator::ensureSequence() const noexcept
{
    if (! sequenceDirty)
        return;
    sequenceDirty = false;

    // Ascending pitch; equal pitches (MPE) keep the order they were pressed in.
    sortedCount = held;
    for (int i = 0; i < held; ++i)
        sortedIndex[static_cast<std::size_t> (i)] = i;
    std::stable_sort (sortedIndex.begin(), sortedIndex.begin() + sortedCount,
                      [this] (int a, int b) { return heldNotes[static_cast<std::size_t> (a)].note < heldNotes[static_cast<std::size_t> (b)].note; });

    sequenceLength = 0;
    auto add = [this] (const Held& h, int octave) {
        const int n = h.note + 12 * octave;
        if (n <= 127 && sequenceLength < maxSequence)
            sequence[static_cast<std::size_t> (sequenceLength++)] = { static_cast<std::uint8_t> (n), h.velocity, h.channel };
    };
    const int octaves = settings.pattern == ArpPattern::chord ? 1 : settings.octaves;
    for (int octave = 0; octave < octaves; ++octave)
        for (int i = 0; i < sortedCount; ++i)
            add (settings.pattern == ArpPattern::played ? heldNotes[static_cast<std::size_t> (i)]
                                                        : heldNotes[static_cast<std::size_t> (sortedIndex[static_cast<std::size_t> (i)])],
                 octave);

    if (settings.pattern == ArpPattern::down)
        std::reverse (sequence.begin(), sequence.begin() + sequenceLength);
    else if (settings.pattern == ArpPattern::upDown && sequenceLength > 2)
    {
        // Up, then back down without repeating the top or the bottom: C E G E | C E G E ...
        const int up = sequenceLength;
        for (int i = up - 2; i >= 1 && sequenceLength < maxSequence; --i)
            sequence[static_cast<std::size_t> (sequenceLength++)] = sequence[static_cast<std::size_t> (i)];
    }
}

int Arpeggiator::pickRandom (std::int64_t step, int previous, int length) const noexcept
{
    if (length <= 1)
        return 0;
    Prng prng (Prng::deriveSeed (phraseSeed, static_cast<std::uint64_t> (step), static_cast<std::uint64_t> (length)));
    if (previous < 0 || previous >= length)
        return static_cast<int> (prng.nextBelow (static_cast<std::uint64_t> (length)));
    // Any note but the one just played, all equally likely.
    const int pick = static_cast<int> (prng.nextBelow (static_cast<std::uint64_t> (length - 1)));
    return pick >= previous ? pick + 1 : pick;
}

int Arpeggiator::stepNotes (std::int64_t step, int previous, std::array<Step, maxHeld>& out, int& index) const noexcept
{
    ensureSequence();
    index = -1;
    if (sequenceLength == 0)
        return 0;
    if (settings.pattern == ArpPattern::chord)
    {
        // The whole chord on every step, climbing an octave per step over OCTAVES (only the
        // octaves the chord's lowest note can reach).
        const int lowest = sequence[0].note;
        const int reachable = std::max (1, std::min (settings.octaves, (127 - lowest) / 12 + 1));
        const int octave = static_cast<int> (step % reachable);
        int count = 0;
        for (int i = 0; i < sequenceLength && count < maxHeld; ++i)
        {
            const int n = sequence[static_cast<std::size_t> (i)].note + 12 * octave;
            if (n <= 127)
                out[static_cast<std::size_t> (count++)] = { static_cast<std::uint8_t> (n), sequence[static_cast<std::size_t> (i)].velocity,
                                                            sequence[static_cast<std::size_t> (i)].channel };
        }
        index = octave;
        return count;
    }
    index = settings.pattern == ArpPattern::random ? pickRandom (step, previous, sequenceLength)
                                                   : static_cast<int> (step % sequenceLength);
    out[0] = sequence[static_cast<std::size_t> (index)];
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Time

double Arpeggiator::stepSamples() const noexcept
{
    return gridQuarters * samplesPerQuarter();
}

void Arpeggiator::syncGridTo (int offset) noexcept
{
    // The first boundary that lands at or after `offset` is the next to play. A boundary b
    // lands on sample ceil ((b - ppqStart) * spq - eps).
    const double spq = samplesPerQuarter();
    const double after = ppqStart + (offset - 1 + timeEpsilon) / spq;
    gridIndex = static_cast<std::int64_t> (std::floor (after / gridQuarters));
}

std::int64_t Arpeggiator::nextStepTime() const noexcept
{
    if (! running)
        return never;
    const std::int64_t now = blockStart + cursor;
    if (synced)
    {
        const double boundary = static_cast<double> (gridIndex + 1) * gridQuarters;
        const double offset = std::ceil ((boundary - ppqStart) * samplesPerQuarter() - timeEpsilon);
        if (offset > static_cast<double> (blockSize) + 1.0)
            return blockStart + blockSize + 1;   // not in this block
        return std::max (now, blockStart + static_cast<std::int64_t> (offset));
    }
    const double exact = stepCount == 0 ? phraseStartExact : lastStepExact + stepSamples();
    return std::max (now, static_cast<std::int64_t> (std::ceil (exact - timeEpsilon)));
}

void Arpeggiator::startPhrase (int offset) noexcept
{
    running = true;
    stepCount = 0;
    previousIndex = -1;
    phraseSeed = Prng::deriveSeed (baseSeed, phraseCounter++, 0x417270);
    for (auto& h : history)
        h = { -1, -1 };
    phraseStartExact = static_cast<double> (blockStart + offset);
    if (synced)
        syncGridTo (offset);   // the first step waits for the grid
}

void Arpeggiator::advanceTo (int offset) noexcept
{
    const int end = std::clamp (offset, cursor, blockSize);
    const std::int64_t endTime = blockStart + end;
    for (int guard = 0; guard < maxEvents; ++guard)
    {
        std::int64_t off = never;
        int offNote = -1;
        if (sounding > 0)
            for (int n = 0; n < 128; ++n)
                if (offTime[static_cast<std::size_t> (n)] < off)
                {
                    off = offTime[static_cast<std::size_t> (n)];
                    offNote = n;
                }
        const std::int64_t step = nextStepTime();
        if (std::min (off, step) >= endTime)
            break;
        if (off <= step)   // a note-off before a note-on at the same sample
        {
            const std::int64_t at = std::max (off, blockStart + cursor);
            if (! emit (at, Event::Kind::noteOff, offNote, 0, offChannel[static_cast<std::size_t> (offNote)]))
                break;
            offTime[static_cast<std::size_t> (offNote)] = never;
            --sounding;
            cursor = static_cast<int> (at - blockStart);
        }
        else
        {
            cursor = static_cast<int> (step - blockStart);
            fireStep (step);
        }
    }
    cursor = end;
}

void Arpeggiator::fireStep (std::int64_t time) noexcept
{
    // The clock first: whatever happens below, this step is done. A step that is late (the
    // host moved a little) plays now, once; older ones are not caught up.
    if (synced)
    {
        const double spq = samplesPerQuarter();
        const auto latest = static_cast<std::int64_t> (std::floor ((ppqStart + (cursor + timeEpsilon) / spq) / gridQuarters));
        gridIndex = std::max (gridIndex + 1, latest);
        lastStepExact = static_cast<double> (time);
    }
    else
    {
        double exact = stepCount == 0 ? phraseStartExact : lastStepExact + stepSamples();
        if (exact < static_cast<double> (time) - 1.0)
            exact = static_cast<double> (time);
        lastStepExact = exact;
    }

    std::array<Step, maxHeld> notes;
    int index = -1;
    const int count = stepNotes (stepCount, previousIndex, notes, index);
    const auto gateSamples = std::max<std::int64_t> (1, std::llround (settings.gate * stepSamples()));

    // A pitch still sounding (gate above 100 %) ends right before it plays again: the engine
    // ends every voice of a pitch on its note-off, so the old note-off must not reach the new one.
    for (int i = 0; i < count; ++i)
    {
        const auto n = static_cast<std::size_t> (notes[static_cast<std::size_t> (i)].note);
        if (offTime[n] != never && emit (time, Event::Kind::noteOff, static_cast<int> (n), 0, offChannel[n]))
        {
            offTime[n] = never;
            --sounding;
        }
    }
    int low = 128, high = -1;
    for (int i = 0; i < count; ++i)
    {
        const auto& s = notes[static_cast<std::size_t> (i)];
        if (offTime[s.note] == never && emit (time, Event::Kind::noteOn, s.note, std::max<int> (1, s.velocity), s.channel))
        {
            offTime[s.note] = time + gateSamples;
            offChannel[s.note] = s.channel;
            ++sounding;
        }
        low = std::min (low, static_cast<int> (s.note));
        high = std::max (high, static_cast<int> (s.note));
    }
    history[static_cast<std::size_t> (stepCount % displaySteps)] = count > 0 ? std::array<std::int8_t, 2> { static_cast<std::int8_t> (low), static_cast<std::int8_t> (high) }
                                                                              : std::array<std::int8_t, 2> { -1, -1 };
    previousIndex = index;
    ++stepCount;
}

// ---------------------------------------------------------------------------------------------
// Output

bool Arpeggiator::emit (std::int64_t time, Event::Kind kind, int note, int velocity, int channel) noexcept
{
    if (eventCount >= maxEvents)
        return false;
    Event e;
    e.offset = static_cast<int> (std::clamp<std::int64_t> (time - blockStart, 0, std::max (0, blockSize - 1)));
    e.kind = kind;
    e.note = static_cast<std::uint8_t> (std::clamp (note, 0, 127));
    e.velocity = static_cast<std::uint8_t> (std::clamp (velocity, 0, 127));
    e.channel = static_cast<std::uint8_t> (std::clamp (channel, 1, 16));
    events[static_cast<std::size_t> (eventCount++)] = e;
    return true;
}

void Arpeggiator::endSounding (std::int64_t time) noexcept
{
    for (int n = 0; n < 128 && sounding > 0; ++n)
        if (offTime[static_cast<std::size_t> (n)] != never)
        {
            if (! emit (time, Event::Kind::noteOff, n, 0, offChannel[static_cast<std::size_t> (n)]))
                return;   // full: the rest end at their gate
            offTime[static_cast<std::size_t> (n)] = never;
            --sounding;
        }
}

void Arpeggiator::display (Display& out) const noexcept
{
    out.low.fill (-1);
    out.high.fill (-1);
    out.current = -1;
    out.active = running;
    if (! running)
        return;
    const std::int64_t current = stepCount - 1;
    const std::int64_t first = current < 0 ? 0 : current / displaySteps * displaySteps;
    int previous = previousIndex;
    std::array<Step, maxHeld> notes;
    for (int j = 0; j < displaySteps; ++j)
    {
        const std::int64_t s = first + j;
        if (s < stepCount)
        {
            const auto& h = history[static_cast<std::size_t> (s % displaySteps)];
            out.low[static_cast<std::size_t> (j)] = h[0];
            out.high[static_cast<std::size_t> (j)] = h[1];
            continue;
        }
        int index = -1;
        const int count = stepNotes (s, previous, notes, index);
        previous = index;
        if (count == 0)
            continue;
        int low = 127, high = 0;
        for (int i = 0; i < count; ++i)
        {
            low = std::min (low, static_cast<int> (notes[static_cast<std::size_t> (i)].note));
            high = std::max (high, static_cast<int> (notes[static_cast<std::size_t> (i)].note));
        }
        out.low[static_cast<std::size_t> (j)] = static_cast<std::int8_t> (low);
        out.high[static_cast<std::size_t> (j)] = static_cast<std::int8_t> (high);
    }
    out.current = current < 0 ? -1 : static_cast<int> (current - first);
}

} // namespace osp
