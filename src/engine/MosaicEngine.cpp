#include "engine/MosaicEngine.h"

#include <cmath>
#include <numbers>

namespace osp
{

using reimagined::ramp;

void MosaicEngine::prepare (double outputRate) noexcept
{
    rate = outputRate;
    // Residual bands: constant-peak band-passes, normalised so white noise in comes out at
    // unit variance (the analysis measured each band's noise as a variance).
    for (int b = 0; b < bands; ++b)
    {
        const double lo = b == 0 ? 40.0 : ReimaginedAnalysis::residualEdgesHz[static_cast<std::size_t> (b - 1)];
        const double hi = std::min (ReimaginedAnalysis::residualEdgesHz[static_cast<std::size_t> (b)], 0.45 * rate);
        noiseNorm[static_cast<std::size_t> (b)] = 0.0f;
        if (hi <= lo * 1.05)
            continue;
        const double fc = std::sqrt (lo * hi);
        const double q = fc / (hi - lo);
        const double w = 2.0 * std::numbers::pi * fc / rate;
        const double alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        Biquad f;
        f.b0 = static_cast<float> (alpha / a0);
        f.b1 = 0.0f;
        f.b2 = static_cast<float> (-alpha / a0);
        f.a1 = static_cast<float> (-2.0 * std::cos (w) / a0);
        f.a2 = static_cast<float> ((1.0 - alpha) / a0);
        // Impulse-response energy: output variance for unit-variance white noise.
        Biquad probe = f;
        double energy = 0.0;
        for (int i = 0; i < 16384; ++i)
        {
            const float y = probe.process (i == 0 ? 1.0f : 0.0f);
            energy += static_cast<double> (y) * y;
        }
        // Uniform noise in -1..1 has variance 1/3.
        noiseNorm[static_cast<std::size_t> (b)] = energy > 1.0e-12 ? static_cast<float> (1.0 / std::sqrt (energy / 3.0)) : 0.0f;
        noise[static_cast<std::size_t> (b)] = f;
        noise[static_cast<std::size_t> (b + bands)] = f;
    }
    done = true;
}

double MosaicEngine::naturalFrame (double t) const noexcept
{
    const auto& frames = mosaic->frames;
    const auto count = static_cast<int> (frames.size());
    const double total = frames.back().seconds;
    if (note.reverse)
        t = total - t;
    if (t <= frames.front().seconds)
        return 0.0;
    for (int k = 0; k + 1 < count; ++k)
    {
        const double s0 = frames[static_cast<std::size_t> (k)].seconds, s1 = frames[static_cast<std::size_t> (k + 1)].seconds;
        if (t < s1)
            return k + (t - s0) / std::max (1.0e-6, s1 - s0);
    }
    return count - 1;
}

float MosaicEngine::frameValue (double pos, int partial) const noexcept
{
    const auto& frames = mosaic->frames;
    const int last = static_cast<int> (frames.size()) - 1;
    const int p = std::clamp (partial, 0, partials - 1);
    pos = std::clamp (pos, 0.0, static_cast<double> (last));
    const int i = std::min (static_cast<int> (pos), std::max (0, last - 1));
    const auto f = static_cast<float> (pos - i);
    const float a = frames[static_cast<std::size_t> (i)].partial[static_cast<std::size_t> (p)];
    const float b = frames[static_cast<std::size_t> (std::min (i + 1, last))].partial[static_cast<std::size_t> (p)];
    return a + (b - a) * f;
}

bool MosaicEngine::start (const ReimaginedNote& n, const ReimaginedControl& c) noexcept
{
    note = n;
    mosaic = n.analysis != nullptr && n.analysis->mosaic.ready && n.analysis->mosaic.frames.size() >= 2 ? &n.analysis->mosaic : nullptr;
    if (mosaic == nullptr)
        return false;   // nothing to rebuild from: the voice plays the recording
    rng.reseed (Prng::deriveSeed (n.seed, 0x6d6f73ull, 0));
    for (int h = 0; h < partials; ++h)
    {
        const double phase = 2.0 * std::numbers::pi * rng.nextDouble();
        re[static_cast<std::size_t> (h)] = static_cast<float> (std::cos (phase));
        im[static_cast<std::size_t> (h)] = static_cast<float> (std::sin (phase));
        amp[static_cast<std::size_t> (h)] = 0.0f;
        detune[static_cast<std::size_t> (h)] = static_cast<float> (rng.bipolar());
    }
    oddCount = evenCount = 0;
    for (auto& f : noise)
        f.s1 = f.s2 = 0.0f;
    noiseLevel.fill (0.0f);
    emphasisPhase = 2.0 * std::numbers::pi * rng.nextDouble();
    seconds = 0.0;
    position = naturalFrame (0.0);
    mix = 0.0f;
    done = false;
    amount = std::clamp (c.amount, 0.0, 1.0);
    control (c);
    return true;
}

void MosaicEngine::control (const ReimaginedControl& c) noexcept
{
    amount = reimagined::glide (amount, std::clamp (c.amount, 0.0, 1.0));
    step = c.step;
    const auto& p = c.settings->mosaic;
    const double dt = 32.0 / rate;
    seconds += dt;
    const auto& frames = mosaic->frames;
    const int last = static_cast<int> (frames.size()) - 1;

    // How much of the sound is rebuilt: the recording's own attack stays at the front.
    const double attackHold = 0.12 - 0.085 * amount;
    const double mixTarget = (0.6 * ramp (amount, 0.0, 0.25) + 0.4 * ramp (amount, 0.25, 0.6)) * ramp (seconds, 0.0, attackHold);
    mixStep = (static_cast<float> (mixTarget) - mix) / 32.0f;

    // Where in the recording's evolution the spectrum is: the attack as recorded, then
    // MOTION between one stable frame and travelling the body (ping-pong when it sustains).
    // LOOP on holds a sustaining sound; LOOP off lets it run its course and end. REVERSE
    // runs the evolution backwards: held, it ping-pongs the body once it gets there (never
    // reaching the attack); not held, it ends on the attack.
    const bool holds = mosaic->sustains && note.loop;
    const double travel = std::clamp (p.motion, 0.0, 1.0) * (0.4 + 0.6 * ramp (amount, 0.55, 0.85));
    const double natural = naturalFrame (seconds);
    double target = natural;
    const bool inBody = note.reverse ? holds && natural <= mosaic->bodyFrame : natural >= mosaic->bodyFrame;
    if (inBody)
    {
        const double body = mosaic->bodyFrame, span = last - body;
        double wander = body;
        if (span > 0.0)
        {
            const double bodySeconds = frames[static_cast<std::size_t> (mosaic->bodyFrame)].seconds;
            // Time in the body: forwards from when the recording reaches it, backwards from
            // when the reversed evolution does.
            const double inside = note.reverse ? seconds - (frames.back().seconds - bodySeconds) : seconds - bodySeconds;
            const double d = inside / std::max (1.0e-3, (frames.back().seconds - bodySeconds) / span);
            const double phase = std::fmod (std::max (0.0, d), 2.0 * span);
            wander = holds ? body + (phase <= span ? phase : 2.0 * span - phase) : std::min (natural, static_cast<double> (last));
        }
        target = mosaic->stableFrame + (wander - mosaic->stableFrame) * travel;
    }
    position += (target - position) * 0.03;

    // A sound that decays keeps decaying (its loudness follows the recording in time,
    // whatever spectrum it is on); one that sustains holds while LOOP is on.
    auto levelAt = [&] (double pos) {
        pos = std::clamp (pos, 0.0, static_cast<double> (last));
        const int i = std::min (static_cast<int> (pos), std::max (0, last - 1));
        const double f = pos - i;
        return (1.0 - f) * frames[static_cast<std::size_t> (i)].level + f * frames[static_cast<std::size_t> (std::min (i + 1, last))].level;
    };
    double levelGain = 1.0, tail = 1.0;
    if (! holds)
    {
        const double over = seconds - frames.back().seconds;
        tail = over > 0.0 ? std::max (0.0, 1.0 - over / 0.3) : 1.0;
        levelGain = std::min (4.0, tail * levelAt (natural) / std::max (1.0e-6, levelAt (position)));
        done = over > 0.3;
    }

    const bool textured = p.model == MosaicModel::textured;
    const double detail = std::clamp (p.detail, 0.0, 1.0);
    const double count = 8.0 + 40.0 * detail;
    const double smoothing = 0.7 * (1.0 - detail);
    const double deep = ramp (amount, 0.85, 1.0);
    const double tilt = 0.2 * (note.velocity - 0.7);
    const double inharmonic = mosaic->inharmonicity * (textured ? 1.0 : 0.3);
    const double srcRate = note.source->sampleRate();
    const int i0 = std::min (static_cast<int> (std::clamp (position, 0.0, static_cast<double> (last))), std::max (0, last - 1));
    const double fr = std::clamp (position - i0, 0.0, 1.0);
    const double f0Ratio = (1.0 - fr) * frames[static_cast<std::size_t> (i0)].f0Ratio + fr * frames[static_cast<std::size_t> (std::min (i0 + 1, last))].f0Ratio;
    const double f0 = mosaic->fundamentalHz * f0Ratio * step * rate / srcRate;
    emphasisPhase += 2.0 * std::numbers::pi * 0.07 * dt;
    oddCount = evenCount = 0;
    for (int h = 1; h <= partials; ++h)
    {
        const auto index = slotOf (h);
        double a = 0.0;
        const double f = h * f0 * std::sqrt (1.0 + inharmonic * h * h)
                         * std::exp2 ((textured ? 1.5 : 0.3) * (0.5 + 0.5 * deep) * detune[index] / 1200.0);
        if (h <= count + 1.0 && f < 0.45 * rate)
        {
            const double raw = frameValue (position, h - 1);
            double smooth = 0.0;
            for (int k = -2; k <= 2; ++k)
                smooth += frameValue (position, std::clamp (h - 1 + k, 0, partials - 1));
            a = (raw + (0.2 * smooth - raw) * smoothing) * levelGain * std::pow (static_cast<double> (h), tilt);
            a *= std::max (0.2, 1.0 + deep * 0.8 * std::sin (2.0 * std::numbers::pi * h / 9.0 + emphasisPhase));
            a *= std::clamp (count + 1.0 - h, 0.0, 1.0);                    // soft edge of the partial count
            a *= 1.0 - ramp (f, 0.38 * rate, 0.45 * rate);                  // never alias
        }
        const double w = 2.0 * std::numbers::pi * f / rate;
        rotRe[index] = static_cast<float> (std::cos (w));
        rotIm[index] = static_cast<float> (std::sin (w));
        ampStep[index] = (static_cast<float> (a) - amp[index]) / 32.0f;
        if (a > 0.0 || amp[index] != 0.0f)
            ((h & 1) != 0 ? oddCount : evenCount) = static_cast<int> (index - ((h & 1) != 0 ? 0 : half)) + 1;
        const float mag = std::sqrt (re[index] * re[index] + im[index] * im[index]);
        if (mag > 1.0e-6f)
        {
            re[index] /= mag;
            im[index] /= mag;
        }
    }

    // The residual: noise in six bands, as measured between the partials.
    const double residual = (textured ? 1.0 : 0.12) * (0.8 + 0.5 * note.velocity);
    // A sustaining sound breathes on the spectrum it holds; a decaying one keeps the noise
    // it had at that moment of the recording (an attack's burst stays at the attack).
    const double noiseAt = holds ? position : std::clamp (natural, 0.0, static_cast<double> (last));
    const int n0 = std::min (static_cast<int> (noiseAt), std::max (0, last - 1));
    const double nf = noiseAt - n0;
    for (int b = 0; b < bands; ++b)
    {
        const auto bi = static_cast<std::size_t> (b);
        const double v = (1.0 - nf) * frames[static_cast<std::size_t> (n0)].residual[bi] + nf * frames[static_cast<std::size_t> (std::min (n0 + 1, last))].residual[bi];
        noiseStep[bi] = (static_cast<float> (v * residual * (holds ? levelGain : tail)) * noiseNorm[bi] - noiseLevel[bi]) / 32.0f;
    }
}

void MosaicEngine::render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept
{
    if (mix <= 0.0f && mixStep <= 0.0f)
    {
        // Nothing rebuilt (yet): the recording as it is.
        for (int i = 0; i < n; ++i)
        {
            outL[i] = dryL[i];
            outR[i] = dryR[i];
        }
        mix = 0.0f;
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        auto advance = [this] (int from, int to) {
            for (int k = from; k < to; ++k)
            {
                const float x = re[static_cast<std::size_t> (k)] * rotRe[static_cast<std::size_t> (k)] - im[static_cast<std::size_t> (k)] * rotIm[static_cast<std::size_t> (k)];
                const float y = re[static_cast<std::size_t> (k)] * rotIm[static_cast<std::size_t> (k)] + im[static_cast<std::size_t> (k)] * rotRe[static_cast<std::size_t> (k)];
                re[static_cast<std::size_t> (k)] = x;
                im[static_cast<std::size_t> (k)] = y;
                amp[static_cast<std::size_t> (k)] += ampStep[static_cast<std::size_t> (k)];
                out[static_cast<std::size_t> (k)] = amp[static_cast<std::size_t> (k)] * y;
            }
        };
        auto sum = [this] (int from, int to) {
            float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
            int k = from;
            for (; k + 3 < to; k += 4)
            {
                s0 += out[static_cast<std::size_t> (k)];
                s1 += out[static_cast<std::size_t> (k + 1)];
                s2 += out[static_cast<std::size_t> (k + 2)];
                s3 += out[static_cast<std::size_t> (k + 3)];
            }
            for (; k < to; ++k)
                s0 += out[static_cast<std::size_t> (k)];
            return (s0 + s1) + (s2 + s3);
        };
        advance (0, oddCount);
        advance (half, half + evenCount);
        const float odd = sum (0, oddCount), even = sum (half, half + evenCount);
        // A little width: odd and even partials lean apart.
        float l = 0.92f * (odd + 0.85f * even), r = 0.92f * (0.85f * odd + even);
        const auto uL = static_cast<float> (rng.bipolar()), uR = static_cast<float> (rng.bipolar());
        for (int b = 0; b < bands; ++b)
        {
            const auto bi = static_cast<std::size_t> (b);
            noiseLevel[bi] += noiseStep[bi];
            if (noiseLevel[bi] <= 0.0f)
                continue;
            l += noise[bi].process (uL * noiseLevel[bi]);
            r += noise[bi + bands].process (uR * noiseLevel[bi]);
        }
        mix = std::clamp (mix + mixStep, 0.0f, 1.0f);
        outL[i] = dryL[i] + (l - dryL[i]) * mix;
        outR[i] = dryR[i] + (r - dryR[i]) * mix;
    }
}

} // namespace osp
