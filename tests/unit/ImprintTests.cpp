// SYNTHESIS S2, the spectral imprint: the descriptor (analysis), the band shaper (experiments A
// and B) and the channel vocoder (experiment C). The tests pin what each part promises; the
// hidden research case ("[.][imprint-research]", OSP_IMPRINT_RESEARCH=<dir>) runs the three
// experiments on the stand-in references and writes the renders and the measurements
// (docs/research/synthesize/s2-imprint-research.md).

#include "analysis/Analyzer.h"
#include "analysis/spectrum/SpectralEnvelope.h"
#include "audio/utility/TestSignals.h"
#include "core/Fft.h"
#include "core/PitchMath.h"
#include "core/Prng.h"
#include "engine/SpectralImprint.h"
#include "engine/SynthVoice.h"
#include "io/AudioFileIO.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

using namespace osp;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    /** A body: resonances (Hz, dB, width in octaves) on a gentle downward tilt. */
    struct Body
    {
        std::vector<std::array<double, 3>> peaks;
        double tiltDbPerOctave = -3.0;
        double dbAt (double hz) const
        {
            double db = tiltDbPerOctave * std::log2 (std::max (hz, 50.0) / 200.0);
            for (const auto& p : peaks)
            {
                const double d = std::log2 (hz / p[0]) / p[2];
                db += p[1] * std::exp (-0.5 * d * d);
            }
            return db;
        }
    };

    const Body nyckelharpaBody { { { { 480.0, 12.0, 0.25 } }, { { 1150.0, 9.0, 0.3 } }, { { 2700.0, 11.0, 0.22 } }, { { 4200.0, 6.0, 0.25 } } }, -2.0 };
    const Body celloBody { { { { 230.0, 10.0, 0.3 } }, { { 620.0, 8.0, 0.35 } }, { { 1500.0, 6.0, 0.35 } } }, -4.5 };
    const Body vowelBody { { { { 730.0, 16.0, 0.18 } }, { { 1090.0, 13.0, 0.18 } }, { { 2440.0, 10.0, 0.15 } } }, -4.0 };

    /**
        A bowed or sung stand-in: harmonics through a body, vibrato, a noise part through the
        same body (bow hair, breath), an attack and a release. `swell` rises 12 dB over the
        note (the cello's crescendo, its upper harmonics opening with it).
    */
    AudioData bowedStandIn (double f0, double seconds, const Body& body, double noiseDb, bool swell, std::uint64_t seed)
    {
        const auto n = static_cast<std::int64_t> (seconds * rate);
        auto audio = AudioData::allocate (1, n, rate);
        auto& out = audio.channels[0];
        Prng rng (seed);
        const int harmonics = std::min (80, static_cast<int> (10000.0 / f0));
        std::vector<double> phases (static_cast<std::size_t> (harmonics + 1));
        for (auto& p : phases)
            p = rng.uniform (0.0, 2.0 * std::numbers::pi);
        double vib = 0.0, phaseAcc = 0.0;
        // The noise part: white noise through a fixed bank following the body (each 1/3 octave).
        std::vector<double> noise (static_cast<std::size_t> (n));
        {
            std::vector<double> centres;
            for (double hz = 100.0; hz < 10000.0; hz *= std::exp2 (1.0 / 3.0))
                centres.push_back (hz);
            std::vector<std::array<double, 4>> st (centres.size(), { 0.0, 0.0, 0.0, 0.0 });
            for (std::int64_t i = 0; i < n; ++i)
            {
                const double w = rng.bipolar();
                double s = 0.0;
                for (std::size_t c = 0; c < centres.size(); ++c)
                {
                    const double g = std::tan (std::numbers::pi * centres[c] / rate), k = 1.0 / 4.3;
                    const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    auto& z = st[c];
                    const double v3 = w - z[1];
                    const double v1 = a1 * z[0] + a2 * v3;
                    const double v2 = z[1] + a2 * z[0] + a3 * v3;
                    z[0] = 2.0 * v1 - z[0];
                    z[1] = 2.0 * v2 - z[1];
                    s += k * v1 * std::pow (10.0, body.dbAt (centres[c]) / 20.0);
                }
                noise[static_cast<std::size_t> (i)] = s;
            }
        }
        double peak = 0.0;
        std::vector<double> tone (static_cast<std::size_t> (n));
        for (std::int64_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            vib = 12.0 * std::sin (2.0 * std::numbers::pi * 5.3 * t) * std::min (1.0, t / 0.6);   // cents, growing in
            const double hz = f0 * std::exp2 (vib / 1200.0);
            phaseAcc += 2.0 * std::numbers::pi * hz / rate;
            const double open = swell ? std::min (1.0, t / seconds) : 1.0;   // the crescendo opens the top
            double s = 0.0;
            for (int h = 1; h <= harmonics; ++h)
            {
                const double fh = h * hz;
                const double tilt = swell ? (open - 1.0) * 6.0 * std::log2 (static_cast<double> (h)) : 0.0;
                s += std::pow (10.0, (body.dbAt (fh) + tilt) / 20.0) / h * std::sin (h * phaseAcc + phases[static_cast<std::size_t> (h)]);
            }
            tone[static_cast<std::size_t> (i)] = s;
            peak = std::max (peak, std::abs (s));
        }
        double noisePeak = 1.0e-9;
        for (double v : noise)
            noisePeak = std::max (noisePeak, std::abs (v));
        const double noiseGain = std::pow (10.0, noiseDb / 20.0) * peak / noisePeak;
        for (std::int64_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double attack = std::min (1.0, t / 0.12), release = std::min (1.0, (seconds - t) / 0.15);
            const double dyn = swell ? std::pow (10.0, (-12.0 + 12.0 * t / seconds) / 20.0) : 1.0;
            out[static_cast<std::size_t> (i)] = static_cast<float> (0.5 / peak * attack * release * dyn
                                                                    * (tone[static_cast<std::size_t> (i)] + noiseGain * noise[static_cast<std::size_t> (i)]));
        }
        return audio;
    }

    struct Reference
    {
        std::string name;
        std::function<AudioData (double)> make;   ///< the reference at a pitch
        double rootHz;
    };

    std::vector<Reference> references()
    {
        return {
            { "A-nyckelharpa", [] (double hz) { return bowedStandIn (hz, 3.0, nyckelharpaBody, -24.0, false, 11); }, midiToHz (62) },
            { "B-vocal", [] (double hz) { return bowedStandIn (hz, 3.0, vowelBody, -16.0, false, 12); }, midiToHz (64) },
            { "C-cello", [] (double hz) { return bowedStandIn (hz, 4.0, celloBody, -28.0, true, 13); }, midiToHz (48) },
            { "D-pluck", [] (double hz) { auto a = testsignals::pluck (hz, 2.5, rate, 14, 0.7, 1); return a; }, midiToHz (55) },
        };
    }

    /** Energy (dB) in each harmonic's region, k f0 +- f0 / 2, from 0.4 s on (a 65536-point
        Hann FFT): the timbre itself, vibrato and breath included, at the pitch both share. */
    std::vector<double> harmonicDb (const AudioData& a, double f0)
    {
        const int order = 16, n = 1 << order;
        Fft fft (order);
        std::vector<std::complex<double>> buf (static_cast<std::size_t> (n));
        const auto& x = a.channels[0];
        const auto start = static_cast<std::size_t> (0.4 * rate);
        for (int i = 0; i < n; ++i)
        {
            const auto j = start + static_cast<std::size_t> (i);
            const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
            buf[static_cast<std::size_t> (i)] = { j < x.size() ? x[j] * w : 0.0, 0.0 };
        }
        fft.forward (buf.data());
        std::vector<double> out;
        for (int k = 1; k * f0 < 8000.0; ++k)
        {
            const int b0 = static_cast<int> ((k - 0.5) * f0 / rate * n), b1 = static_cast<int> ((k + 0.5) * f0 / rate * n);
            double e = 1.0e-20;
            for (int b = std::max (1, b0); b < b1; ++b)
                e += std::norm (buf[static_cast<std::size_t> (b)]);
            out.push_back (10.0 * std::log10 (e));
        }
        return out;
    }

    /** How far a sound's timbre is from the reference's at the same pitch: the harmonic-energy
        difference (dB RMS over 150 Hz .. 8 kHz, each octave weighing the same), its broad tilt
        and level removed (the oscillator keeps its own tilt; the imprint brings the body). */
    double shapeDistance (const AudioData& sound, const AudioData& reference, double f0)
    {
        const auto a = harmonicDb (sound, f0), b = harmonicDb (reference, f0);
        double sw = 0.0, sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        std::vector<std::array<double, 3>> points;
        for (std::size_t k = 0; k < std::min (a.size(), b.size()); ++k)
        {
            const double hz = static_cast<double> (k + 1) * f0;
            if (hz < 150.0)
                continue;
            const double w = 1.0 / static_cast<double> (k + 1), x = std::log2 (hz), y = a[k] - b[k];
            points.push_back ({ w, x, y });
            sw += w; sx += w * x; sy += w * y; sxx += w * x * x; sxy += w * x * y;
        }
        if (points.size() < 3)
            return 0.0;
        const double slope = (sw * sxy - sx * sy) / (sw * sxx - sx * sx), icpt = (sy - slope * sx) / sw;
        double e = 0.0;
        for (const auto& p : points)
        {
            const double r = p[2] - (icpt + slope * p[1]);
            e += p[0] * r * r;
        }
        return std::sqrt (e / sw);
    }

    double rmsDb (const AudioData& a, double from = 0.0, double to = 1.0e9)
    {
        const auto& x = a.channels[0];
        const auto i0 = static_cast<std::size_t> (std::max (0.0, from) * rate);
        const auto i1 = std::min (x.size(), static_cast<std::size_t> (std::max (0.0, to) * rate));
        double e = 0.0;
        for (auto i = i0; i < i1; ++i)
            e += static_cast<double> (x[i]) * x[i];
        return 10.0 * std::log10 (std::max (1.0e-20, e / static_cast<double> (std::max<std::size_t> (1, i1 - i0))));
    }

    enum class Method { analog, staticImprint, movingImprint, vocoder };
    const char* methodName (Method m)
    {
        switch (m)
        {
            case Method::analog: return "analog";
            case Method::staticImprint: return "A-static";
            case Method::movingImprint: return "B-time-varying";
            case Method::vocoder: return "C-filterbank";
        }
        return "";
    }

    struct RenderOptions
    {
        Method method = Method::analog;
        double imprint = 1.0, motion = 0.0;
        int bands = 24;
        bool perceptual = true;
        double formantFollow = imprint::defaultFormantFollow;
        double seconds = 3.0;
    };

    /** The SYNTH oscillator alone at `hz` (Held, S1's saw), coloured by `method`, for `seconds`;
        `contour` is the voice's own sample output (experiment C reads it). */
    AudioData renderOscillator (double hz, const imprint::Profile& profile, const AudioData& contour, const RenderOptions& o)
    {
        synth::Voice voice;
        voice.prepare (rate);
        voice.start (0x5eed, 0.8, synth::Envelope::gate);
        voice.setPitch (hz);
        imprint::Shaper shaper;
        imprint::Vocoder vocoder;
        if (o.method == Method::staticImprint || o.method == Method::movingImprint)
            shaper.prepare (rate, o.bands, o.perceptual);
        if (o.method == Method::vocoder)
            vocoder.prepare (rate, o.bands, o.perceptual);
        const auto n = static_cast<std::int64_t> (o.seconds * rate);
        auto out = AudioData::allocate (1, n, rate);
        constexpr int control = 32;
        for (std::int64_t i = 0; i < n; ++i)
        {
            if (i % control == 0 && shaper.layout().count > 0 && o.method != Method::vocoder && o.method != Method::analog)
            {
                // B: follow the attack's frames, then wander back and forth through the sustain
                // (a ping-pong at the recording's own pace: no seam, no release while held).
                double position = -1.0;
                if (o.method == Method::movingImprint && profile.frameCount() > 0)
                {
                    const double f = static_cast<double> (i) / rate / profile.frameSeconds;
                    if (f < profile.sustainBegin)
                        position = f;
                    else
                    {
                        const double span = std::max (1, profile.sustainEnd - profile.sustainBegin - 1);
                        const double u = std::fmod (f - profile.sustainBegin, 2.0 * span);
                        position = profile.sustainBegin + (u < span ? u : 2.0 * span - u);
                    }
                }
                shaper.setTarget (profile, o.imprint, position, o.method == Method::movingImprint ? o.motion : 0.0, hz, o.formantFollow, control);
            }
            float v = voice.next (0.0f);
            if (o.method == Method::staticImprint || o.method == Method::movingImprint)
                v = shaper.active() ? shaper.process (v) : v;
            else if (o.method == Method::vocoder)
            {
                const auto& c = contour.channels[0];
                const float m = i < static_cast<std::int64_t> (c.size()) ? c[static_cast<std::size_t> (i)] : 0.0f;
                v = vocoder.process (v, m, static_cast<float> (o.imprint));
            }
            out.channels[0][static_cast<std::size_t> (i)] = v;
        }
        return out;
    }

    /** The SYNTH blend in a voice: the sample and the oscillator (levelled to the sample's
        sustain) at `amount` (S1's hybrid law). */
    AudioData blend (const AudioData& sample, const AudioData& oscillator, double amount)
    {
        const auto gains = synth::blendGains (amount, synth::Blend::hybrid);
        const double match = std::pow (10.0, (rmsDb (sample, 0.3, 1.5) - rmsDb (oscillator, 0.3, 1.5)) / 20.0);
        auto out = AudioData::allocate (1, oscillator.numFrames(), rate);
        for (std::size_t i = 0; i < out.channels[0].size(); ++i)
        {
            const float s = i < sample.channels[0].size() ? sample.channels[0][i] : 0.0f;
            out.channels[0][i] = gains.sample * s + gains.synth * static_cast<float> (match) * oscillator.channels[0][i];
        }
        return out;
    }

    double measuredPitchHz (const AudioData& a)
    {
        AudioData mid = AudioData::allocate (1, static_cast<std::int64_t> (1.0 * rate), rate);
        std::copy_n (a.channels[0].begin() + static_cast<std::ptrdiff_t> (0.5 * rate), mid.numFrames(), mid.channels[0].begin());
        return Analyzer::analyse (mid).pitch.fundamentalHz;
    }
}

//==============================================================================
TEST_CASE ("imprint: the profile reads a body's resonances, tilt aside, in its sustain", "[unit][imprint]")
{
    const auto vowel = bowedStandIn (midiToHz (64), 2.0, vowelBody, -30.0, false, 3);
    const auto p = imprint::analyse (vowel, midiToHz (64));
    REQUIRE (p.valid);
    CHECK (p.staticDb.size() == static_cast<std::size_t> (imprint::gridPoints));
    CHECK (p.frameCount() >= 30);
    CHECK (p.attackEnd >= 1);
    CHECK (p.sustainBegin >= p.attackEnd - 1);
    CHECK (p.sustainEnd - p.sustainBegin >= 10);   // a long steady sustain
    // The first formants stand out of their surroundings; a trough between them stays lower.
    const float f1 = imprint::Profile::at (p.staticDb.data(), 730.0), f2 = imprint::Profile::at (p.staticDb.data(), 1090.0);
    const float f3 = imprint::Profile::at (p.staticDb.data(), 2440.0), trough = imprint::Profile::at (p.staticDb.data(), 1700.0);
    CHECK (f1 > trough + 4.0f);
    CHECK (f2 > trough + 3.0f);
    CHECK (f3 > trough + 1.0f);   // (the narrow third formant, blurred by the cepstral smoothing at this pitch)
    for (float v : p.staticDb)
        CHECK (std::abs (v) <= imprint::maxBandDb + 1.0e-3);
    // Silence: no profile (the voice plays Analog).
    CHECK_FALSE (imprint::analyse (testsignals::silence (1.0, rate), 220.0).valid);
    CHECK_FALSE (imprint::analyse (AudioData {}, 220.0).valid);
}

TEST_CASE ("imprint: a pluck without a sustain settles on its early body", "[unit][imprint]")
{
    const auto pluck = testsignals::pluck (midiToHz (55), 2.0, rate, 4, 0.7, 1);
    const auto p = imprint::analyse (pluck, midiToHz (55));
    REQUIRE (p.valid);
    CHECK (p.sustainEnd > p.sustainBegin);
    CHECK ((p.sustainEnd - p.sustainBegin) * p.frameSeconds <= 2.0);
    CHECK (p.sustainBegin * p.frameSeconds < 0.6);   // early in the note, not its dying tail
}

TEST_CASE ("imprint: the shaper meets its target at the band centres and keeps the level", "[unit][imprint]")
{
    imprint::Shaper shaper;
    shaper.prepare (rate, 24, true);
    imprint::Profile flat;
    flat.valid = true;
    flat.staticDb.assign (imprint::gridPoints, 0.0f);
    // A flat profile (or IMPRINT 0) is no colour at all: the shaper is not engaged.
    shaper.setTarget (flat, 1.0, -1.0, 0.0, 220.0, 0.0, 32);
    CHECK_FALSE (shaper.active());
    shaper.reset();
    imprint::Profile p = flat;
    p.staticDb = imprint::analyse (bowedStandIn (midiToHz (64), 2.0, vowelBody, -30.0, false, 3), midiToHz (64)).staticDb;
    shaper.setTarget (p, 0.0, -1.0, 0.0, 220.0, 0.0, 32);
    CHECK_FALSE (shaper.active());
    shaper.reset();   // (a note's first target is taken at once; later ones ease in)
    shaper.setTarget (p, 1.0, -1.0, 0.0, 220.0, 0.0, 32);
    REQUIRE (shaper.active());
    // At the centres the cascade is within 1.5 dB of the profile (the bands' spill compensated);
    // the levelling is the same at every frequency, so compare shapes.
    const auto& layout = shaper.layout();
    double worst = 0.0, offset = 0.0;
    std::vector<double> errors;
    for (int b = 2; b < layout.count - 2; ++b)
    {
        const double hz = layout.hz[static_cast<std::size_t> (b)];
        errors.push_back (shaper.responseDb (hz) - imprint::Profile::over (p.staticDb.data(), hz, layout.octaves[static_cast<std::size_t> (b)]));
    }
    for (double e : errors)
        offset += e / static_cast<double> (errors.size());
    for (double e : errors)
        worst = std::max (worst, std::abs (e - offset));
    CHECK (worst < 1.5);

    // The levelled oscillator stays within 2 dB of the plain one across the keyboard.
    imprint::Profile none;
    for (int note : { 36, 48, 60, 72, 84 })
    {
        RenderOptions plain, coloured;
        coloured.method = Method::staticImprint;
        const auto a = renderOscillator (midiToHz (note), none, {}, plain);
        const auto b = renderOscillator (midiToHz (note), p, {}, coloured);
        INFO ("note " << note);
        CHECK (std::abs (rmsDb (b, 0.2, 2.5) - rmsDb (a, 0.2, 2.5)) < 2.0);
    }
}

TEST_CASE ("imprint: IMPRINT 0 is the oscillator exactly; the shaper and vocoder never allocate", "[unit][imprint]")
{
    const auto vowel = bowedStandIn (midiToHz (60), 2.0, vowelBody, -30.0, false, 5);
    const auto p = imprint::analyse (vowel, midiToHz (60));
    RenderOptions analog, zero;
    zero.method = Method::staticImprint;
    zero.imprint = 0.0;
    const auto a = renderOscillator (midiToHz (60), p, vowel, analog);
    const auto b = renderOscillator (midiToHz (60), p, vowel, zero);
    CHECK (a.channels[0] == b.channels[0]);

    imprint::Shaper shaper;
    shaper.prepare (rate, 32, true);
    imprint::Vocoder vocoder;
    vocoder.prepare (rate, 32, true);
    test::AllocationScope scope;
    float sink = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        if (i % 32 == 0)
            shaper.setTarget (p, 0.7, i / 2400.0, 0.5, 261.6, 0.25, 32);
        sink += shaper.process (0.1f * std::sin (0.05f * static_cast<float> (i)));
        sink += vocoder.process (0.1f, vowel.channels[0][static_cast<std::size_t> (i % 48000)], 0.7f);
    }
    CHECK (scope.count() == 0);
    CHECK (std::isfinite (sink));
}

TEST_CASE ("imprint: every method keeps the played pitch", "[unit][imprint]")
{
    const auto vowel = bowedStandIn (midiToHz (64), 3.0, vowelBody, -20.0, false, 6);
    const auto p = imprint::analyse (vowel, midiToHz (64));
    for (int note : { 52, 64, 76 })
        for (auto m : { Method::analog, Method::staticImprint, Method::movingImprint, Method::vocoder })
        {
            RenderOptions o;
            o.method = m;
            o.motion = 0.7;
            const auto contour = bowedStandIn (midiToHz (note), 3.0, vowelBody, -20.0, false, 6);
            const auto out = renderOscillator (midiToHz (note), p, contour, o);
            INFO (methodName (m) << " note " << note);
            CHECK (std::abs (1200.0 * std::log2 (measuredPitchHz (out) / midiToHz (note))) < 5.0);
        }
}

//==============================================================================
// The research comparison (hidden): renders, measurements and timings.

TEST_CASE ("imprint research: experiments A, B and C on the stand-in references", "[.][imprint-research]")
{
    const char* dirEnv = std::getenv ("OSP_IMPRINT_RESEARCH");
    if (dirEnv == nullptr)
        return;
    const std::filesystem::path dir (dirEnv);
    const auto refs = references();
    std::filesystem::create_directories (dir / "renders");
    std::ofstream report (dir / "measurements.md");
    std::string error;
    auto save = [&] (const std::string& name, const AudioData& a, bool levelled) {
        AudioData out = a;
        if (levelled)
        {
            // Loudness-matched copies (-20 dBFS RMS) for timbre; the plain ones keep the gain.
            const double g = std::pow (10.0, (-20.0 - rmsDb (a)) / 20.0);
            for (auto& v : out.channels[0])
                v = static_cast<float> (std::clamp (v * g, -1.0, 1.0));
        }
        REQUIRE (io::writeAudioFile (dir / "renders" / (name + (levelled ? "-matched" : "") + ".wav"), out, io::SampleFormat::pcm24, error));
    };

    // 1. Similarity, stability across the keyboard, level, after the sample ends.
    report << "# Spectral imprint research: measurements (stand-in references)\n\n";
    report << "Timbre distance: the harmonic-energy difference to the reference recording at that pitch "
              "(dB RMS, 150 Hz-8 kHz, each octave weighing the same, broad tilt and level removed; lower is closer). The oscillator alone, IMPRINT 100 %, MOTION 70 % for B, 24 perceptual bands.\n\n";
    report << "| Reference | Key | Analog | A static | B time-varying | C filterbank | Level A / B / C vs Analog (dB) |\n|---|---|---|---|---|---|---|\n";
    for (const auto& ref : refs)
    {
        const auto atRoot = ref.make (ref.rootHz);
        const auto profile = imprint::analyse (atRoot, ref.rootHz);
        for (int semis : { -12, 0, 7, 12 })
        {
            const double hz = ref.rootHz * std::exp2 (semis / 12.0);
            const auto truth = ref.make (hz);   // the instrument played at that key (its body stays)
            double d[4] {}, lv[4] {};
            for (auto m : { Method::analog, Method::staticImprint, Method::movingImprint, Method::vocoder })
            {
                RenderOptions o;
                o.method = m;
                o.motion = 0.7;
                o.seconds = std::min (3.0, truth.durationSeconds());
                const auto out = renderOscillator (hz, profile, truth, o);
                d[static_cast<int> (m)] = shapeDistance (out, truth, hz);
                lv[static_cast<int> (m)] = rmsDb (out, 0.2, o.seconds - 0.2);
            }
            char line[256];
            std::snprintf (line, sizeof (line), "| %s | %+d | %.1f | %.1f | %.1f | %.1f | %+.1f / %+.1f / %+.1f |\n", ref.name.c_str(), semis,
                           d[0], d[1], d[2], d[3], lv[1] - lv[0], lv[2] - lv[0], lv[3] - lv[0]);
            report << line;
        }
    }

    // 2. Formant follow (internal setting): fixed, half, full, on the vocal at +12.
    {
        const auto& ref = refs[1];
        const auto profile = imprint::analyse (ref.make (ref.rootHz), ref.rootHz);
        report << "\n## Formant follow (B-vocal, A static, 100 %)\n\n| Key | follow 0 | 0.5 | 1.0 |\n|---|---|---|---|\n";
        for (int semis : { -12, 7, 12 })
        {
            const double hz = ref.rootHz * std::exp2 (semis / 12.0);
            const auto truth = ref.make (hz);
            report << "| " << (semis > 0 ? "+" : "") << semis;
            for (double follow : { 0.0, 0.5, 1.0 })
            {
                RenderOptions o;
                o.method = Method::staticImprint;
                o.formantFollow = follow;
                char cell[32];
                std::snprintf (cell, sizeof (cell), " | %.1f", shapeDistance (renderOscillator (hz, profile, truth, o), truth, hz));
                report << cell;
            }
            report << " |\n";
        }
    }

    // 3. Band count and spacing (A and C), the vocal and the nyckelharpa at their roots.
    report << "\n## Bands and spacing (shape distance at the root)\n\n| Reference | Method | 16 log | 16 ERB | 24 log | 24 ERB | 32 log | 32 ERB |\n|---|---|---|---|---|---|---|---|\n";
    for (std::size_t r : { std::size_t (0), std::size_t (1) })
    {
        const auto& ref = refs[r];
        const auto truth = ref.make (ref.rootHz);
        const auto profile = imprint::analyse (truth, ref.rootHz);
        for (auto m : { Method::staticImprint, Method::vocoder })
        {
            report << "| " << ref.name << " | " << methodName (m);
            for (int bands : { 16, 24, 32 })
                for (bool erb : { false, true })
                {
                    RenderOptions o;
                    o.method = m;
                    o.bands = bands;
                    o.perceptual = erb;
                    char cell[32];
                    std::snprintf (cell, sizeof (cell), " | %.1f", shapeDistance (renderOscillator (ref.rootHz, profile, truth, o), truth, ref.rootHz));
                    report << cell;
                }
            report << " |\n";
        }
    }

    // 4. After the sample ends (a pluck held 3 s): the level 1.5-2.5 s against 0.2-0.6 s.
    {
        const auto& ref = refs[3];
        const auto truth = ref.make (ref.rootHz);
        const auto profile = imprint::analyse (truth, ref.rootHz);
        report << "\n## A held pluck after its sound has died (dB, 1.5-2.5 s against 0.2-0.6 s)\n\n| Analog | A | B | C |\n|---|---|---|---|\n|";
        for (auto m : { Method::analog, Method::staticImprint, Method::movingImprint, Method::vocoder })
        {
            RenderOptions o;
            o.method = m;
            o.motion = 0.7;
            const auto out = renderOscillator (ref.rootHz, profile, truth, o);
            char cell[32];
            std::snprintf (cell, sizeof (cell), " %+.1f |", rmsDb (out, 1.5, 2.5) - rmsDb (out, 0.2, 0.6));
            report << cell;
        }
        report << "\n";
    }

    // 5. Cost: ns per sample per voice, and a polyphonic run (N voices interleaved per block).
    {
        report << "\n## Cost (this machine; ns per sample per voice, median of 5)\n\n| Stage | 1 voice | 8 | 16 | 32 |\n|---|---|---|---|---|\n";
        const auto& ref = refs[0];
        const auto truth = ref.make (ref.rootHz);
        const auto profile = imprint::analyse (truth, ref.rootHz);
        auto time = [&] (const std::string& name, int bandsOrZero, int kind) {
            report << "| " << name;
            for (int voices : { 1, 8, 16, 32 })
            {
                std::vector<double> runs;
                for (int run = 0; run < 5; ++run)
                {
                    std::vector<synth::Voice> osc (static_cast<std::size_t> (voices));
                    std::vector<imprint::Shaper> shapers (static_cast<std::size_t> (voices));
                    std::vector<imprint::Vocoder> vocoders (static_cast<std::size_t> (voices));
                    for (int v = 0; v < voices; ++v)
                    {
                        osc[static_cast<std::size_t> (v)].prepare (rate);
                        osc[static_cast<std::size_t> (v)].start (static_cast<std::uint64_t> (v + 1), 0.8, synth::Envelope::gate);
                        osc[static_cast<std::size_t> (v)].setPitch (ref.rootHz * std::exp2 (v / 12.0));
                        if (kind == 1 || kind == 3)
                            shapers[static_cast<std::size_t> (v)].prepare (rate, bandsOrZero, true);
                        if (kind == 2)
                            vocoders[static_cast<std::size_t> (v)].prepare (rate, bandsOrZero, true);
                    }
                    const int blocks = 375, block = 256;   // 2 s
                    float sink = 0.0f;
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int b = 0; b < blocks; ++b)
                        for (int v = 0; v < voices; ++v)
                        {
                            auto& o = osc[static_cast<std::size_t> (v)];
                            auto& s = shapers[static_cast<std::size_t> (v)];
                            auto& c = vocoders[static_cast<std::size_t> (v)];
                            for (int i = 0; i < block; ++i)
                            {
                                if ((kind == 1 || kind == 3) && i % 32 == 0)
                                    s.setTarget (profile, 0.8, (b * block + i) / rate / profile.frameSeconds, kind == 1 ? 0.5 : 0.0, ref.rootHz, 0.0, 32);
                                float x = o.next (0.0f);
                                if (kind == 1 || kind == 3)
                                    x = s.process (x);
                                else if (kind == 2)
                                    x = c.process (x, truth.channels[0][static_cast<std::size_t> ((b * block + i) % 48000)], 0.8f);
                                sink += x;
                            }
                        }
                    const double ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count();
                    runs.push_back (ns / (static_cast<double> (blocks) * block * voices) + (std::isfinite (sink) ? 0.0 : 1.0e9));
                }
                std::sort (runs.begin(), runs.end());
                char cell[32];
                std::snprintf (cell, sizeof (cell), " | %.1f", runs[2]);
                report << cell;
            }
            report << " |\n";
        };
        time ("oscillator alone (Analog)", 0, 0);
        for (int bands : { 16, 24, 32 })
            time ("oscillator + A shaper (MOTION 0), " + std::to_string (bands) + " bands", bands, 3);
        for (int bands : { 16, 24, 32 })
            time ("oscillator + B shaper (MOTION 50 %), " + std::to_string (bands) + " bands", bands, 1);
        for (int bands : { 16, 24, 32 })
            time ("oscillator + C vocoder, " + std::to_string (bands) + " bands", bands, 2);
        // The analysis itself (offline, per recording / Atlas member).
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 5; ++i)
            (void) imprint::analyse (truth, ref.rootHz);
        report << "\nAnalysis of a 3 s recording: " << std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 5.0
               << " ms; descriptor " << profile.bytes() << " bytes (" << profile.frameCount() << " frames).\n";
        report << "\nLatency: A and B none (minimum-phase filters, the target set at the note's start); C follows its contour "
                  "with 3 ms attack / 30 ms release envelope followers.\n";
    }

    // 6. Listening renders: per reference, Analog and Imprint at SYNTH 25-100 %, IMPRINT 0/50/100,
    //    MOTION 0/30/70 (a chosen set, not every combination), plain and loudness-matched.
    std::ofstream index (dir / "listening-index.md");
    index << "# Listening index (stand-in references)\n\nEach file: reference, method, SYNTH %, IMPRINT %, MOTION %. "
             "`-matched` copies are loudness-matched (-20 dBFS RMS) for timbre; the plain files keep the real gain.\n\n";
    for (const auto& ref : refs)
    {
        const auto sample = ref.make (ref.rootHz);
        const auto profile = imprint::analyse (sample, ref.rootHz);
        save (ref.name + "-00-source", sample, false);
        index << "\n## " << ref.name << "\n\n- `" << ref.name << "-00-source`: the reference itself\n";
        struct Combo { Method m; int synthPct, imprintPct, motionPct; };
        const std::vector<Combo> combos {
            { Method::analog, 25, 0, 0 }, { Method::analog, 50, 0, 0 }, { Method::analog, 75, 0, 0 }, { Method::analog, 100, 0, 0 },
            { Method::staticImprint, 50, 50, 0 }, { Method::staticImprint, 50, 100, 0 }, { Method::staticImprint, 100, 100, 0 },
            { Method::staticImprint, 25, 100, 0 }, { Method::staticImprint, 75, 100, 0 },
            { Method::movingImprint, 50, 100, 30 }, { Method::movingImprint, 100, 100, 70 },
            { Method::vocoder, 50, 100, 0 }, { Method::vocoder, 100, 100, 0 },
        };
        int k = 1;
        for (const auto& c : combos)
        {
            RenderOptions o;
            o.method = c.m;
            o.imprint = c.imprintPct / 100.0;
            o.motion = c.motionPct / 100.0;
            o.seconds = std::max (3.0, sample.durationSeconds());
            const auto osc = renderOscillator (ref.rootHz, profile, sample, o);
            const auto mixed = blend (sample, osc, c.synthPct / 100.0);
            char name[128];
            std::snprintf (name, sizeof (name), "%s-%02d-%s-synth%03d-imprint%03d-motion%03d", ref.name.c_str(), k++, methodName (c.m), c.synthPct, c.imprintPct, c.motionPct);
            save (name, mixed, false);
            save (name, mixed, true);
            index << "- `" << name << "`\n";
        }
    }
    SUCCEED();
}
