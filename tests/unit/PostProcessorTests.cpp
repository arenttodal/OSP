#include "audio/utility/TestSignals.h"
#include "core/Fft.h"
#include "engine/InstrumentBuilder.h"
#include "engine/InstrumentEngine.h"
#include "engine/PostProcessor.h"
#include "research/Experiment.h"
#include "support/TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>

using namespace osp;
using Catch::Approx;

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
    const auto room = measure (SpaceType::room), hall = measure (SpaceType::hall);
    const auto plate = measure (SpaceType::plate), spring = measure (SpaceType::spring);
    INFO ("t30 room " << room.t30 << " hall " << hall.t30 << " plate " << plate.t30 << " spring " << spring.t30);
    INFO ("centroid room " << room.centroid << " hall " << hall.centroid << " plate " << plate.centroid << " spring " << spring.centroid);
    INFO ("early share room " << room.earlyShare << " hall " << hall.earlyShare << " plate " << plate.earlyShare << " spring " << spring.earlyShare);
    CHECK (plate.centroid > room.centroid);         // plate bright, room dark
    CHECK (room.earlyShare > plate.earlyShare);     // room: present early reflections
    CHECK (spring.centroid < plate.centroid);       // spring band-limited
    for (const auto* m : { &room, &hall, &plate, &spring })
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
    const SpaceType order[] = { SpaceType::plate, SpaceType::spring, SpaceType::room, SpaceType::hall };
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

TEST_CASE ("post: SPACE level per type (measurement)", "[.][space-level]")
{
    for (int t = 0; t < 4; ++t)
    {
        PostProcessor post;
        post.prepare (48000.0, 256);
        Shaping sh;
        sh.spaceType = static_cast<SpaceType> (t);
        sh.spaceDecaySeconds = 2.0;
        sh.movementMode = MovementMode::pulse;
        post.setShaping (sh);
        Macros m;
        m.space = 0.5;
        m.reimagined = 0.0;
        m.motion = 0.0;
        post.setMacros (m);
        post.reset();
        // Pink-ish noise burst 0.5 s, then 4 s of tail.
        AudioData x = AudioData::allocate (2, 5 * 48000, 48000.0);
        Prng rng (7);
        float b = 0.0f;
        for (int i = 0; i < 24000; ++i)
        {
            b = 0.97f * b + 0.03f * static_cast<float> (rng.bipolar());
            x.channels[0][static_cast<std::size_t> (i)] = x.channels[1][static_cast<std::size_t> (i)] = 4.0f * b;
        }
        const auto out = process (post, x);
        double wet = 0, dry = 0;
        for (std::size_t i = 0; i < out.channels[0].size(); ++i)
        {
            const double w0 = out.channels[0][i] - 0.9 * x.channels[0][i], w1 = out.channels[1][i] - 0.9 * x.channels[1][i];
            wet += w0 * w0 + w1 * w1;
            dry += 2.0 * x.channels[0][i] * x.channels[0][i];
        }
        std::printf ("type %d wet/dry %.2f dB\n", t, 10.0 * std::log10 (wet / dry));
    }
}

TEST_CASE ("post: SPACE and ECHO impulse responses (measurement)", "[.][space-ir]")
{
    // Raw float32 stereo-interleaved impulse responses for offline analysis (OSP_SPACE_DIR).
    const char* dir = std::getenv ("OSP_SPACE_DIR");
    if (dir == nullptr)
        return;
    auto render = [dir] (const char* name, Shaping sh, Macros m, double seconds) {
        PostProcessor post;
        post.prepare (48000.0, 256);
        sh.movementMode = MovementMode::pulse;
        post.setShaping (sh);
        m.reimagined = 0.0;
        m.motion = 0.0;
        post.setMacros (m);
        post.reset();
        AudioData x = AudioData::allocate (2, static_cast<int> (seconds * 48000), 48000.0);
        x.channels[0][100] = x.channels[1][100] = 1.0f;
        auto out = process (post, x);
        out.channels[0][100] = out.channels[1][100] = 0.0f;   // the dry impulse
        std::vector<float> inter;
        for (std::size_t i = 0; i < out.channels[0].size(); ++i)
        {
            inter.push_back (out.channels[0][i]);
            inter.push_back (out.channels[1][i]);
        }
        const auto path = std::string (dir) + "/" + name + ".f32";
        if (auto* f = std::fopen (path.c_str(), "wb"))
        {
            std::fwrite (inter.data(), sizeof (float), inter.size(), f);
            std::fclose (f);
        }
    };
    const char* names[] = { "room", "hall", "plate", "spring" };
    for (int t = 0; t < 4; ++t)
    {
        Shaping sh;
        sh.spaceType = static_cast<SpaceType> (t);
        sh.spaceDecaySeconds = t == 0 ? 1.0 : 2.2;
        sh.spaceLowCutHz = 20.0;
        sh.spaceHighCutHz = 20000.0;
        Macros m;
        m.space = 1.0;
        m.echo = 0.0;
        render (names[t], sh, m, 6.0);
    }
    for (int t = 0; t < 2; ++t)
    {
        Shaping sh;
        sh.echoType = static_cast<EchoType> (t);
        sh.echoSync = false;
        sh.echoTimeMs = 300.0;
        sh.echoFeedback = 0.6;
        Macros m;
        m.space = 0.0;
        m.echo = 1.0;
        render (t == 0 ? "echo-tape" : "echo-bbd", sh, m, 4.0);
    }
}

namespace
{
    PostProcessor makePost (const Shaping& sh, double space, double echo)
    {
        PostProcessor post;
        post.prepare (48000.0, 256);
        Shaping s = sh;
        s.movementMode = MovementMode::pulse;   // a bus mode at MOVEMENT 0: passes through
        post.setShaping (s);
        Macros m;
        m.space = space;
        m.echo = echo;
        m.reimagined = 0.0;
        m.motion = 0.0;
        post.setMacros (m);
        post.reset();
        return post;
    }

    AudioData impulse (double seconds)
    {
        AudioData x = AudioData::allocate (2, static_cast<int> (seconds * 48000), 48000.0);
        x.channels[0][100] = x.channels[1][100] = 1.0f;
        return x;
    }

    /** Energy-decay (Schroeder) time from -5 to -25 dB, scaled to 60 dB. */
    double decayTime (const std::vector<float>& x)
    {
        std::vector<double> e (x.size() + 1, 0.0);
        for (std::size_t i = x.size(); i-- > 0;)
            e[i] = e[i + 1] + static_cast<double> (x[i]) * x[i];
        std::size_t a = 0, b = 0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double db = 10.0 * std::log10 (e[i] / e[0] + 1.0e-30);
            if (a == 0 && db < -5.0) a = i;
            if (db < -25.0) { b = i; break; }
        }
        return 3.0 * static_cast<double> (b - a) / 48000.0;
    }
}

TEST_CASE ("space v2: every type decays as DECAY says and stays bounded at the extremes", "[unit][post][space]")
{
    for (int t = 0; t < 3; ++t)
    {
        Shaping sh;
        sh.spaceType = static_cast<SpaceType> (t);
        sh.spaceDecaySeconds = 2.0;
        sh.spaceLowCutHz = 20.0;
        sh.spaceHighCutHz = 20000.0;
        auto post = makePost (sh, 1.0, 0.0);
        auto out = process (post, impulse (7.0));
        out.channels[0][100] = 0.0f;
        const double rt = decayTime (out.channels[0]);
        INFO ("type " << t << " RT " << rt);
        CHECK (rt > 1.4);
        CHECK (rt < 2.8);
    }
    // Every type at its most: longest decay, biggest, most modulated, darkest - finite and bounded.
    for (int t = 0; t < 4; ++t)
    {
        Shaping sh;
        sh.spaceType = static_cast<SpaceType> (t);
        sh.spaceDecaySeconds = 8.0;
        sh.spaceSize = 1.0;
        sh.spaceModulation = 1.0;
        sh.spaceDamping = 0.0;
        sh.spacePreDelayMs = 250.0;
        auto post = makePost (sh, 1.0, 0.0);
        AudioData x = AudioData::allocate (2, 10 * 48000, 48000.0);
        Prng rng (3);
        for (int i = 0; i < 48000; ++i)
            x.channels[0][static_cast<std::size_t> (i)] = x.channels[1][static_cast<std::size_t> (i)] = 0.5f * static_cast<float> (rng.bipolar());
        const auto out = process (post, x);
        float peak = 0.0f;
        bool finite = true;
        for (const auto& ch : out.channels)
            for (float v : ch)
            {
                finite = finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
            }
        INFO ("type " << t << " peak " << peak);
        CHECK (finite);
        CHECK (peak < 4.0f);
    }
}

TEST_CASE ("space v2: HALL holds its bass longer, the EQ removes what it cuts, PRE-DELAY moves the onset", "[unit][post][space]")
{
    auto band = [] (const std::vector<float>& x, double lo, double hi) {
        // A crude band split: one-pole low-pass at hi minus one-pole at lo, energy over the tail.
        std::vector<float> y (x.size());
        float a = 0.0f, b = 0.0f;
        const float ka = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * hi / 48000.0));
        const float kb = static_cast<float> (1.0 - std::exp (-2.0 * std::numbers::pi * lo / 48000.0));
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            a += (x[i] - a) * ka;
            b += (x[i] - b) * kb;
            y[i] = a - b;
        }
        return y;
    };
    Shaping hall;
    hall.spaceType = SpaceType::hall;
    hall.spaceDecaySeconds = 2.2;
    hall.spaceLowCutHz = 20.0;
    hall.spaceHighCutHz = 20000.0;
    auto post = makePost (hall, 1.0, 0.0);
    auto open = process (post, impulse (7.0));
    open.channels[0][100] = 0.0f;
    const double low = decayTime (band (open.channels[0], 60.0, 250.0)), high = decayTime (band (open.channels[0], 4000.0, 12000.0));
    INFO ("hall RT low " << low << " high " << high);
    CHECK (low > high * 1.3);   // warm: the bass outlasts the air

    hall.spaceLowCutHz = 600.0;
    auto cutPost = makePost (hall, 1.0, 0.0);
    auto cut = process (cutPost, impulse (7.0));
    cut.channels[0][100] = 0.0f;
    auto spectrumEnergy = [] (const std::vector<float>& x, double lo, double hi) {
        const Fft fft (16);
        const int n = fft.size();
        std::vector<std::complex<double>> b (static_cast<std::size_t> (n));
        for (int i = 0; i < n && static_cast<std::size_t> (i) < x.size(); ++i)
            b[static_cast<std::size_t> (i)] = { x[static_cast<std::size_t> (i)], 0.0 };
        fft.forward (b.data());
        double e = 0.0;
        for (int k = 1; k < n / 2; ++k)
            if (const double hz = k * 48000.0 / n; hz >= lo && hz <= hi)
                e += std::norm (b[static_cast<std::size_t> (k)]);
        return e;
    };
    // LOW CUT at 600 Hz takes the bass out of the room (12 dB/oct: ~ -25 dB around 100 Hz).
    CHECK (spectrumEnergy (cut.channels[0], 40.0, 150.0) < 0.02 * spectrumEnergy (open.channels[0], 40.0, 150.0));
    CHECK (spectrumEnergy (cut.channels[0], 2000.0, 6000.0) > 0.7 * spectrumEnergy (open.channels[0], 2000.0, 6000.0));

    auto onset = [] (const std::vector<float>& x) {
        for (std::size_t i = 101; i < x.size(); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                return static_cast<double> (i - 100) / 48.0;   // ms
        return -1.0;
    };
    hall.spaceLowCutHz = 20.0;
    hall.spacePreDelayMs = 0.0;
    auto a = makePost (hall, 1.0, 0.0);
    hall.spacePreDelayMs = 100.0;
    auto b = makePost (hall, 1.0, 0.0);
    auto x0 = process (a, impulse (1.0)), x1 = process (b, impulse (1.0));
    x0.channels[0][100] = x1.channels[0][100] = 0.0f;
    INFO ("onsets " << onset (x0.channels[0]) << " / " << onset (x1.channels[0]) << " ms");
    CHECK (onset (x1.channels[0]) - onset (x0.channels[0]) == Approx (100.0).margin (2.0));
}

TEST_CASE ("echo: repeats at TIME, each quieter by FEEDBACK; PING-PONG alternates sides", "[unit][post][echo]")
{
    for (int type = 0; type < 2; ++type)
    {
        Shaping sh;
        sh.echoType = static_cast<EchoType> (type);
        sh.echoSync = false;
        sh.echoTimeMs = 250.0;
        sh.echoFeedback = 0.5;
        sh.echoAge = 0.0;
        sh.echoStereo = EchoStereo::pingPong;
        auto post = makePost (sh, 0.0, 1.0);
        auto out = process (post, impulse (2.0));
        auto peakNear = [&] (int channel, double seconds) {
            const auto c = static_cast<std::size_t> (100 + seconds * 48000.0);
            float p = 0.0f;
            for (std::size_t i = c - 600; i < c + 600; ++i)
                p = std::max (p, std::abs (out.channels[static_cast<std::size_t> (channel)][i]));
            return p;
        };
        INFO ("type " << type);
        // First repeat on the left, the second on the right, the third left again.
        CHECK (peakNear (0, 0.25) > 4.0f * peakNear (1, 0.25));
        CHECK (peakNear (1, 0.5) > 4.0f * peakNear (0, 0.5));
        CHECK (peakNear (0, 0.75) > 4.0f * peakNear (1, 0.75));
        // ... each quieter than the one before it.
        CHECK (peakNear (1, 0.5) < peakNear (0, 0.25));
        CHECK (peakNear (0, 0.75) < peakNear (1, 0.5));
    }
}

TEST_CASE ("echo: FEEDBACK 100 % runs away into saturation, never into overload; off is exactly dry", "[unit][post][echo]")
{
    Shaping sh;
    sh.echoSync = false;
    sh.echoTimeMs = 120.0;
    sh.echoFeedback = 1.0;
    sh.echoAge = 1.0;
    for (int type = 0; type < 2; ++type)
    {
        sh.echoType = static_cast<EchoType> (type);
        auto post = makePost (sh, 0.0, 1.0);
        AudioData x = AudioData::allocate (2, 12 * 48000, 48000.0);
        Prng rng (9);
        for (int i = 0; i < 24000; ++i)
            x.channels[0][static_cast<std::size_t> (i)] = x.channels[1][static_cast<std::size_t> (i)] = 0.8f * static_cast<float> (rng.bipolar());
        const auto out = process (post, x);
        float peak = 0.0f;
        for (const auto& ch : out.channels)
            for (float v : ch)
            {
                REQUIRE (std::isfinite (v));
                peak = std::max (peak, std::abs (v));
            }
        INFO ("type " << type << " peak " << peak);
        CHECK (peak < 3.0f);
    }
    // ECHO at 0: the post stage is exactly what it was without it.
    Shaping plain;
    auto withEcho = makePost (plain, 0.3, 0.0);
    const auto audio = testsignals::vowel (220.0, 1.0, 48000.0, 1);
    const auto a = process (withEcho, audio);
    auto again = makePost (plain, 0.3, 0.0);
    CHECK (test::maxDifference (a, process (again, audio)) == 0.0);
}

TEST_CASE ("echo: synced TIME follows the tempo", "[unit][post][echo]")
{
    EchoDelay::Settings s;
    s.sync = true;
    s.division = 5;   // 1/8 dotted
    CHECK (EchoDelay::timeSeconds (s, 120.0) == Approx (0.375));
    CHECK (EchoDelay::timeSeconds (s, 90.0) == Approx (0.5));
    s.division = 11;   // a bar at 40 bpm is longer than the line: capped
    CHECK (EchoDelay::timeSeconds (s, 40.0) == Approx (EchoDelay::maxSeconds));
    s.sync = false;
    s.timeMs = 333.0;
    CHECK (EchoDelay::timeSeconds (s, 120.0) == Approx (0.333));
}

TEST_CASE ("shaper: CUSTOM plays the musician's steps", "[unit][shaper]")
{
    ShaperParams p;
    p.custom = true;
    for (auto& st : p.customSteps)
        st = { 1.0f, 1.0f, StepShape::hold };
    p.customSteps[4] = { 0.0f, 0.0f, StepShape::hold };   // one closed step (the fifth)
    p.smooth = 0.0;
    CHECK (RhythmicShaper::evaluate (p, 0.5 / 16.0) == Approx (1.0f));
    CHECK (RhythmicShaper::evaluate (p, 4.5 / 16.0) == Approx (0.0f).margin (1.0e-6));
    // The library pattern a CUSTOM pattern starts from plays exactly like the pattern.
    ShaperParams lib;
    ShaperParams copy = lib;
    copy.custom = true;
    copy.customSteps = RhythmicShaper::patternSteps (lib.pattern);
    for (int i = 0; i < 64; ++i)
        CHECK (RhythmicShaper::evaluate (copy, i / 64.0) == Approx (RhythmicShaper::evaluate (lib, i / 64.0)));
}
