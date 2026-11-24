// SUB (ANDOR ANALOG's sub oscillator, src/engine/sub; docs/sub/osp-sub.md): an independent
// polyphonic sub-bass instrument on the musician's own keys (the MIDI before the arpeggiator),
// joined after the effects and before the output gain. The oscillator's own cases are ANDOR
// ANALOG's (its tests/SubTests.cpp, SUB-01 / SUB-03); the engine cases are ANDOR OSP's.

#include "engine/InstrumentEngine.h"
#include "engine/sub/SubOsc.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace osp;
using osp::sub::SubOsc;
using osp::sub::SubSettings;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 64;

    void send (SubOsc& s, std::uint8_t a, std::uint8_t b, std::uint8_t c)
    {
        const std::uint8_t m[3] { a, b, c };
        s.handleMidi (m, 3);
    }

    void send (InstrumentEngine& e, std::uint8_t a, std::uint8_t b, std::uint8_t c)
    {
        const std::uint8_t m[3] { a, b, c };
        e.handleSubMidi (m, 3);
    }

    /** Positive-going zero crossings over [from, to) seconds: the fundamental (Hz). */
    double frequencyByCrossings (const std::vector<float>& x, double from, double to)
    {
        const auto a = static_cast<std::size_t> (from * rate), b = std::min (x.size(), static_cast<std::size_t> (to * rate));
        double first = -1.0, last = -1.0;
        int count = 0;
        for (std::size_t i = a + 1; i < b; ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f)
            {
                const double t = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = t;
                else
                    ++count;
                last = t;
            }
        return count > 0 ? rate * count / (last - first) : 0.0;
    }

    double rms (const std::vector<float>& x, double from, double to)
    {
        const auto a = static_cast<std::size_t> (from * rate), b = std::min (x.size(), static_cast<std::size_t> (to * rate));
        double sum = 0.0;
        for (std::size_t i = a; i < b; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return b > a ? std::sqrt (sum / static_cast<double> (b - a)) : 0.0;
    }

    float largestStep (const std::vector<float>& x)
    {
        float m = 0.0f;
        for (std::size_t i = 1; i < x.size(); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    EngineSettings quiet()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.shaping = Shaping::neutral();   // (no filter: SUB unfiltered)
        return s;
    }

    /** An engine with no recording loaded: only SUB can sound. */
    struct Rig
    {
        std::unique_ptr<InstrumentEngine> engine = std::make_unique<InstrumentEngine>();
        std::vector<float> l = std::vector<float> (block), r = std::vector<float> (block);
        std::vector<float> left, right;
        explicit Rig (const EngineSettings& s = quiet())
        {
            engine->prepare (rate, block, s);
        }
        void run (double seconds)
        {
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += block)
            {
                const int n = std::min (block, total - done);
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                float* ch[2] = { l.data(), r.data() };
                engine->render (ch, 2, n);
                left.insert (left.end(), l.begin(), l.begin() + n);
                right.insert (right.end(), r.begin(), r.begin() + n);
            }
        }
    };

    SubSettings on (double levelDb = -12.0)
    {
        SubSettings s;
        s.enabled = true;
        s.levelDb = levelDb;
        return s;
    }
} // namespace

TEST_CASE ("SUB-01: the keys released, SUB's voices end; the pedal holds them; ALL NOTES OFF and ALL SOUND OFF stop them", "[unit][sub]")
{
    SubOsc sub;
    sub.prepare (rate);
    sub.setSettings (on (-18.0));
    std::vector<float> l (4800), r (4800);
    send (sub, 0x90, 60, 100);
    send (sub, 0x90, 64, 100);
    sub.render (l.data(), r.data(), 4800);
    CHECK (sub.activeVoices() == 2);
    send (sub, 0xb0, 64, 127);   // pedal down
    send (sub, 0x80, 60, 0);
    sub.render (l.data(), r.data(), 4800);
    CHECK (sub.activeVoices() == 2);
    send (sub, 0xb0, 64, 0);   // pedal up: 60 fades
    CHECK (sub.activeVoices() == 1);
    for (int i = 0; i < 10; ++i)
        sub.render (l.data(), r.data(), 4800);
    CHECK (sub.voiceKey (0) != 60);
    send (sub, 0xb0, 123, 0);   // ALL NOTES OFF: a release
    CHECK (sub.activeVoices() == 0);
    for (int i = 0; i < 10; ++i)
        sub.render (l.data(), r.data(), 4800);
    CHECK (! sub.sounding());

    send (sub, 0x90, 50, 100);
    sub.render (l.data(), r.data(), 4800);
    send (sub, 0xb0, 120, 0);   // ALL SOUND OFF: 5 ms
    sub.render (l.data(), r.data(), 960);
    CHECK (sub.activeVoices() == 0);
    sub.render (l.data(), r.data(), 4800);
    CHECK (! sub.sounding());

    // A repeated key (two note ons, one off) is still held.
    send (sub, 0x90, 55, 100);
    send (sub, 0x90, 55, 100);
    send (sub, 0x80, 55, 0);
    CHECK (sub.activeVoices() == 1);
    send (sub, 0x80, 55, 0);
    CHECK (sub.activeVoices() == 0);
}

TEST_CASE ("SUB-03: FLAT and TAPER shape the level against the key", "[unit][sub]")
{
    SubOsc sub;
    sub.prepare (rate);
    auto set = on();
    set.taper = false;
    sub.setSettings (set);
    const float flat60 = sub.keyGain (60), flat84 = sub.keyGain (84);
    CHECK (std::abs (flat60 - flat84) < 1.0e-6f);
    CHECK (sub.keyGain (47) == 0.0f);   // (out of range)
    CHECK (sub.keyGain (85) == 0.0f);
    set.taper = true;
    sub.setSettings (set);
    // TAPER from C4 at 6 dB/oct: C4 itself and below unchanged, C6 12 dB down, C5 6 dB.
    CHECK (std::abs (sub.keyGain (60) - flat60) < 1.0e-6f);
    CHECK (std::abs (sub.keyGain (50) - flat60) < 1.0e-6f);
    CHECK (std::abs (20.0 * std::log10 (sub.keyGain (72) / flat60) + 6.0) < 1.0e-3);
    CHECK (std::abs (20.0 * std::log10 (sub.keyGain (84) / flat60) + 12.0) < 1.0e-3);
    for (int k = 61; k <= 84; ++k)
        CHECK (sub.keyGain (k) < sub.keyGain (k - 1));
}

TEST_CASE ("SUB: a note right after ALL SOUND OFF plays (only the voices sounding then are cut)", "[unit][sub]")
{
    SubOsc sub;
    sub.prepare (rate);
    sub.setSettings (on (-18.0));
    std::vector<float> l (4800), r (4800);
    send (sub, 0x90, 60, 100);
    sub.render (l.data(), r.data(), 4800);
    send (sub, 0xb0, 120, 0);
    send (sub, 0x90, 64, 100);   // (the same block: a host's panic, then a new note)
    std::fill (l.begin(), l.end(), 0.0f);
    sub.render (l.data(), r.data(), 4800);
    CHECK (sub.activeVoices() == 1);
    CHECK ((sub.voiceKey (0) == 64 || sub.voiceKey (1) == 64));
    float peak = 0.0f;
    for (std::size_t i = 2400; i < l.size(); ++i)
        peak = std::max (peak, std::abs (l[i]));
    CHECK (peak > 0.01f);
}

TEST_CASE ("SUB in the engine: off it renders nothing (bit-identical), its keys heard or not", "[unit][sub]")
{
    // SUB off (the default, and every session from before): the keys SUB hears change nothing.
    Rig a, b;
    send (*b.engine, 0x90, 48, 100);
    send (*b.engine, 0x90, 60, 100);
    a.run (0.3);
    b.run (0.3);
    REQUIRE (a.left.size() == b.left.size());
    CHECK (a.left == b.left);
    CHECK (a.right == b.right);
    for (const float v : b.left)
        CHECK (v == 0.0f);
    CHECK (! b.engine->subOscillator().sounding());
}

TEST_CASE ("SUB in the engine: C4 plays C2, centred, the same in both channels; it is not any effect's input", "[unit][sub]")
{
    auto s = quiet();
    s.macros.space = 1.0;   // SPACE and ECHO at full: SUB never reaches them
    s.macros.echo = 1.0;
    s.outputGainDb = 0.0;   // (SUB is before the output gain, as everything is)
    Rig rig (s);
    rig.engine->setSub (on());
    send (*rig.engine, 0x90, 60, 100);
    rig.run (0.8);
    CHECK (std::abs (frequencyByCrossings (rig.left, 0.2, 0.8) - 65.406) < 0.5);
    CHECK (rig.left == rig.right);
    CHECK (rms (rig.left, 0.2, 0.8) > 0.05);

    // The same notes into SubOsc alone: the engine adds exactly it (no room, no echo on it).
    SubOsc alone;
    alone.prepare (rate);
    alone.setSettings (on());
    send (alone, 0x90, 60, 100);
    std::vector<float> l (rig.left.size()), r (rig.left.size());
    for (std::size_t at = 0; at < l.size(); at += block)
        alone.render (l.data() + at, r.data() + at, static_cast<int> (std::min<std::size_t> (block, l.size() - at)));
    float d = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i)
        d = std::max (d, std::abs (l[i] - rig.left[i]));
    CHECK (d < 2.0e-6f);

    // Released: the tail is SUB's own release, never a reverb's.
    send (*rig.engine, 0x80, 60, 0);
    rig.left.clear();
    rig.run (1.0);
    CHECK (rms (rig.left, 0.5, 1.0) < 1.0e-5);
    CHECK (! rig.engine->subOscillator().sounding());
}

TEST_CASE ("SUB in the engine: turning it on under held keys starts them; off fades them without a click", "[unit][sub]")
{
    Rig rig;
    send (*rig.engine, 0x90, 50, 100);
    send (*rig.engine, 0x90, 57, 100);
    rig.run (0.2);
    CHECK (rig.engine->subOscillator().activeVoices() == 0);
    rig.engine->setSub (on());
    CHECK (rig.engine->subOscillator().activeVoices() == 2);
    rig.run (0.2);
    CHECK (rms (rig.left, 0.3, 0.4) > 0.01);
    rig.engine->setSub (SubSettings {});
    rig.left.clear();
    rig.run (1.0);
    CHECK (largestStep (rig.left) < 0.02f);
    CHECK (! rig.engine->subOscillator().sounding());
}

TEST_CASE ("SUB in the engine: the range and the pitch wheel", "[unit][sub]")
{
    Rig rig;
    auto set = on();
    rig.engine->setSub (set);
    send (*rig.engine, 0x90, 40, 100);   // (below C3: out of the default range)
    send (*rig.engine, 0x90, 60, 100);
    send (*rig.engine, 0x90, 90, 100);   // (above C6)
    CHECK (rig.engine->subOscillator().activeVoices() == 1);
    set.lowKey = 36;
    set.highKey = 96;
    rig.engine->setSub (set);
    CHECK (rig.engine->subOscillator().activeVoices() == 3);
    rig.run (0.3);
    CHECK (largestStep (rig.left) < 0.02f);

    // The on-screen wheel: a full bend up at 12 semitones moves C2 to C3.
    Rig bent;
    bent.engine->setSub (on());
    bent.engine->setSubBendRange (12.0);
    bent.engine->setSubBend (1.0f);
    send (*bent.engine, 0x90, 60, 100);
    bent.run (0.8);
    CHECK (std::abs (frequencyByCrossings (bent.left, 0.2, 0.8) - 130.81) < 1.0);
}

TEST_CASE ("SUB in the engine: it follows CHARACTER's low-pass; HP, BP and TILT leave it unfiltered", "[unit][sub]")
{
    auto measure = [] (FilterType type, double character) {
        auto s = quiet();
        s.shaping.filterType = type;
        s.shaping.filterMinHz = 40.0;
        s.shaping.filterMaxHz = 18000.0;
        s.shaping.envAmount = 0.0;
        s.macros.character = character;
        Rig rig (s);
        rig.engine->setSub (on());
        send (*rig.engine, 0x90, 60, 100);
        rig.run (0.6);
        return rms (rig.left, 0.3, 0.6);
    };
    const double open = measure (FilterType::lp24, 1.0);
    const double closed = measure (FilterType::lp24, 0.0);   // 40 Hz under a 65 Hz sub
    CHECK (closed < 0.5 * open);
    CHECK (std::abs (measure (FilterType::hp12, 0.0) - open) < 0.02 * open);
    CHECK (std::abs (measure (FilterType::tilt, 0.0) - open) < 0.02 * open);

    // The follow itself: CHARACTER's position on the travel, as a MIDI note.
    auto s = quiet();
    s.shaping.filterType = FilterType::lp12;
    s.shaping.filterMinHz = 110.0;
    s.shaping.filterMaxHz = 440.0;
    s.shaping.envAmount = 0.5;
    s.macros.character = 0.5;
    Rig rig (s);
    const auto f = rig.engine->subFollow();
    CHECK (f.on);
    CHECK (std::abs (f.baseNote - 57.0f) < 0.01f);   // 220 Hz: A3
    CHECK (f.envDepth > 0.0f);
    CHECK (f.sustain == 0.0);
}

TEST_CASE ("SUB in the engine: MOVEMENT's PULSE moves it with the rest; DRIFT does not", "[unit][sub]")
{
    auto swing = [] (MovementMode mode) {
        auto s = quiet();
        s.macros.motion = 1.0;
        s.shaping.movementMode = mode;
        s.shaping.pulseRate = 0.5;
        s.shaping.pulseShape = 0.0;
        Rig rig (s);
        rig.engine->setSub (on());
        send (*rig.engine, 0x90, 60, 100);
        rig.run (3.0);
        // The level over 50 ms windows: its lowest against its highest.
        double lo = 1.0e9, hi = 0.0;
        for (double t = 1.0; t + 0.05 <= 3.0; t += 0.05)
        {
            const double v = rms (rig.left, t, t + 0.05);
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        return lo / hi;
    };
    CHECK (swing (MovementMode::pulse) < 0.6);
    CHECK (swing (MovementMode::drift) > 0.9);
}

TEST_CASE ("SUB in the engine: its keys, settings and rendering never allocate", "[unit][sub]")
{
    Rig rig;
    rig.run (0.05);
    std::vector<float> l (block), r (block);
    float* ch[2] = { l.data(), r.data() };
    {
        test::AllocationScope scope;
        auto set = on();
        rig.engine->setSub (set);
        for (int k = 40; k < 70; k += 3)
            send (*rig.engine, 0x90, static_cast<std::uint8_t> (k), 100);
        for (int i = 0; i < 50; ++i)
        {
            rig.engine->render (ch, 2, block);
            set.lowKey = 30 + i % 20;
            set.grit = 0.01 * i;
            rig.engine->setSub (set);
        }
        send (*rig.engine, 0xb0, 123, 0);
        rig.engine->render (ch, 2, block);
        CHECK (scope.count() == 0);
    }
}

TEST_CASE ("SUB-04: MONO plays one note - the lowest key held (or the latest), legato as keys come and go; POLY as before", "[unit][sub]")
{
    // The key of the one voice sounding (-1: none).
    auto playing = [] (const SubOsc& s) {
        int key = -1, count = 0;
        for (int i = 0; i < SubOsc::maxVoices; ++i)
            if (s.voiceKey (i) >= 0)
            {
                key = s.voiceKey (i);
                ++count;
            }
        return count == 1 ? key : (count == 0 ? -1 : -100 - count);
    };
    std::vector<float> l (480), r (480);
    SECTION ("LOWEST (the default MONO note)")
    {
        SubOsc sub;
        sub.prepare (rate);
        auto s = on (-18.0);
        s.mono = true;
        sub.setSettings (s);
        send (sub, 0x90, 60, 100);
        send (sub, 0x90, 67, 100);
        send (sub, 0x90, 64, 100);
        CHECK (sub.activeVoices() == 1);
        CHECK (playing (sub) == 60);           // (not the latest: the lowest)
        send (sub, 0x90, 55, 100);
        CHECK (playing (sub) == 55);           // a lower key takes it, the same voice (legato)
        CHECK (sub.activeVoices() == 1);
        send (sub, 0x80, 55, 0);
        CHECK (playing (sub) == 60);           // back to the lowest still held
        send (sub, 0x80, 60, 0);
        CHECK (playing (sub) == 64);
        // The pedal: a released key it holds still counts.
        send (sub, 0xb0, 64, 127);
        send (sub, 0x90, 50, 100);
        send (sub, 0x80, 50, 0);
        CHECK (playing (sub) == 50);
        send (sub, 0xb0, 64, 0);
        CHECK (playing (sub) == 64);
        // A key outside the range never plays; none held: released.
        send (sub, 0x90, 20, 100);
        CHECK (playing (sub) == 64);
        send (sub, 0x80, 20, 0);
        send (sub, 0x80, 64, 0);
        send (sub, 0x80, 67, 0);
        CHECK (sub.activeVoices() == 0);
        for (int i = 0; i < 40; ++i)
            sub.render (l.data(), r.data(), 480);
        CHECK (! sub.sounding());
    }
    SECTION ("LATEST")
    {
        SubOsc sub;
        sub.prepare (rate);
        auto s = on (-18.0);
        s.mono = true;
        s.latest = true;
        sub.setSettings (s);
        send (sub, 0x90, 60, 100);
        send (sub, 0x90, 67, 100);
        send (sub, 0x90, 64, 100);
        CHECK (playing (sub) == 64);
        send (sub, 0x80, 64, 0);
        CHECK (playing (sub) == 67);           // the latest still held
        CHECK (sub.activeVoices() == 1);
    }
    SECTION ("switched while playing: POLY to MONO keeps one, MONO to POLY gives every key its own")
    {
        SubOsc sub;
        sub.prepare (rate);
        auto s = on (-18.0);
        sub.setSettings (s);
        send (sub, 0x90, 60, 100);
        send (sub, 0x90, 64, 100);
        send (sub, 0x90, 67, 100);
        CHECK (sub.activeVoices() == 3);
        s.mono = true;
        sub.setSettings (s);
        CHECK (sub.activeVoices() == 1);
        s.mono = false;
        sub.setSettings (s);
        CHECK (sub.activeVoices() == 3);
    }
    SECTION ("the same MIDI, the same output (deterministic)")
    {
        auto render = [&] {
            SubOsc sub;
            sub.prepare (rate);
            auto s = on (-12.0);
            s.mono = true;
            sub.setSettings (s);
            std::vector<float> out;
            const int keys[] { 60, 55, 67, 50 };
            for (const int k : keys)
            {
                send (sub, 0x90, static_cast<std::uint8_t> (k), 100);
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                sub.render (l.data(), r.data(), 480);
                out.insert (out.end(), l.begin(), l.end());
            }
            return out;
        };
        CHECK (render() == render());
    }
}
