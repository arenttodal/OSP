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

    /** Share of the spectrum's energy above `hz` (48 kHz, 8192-point frame from `from`). */
    double shareAbove (const std::vector<float>& x, std::size_t from, double hz)
    {
        const Fft fft (13);
        const int n = fft.size();
        std::vector<std::complex<double>> b (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
            b[static_cast<std::size_t> (i)] = { x[from + static_cast<std::size_t> (i)] * (0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n)), 0.0 };
        fft.forward (b.data());
        double above = 0, all = 0;
        for (int k = 1; k < n / 2; ++k)
        {
            const double p = std::norm (b[static_cast<std::size_t> (k)]);
            all += p;
            if (k * 48000.0 / n > hz)
                above += p;
        }
        return above / all;
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

TEST_CASE ("post: the four SPACE types have their own identity", "[unit][post][space]")
{
    // Impulse responses: decay time (energy -30 dB point), brightness, onset density.
    struct Measure { double t30, centroid, earlyShare; };
    auto measure = [] (SpaceType type) {
        PostProcessor post;
        post.prepare (48000.0, 256);
        Shaping sh;
        sh.spaceType = type;
        sh.spaceDecaySeconds = 2.0;
        sh.movementMode = MovementMode::pulse; // a bus mode at MOVEMENT 0: passes through
        post.setShaping (sh);
        Macros m;
        m.space = 1.0;
        m.reimagined = 0.0;
        m.motion = 0.0;
        post.setMacros (m);
        post.reset();
        AudioData x = AudioData::allocate (2, 4 * 48000, 48000.0);
        x.channels[0][100] = x.channels[1][100] = 1.0f;
        const auto out = process (post, x);
        std::vector<float> wet (out.channels[0]);
        wet[100] = 0.0f; // the dry impulse
        double total = 0.0;
        for (float v : wet) total += v * v;
        double acc = 0.0, t30 = 0.0;
        for (std::size_t i = 0; i < wet.size(); ++i)
        {
            acc += wet[i] * wet[i];
            if (acc > total * (1.0 - 1.0e-3)) { t30 = static_cast<double> (i) / 48000.0; break; }
        }
        const double early = energy (wet, 0, 48000 / 20);
        return Measure { t30, centroid (wet, 2400), early / total };
    };
    const auto room = measure (SpaceType::room), chamber = measure (SpaceType::chamber);
    const auto plate = measure (SpaceType::plate), spring = measure (SpaceType::spring);
    INFO ("t30 room " << room.t30 << " chamber " << chamber.t30 << " plate " << plate.t30 << " spring " << spring.t30);
    INFO ("centroid room " << room.centroid << " chamber " << chamber.centroid << " plate " << plate.centroid << " spring " << spring.centroid);
    INFO ("early share room " << room.earlyShare << " chamber " << chamber.earlyShare << " plate " << plate.earlyShare << " spring " << spring.earlyShare);
    CHECK (plate.centroid > room.centroid);         // plate bright, room dark
    CHECK (room.earlyShare > plate.earlyShare);     // room: present early reflections
    CHECK (spring.centroid < plate.centroid);       // spring band-limited
    for (const auto* m : { &room, &chamber, &plate, &spring })
        CHECK (m->t30 > 0.2);
}

TEST_CASE ("post: SPACE at zero is bypassed and changing type does not click", "[unit][post][space]")
{
    const auto audio = testsignals::vowel (220.0, 3.0, 48000.0, 1);
    const auto model = modelFor (audio);
    PostProcessor post;
    post.prepare (48000.0, 256);
    post.setModel (model.get());
    Shaping sh;
    sh.movementMode = MovementMode::pulse;
    post.setShaping (sh);
    Macros m;
    m.space = 0.0;
    m.reimagined = 0.0;
    m.motion = 0.0;
    post.setMacros (m);
    post.reset();
    CHECK (test::maxDifference (process (post, audio), audio) == 0.0);

    m.space = 0.6;
    post.setMacros (m);
    AudioData out = audio;
    // A click is a step right after a switch that is larger than anything the same
    // signal does on its own: compare the steps just after each type change with the
    // largest step elsewhere.
    float steady = 0.0f, atSwitch = 0.0f, prev = 0.0f;
    const SpaceType order[] = { SpaceType::plate, SpaceType::spring, SpaceType::room, SpaceType::chamber };
    for (int pos = 0, block = 0; pos < static_cast<int> (out.channels[0].size()); pos += 256, ++block)
    {
        sh.spaceType = order[(block / 50) % 4];
        sh.spaceDecaySeconds = 1.0 + 0.02 * (block % 100);
        post.setShaping (sh);
        const int n = std::min (256, static_cast<int> (out.channels[0].size()) - pos);
        post.process (out.channels[0].data() + pos, out.channels[1].data() + pos, n);
        const bool nearSwitch = block % 50 < 2 && block >= 50;
        for (int i = 0; i < n; ++i)
        {
            const float wet = out.channels[0][static_cast<std::size_t> (pos + i)] - audio.channels[0][static_cast<std::size_t> (pos + i)];
            const float step = std::abs (wet - prev);
            if (block > 4)
                (nearSwitch ? atSwitch : steady) = std::max (nearSwitch ? atSwitch : steady, step);
            prev = wet;
        }
    }
    INFO ("largest step after a switch " << atSwitch << ", elsewhere " << steady);
    CHECK (atSwitch <= steady);
}

TEST_CASE ("post: SPACE sleeps through silence and wakes without a trace", "[unit][post][space]")
{
    // A note, a long silence, a second note. With exact silence the reverb goes to sleep
    // once its tail is below -120 dBFS; fed 1e-20 instead (inaudible, but not silence) it
    // never sleeps. Both must sound the same: the sleep is only a CPU saving, and the
    // modulation phase has to carry on through it.
    const auto note = testsignals::vowel (220.0, 0.5, 48000.0, 1);   // stereo
    const std::size_t gap = 10 * 48000, len = note.numFrames();
    auto make = [&] (float filler) {
        AudioData a = note;
        for (std::size_t c = 0; c < a.channels.size(); ++c)
        {
            a.channels[c].resize (len + gap + len, filler);
            std::copy (note.channels[c].begin(), note.channels[c].end(), a.channels[c].begin() + static_cast<std::ptrdiff_t> (len + gap));
        }
        return a;
    };
    const auto model = modelFor (note);
    auto run = [&] (const AudioData& in) {
        PostProcessor post;
        post.prepare (48000.0, 256);
        post.setModel (model.get());
        Shaping sh;
        sh.spaceType = SpaceType::room;
        sh.spaceDecaySeconds = 1.0;
        post.setShaping (sh);
        Macros m;
        m.space = 0.6;
        m.reimagined = 0.0;
        m.motion = 0.0;
        post.setMacros (m);
        post.reset();
        return process (post, in);
    };
    const auto silent = make (0.0f), awake = make (1.0e-20f);
    const auto a = run (silent), b = run (awake);

    // Asleep: the last seconds of the gap are exact silence.
    CHECK (energy (a.channels[0], len + gap - 2 * 48000, len + gap) == 0.0);
    // Awake again at once: the second note has its room around it ...
    const auto onset = len + gap;
    double wetSecond = 0.0;
    for (std::size_t i = onset; i < onset + len; ++i)
        wetSecond = std::max (wetSecond, static_cast<double> (std::abs (a.channels[0][i] - silent.channels[0][i])));
    CHECK (wetSecond > 1.0e-3);
    // ... and it is the same room the never-sleeping reverb gives (differences far below hearing).
    double diff = 0.0;
    for (std::size_t c = 0; c < a.channels.size(); ++c)
        for (std::size_t i = onset; i < a.channels[c].size(); ++i)
            diff = std::max (diff, static_cast<double> (std::abs (a.channels[c][i] - b.channels[c][i])));
    INFO ("largest difference after waking " << diff);
    CHECK (diff < 1.0e-5);
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
        return shareAbove (l, 12000, 400.0); // above the tone and the resonators' fifth (330 Hz)
    };
    CHECK (render (1.0) > render (0.0) * 2.0);
}
