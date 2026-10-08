#include "engine/ToyboxEngine.h"

#include <cmath>

namespace osp
{

using reimagined::ramp;

void ToyboxEngine::prepare (double outputRate) noexcept
{
    rate = outputRate;
    fastCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.015 * rate)));
    slowCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.3 * rate)));
    done = true;
}

bool ToyboxEngine::start (const ReimaginedNote& n, const ReimaginedControl& c) noexcept
{
    note = n;
    rng.reseed (Prng::deriveSeed (n.seed, 0x746f79ull, 0));
    amount = std::clamp (c.amount, 0.0, 1.0);
    done = false;
    fade = 1.0;
    fadeStep = 0.0;
    turnPending = false;
    dacPhase = 1.0;   // the first sample is a DAC tick
    heldL = heldR = 0.0f;
    sumL = sumR = 0.0;
    sumCount = 0;
    fast = slow = 0.0f;
    gainMod = 1.0f;
    factor = 1.0;
    historyWrite = historyCount = 0;
    for (auto& f : post)
        f.reset();
    // (At very high sample rates the far tap comes a little closer: the history is bounded.)
    tapPeriods = { std::min (historySize - 2, static_cast<int> (rng.uniform (0.045, 0.070) * rate / 32.0)),
                   std::min (historySize - 2, static_cast<int> (rng.uniform (0.110, 0.150) * rate / 32.0)) };

    if (! n.granular)
    {
        const auto& src = *n.source;
        const double sr = src.sampleRate();
        const auto& cont = n.model->original.continuation;
        const auto frames = static_cast<double> (src.numFrames());
        soundEnd = std::clamp (frames - std::max (0.0, n.model->analysis.envelope.trailingSilenceSeconds) * sr, src.startFrame() + 1.0, frames);
        sustains = n.loop && cont.canSustain && cont.sustainEndFrame - cont.sustainStartFrame > 0.08 * sr;
        if (sustains)
        {
            regionStart = cont.sustainStartFrame;
            regionEnd = cont.sustainEndFrame;
        }
        else
        {
            // Nothing stable (or LOOP off): turn over what follows the attack, on to the end.
            regionStart = std::min (soundEnd - 1.0, src.startFrame() + (n.model->analysis.envelope.attackSeconds + 0.05) * sr);
            regionEnd = soundEnd;
        }
        noteDir = n.reverse ? -1.0 : 1.0;
        heads[0] = { n.startFrame, noteDir };
        heads[1] = heads[0];
    }
    control (c);
    amount = std::clamp (c.amount, 0.0, 1.0);
    if (! n.granular)
        legEnd = heads[0].dir > 0.0 ? std::max (heads[0].pos, regionStart) + legLength : std::min (heads[0].pos, regionEnd) - legLength;
    return true;
}

void ToyboxEngine::control (const ReimaginedControl& c) noexcept
{
    amount = reimagined::glide (amount, std::clamp (c.amount, 0.0, 1.0));
    step = c.step;
    const auto& p = c.settings->toybox;
    play = p.play;

    // Early amount = digital character; turning and self-modulation come later.
    const double early = ramp (amount, 0.0, 0.3);
    const double dig = std::clamp ((0.25 + 0.75 * std::clamp (p.digital, 0.0, 1.0)) * (0.6 * early + 0.4 * ramp (amount, 0.3, 1.0))
                                       + 0.1 * (note.velocity - 0.7) * early,
                                   0.0, 1.0);
    const double srcRate = note.granular ? rate : note.source->sampleRate();
    const double storeRate = srcRate * std::exp2 (-dig * std::log2 (std::max (1.0, srcRate / 7000.0)));
    period = srcRate / storeRate;
    dacIncrement = storeRate / rate;
    const double bits = 16.0 - 8.0 * dig;
    quantise = bits < 15.5;
    levels = static_cast<float> (std::exp2 (bits - 1.0));
    nearest = static_cast<float> (ramp (dig, 0.4, 0.9));
    for (auto& f : post)
        f.setCutoff (std::min (0.45 * rate, 0.62 * storeRate), rate);

    const double m = std::clamp (p.motion, 0.0, 1.0);
    motion = m * ramp (amount, 0.25, 0.75);
    legLength = 1.2 * std::exp2 (-4.0 * motion) * srcRate;
    const double echo = ramp (amount, 0.25, 0.55) * (0.5 + 0.5 * m);
    tapLevel = { static_cast<float> (0.32 * echo), static_cast<float> (0.16 * echo) };
    selfMod = m * ramp (amount, 0.5, 0.9) * (play == ToyboxPlay::chaos ? 1.4 : 1.0);

    // The sound's own envelope (fast against slow) bends its rate and level.
    const float dev = std::clamp ((fast - slow) / (slow + 1.0e-4f), -1.0f, 1.0f);
    factor = std::exp2 (22.0 * selfMod * dev / 1200.0);
    gainMod = static_cast<float> (1.0 + 0.3 * selfMod * dev);

    if (! note.granular)
    {
        history[static_cast<std::size_t> (historyWrite)] = heads[0].pos;
        historyWrite = (historyWrite + 1) % historySize;
        historyCount = std::min (historyCount + 1, historySize);
    }
}

float ToyboxEngine::boxAt (int channel, std::int64_t k) const noexcept
{
    // The primitive anti-alias filter: the average over one stored-sample period.
    const int m = std::max (1, static_cast<int> (std::ceil (period - 1.0e-6)));
    const auto first = static_cast<std::int64_t> (std::llround (static_cast<double> (k) * period - 0.5 * (m - 1)));
    const auto frames = note.source->numFrames();
    const float* data = note.source->channelData (channel);
    float sum = 0.0f;
    for (int i = 0; i < m; ++i)
    {
        const auto index = first + i;
        if (index >= 0 && index < frames)
            sum += data[index];
    }
    return sum / static_cast<float> (m);
}

float ToyboxEngine::storedAt (int channel, double pos) const noexcept
{
    const double x = pos / period;
    const double k = std::floor (x);
    const auto f = static_cast<float> (x - k);
    const auto i = static_cast<std::int64_t> (k);
    const float s0 = boxAt (channel, i), s1 = boxAt (channel, i + 1);
    return s0 + (s1 - s0) * f * (1.0f - nearest);
}

void ToyboxEngine::turn (double newPos, double newDir, double seconds) noexcept
{
    heads[1] = heads[0];
    heads[0] = { newPos, newDir };
    fade = 0.0;
    fadeStep = 1.0 / std::max (1.0, seconds * rate);
    turnPending = false;
}

void ToyboxEngine::requestTurn (double legScale) noexcept
{
    turnPending = true;
    pendingLeg = legScale;
    turnWait = static_cast<int> (0.02 * rate);   // an extremum comes within a period of anything audible
    lastValue = reimagined::readHermite (*note.source, 0, heads[0].pos);
    lastSlope = 0.0f;
}

void ToyboxEngine::reverseNow (bool atExtremum) noexcept
{
    auto& h = heads[0];
    if (atExtremum)
        h.dir = -h.dir;
    else
    {
        // No extremum found (a very low or noisy sound): a short crossfade instead.
        heads[1] = h;
        h.dir = -h.dir;
        fade = 0.0;
        fadeStep = 1.0 / (0.006 * rate);
    }
    turnPending = false;
    // Legs the way the note plays are longer, so the region walks with it: slowly while the
    // body sustains, briskly with LOOP off (about half speed: the note plays through and ends).
    const double along = sustains ? (play != ToyboxPlay::chaos ? 1.3 : 1.0) : 2.5;
    const double leg = legLength * pendingLeg * (h.dir == noteDir ? along : 1.0);
    legEnd = std::clamp (h.pos + h.dir * leg, lowBound(), highBound());
}

void ToyboxEngine::planLeg() noexcept
{
    auto& h = heads[0];
    const double srcRate = note.source->sampleRate();
    // The walk reached the far end of the body: a sustaining sound starts the body again
    // (backwards: from its end).
    if (sustains && h.dir == noteDir && (noteDir > 0.0 ? h.pos >= regionEnd - 1.0 : h.pos <= regionStart + 1.0))
    {
        turn (noteDir > 0.0 ? regionStart + 0.01 * srcRate : regionEnd - 0.01 * srcRate, noteDir, 0.02);
        legEnd = heads[0].pos + noteDir * legLength;
        return;
    }
    if (play == ToyboxPlay::chaos)
    {
        // Irregular, never random noise: legs of different lengths, an occasional skip
        // ahead in the same direction, now and then a jump elsewhere in the region.
        const double pick = rng.nextDouble();
        const double scale = rng.uniform (0.4, 1.6);
        if (pick < 0.15 && regionEnd - regionStart > 2.0 * legLength)
        {
            // Held, anywhere in the body; LOOP off, only onwards (the way the note plays).
            double from = regionStart, to = regionEnd - legLength;
            if (! sustains)
                (noteDir > 0.0 ? from : to) = std::clamp (h.pos, from, to);
            turn (rng.uniform (from, to), rng.nextDouble() < 0.5 ? -1.0 : 1.0, 0.012);
            legEnd = std::clamp (heads[0].pos + heads[0].dir * legLength * scale, lowBound(), highBound());
        }
        else if (pick < 0.75)
            requestTurn (scale);
        else
            legEnd = std::clamp (h.pos + h.dir * legLength * scale, lowBound(), highBound());
    }
    else
        requestTurn (1.0);   // a pendulum whose region walks slowly through the sound (legs the note's way are longer)
}

void ToyboxEngine::render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept
{
    const double readStep = step * factor;
    const bool stereo = ! note.granular && note.source->numChannels() > 1;
    for (int i = 0; i < n; ++i)
    {
        if (done)
        {
            outL[i] = outR[i] = 0.0f;
            continue;
        }
        if (note.granular)
        {
            sumL += dryL[i];
            sumR += dryR[i];
            ++sumCount;
        }
        dacPhase += dacIncrement;
        if (dacPhase >= 1.0)
        {
            dacPhase -= std::floor (dacPhase);
            float l, r;
            if (note.granular)
            {
                l = static_cast<float> (sumL / std::max (1, sumCount));
                r = static_cast<float> (sumR / std::max (1, sumCount));
                sumL = sumR = 0.0;
                sumCount = 0;
            }
            else
            {
                const auto f = static_cast<float> (fade);
                l = storedAt (0, heads[0].pos);
                r = stereo ? storedAt (1, heads[0].pos) : l;
                if (f < 1.0f)
                {
                    const float l1 = storedAt (0, heads[1].pos), r1 = stereo ? storedAt (1, heads[1].pos) : l1;
                    const float gIn = std::sin (1.5707963f * f), gOut = std::cos (1.5707963f * f);
                    l = l * gIn + l1 * gOut;
                    r = r * gIn + r1 * gOut;
                }
                // Memory taps: fragments of what the head played a moment ago.
                for (std::size_t t = 0; t < tapLevel.size(); ++t)
                    if (tapLevel[t] > 0.0f && historyCount > tapPeriods[t])
                    {
                        const double pos = history[static_cast<std::size_t> ((historyWrite - 1 - tapPeriods[t] + historySize) % historySize)];
                        const float tl = storedAt (0, pos);
                        l += tapLevel[t] * tl;
                        r += tapLevel[t] * (stereo ? storedAt (1, pos) : tl);
                    }
            }
            if (quantise)
            {
                l = std::round (l * levels) / levels;
                r = std::round (r * levels) / levels;
            }
            heldL = l * gainMod;
            heldR = r * gainMod;
        }
        const float m = 0.5f * std::abs (heldL + heldR);
        fast += fastCoef * (m - fast);
        slow += slowCoef * (m - slow);
        outL[i] = post[1].process (post[0].process (heldL));
        outR[i] = post[3].process (post[2].process (heldR));

        if (note.granular)
            continue;
        heads[0].pos += heads[0].dir * readStep;
        if (fade < 1.0)
        {
            heads[1].pos += heads[1].dir * readStep;
            fade = std::min (1.0, fade + fadeStep);
        }
        auto& h = heads[0];
        if (turnPending)
        {
            const float v = reimagined::readHermite (*note.source, 0, h.pos);
            const float slope = v - lastValue;
            if (slope * lastSlope < 0.0f)
                reverseNow (true);
            else if (--turnWait <= 0)
                reverseNow (false);
            lastValue = v;
            lastSlope = slope;
        }
        else if (motion > 1.0e-3 && play != ToyboxPlay::forward)
        {
            // (Not gated on being inside the region: a fast head can step past its edge.)
            const bool inRegion = h.pos >= lowBound() && h.pos <= highBound();
            if (fade >= 1.0 && (h.dir > 0.0 ? h.pos >= legEnd : h.pos <= legEnd))
                planLeg();
            else if (! inRegion && h.dir != noteDir && fade >= 1.0)
                requestTurn (1.0);   // never back into the attack (REVERSE: never forward out of the body)
        }
        else if (sustains && fade >= 1.0)
        {
            // FWD: a sustaining body loops (crossfaded), like a sampler's loop points.
            if (h.dir > 0.0 && h.pos >= regionEnd)
                turn (regionStart + (h.pos - regionEnd), 1.0, 0.02);
            else if (h.dir < 0.0 && h.pos <= regionStart)
                turn (regionEnd - (regionStart - h.pos), -1.0, 0.02);
        }
        // The note ends where it plays to (a swing against it turns at its legEnd instead).
        if (h.dir == noteDir && (noteDir > 0.0 ? h.pos >= soundEnd : h.pos <= 0.0))
            done = true;
        else if (h.pos >= soundEnd + 0.05 * note.source->sampleRate() || h.pos <= -0.05 * note.source->sampleRate())
            done = true;   // (never wanders off the recording for long)
    }
}

} // namespace osp
