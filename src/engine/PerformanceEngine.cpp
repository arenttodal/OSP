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

void PerformanceEngine::advance (double timeSeconds) noexcept
{
    // Ornstein-Uhlenbeck step over the elapsed time: correlation decays with
    // exp(-dt / tau); the stationary variance stays 1.
    const double dt = lastTime < 0.0 ? memorySeconds : std::max (0.0, timeSeconds - lastTime);
    const double a = std::exp (-dt / memorySeconds);
    const double b = std::sqrt (std::max (0.0, 1.0 - a * a));
    latentForce = a * latentForce + b * rng.gaussian();
    latentColour = a * latentColour + b * rng.gaussian();
    latentTiming = a * latentTiming + b * rng.gaussian();
}

void PerformanceEngine::perform (NoteShape& shape, int note, int /*velocity*/, double timeSeconds, std::uint64_t eventIndex,
                                 const PerformanceProfile& p, const SourceCharacter& c, double life) noexcept
{
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
    }
    advance (timeSeconds);
    lastTime = timeSeconds;
    lastNote = note;
    lastInterval = interval;

    life = std::clamp (life, 0.0, 1.0);
    if (life <= 0.0)
        return; // LIFE = 0: the recorded performance, identical every time

    // Note-level randomness comes from its own stream so the latent path does not
    // depend on how many draws a note uses.
    Prng noteRng (Prng::deriveSeed (baseSeed, eventIndex, static_cast<std::uint64_t> (note) + 0x6e6f7465));
    auto jitter = [&noteRng] { return std::clamp (noteRng.gaussian(), -2.5, 2.5); };

    // 0.5 -> realistic (1x the calibrated spread), 1 -> creative (2x plus reinterpretation).
    const double scale = life / 0.5;
    const double fast = std::clamp ((0.25 - interval) / 0.2, 0.0, 1.0); // 1 for very fast repeats
    const double repeated = repeats > 0 ? 1.0 : 0.0;

    const double f = std::clamp (latentForce, -2.5, 2.5);
    const double col = std::clamp (latentColour, -2.5, 2.5);
    const double tim = std::clamp (latentTiming, -2.5, 2.5);
    const double alt = repeated * static_cast<double> (alternation) * (0.5 + 0.5 * fast);

    // Correlated outputs (spec §30): force drives level, brightness, transient, initial
    // pitch and (inversely) damping; colour moves brightness against body; timing moves
    // start and micro-pitch.
    shape.gain *= static_cast<float> (std::pow (10.0, scale * p.gainDb * (0.75 * f + 0.35 * jitter()) / 20.0));
    shape.brightnessDb += static_cast<float> (scale * p.brightnessDb * (0.5 * f + 0.5 * col + 0.25 * jitter() + 0.3 * alt));
    shape.bodyDb += static_cast<float> (scale * p.bodyDb * (0.4 * f - 0.4 * col + 0.3 * jitter()));
    shape.transientDb += static_cast<float> (scale * p.transientDb * (0.6 * f + 0.6 * alt * (1.0 + fast) + 0.3 * jitter()));
    const double pitchDrift = 1.0 - 0.5 * fast; // fast repeats: less implausible pitch drift
    shape.pitchCents += scale * p.pitchCents * pitchDrift * (0.6 * tim + 0.4 * jitter());
    shape.pitchSettleCents += scale * p.pitchSettleCents * pitchDrift * (0.5 + 0.5 * f + 0.3 * jitter()) * (0.4 + 0.6 * c.expressiveSustain + 0.4 * c.transientTonal);
    shape.pitchSettleSeconds = 0.05 + 0.1 * c.expressiveSustain;
    shape.dampingDbPerSecond += static_cast<float> (scale * p.decayDbPerSecond * (-0.6 * f + 0.4 * jitter()));
    shape.startOffsetSeconds += 0.001 * scale * p.startOffsetMs * std::abs (0.6 * tim + 0.4 * jitter());
    shape.pan += static_cast<float> (scale * p.pan * (0.7 * jitter() + 0.3 * col));

    // Above "realistic", reinterpretation: occasionally enter the note later (a softer,
    // bowed-in or half-damped attack) and widen the colour range.
    if (life > 0.6)
    {
        const double extra = (life - 0.6) / 0.4;
        if (noteRng.nextDouble() < 0.35 * extra && c.transientTonal < 0.7)
            shape.attackSoftenSeconds += static_cast<float> (extra * noteRng.uniform (0.02, 0.12));
        shape.brightnessDb += static_cast<float> (extra * 2.0 * jitter());
    }
}

} // namespace osp
