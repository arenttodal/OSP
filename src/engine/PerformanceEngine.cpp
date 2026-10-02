#include "engine/PerformanceEngine.h"

#include <algorithm>
#include <cmath>

namespace osp
{

void PerformanceEngine::reset (std::uint64_t seed) noexcept
{
    baseSeed = seed;
    rng.reseed (Prng::deriveSeed (seed, 0x706572, 0));
    // The player's starting state is itself part of the performance (seeded, not zero).
    latentForce = 0.5 * rng.gaussian();
    latentColour = 0.5 * rng.gaussian();
    latentTiming = 0.5 * rng.gaussian();
    lastTime = -1.0;
    lastNote = -1;
    repeats = 0;
    lastInterval = 10.0;
    alternation = 1;
}

namespace
{
    /** How each LIFE mode plays (NATURAL is the calibrated player: all 1). */
    struct ModeShape
    {
        double latent, jitter, timing, memory, settle, tail;
    };

    ModeShape modeShape (LifeMode mode) noexcept
    {
        switch (mode)
        {
            case LifeMode::loose: return { 0.55, 1.5, 1.8, 1.2, 1.2, 2.5 };   // less consistent, looser timing, short memory
            case LifeMode::fray: return { 0.9, 1.15, 1.3, 3.0, 2.0, 3.5 };    // heavier tails plus frayed edges
            case LifeMode::natural: break;
        }
        return { 1.0, 1.0, 1.0, PerformanceEngine::memorySeconds, 1.0, 2.5 };
    }
}

double PerformanceEngine::Variation::distance (const Variation& o) const noexcept
{
    // What a listener compares between two plays: level, brightness, attack, pitch, timing.
    const double d[] = { gain - o.gain, bright - o.bright, transient - o.transient, pitch - o.pitch, start - o.start };
    double sum = 0.0;
    for (double x : d)
        sum += x * x;
    return std::sqrt (sum / static_cast<double> (std::size (d)));
}

PerformanceEngine::Variation PerformanceEngine::Variation::scaled (double pitchScale, double toneScale, double attackScale) const noexcept
{
    Variation v = *this;
    v.bright *= toneScale;
    v.body *= toneScale;
    v.transient *= attackScale;
    v.start *= attackScale;
    v.pitch *= pitchScale;
    v.settle *= pitchScale;
    return v;
}

void PerformanceEngine::advance (double timeSeconds, double memory) noexcept
{
    // Ornstein-Uhlenbeck step over the elapsed time: correlation decays with
    // exp(-dt / tau); the stationary variance stays 1.
    const double dt = lastTime < 0.0 ? memory : std::max (0.0, timeSeconds - lastTime);
    const double a = std::exp (-dt / memory);
    const double b = std::sqrt (std::max (0.0, 1.0 - a * a));
    latentForce = a * latentForce + b * rng.gaussian();
    latentColour = a * latentColour + b * rng.gaussian();
    latentTiming = a * latentTiming + b * rng.gaussian();
}

void PerformanceEngine::perform (NoteShape& shape, int note, int /*velocity*/, double timeSeconds, std::uint64_t eventIndex,
                                 const PerformanceProfile& p, const SourceCharacter& c, double life, const Shaping& settings) noexcept
{
    const auto mode = modeShape (settings.lifeMode);

    // Repetition awareness (spec §32).
    const double interval = lastTime < 0.0 ? 10.0 : timeSeconds - lastTime;
    if (note == lastNote && interval < 1.5)
    {
        ++repeats;
        alternation = -alternation;
    }
    else
    {
        repeats = 0;
        alternation = 1;
        previousCount = 0;
    }
    advance (timeSeconds, mode.memory);
    lastTime = timeSeconds;
    lastNote = note;
    lastInterval = interval;

    life = std::clamp (life, 0.0, 1.0);
    if (life <= 0.0)
        return; // LIFE = 0: the recorded performance, identical every time

    // Note-level randomness comes from its own stream so the latent path does not
    // depend on how many draws a note uses.
    Prng noteRng (Prng::deriveSeed (baseSeed, eventIndex, static_cast<std::uint64_t> (note) + 0x6e6f7465));
    auto jitter = [&noteRng, &mode] { return std::clamp (noteRng.gaussian(), -mode.tail, mode.tail); };

    // 0.5 -> realistic (1x the calibrated spread), 1 -> creative (2x plus reinterpretation).
    const double scale = life / 0.5;
    const double fast = std::clamp ((0.25 - interval) / 0.2, 0.0, 1.0); // 1 for very fast repeats
    const double repeated = repeats > 0 ? 1.0 : 0.0;
    // The popup's PITCH (cents), TONE and ATTACK, relative to their calibrated defaults.
    const double pitchScale = std::max (0.0, settings.lifePitchCents) / 4.0;
    const double toneScale = std::max (0.0, settings.lifeTone) / 0.3;
    const double attackScale = std::max (0.0, settings.lifeAttack) / 0.25;

    const double f = std::clamp (latentForce, -2.5, 2.5);
    const double col = std::clamp (latentColour, -2.5, 2.5);
    const double tim = std::clamp (latentTiming, -2.5, 2.5);
    const double alt = repeated * static_cast<double> (alternation) * (0.5 + 0.5 * fast);
    const double lw = mode.latent, jw = mode.jitter;

    // Correlated outputs (spec §30): force drives level, brightness, transient, initial
    // pitch and (inversely) damping; colour moves brightness against body; timing moves
    // start and micro-pitch.
    auto draw = [&] {
        Variation v;
        v.gain = lw * 0.75 * f + jw * 0.35 * jitter();
        v.bright = lw * (0.5 * f + 0.5 * col) + jw * 0.25 * jitter() + 0.3 * alt;
        v.body = lw * (0.4 * f - 0.4 * col) + jw * 0.3 * jitter();
        v.transient = lw * 0.6 * f + 0.6 * alt * (1.0 + fast) + jw * 0.3 * jitter();
        v.pitch = lw * 0.6 * tim + jw * 0.4 * jitter();
        v.settle = 0.5 + lw * 0.5 * f + jw * 0.3 * jitter();
        v.damping = -lw * 0.6 * f + jw * 0.4 * jitter();
        v.start = std::abs (lw * 0.6 * tim + jw * 0.4 * jitter()) * mode.timing;
        v.pan = jw * 0.7 * jitter() + lw * 0.3 * col;
        return v;
    };
    Variation v = draw();

    // Repeat guard: a repeated note must not sound like either of its last two plays
    // (near-identical repeats or an A-B-A-B loop). Re-draw its own part, keep the best.
    if (repeated > 0.0 && previousCount > 0)
    {
        auto closest = [this, pitchScale, toneScale, attackScale] (const Variation& candidate) {
            const auto heard = candidate.scaled (pitchScale, toneScale, attackScale);
            double d = 1.0e9;
            for (int i = 0; i < previousCount; ++i)
                d = std::min (d, heard.distance (previous[static_cast<std::size_t> (i)]));
            return d;
        };
        double best = closest (v);
        if (best < minimumDistance)
        {
            ++guarded;
            for (int attempt = 0; attempt < 8 && best < minimumDistance; ++attempt)
            {
                const auto candidate = draw();
                const double d = closest (candidate);
                if (d > best)
                {
                    best = d;
                    v = candidate;
                }
            }
        }
    }
    previous[1] = previous[0];
    previous[0] = v.scaled (pitchScale, toneScale, attackScale);
    previousCount = std::min (2, previousCount + 1);

    shape.gain *= static_cast<float> (std::pow (10.0, scale * p.gainDb * v.gain / 20.0));
    shape.brightnessDb += static_cast<float> (scale * p.brightnessDb * v.bright * toneScale);
    shape.bodyDb += static_cast<float> (scale * p.bodyDb * v.body * toneScale);
    shape.transientDb += static_cast<float> (scale * p.transientDb * v.transient * attackScale);
    const double pitchDrift = 1.0 - 0.5 * fast; // fast repeats: less implausible pitch drift
    shape.pitchCents += scale * p.pitchCents * pitchDrift * v.pitch * pitchScale;
    shape.pitchSettleCents += scale * p.pitchSettleCents * pitchDrift * v.settle * (0.4 + 0.6 * c.expressiveSustain + 0.4 * c.transientTonal)
                              * pitchScale * mode.settle;
    shape.pitchSettleSeconds = 0.05 + 0.1 * c.expressiveSustain;
    shape.dampingDbPerSecond += static_cast<float> (scale * p.decayDbPerSecond * v.damping);
    shape.startOffsetSeconds += 0.001 * scale * p.startOffsetMs * v.start * attackScale;
    shape.pan += static_cast<float> (scale * p.pan * v.pan);

    // Above "realistic", reinterpretation: occasionally enter the note later (a softer,
    // bowed-in or half-damped attack) and widen the colour range.
    if (life > 0.6)
    {
        const double extra = (life - 0.6) / 0.4;
        if (noteRng.nextDouble() < 0.35 * extra && c.transientTonal < 0.7)
            shape.attackSoftenSeconds += static_cast<float> (extra * noteRng.uniform (0.02, 0.12) * attackScale);
        shape.brightnessDb += static_cast<float> (extra * 2.0 * jitter() * toneScale);
    }

    // FRAY: now and then one edge frays - a pitch break into the note, a ragged or late
    // entry, or a colour outlier. Rare and bounded, never a different instrument.
    if (settings.lifeMode == LifeMode::fray)
    {
        Prng frayRng (Prng::deriveSeed (baseSeed, eventIndex, static_cast<std::uint64_t> (note) + 0x66726179));
        if (frayRng.nextDouble() < std::min (0.6, 0.3 * scale))
        {
            const double sign = frayRng.nextDouble() < 0.5 ? -1.0 : 1.0;
            switch (static_cast<int> (frayRng.nextDouble() * 3.0))
            {
                case 0:
                    shape.pitchSettleCents += sign * std::max (0.0, settings.lifePitchCents) * frayRng.uniform (0.8, 1.6) * std::min (1.5, scale);
                    shape.pitchSettleSeconds += 0.06;
                    break;
                case 1:
                    if (c.transientTonal < 0.8)
                        shape.attackSoftenSeconds += static_cast<float> (frayRng.uniform (0.02, 0.09) * attackScale);
                    shape.transientDb -= static_cast<float> (3.0 * attackScale);
                    break;
                default:
                    shape.brightnessDb += static_cast<float> (sign * 3.0 * toneScale * std::min (1.5, scale));
                    break;
            }
        }
    }
}

} // namespace osp
