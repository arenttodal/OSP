// ANDOR Drums M9: MACHINE, the older-drum-machine character built only from OSP's existing
// processing (spec Part 9). Off is exactly off; on, it narrows the bandwidth and adds
// variation without detuning, stays deterministic and keeps END.
#include "audio/utility/TestSignals.h"
#include "drums/DrumKitEngine.h"
#include "engine/InstrumentBuilder.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace osp;
using namespace osp::drums;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    std::shared_ptr<InstrumentModel> decaying (AudioData audio)
    {
        auto& x = audio.channels[0];
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] *= static_cast<float> (std::pow (10.0, -30.0 / 20.0 * static_cast<double> (i) / static_cast<double> (x.size())));
        testsignals::applyFades (audio, 0.002, 0.02);
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }
    const std::shared_ptr<InstrumentModel>& tom() { static auto m = decaying (testsignals::sine (110.0, 1.0, rate, 0.5, 1)); return m; }
    const std::shared_ptr<InstrumentModel>& bright() { static auto m = decaying (testsignals::saw (220.0, 1.0, rate, 0.4, 1)); return m; }

    KitSettings quietKit (double machine = 0.0)
    {
        KitSettings kit;
        kit.macros.life = 0.0;
        kit.macros.dynamics = 0.0;
        kit.shaping = Shaping::neutral();
        kit.volumeDb = 0.0;
        kit.machine = machine;
        return kit;
    }

    struct Rig
    {
        DrumKitEngine engine;
        std::vector<float> l, r;
        explicit Rig (double machine = 0.0)
        {
            engine.prepare (rate, 256, quietKit (machine), {});
            l.resize (256);
            r.resize (256);
        }
        std::vector<float> run (double seconds)
        {
            std::vector<float> out;
            const auto total = static_cast<int> (seconds * rate);
            for (int done = 0; done < total; done += 256)
            {
                const int n = std::min (256, total - done);
                float* ch[2] = { l.data(), r.data() };
                engine.render (ch, 2, n);
                out.insert (out.end(), l.begin(), l.begin() + n);
            }
            return out;
        }
        std::vector<float> hits (const InstrumentModel* model, int count, double spacing = 0.25)
        {
            engine.setPadModel (0, model);
            std::vector<float> out;
            for (int h = 0; h < count; ++h)
            {
                engine.noteOn (36, 100);
                auto part = run (spacing);
                out.insert (out.end(), part.begin(), part.end());
            }
            return out;
        }
    };

    double rmsOf (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        to = std::min (to, x.size());
        double s = 0.0;
        for (std::size_t i = from; i < to; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return to > from ? std::sqrt (s / static_cast<double> (to - from)) : 0.0;
    }

    /** The share of high frequencies: rms of the first difference over rms (rises with brightness). */
    double brightness (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double d = 0.0, s = 0.0;
        for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, x.size()); ++i)
        {
            const double diff = static_cast<double> (x[i]) - x[i - 1];
            d += diff * diff;
            s += static_cast<double> (x[i]) * x[i];
        }
        return s > 0.0 ? std::sqrt (d / s) : 0.0;
    }

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

TEST_CASE ("MACHINE at 0 leaves the kit bit-identical", "[drums][machine]")
{
    Rig plain, zero (0.0);
    zero.engine.setMachine (0.0);
    CHECK (zero.engine.effectiveMacros().drive == plain.engine.effectiveMacros().drive);
    CHECK (plain.hits (bright().get(), 4) == zero.hits (bright().get(), 4));

    // Up and back to 0 between notes: the same settings as never having touched it.
    Rig there (0.0);
    there.engine.setMachine (0.8);
    there.engine.setMachine (0.0);
    CHECK (there.engine.effectiveShaping().filterType == plain.engine.effectiveShaping().filterType);
    CHECK (there.engine.effectiveShaping().filterMaxHz == plain.engine.effectiveShaping().filterMaxHz);
}

TEST_CASE ("MACHINE narrows the bandwidth, more as it rises", "[drums][machine]")
{
    const auto from = static_cast<std::size_t> (0.01 * rate), to = static_cast<std::size_t> (0.2 * rate);
    Rig off, subtle (0.35), strong (1.0);
    const auto a = off.hits (bright().get(), 1), b = subtle.hits (bright().get(), 1), c = strong.hits (bright().get(), 1);
    const double ba = brightness (a, from, to), bb = brightness (b, from, to), bc = brightness (c, from, to);
    INFO ("brightness off " << ba << ", subtle " << bb << ", strong " << bc);
    CHECK (bb < ba);
    CHECK (bc < bb);
    CHECK (bc < 0.8 * ba);
    // Still the same drum: the level stays within a few dB.
    CHECK (rmsOf (c, from, to) / rmsOf (a, from, to) == Approx (1.0).margin (0.5));
}

TEST_CASE ("MACHINE keeps the pitch: tuned percussion stays in tune", "[drums][machine]")
{
    Rig off, strong (1.0);
    const auto a = off.hits (tom().get(), 6), b = strong.hits (tom().get(), 6);
    const auto hitLength = static_cast<std::size_t> (0.25 * rate);
    for (std::size_t h = 0; h < 6; ++h)
    {
        const auto from = h * hitLength + static_cast<std::size_t> (0.02 * rate), to = (h + 1) * hitLength - 100;
        const double cents = 1200.0 * std::log2 (frequencyOf (b, from, to) / frequencyOf (a, from, to));
        INFO ("hit " << h << ": " << cents << " cents");
        CHECK (std::abs (cents) < 12.0);
    }
}

TEST_CASE ("MACHINE varies repeated hits and is deterministic", "[drums][machine]")
{
    Rig one (0.6), two (0.6);
    const auto a = one.hits (bright().get(), 4), b = two.hits (bright().get(), 4);
    CHECK (a == b);

    // Two hits are not copies of each other (LIFE's per-hit variation, never below 25 %).
    const auto hitLength = static_cast<std::size_t> (0.25 * rate);
    double diff = 0.0;
    for (std::size_t i = 0; i < hitLength; ++i)
        diff = std::max (diff, std::abs (static_cast<double> (a[i]) - a[hitLength + i]));
    CHECK (diff > 1.0e-4);
}

TEST_CASE ("MACHINE keeps END: a sliced layer still stops where it ends", "[drums][machine][layers]")
{
    Rig cut (1.0);
    LayerPlan p;
    p.count = 1;
    p.layer[0].end = 0.2;
    std::array<const InstrumentModel*, maxLayers> models {};
    models[0] = tom().get();
    cut.engine.setPadLayers (0, models, p);
    cut.engine.noteOn (36, 100);
    const auto c = cut.run (0.6);
    CHECK (rmsOf (c, 2400, 7200) > 0.01);
    // Shared effects (tape, saturation) can leave a short tail after the voice stops.
    CHECK (rmsOf (c, static_cast<std::size_t> (0.3 * rate), c.size()) < 1.0e-3);
    CHECK (cut.engine.activeVoiceCount (0) == 0);
}
