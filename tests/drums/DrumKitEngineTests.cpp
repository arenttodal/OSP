// ANDOR Drums Milestone 1: independent pads on OSP's own engine, both keyboard modes, chokes,
// voice modes, mixing and determinism (no plugin, no GUI).
#include "audio/utility/TestSignals.h"
#include "drums/DrumKitEngine.h"
#include "engine/InstrumentBuilder.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace osp;
using namespace osp::drums;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    /** A struck, decaying tone (a stand-in drum with a measurable pitch). */
    std::shared_ptr<InstrumentModel> drumModel (double hz, double seconds = 1.0, double decayDb = 40.0)
    {
        auto audio = testsignals::sine (hz, seconds, rate, 0.5, 1);
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -decayDb / 20.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.002, 0.02);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    const std::shared_ptr<InstrumentModel>& kick() { static auto m = drumModel (110.0); return m; }
    const std::shared_ptr<InstrumentModel>& snare() { static auto m = drumModel (880.0); return m; }

    KitSettings quietKit()
    {
        KitSettings kit;
        kit.macros.life = 0.0;
        kit.macros.dynamics = 0.0;
        kit.shaping = Shaping::neutral();
        kit.volumeDb = 0.0;
        return kit;
    }

    struct Rig
    {
        DrumKitEngine engine;
        std::vector<float> l, r;
        int block;
        Rig (const KitSettings& kit, const std::array<PadSettings, maxPads>& pads, int blockSize = 256) : block (blockSize)
        {
            engine.prepare (rate, block, kit, pads);
            l.resize (static_cast<std::size_t> (block));
            r.resize (static_cast<std::size_t> (block));
        }
        explicit Rig (const KitSettings& kit = quietKit(), int blockSize = 256) : Rig (kit, std::array<PadSettings, maxPads> {}, blockSize) {}
        std::vector<float> run (double seconds, std::vector<float>* right = nullptr)
        {
            std::vector<float> out;
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += block)
            {
                const int n = std::min (block, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                out.insert (out.end(), l.begin(), l.begin() + n);
                if (right != nullptr)
                    right->insert (right->end(), r.begin(), r.begin() + n);
            }
            return out;
        }
    };

    double rmsOf (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
    {
        to = std::min (to, x.size());
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return to > from ? std::sqrt (s / static_cast<double> (to - from)) : 0.0;
    }

    /** Frequency from upward zero crossings over [from, to). */
    double frequencyOf (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        int first = -1, last = -1, crossings = 0;
        for (std::size_t i = from + 1; i < std::min (to, x.size()); ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f)
            {
                if (first < 0)
                    first = static_cast<int> (i);
                last = static_cast<int> (i);
                ++crossings;
            }
        return crossings > 1 ? rate * (crossings - 1) / static_cast<double> (last - first) : 0.0;
    }
} // namespace

TEST_CASE ("Milestone 1: kick and snare answer only their own notes", "[drums][engine]")
{
    Rig rig;
    rig.engine.setPadModel (0, kick().get());
    rig.engine.setPadModel (1, snare().get());

    rig.engine.noteOn (36, 100);
    CHECK (rig.engine.activeVoiceCount (0) == 1);
    CHECK (rig.engine.activeVoiceCount (1) == 0);
    rig.engine.noteOn (38, 100);
    CHECK (rig.engine.activeVoiceCount (0) == 1);
    CHECK (rig.engine.activeVoiceCount (1) == 1);
    rig.engine.noteOn (60, 100);   // unmapped: nothing
    rig.engine.noteOn (42, 100);   // mapped to an empty pad: nothing
    CHECK (rig.engine.activeVoiceCount() == 2);
    for (int p = 2; p < maxPads; ++p)
        CHECK (rig.engine.activeVoiceCount (p) == 0);
}

TEST_CASE ("Milestone 1: pads play at their natural pitch and together", "[drums][engine]")
{
    auto render = [] (std::initializer_list<int> notes) {
        Rig rig;
        rig.engine.setPadModel (0, kick().get());
        rig.engine.setPadModel (1, snare().get());
        for (int n : notes)
            rig.engine.noteOn (n, 100);
        return rig.run (0.4);
    };
    const auto k = render ({ 36 });
    const auto s = render ({ 38 });
    const auto both = render ({ 36, 38 });
    const std::size_t a = 2400, b = 12000;   // 50..250 ms
    CHECK (frequencyOf (k, a, b) == Approx (110.0).margin (1.0));
    CHECK (frequencyOf (s, a, b) == Approx (880.0).margin (4.0));
    REQUIRE (rmsOf (k, a, b) > 0.01);
    REQUIRE (rmsOf (s, a, b) > 0.01);
    // Both at once is exactly both alone, summed (independent pads, linear shared stage at rest).
    double worst = 0.0;
    for (std::size_t i = 0; i < both.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (both[i] - (k[i] + s[i]))));
    CHECK (worst < 1.0e-5);
}

TEST_CASE ("Milestone 1: each pad keeps its own settings", "[drums][engine]")
{
    // Plays one pad alone, with the kick's settings changed or not.
    auto play = [] (int note, double kickGainDb, double kickTune) {
        Rig rig;
        rig.engine.setPadModel (0, kick().get());
        rig.engine.setPadModel (1, snare().get());
        PadSettings ks;
        ks.gainDb = kickGainDb;
        ks.tuneSemitones = kickTune;
        rig.engine.setPadSettings (0, ks);
        rig.engine.noteOn (note, 100);
        return rig.run (0.3);
    };
    const auto kick0 = play (36, 0.0, 0.0);
    const auto kickQuiet = play (36, -12.0, 0.0);
    CHECK (rmsOf (kickQuiet, 2400) / rmsOf (kick0, 2400) == Approx (std::pow (10.0, -12.0 / 20.0)).epsilon (0.01));
    CHECK (frequencyOf (play (36, 0.0, 12.0), 2400, 12000) == Approx (220.0).margin (2.0));   // TUNE +12 on the kick
    // The snare does not move, sample for sample.
    CHECK (test::maxDifference (AudioData { rate, { play (38, 0.0, 0.0) } }, AudioData { rate, { play (38, -12.0, 12.0) } }) == 0.0);
}

TEST_CASE ("EXPANDED: one octave of kick, transposed by semitones from the root", "[drums][engine]")
{
    KitSettings kit = quietKit();
    kit.keyboardMode = KeyboardMode::expanded;
    REQUIRE (applyZonePreset (kit.keyMap, ZonePreset::oneOctave, { 0, 1 }, 36).applied);
    REQUIRE (resizeZone (kit.keyMap, 1, 49, 60).applied);
    REQUIRE (resizeZone (kit.keyMap, 0, 36, 48).applied);
    for (int note : { 36, 41, 48 })
    {
        Rig rig (kit);
        rig.engine.setPadModel (0, kick().get());
        rig.engine.setPadModel (1, snare().get());
        rig.engine.noteOn (note, 100);
        CHECK (rig.engine.activeVoiceCount (1) == 0);
        const auto out = rig.run (0.3);
        CHECK (frequencyOf (out, 2400, 12000) == Approx (110.0 * std::pow (2.0, (note - 36) / 12.0)).epsilon (0.01));
    }
    // A centred root: below it the kick goes down.
    REQUIRE (setZoneRoot (kit.keyMap, 0, 42).applied);
    Rig rig (kit);
    rig.engine.setPadModel (0, kick().get());
    rig.engine.noteOn (36, 100);
    CHECK (frequencyOf (rig.run (0.3), 2400, 12000) == Approx (110.0 * std::pow (2.0, -6.0 / 12.0)).epsilon (0.01));
}

TEST_CASE ("Switching PAD / EXPANDED keeps both maps; a held note still ends", "[drums][engine]")
{
    Rig rig;
    rig.engine.setPadModel (0, kick().get());
    PadSettings gate;
    gate.trigger = TriggerMode::gate;
    rig.engine.setPadSettings (0, gate);
    const auto map = rig.engine.keyMap();

    rig.engine.noteOn (36, 100);   // PAD: kick
    rig.run (0.05);
    rig.engine.setKeyboardMode (KeyboardMode::expanded);
    CHECK (rig.engine.keyMap() == map);
    rig.engine.noteOff (36);       // in EXPANDED 36 belongs to pad 2, but this note-off ends the kick
    rig.run (0.3);
    CHECK (rig.engine.activeVoiceCount (0) == 0);

    rig.engine.noteOn (12, 100);   // EXPANDED default: pad 0 owns 12..23
    CHECK (rig.engine.activeVoiceCount (0) == 1);
    rig.engine.setKeyboardMode (KeyboardMode::pad);
    rig.engine.noteOn (12, 100);   // PAD: nobody answers 12
    CHECK (rig.engine.activeVoiceCount (0) == 1);
}

TEST_CASE ("One-shot ignores note-off; gate releases", "[drums][engine]")
{
    for (auto mode : { TriggerMode::oneShot, TriggerMode::gate })
    {
        Rig rig;
        rig.engine.setPadModel (0, kick().get());
        PadSettings ps;
        ps.trigger = mode;
        ps.adsr.releaseSeconds = 0.02;
        rig.engine.setPadSettings (0, ps);
        rig.engine.noteOn (36, 100);
        rig.run (0.02);
        rig.engine.noteOff (36);
        rig.run (0.1);
        CHECK (rig.engine.activeVoiceCount (0) == (mode == TriggerMode::oneShot ? 1 : 0));
    }
}

TEST_CASE ("Choke groups: a closed hat cuts the open hat, quickly and without a click", "[drums][engine]")
{
    std::array<PadSettings, maxPads> pads {};
    pads[2] = PadSettings::defaultsFor (DrumCategory::closedHat);
    pads[3] = PadSettings::defaultsFor (DrumCategory::openHat);
    REQUIRE (pads[2].chokeGroup == pads[3].chokeGroup);
    REQUIRE (pads[2].chokeGroup > 0);
    Rig rig (quietKit(), pads);
    auto open = drumModel (660.0, 1.5, 10.0);
    rig.engine.setPadModel (2, snare().get());
    rig.engine.setPadModel (3, open.get());
    rig.engine.noteOn (46, 110);   // open hat
    const auto before = rig.run (0.2);
    rig.engine.noteOn (42, 110);   // closed hat chokes it
    const auto after = rig.run (0.05);
    CHECK (rig.engine.activeVoiceCount (3) == 0);
    CHECK (rig.engine.activeVoiceCount (2) == 1);
    // No step at the choke: the first samples continue smoothly from the open hat.
    CHECK (std::abs (after[0] - before.back()) < 0.05);

    // A pad outside the group is untouched by the group's chokes.
    rig.engine.setPadModel (0, kick().get());
    rig.engine.noteOn (36, 100);
    rig.engine.noteOn (46, 100);
    rig.run (0.05);
    CHECK (rig.engine.activeVoiceCount (0) == 1);
}

TEST_CASE ("Mono retrigger keeps one kick; poly lets hits overlap", "[drums][engine]")
{
    for (auto mode : { VoiceMode::monoRetrigger, VoiceMode::poly })
    {
        Rig rig;
        rig.engine.setPadModel (0, kick().get());
        PadSettings ps;
        ps.voiceMode = mode;
        rig.engine.setPadSettings (0, ps);
        for (int i = 0; i < 4; ++i)
        {
            rig.engine.noteOn (36, 100);
            rig.run (0.03);
        }
        CHECK (rig.engine.activeVoiceCount (0) == (mode == VoiceMode::monoRetrigger ? 1 : 4));
    }
}

TEST_CASE ("Mute and solo", "[drums][engine]")
{
    auto energy = [] (bool muteKick, bool soloSnare) {
        Rig rig;
        rig.engine.setPadModel (0, kick().get());
        rig.engine.setPadModel (1, snare().get());
        PadSettings k, s;
        k.mute = muteKick;
        s.solo = soloSnare;
        rig.engine.setPadSettings (0, k);
        rig.engine.setPadSettings (1, s);
        rig.engine.noteOn (36, 100);
        const auto kOut = rig.run (0.2);
        rig.engine.noteOn (38, 100);
        const auto sOut = rig.run (0.2);
        return std::pair { rmsOf (kOut, 960), rmsOf (sOut, 960) };
    };
    const auto normal = energy (false, false);
    CHECK (normal.first > 0.01);
    CHECK (energy (true, false).first < 1.0e-7);
    const auto solo = energy (false, true);
    CHECK (solo.first < 1.0e-7);
    CHECK (solo.second > 0.01);
}

TEST_CASE ("Nine pads at once; output independent of the host's block size", "[drums][engine]")
{
    std::vector<std::shared_ptr<InstrumentModel>> models;
    for (int p = 0; p < maxPads; ++p)
        models.push_back (drumModel (100.0 * (p + 1), 0.6));
    auto render = [&] (int block) {
        KitSettings kit;   // the default drum kit: LIFE on, so the seed matters
        kit.macros.space = 0.3;
        kit.macros.echo = 0.2;
        std::array<PadSettings, maxPads> pads {};
        const std::array<DrumCategory, maxPads> cats { DrumCategory::kick, DrumCategory::snare, DrumCategory::closedHat, DrumCategory::openHat,
                                                       DrumCategory::clap, DrumCategory::tom, DrumCategory::rim, DrumCategory::percussion, DrumCategory::cymbal };
        for (int p = 0; p < maxPads; ++p)
            pads[static_cast<std::size_t> (p)] = PadSettings::defaultsFor (cats[static_cast<std::size_t> (p)]);
        Rig rig (kit, pads, block);
        for (int p = 0; p < maxPads; ++p)
            rig.engine.setPadModel (p, models[static_cast<std::size_t> (p)].get());
        for (int n : KeyMap::defaults().padNote)
            rig.engine.noteOn (n, 90);
        auto out = rig.run (0.25);
        CHECK (rig.engine.activeVoiceCount() == maxPads);
        for (int n : KeyMap::defaults().padNote)
            rig.engine.noteOn (n, 60);
        auto more = rig.run (0.25);
        out.insert (out.end(), more.begin(), more.end());
        return out;
    };
    const auto a = render (64);
    const auto b = render (480);
    REQUIRE (a.size() == b.size());
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, static_cast<double> (std::abs (a[i] - b[i])));
    CHECK (worst < 1.0e-6);
    CHECK (rmsOf (a) > 0.01);
}

TEST_CASE ("A mouse audition plays the pad at its natural pitch in either mode", "[drums][engine]")
{
    KitSettings kit = quietKit();
    kit.keyboardMode = KeyboardMode::expanded;
    Rig rig (kit);
    rig.engine.setPadModel (4, kick().get());
    rig.engine.triggerPad (4, 100);
    CHECK (rig.engine.padHitsForDisplay (4) == 1u);
    CHECK (frequencyOf (rig.run (0.3), 2400, 12000) == Approx (110.0).margin (1.0));
}

TEST_CASE ("InstrumentEngine::fadeOutAll ends every voice within the fade", "[drums][engine]")
{
    InstrumentEngine engine;
    EngineSettings s;
    engine.prepare (rate, 256, s);
    engine.setModel (kick().get());
    engine.noteOn (60, 100);
    engine.noteOn (64, 100);
    std::vector<float> l (256), r (256);
    float* ch[2] = { l.data(), r.data() };
    engine.render (ch, 2, 256);
    REQUIRE (engine.activeVoiceCount() == 2);
    engine.fadeOutAll (0.004);
    engine.render (ch, 2, 256);
    CHECK (engine.activeVoiceCount() == 0);
}

// Not part of the regular run ("[.]"): `osp_drums_tests "[perf]"` prints the measurements
// recorded in docs/drums/architecture.md.
TEST_CASE ("Performance: callback time idle, nine pads and heavy retriggering", "[.][perf]")
{
    std::vector<std::shared_ptr<InstrumentModel>> models;
    for (int p = 0; p < maxPads; ++p)
        models.push_back (drumModel (90.0 * (p + 1), 1.5, 20.0));
    constexpr int block = 256;
    // `every`: blocks between hits; `hits`: pads struck together.
    auto measure = [&] (const char* label, int every, int hits, bool effects) {
        KitSettings kit;
        if (effects)
        {
            kit.macros.space = 0.4;
            kit.macros.echo = 0.3;
            kit.macros.drive = 0.4;
            kit.macros.motion = 0.4;
        }
        std::array<PadSettings, maxPads> pads {};
        const std::array<DrumCategory, maxPads> cats { DrumCategory::kick, DrumCategory::snare, DrumCategory::closedHat, DrumCategory::openHat,
                                                       DrumCategory::clap, DrumCategory::tom, DrumCategory::rim, DrumCategory::percussion, DrumCategory::cymbal };
        for (int p = 0; p < maxPads; ++p)
            pads[static_cast<std::size_t> (p)] = PadSettings::defaultsFor (cats[static_cast<std::size_t> (p)]);
        Rig rig (kit, pads, block);
        for (int p = 0; p < maxPads; ++p)
            rig.engine.setPadModel (p, models[static_cast<std::size_t> (p)].get());
        const auto notes = KeyMap::defaults().padNote;
        const int blocks = static_cast<int> (10.0 * rate / block);
        float* ch[2] = { rig.l.data(), rig.r.data() };
        double worst = 0.0, total = 0.0;
        int peakVoices = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int h = 0; every > 0 && b % every == 0 && h < hits; ++h)
                rig.engine.noteOn (notes[static_cast<std::size_t> ((b + h) % maxPads)], 40 + (b * 7 + h * 13) % 87);
            const auto t0 = std::chrono::steady_clock::now();
            rig.engine.render (ch, 2, block);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            worst = std::max (worst, us);
            total += us;
            peakVoices = std::max (peakVoices, rig.engine.activeVoiceCount());
        }
        const double budget = 1.0e6 * block / rate;
        std::printf ("%-44s mean %7.1f us  worst %7.1f us  (%5.2f%% of a %0.0f us block)  peak voices %d\n", label, total / blocks, worst,
                     100.0 * total / blocks / budget, budget, peakVoices);
    };
    measure ("idle kit (9 sounds, no notes)", 0, 0, false);
    measure ("idle kit, shared effects on", 0, 0, true);
    measure ("16ths at 120 BPM, 2 pads per step + fx", 23, 2, true);      // ~125 ms apart
    measure ("32nds at 120 BPM, 3 pads per step + fx", 12, 3, true);
    measure ("stress: a hit every block (5 ms) + fx", 1, 1, true);
    measure ("stress: 3 hits every block (5 ms) + fx", 1, 3, true);
    std::printf ("memory: %d voice slots x %zu bytes per pad engine = %.1f MB per pad, %.1f MB for 9 pads\n",
                 InstrumentEngine::voiceSlots(), sizeof (InstrumentVoice),
                 InstrumentEngine::voiceSlots() * sizeof (InstrumentVoice) / 1.0e6,
                 maxPads * InstrumentEngine::voiceSlots() * sizeof (InstrumentVoice) / 1.0e6);
}

TEST_CASE ("Sustain pedal holds gate pads across a mode switch; release ends them", "[drums][engine]")
{
    Rig rig;
    rig.engine.setPadModel (0, kick().get());
    rig.engine.setPadModel (1, snare().get());
    PadSettings gate;
    gate.trigger = TriggerMode::gate;
    gate.adsr.releaseSeconds = 0.02;
    rig.engine.setPadSettings (0, gate);
    rig.engine.setPadSettings (1, gate);

    rig.engine.setSustainPedal (true);
    rig.engine.noteOn (36, 100);   // PAD kick
    rig.engine.noteOn (38, 100);   // PAD snare
    rig.run (0.02);
    rig.engine.setKeyboardMode (KeyboardMode::expanded);
    rig.engine.noteOff (36);
    rig.engine.noteOff (38);
    rig.run (0.1);
    CHECK (rig.engine.activeVoiceCount (0) == 1);   // held by the pedal
    CHECK (rig.engine.activeVoiceCount (1) == 1);
    rig.engine.setSustainPedal (false);
    rig.run (0.1);
    CHECK (rig.engine.activeVoiceCount() == 0);   // no stuck notes

    rig.engine.noteOn (12, 100);   // EXPANDED: pad 0
    rig.engine.allNotesOff();
    rig.run (0.1);
    CHECK (rig.engine.activeVoiceCount() == 0);
}
