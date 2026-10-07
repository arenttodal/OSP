#include "engine/MirageEngine.h"

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "core/PitchMath.h"

#include <cmath>
#include <numbers>

namespace osp
{

using reimagined::ramp;

void MirageEngine::prepare (double outputRate) noexcept
{
    rate = outputRate;
    filter.prepare (outputRate);
    envCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.03 * rate)));
    done = true;
}

bool MirageEngine::start (const ReimaginedNote& n, const ReimaginedControl& c) noexcept
{
    note = n;
    rng.reseed (Prng::deriveSeed (n.seed, 0x6d697261ull, 0));
    amount = std::clamp (c.amount, 0.0, 1.0);
    done = false;
    fade = 1.0;
    fadeStep = 0.0;
    clockPhase = 1.0;
    sumL = sumR = 0.0;
    sumCount = 0;
    heldL = heldR = 0.0f;
    envelope = 0.0f;
    filterEnv = 1.0;
    // The filter envelope falls back over 0.4-1.2 s (softer notes sooner).
    filterEnvCoef = std::exp (-32.0 / ((0.4 + 0.8 * n.velocity) * rate));
    wanderPhase = 2.0 * std::numbers::pi * rng.nextDouble();
    filter.reset();

    if (! n.granular)
    {
        const auto& src = *n.source;
        const double sr = src.sampleRate();
        const auto& cont = n.model->original.continuation;
        const auto frames = static_cast<double> (src.numFrames());
        soundEnd = std::clamp (frames - std::max (0.0, n.model->analysis.envelope.trailingSilenceSeconds) * sr, src.startFrame() + 1.0, frames);
        // Fixed loop points over the stable body, as on the machines this remembers.
        loops = cont.canSustain && cont.sustainEndFrame - cont.sustainStartFrame > 0.08 * sr;
        if (loops && cont.bestLoop >= 0)
        {
            const auto& j = cont.jumps[static_cast<std::size_t> (cont.bestLoop)];
            loopStart = j.toFrame;
            loopEnd = j.fromFrame;
            loopCorrelation = j.correlation;
        }
        else
        {
            loopStart = cont.sustainStartFrame;
            loopEnd = cont.sustainEndFrame;
            loopCorrelation = 0.0f;
        }
        direction = n.reverse ? -1.0 : 1.0;
        heads = { n.startFrame, n.startFrame };
    }
    control (c);
    amount = std::clamp (c.amount, 0.0, 1.0);
    return true;
}

void MirageEngine::control (const ReimaginedControl& c) noexcept
{
    amount = reimagined::glide (amount, std::clamp (c.amount, 0.0, 1.0));
    step = c.step;
    const auto& p = c.settings->mirage;

    // Clocking first, the filter's weight later, its aggression last.
    const double clock = (0.3 + 0.7 * std::clamp (p.clock, 0.0, 1.0)) * (0.65 * ramp (amount, 0.0, 0.35) + 0.35 * ramp (amount, 0.35, 1.0));
    const double body = ramp (amount, 0.15, 0.7);
    const double heavy = ramp (amount, 0.6, 0.95);
    const double fil = std::clamp (p.filter, 0.0, 1.0);

    const double srcRate = note.source->sampleRate();
    const double storeRate = srcRate * std::exp2 (-clock * std::log2 (std::max (1.0, srcRate / 9000.0)));
    period = srcRate / storeRate;
    hold = static_cast<float> (ramp (clock, 0.0, 0.25));
    // 16 bits down to about 8.5; soft passages a little coarser (more texture).
    const double bits = 16.0 - 7.5 * clock;
    const double softer = 1.0 + 0.8 * clock * (1.0 - std::clamp (4.0 * static_cast<double> (envelope), 0.0, 1.0));
    quantStep = bits < 15.5 ? static_cast<float> (std::exp2 (1.0 - bits) * softer) : 0.0f;

    // The filter follows the key (the note's frequency now), touch and its own envelope.
    const double speed = std::max (1.0e-3, step / std::max (1.0e-9, note.rootStep));
    const double noteHz = midiToHz (note.model->rootMidi) * speed;
    const double open = p.tone == MirageTone::open ? 1.6 : 0.0;
    filterEnv *= filterEnvCoef;
    wanderPhase += 2.0 * std::numbers::pi * 0.19 * 32.0 / rate;
    wander = 0.08 * body * (std::sin (wanderPhase) + 0.5 * std::sin (2.7 * wanderPhase + 1.3));
    const double keyed = std::log2 (std::max (40.0, noteHz)) + 2.6 + open - 1.6 * fil
                         + 2.2 * filterEnv * (0.4 + 0.6 * note.velocity) + 0.6 * (note.velocity - 0.7) + wander;
    const double octaves = std::log2 (19000.0) + (keyed - std::log2 (19000.0)) * body;   // open at low amounts
    const double resonance = body * (0.1 + 0.55 * fil + 0.1 * heavy);
    const double drive = body * (0.1 + 0.25 * fil) + heavy * (0.25 + 0.25 * fil) + 0.1 * body * (note.velocity - 0.7);
    filter.setParameters (FilterType::lp24, std::clamp (std::exp2 (octaves), 60.0, 0.45 * rate), std::clamp (resonance, 0.0, 0.8),
                          std::clamp (drive, 0.0, 0.9), 0.0);
}

float MirageEngine::boxAt (int channel, std::int64_t k) const noexcept
{
    const int m = std::max (1, static_cast<int> (std::ceil (period - 1.0e-6)));
    const auto first = static_cast<std::int64_t> (std::llround (static_cast<double> (k) * period - 0.5 * (m - 1)));
    const auto frames = note.source->numFrames();
    const float* data = note.source->channelData (channel);
    float sum = 0.0f;
    for (int i = 0; i < m; ++i)
        if (first + i >= 0 && first + i < frames)
            sum += data[first + i];
    return sum / static_cast<float> (m);
}

float MirageEngine::heldAt (int channel, double pos) const noexcept
{
    // The clock follows the read: a stored sample is held until the read moves on.
    const double x = pos / period;
    const double k = std::floor (x);
    const auto i = static_cast<std::int64_t> (k);
    const float s0 = boxAt (channel, i);
    if (hold >= 1.0f)
        return s0;
    const float s1 = boxAt (channel, i + 1);
    return s0 + (s1 - s0) * static_cast<float> (x - k) * (1.0f - hold);
}

void MirageEngine::render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept
{
    const bool stereo = note.source->numChannels() > 1;
    const double speed = std::max (1.0e-3, step / std::max (1.0e-9, note.rootStep));
    for (int i = 0; i < n; ++i)
    {
        if (done)
        {
            outL[i] = outR[i] = 0.0f;
            continue;
        }
        float l, r;
        if (note.granular)
        {
            // Grains through a clock that runs at the note's speed.
            sumL += dryL[i];
            sumR += dryR[i];
            ++sumCount;
            clockPhase += speed / period;
            if (clockPhase >= 1.0)
            {
                clockPhase -= std::floor (clockPhase);
                heldL = static_cast<float> (sumL / std::max (1, sumCount));
                heldR = static_cast<float> (sumR / std::max (1, sumCount));
                sumL = sumR = 0.0;
                sumCount = 0;
            }
            l = heldL;
            r = heldR;
        }
        else
        {
            l = heldAt (0, heads[0]);
            r = stereo ? heldAt (1, heads[0]) : l;
            if (fade < 1.0)
            {
                float gOut, gIn;
                crossfadeGains (static_cast<float> (fade), loopCorrelation, gOut, gIn);
                const float l1 = heldAt (0, heads[1]), r1 = stereo ? heldAt (1, heads[1]) : l1;
                l = l * gIn + l1 * gOut;
                r = r * gIn + r1 * gOut;
                heads[1] += direction * step;
                fade = std::min (1.0, fade + fadeStep);
            }
            heads[0] += direction * step;
            if (loops && fade >= 1.0)
            {
                // Loop points (crossfaded, 15 ms): forwards past the end, backwards past the start.
                if (direction > 0.0 && heads[0] >= loopEnd)
                {
                    heads[1] = heads[0];
                    heads[0] = loopStart + (heads[0] - loopEnd);
                    fade = 0.0;
                    fadeStep = 1.0 / (0.015 * rate);
                }
                else if (direction < 0.0 && heads[0] <= loopStart && heads[0] > loopStart - 4.0 * step)
                {
                    heads[1] = heads[0];
                    heads[0] = loopEnd - (loopStart - heads[0]);
                    fade = 0.0;
                    fadeStep = 1.0 / (0.015 * rate);
                }
            }
            if (direction > 0.0 ? heads[0] >= soundEnd : heads[0] <= 0.0)
                done = true;
        }
        envelope += envCoef * (0.5f * std::abs (l + r) - envelope);
        if (quantStep > 0.0f)
        {
            l = std::round (l / quantStep) * quantStep;
            r = std::round (r / quantStep) * quantStep;
        }
        filter.process (l, r);
        outL[i] = l;
        outR[i] = r;
    }
}

} // namespace osp
