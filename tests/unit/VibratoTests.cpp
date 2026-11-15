// The MOD WHEEL's VIBRATO (Vibrato.h): the per-note law (DELAY, RISE, DEPTH, RATE, CONTROL), and
// the engine with it - off is exactly as before, it moves each note's pitch by DEPTH at RATE after
// DELAY, the wheel / ALWAYS ON / ASSIGNED decide how much, AMOUNT and DEPTH are per-voice
// modulation destinations, the layers it was given only, deterministic and allocation-free.

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/Vibrato.h"
#include "support/AllocationCounter.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>
#include <vector>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    /** A periodic signal's frequency between two times, from its rising zero crossings. */
    double hzBetween (const std::vector<float>& x, double fromSeconds, double toSeconds)
    {
        const auto a = static_cast<std::size_t> (fromSeconds * rate), b = std::min (x.size(), static_cast<std::size_t> (toSeconds * rate));
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (std::size_t i = a + 1; i < b; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double at = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = at;
                else
                    ++crossings;
                last = at;
            }
        return crossings > 0 ? rate * crossings / (last - first) : 0.0;
    }

    /** The lowest and highest local frequency (15 ms windows) between two times. */
    std::pair<double, double> hzRange (const std::vector<float>& x, double fromSeconds, double toSeconds)
    {
        double lo = 1.0e9, hi = 0.0;
        for (double t = fromSeconds; t + 0.015 <= toSeconds; t += 0.005)
        {
            const double hz = hzBetween (x, t, t + 0.015);
            lo = std::min (lo, hz);
            hi = std::max (hi, hz);
        }
        return { lo, hi };
    }

    EngineSettings plainSettings()
    {
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.motion = 0.0;
        s.macros.reimagined = 0.0;
        s.shaping = Shaping::neutral();
        s.adsr.releaseSeconds = 0.1;
        return s;
    }

    std::vector<float> play (const InstrumentModel& m, const std::function<void (InstrumentEngine&)>& setUp, int velocity = 100, int block = 256,
                             double seconds = 1.6)
    {
        InstrumentEngine e;
        e.prepare (rate, block, plainSettings());
        e.setModel (&m);
        if (setUp)
            setUp (e);
        std::vector<float> l (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block)), out;
        e.noteOn (69, velocity);
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }

    vibrato::Settings on (vibrato::Control control = vibrato::Control::alwaysOn)
    {
        vibrato::Settings v;
        v.enabled = true;
        v.depthCents = 50.0;
        v.rateHz = 5.0;
        v.delaySeconds = 0.25;
        v.riseSeconds = 0.25;
        v.control = control;
        return v;
    }
}

TEST_CASE ("vibrato: one note's law - DELAY, RISE, DEPTH, RATE and CONTROL", "[unit][vibrato]")
{
    auto s = on();
    vibrato::Voice v;
    v.start();
    const double step = 32.0 / rate;
    double peakBefore = 0.0, peakAfter = 0.0;
    int signChanges = 0;
    double previous = 0.0;
    for (double t = 0.0; t < 2.0; t += step)
    {
        const double cents = v.advance (s, step, 0.0);
        if (t < 0.25)
            peakBefore = std::max (peakBefore, std::abs (cents));
        if (t > 0.6)
        {
            peakAfter = std::max (peakAfter, std::abs (cents));
            if ((cents > 0.0) != (previous > 0.0))
                ++signChanges;
        }
        previous = cents;
    }
    CHECK (peakBefore == 0.0);                          // DELAY: nothing at all
    CHECK (peakAfter == Approx (50.0).margin (0.2));    // DEPTH, either side (ALWAYS ON: all of it)
    CHECK (signChanges == Approx (2.0 * 5.0 * 1.4).margin (2.0));   // RATE: 5 Hz over 1.4 s

    // RISE: halfway through it (raised cosine), half the depth.
    CHECK (vibrato::onsetAt (s, 0.25 + 0.125) == Approx (0.5));
    CHECK (vibrato::onsetAt (s, 0.25) == 0.0);
    CHECK (vibrato::onsetAt (s, 0.5) == 1.0);

    // CONTROL: the wheel's share, all, or AMOUNT (0 by default: silent).
    CHECK (vibrato::scaleFor (on (vibrato::Control::modWheel), 0.25) == Approx (0.25));
    CHECK (vibrato::scaleFor (on (vibrato::Control::alwaysOn), 0.0) == 1.0);
    CHECK (vibrato::scaleFor (on (vibrato::Control::assigned), 1.0) == 0.0);
    CHECK (vibrato::scaleFor (on (vibrato::Control::assigned), 1.0, 0.6) == Approx (0.6));   // a route on AMOUNT
    // Off: nothing, whatever the rest says.
    s.enabled = false;
    v.start();
    for (int i = 0; i < 2000; ++i)
        CHECK (v.advance (s, step, 1.0) == 0.0);
}

TEST_CASE ("vibrato: in the engine - off is exactly as before; on moves each note's pitch", "[unit][vibrato]")
{
    auto audio = testsignals::sine (midiToHz (69), 3.0, rate, 0.4, 1);
    testsignals::applyFades (audio, 0.01, 0.05);
    const auto m = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    const auto plain = play (*m, {});
    // Default settings (off), and every CONTROL with nothing to play, are sample for sample the old sound.
    CHECK (play (*m, [] (InstrumentEngine& e) { e.setVibrato (vibrato::Settings {}); }) == plain);
    CHECK (play (*m, [] (InstrumentEngine& e) { e.setVibrato (on (vibrato::Control::modWheel)); }) == plain);   // the wheel down
    CHECK (play (*m, [] (InstrumentEngine& e) { e.setVibrato (on (vibrato::Control::assigned)); }) == plain);   // AMOUNT 0
    auto layerOff = on();
    layerOff.sources[0] = false;   // (layer A is not a source)
    CHECK (play (*m, [&layerOff] (InstrumentEngine& e) { e.setVibrato (layerOff); }) == plain);

    // ALWAYS ON: steady before DELAY; after RISE the pitch swings +-50 cents around A4.
    const auto moving = play (*m, [] (InstrumentEngine& e) { e.setVibrato (on()); });
    const auto [steadyLo, steadyHi] = hzRange (moving, 0.03, 0.24);
    CHECK (steadyHi / steadyLo < 1.002);
    const auto [lo, hi] = hzRange (moving, 0.6, 1.5);
    INFO ("swing " << lo << " .. " << hi << " Hz");
    CHECK (hi / lo == Approx (std::exp2 (100.0 / 1200.0)).epsilon (0.012));   // +-50 cents
    CHECK (std::sqrt (lo * hi) == Approx (440.0).epsilon (0.004));            // centred on the note
    CHECK (play (*m, [] (InstrumentEngine& e) { e.setVibrato (on()); }) == moving);   // deterministic

    // MOD WHEEL: the wheel up plays it; half up, about half the swing.
    const auto wheelFull = play (*m, [] (InstrumentEngine& e) {
        e.setVibrato (on (vibrato::Control::modWheel));
        e.setModWheel (1.0f);
    });
    CHECK (wheelFull != plain);
    const auto wheelHalf = play (*m, [] (InstrumentEngine& e) {
        e.setVibrato (on (vibrato::Control::modWheel));
        e.setModWheel (0.5f);
    });
    const auto [hlo, hhi] = hzRange (wheelHalf, 0.6, 1.5);
    CHECK (std::log2 (hhi / hlo) * 1200.0 == Approx (50.0).margin (6.0));   // +-25 cents

    // ASSIGNED: AMOUNT itself, or a route to VIBRATO AMOUNT (per voice: VEL here), plays it.
    auto amount = on (vibrato::Control::assigned);
    amount.amount = 1.0;
    CHECK (play (*m, [&amount] (InstrumentEngine& e) { e.setVibrato (amount); }) == moving);
    mod::Settings velocity;
    velocity.routes[0] = { mod::Source::vel, mod::Dest::vibratoAmount, 1.0, true };
    const auto routed = play (*m, [&velocity] (InstrumentEngine& e) {
        e.setVibrato (on (vibrato::Control::assigned));
        e.setModulation (velocity);
    }, 127);
    const auto [rlo, rhi] = hzRange (routed, 0.6, 1.5);
    CHECK (std::log2 (rhi / rlo) * 1200.0 > 70.0);
    // A route to VIBRATO DEPTH moves the swing too (ALWAYS ON, DEPTH 0 plus the route).
    auto noDepth = on();
    noDepth.depthCents = 0.0;
    CHECK (play (*m, [&noDepth] (InstrumentEngine& e) { e.setVibrato (noDepth); }) == plain);
    mod::Settings depth;
    depth.routes[0] = { mod::Source::vel, mod::Dest::vibratoDepth, 0.5, true };   // up to 50 cents
    const auto deep = play (*m, [&noDepth, &depth] (InstrumentEngine& e) {
        e.setVibrato (noDepth);
        e.setModulation (depth);
    }, 127);
    const auto [dlo, dhi] = hzRange (deep, 0.6, 1.5);
    CHECK (std::log2 (dhi / dlo) * 1200.0 > 60.0);

    // The destinations: per voice, appended to the extended route list (the frozen list unchanged).
    CHECK (mod::destInfo (mod::Dest::vibratoDepth).owner == mod::Owner::voice);
    CHECK (mod::choiceOf (mod::Dest::vibratoAmount) < 0);
    CHECK (mod::extendedChoiceOf (mod::Dest::vibratoDepth) == 9);
    CHECK (mod::extendedChoiceOf (mod::Dest::vibratoAmount) == 10);
}

TEST_CASE ("vibrato: the same at any block size, and no allocation while it plays", "[unit][vibrato]")
{
    auto audio = testsignals::sine (midiToHz (69), 2.0, rate, 0.4, 1);
    testsignals::applyFades (audio, 0.01, 0.05);
    const auto m = instrument::buildComplete (audio, test::analyse (audio), {}, false);
    auto setUp = [] (InstrumentEngine& e) { e.setVibrato (on()); };
    const auto a = play (*m, setUp, 100, 64, 1.0);
    const auto b = play (*m, setUp, 100, 512, 1.0);
    REQUIRE (a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (a[i] - b[i])));
    CHECK (worst < 1.0e-5);

    InstrumentEngine e;
    e.prepare (rate, 256, plainSettings());
    e.setModel (m.get());
    e.setVibrato (on());
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    e.noteOn (60, 100);
    e.noteOn (64, 100);
    e.render (ch, 2, 256);
    {
        test::AllocationScope scope;
        for (int i = 0; i < 100; ++i)
            e.render (ch, 2, 256);
        CHECK (scope.count() == 0);
    }
}

TEST_CASE ("vibrato: listening demos (OSP_VIBRATO_DEMO_DIR)", "[unit][vibrato][demo]")
{
    // Renders ANDOR OSP's demos only when asked (the owner's listening test): a bowed-like
    // sustained take (a held vowel) with no vibrato, ALWAYS ON, and the wheel brought in; the
    // same take GRANULAR with vibrato; a two-note chord, each note's vibrato from its own start.
    const char* dir = std::getenv ("OSP_VIBRATO_DEMO_DIR");
    if (dir == nullptr)
        return;
    auto writeWav = [dir] (const char* name, const std::vector<float>& x) {
        std::FILE* f = std::fopen ((std::string (dir) + "/" + name).c_str(), "wb");
        REQUIRE (f != nullptr);
        auto u32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
        const auto bytes = static_cast<std::uint32_t> (x.size() * 2);
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
        u32 (16); u16 (1); u16 (1); u32 (48000); u32 (96000); u16 (2); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (bytes);
        for (const float s : x)
            u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (std::clamp (s * 1.4f, -1.0f, 1.0f) * 32767.0f)));
        std::fclose (f);
    };
    auto take = testsignals::vowel (midiToHz (57), 6.0, rate, 7, 0.5, 1);
    testsignals::applyFades (take, 0.15, 0.3);
    const auto m = instrument::buildComplete (take, test::analyse (take), {}, false);
    auto demo = [&] (const char* name, const std::function<void (InstrumentEngine&)>& setUp, std::vector<int> notes, double seconds = 4.0) {
        InstrumentEngine e;
        e.prepare (rate, 256, plainSettings());
        e.setModel (m.get());
        setUp (e);
        std::vector<float> l (256), r (256), out;
        const auto total = static_cast<int> (seconds * rate);
        for (int done = 0, i = 0; done < total; done += 256, ++i)
        {
            for (std::size_t k = 0; k < notes.size(); ++k)
                if (done == static_cast<int> (k) * 24576)   // (each note 0.5 s after the last)
                    e.noteOn (notes[k], 100);
            if (done == total - static_cast<int> (0.6 * rate) / 256 * 256)
                for (const int n : notes)
                    e.noteOff (n);
            float* ch[2] = { l.data(), r.data() };
            e.render (ch, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        writeWav (name, out);
    };
    auto settings = on();
    settings.depthCents = 25.0;
    settings.rateHz = 5.5;
    demo ("osp-sustained-no-vibrato.wav", [] (InstrumentEngine&) {}, { 57 });
    demo ("osp-sustained-vibrato-always-on.wav", [&] (InstrumentEngine& e) { e.setVibrato (settings); }, { 57 });
    demo ("osp-sustained-vibrato-wheel.wav", [&] (InstrumentEngine& e) {
        auto w = settings;
        w.control = vibrato::Control::modWheel;
        e.setVibrato (w);
        e.setModWheel (0.8f);
    }, { 57 });
    demo ("osp-granular-vibrato.wav", [&] (InstrumentEngine& e) {
        e.setSourceMode (0, SourceMode::granular);
        e.setVibrato (settings);
    }, { 57 });
    demo ("osp-chord-each-note-its-own-vibrato.wav", [&] (InstrumentEngine& e) { e.setVibrato (settings); }, { 57, 64, 69 }, 5.0);
}
