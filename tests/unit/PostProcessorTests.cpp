#include "audio/utility/TestSignals.h"
#include "core/Fft.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/PostProcessor.h"
#include "research/Experiment.h"
#include "support/TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

using namespace osp;

namespace
{
    std::shared_ptr<InstrumentModel> modelFor (const AudioData& audio)
    {
        return instrument::buildComplete (audio, test::analyse (audio), {}, false);
    }

    AudioData process (PostProcessor& post, const AudioData& in)
    {
        AudioData out = in;
        if (out.numChannels() == 1)
            out.channels.push_back (out.channels[0]);
        const int n = static_cast<int> (out.numFrames());
        for (int pos = 0; pos < n; pos += 256)
            post.process (out.channels[0].data() + pos, out.channels[1].data() + pos, std::min (256, n - pos));
        return out;
    }

    double centroid (const std::vector<float>& x, std::size_t from)
    {
        const Fft fft (13);
        const int n = fft.size();
        std::vector<std::complex<double>> b (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
            b[static_cast<std::size_t> (i)] = { x[from + static_cast<std::size_t> (i)] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n)), 0.0 };
        fft.forward (b.data());
        double num = 0, den = 0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double p = std::norm (b[static_cast<std::size_t> (k)]);
            num += p * k;
            den += p;
        }
        return num / den;
    }

    double energy (const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double e = 0;
        for (std::size_t i = from; i < to && i < x.size(); ++i)
            e += static_cast<double> (x[i]) * x[i];
        return e;
    }
}

TEST_CASE ("post: neutral macros pass audio through untouched", "[unit][post]")
{
    const auto audio = testsignals::vowel (220.0, 1.0, 48000.0, 1);
    const auto model = modelFor (audio);
    PostProcessor post;
    post.prepare (48000.0, 256);
    post.setModel (model.get());
    Macros neutral;
    neutral.character = 0.5;
    neutral.space = 0.0;
    neutral.reimagined = 0.0;
    post.setMacros (neutral);
    post.reset();
    const auto out = process (post, audio);
    CHECK (test::maxDifference (out, audio) == 0.0);
}

TEST_CASE ("post: CHARACTER tilts the spectrum both ways", "[unit][post]")
{
    auto audio = testsignals::saw (220.0, 1.0, 48000.0, 0.3, 2);
    const auto model = modelFor (audio);
    auto run = [&] (double character) {
        PostProcessor post;
        post.prepare (48000.0, 256);
        post.setModel (model.get());
        Macros m;
        m.character = character;
        m.space = 0.0;
        m.reimagined = 0.0;
        post.setMacros (m);
        post.reset();
        return centroid (process (post, audio).channels[0], 24000);
    };
    const double dark = run (0.0), neutral = run (0.5), bright = run (1.0);
    CHECK (dark < neutral);
    CHECK (neutral < bright);
    CHECK (12.0 * std::log2 (bright / dark) > 2.0);
}

TEST_CASE ("post: SPACE widens a mono source and stays bounded", "[unit][post]")
{
    const auto audio = testsignals::vowel (220.0, 2.0, 48000.0, 1, 0.5, 1);
    const auto model = modelFor (audio);
    PostProcessor post;
    post.prepare (48000.0, 256);
    post.setModel (model.get());
    Macros m;
    m.space = 1.0;
    m.reimagined = 0.0;
    post.setMacros (m);
    post.reset();
    const auto out = process (post, audio);
    double side = 0, mid = 0;
    std::size_t nonFinite = 0;
    for (std::size_t i = 0; i < out.channels[0].size(); ++i)
    {
        const double l = out.channels[0][i], r = out.channels[1][i];
        side += (l - r) * (l - r);
        mid += (l + r) * (l + r);
        nonFinite += std::isfinite (l) ? 0 : 1;
    }
    CHECK (nonFinite == 0);
    CHECK (side / mid > 0.01);
    const double inE = energy (audio.channels[0], 0, audio.channels[0].size());
    const double outE = 0.5 * (energy (out.channels[0], 0, out.channels[0].size()) + energy (out.channels[1], 0, out.channels[1].size()));
    CHECK (10.0 * std::log10 (outE / inE) < 6.0);
}

TEST_CASE ("post: Reimagined resonators ring on after the sound stops", "[unit][post]")
{
    auto audio = testsignals::vowel (220.0, 1.0, 48000.0, 1);
    const auto model = modelFor (audio);
    REQUIRE_FALSE (model->resonanceHz.empty());
    AudioData input = audio;
    for (auto& ch : input.channels)
        ch.resize (ch.size() + 48000, 0.0f); // one second of silence after the vowel
    auto tail = [&] (double reimagined) {
        PostProcessor post;
        post.prepare (48000.0, 256);
        post.setModel (model.get());
        Macros m;
        m.space = 0.0;
        m.reimagined = reimagined;
        post.setMacros (m);
        post.reset();
        const auto out = process (post, input);
        return energy (out.channels[0], 48000 + 2400, 48000 + 24000);
    };
    CHECK (tail (0.0) < 1.0e-12);
    CHECK (tail (1.0) > 1.0e-6);
}

TEST_CASE ("post: moving a macro does not click", "[unit][post]")
{
    const auto audio = testsignals::vowel (220.0, 3.0, 48000.0, 1);
    const auto model = modelFor (audio);
    PostProcessor post;
    post.prepare (48000.0, 256);
    post.setModel (model.get());
    Macros m;
    post.setMacros (m);
    post.reset();
    AudioData out = audio;
    const int n = static_cast<int> (out.numFrames());
    for (int pos = 0; pos < n; pos += 256)
    {
        if (pos == 48000)
        {
            m.character = 1.0;
            m.space = 1.0;
            m.reimagined = 1.0;
            post.setMacros (m);
        }
        post.process (out.channels[0].data() + pos, out.channels[1].data() + pos, std::min (256, n - pos));
    }
    CHECK (research::discontinuityDb (out, 0.3, 2.7) < 18.0);
}

TEST_CASE ("engine: far Reimagined adds harmonics to a pure tone", "[integration][post]")
{
    auto audio = testsignals::sine (220.0, 2.0, 48000.0, 0.5, 2);
    testsignals::applyFades (audio, 0.02, 0.2);
    const auto model = modelFor (audio);
    auto render = [&] (double r) {
        InstrumentEngine engine;
        EngineSettings s;
        s.macros.life = 0.0;
        s.macros.space = 0.0;
        s.macros.reimagined = r;
        engine.prepare (48000.0, 256, s);
        engine.setModel (model.get());
        engine.noteOn (57, 127);
        std::vector<float> l (48000), rr (48000);
        for (int pos = 0; pos < 48000; pos += 256)
        {
            float* ch[2] = { l.data() + pos, rr.data() + pos };
            engine.render (ch, 2, std::min (256, 48000 - pos));
        }
        return centroid (l, 12000);
    };
    CHECK (render (1.0) > render (0.0) * 1.05);
}
