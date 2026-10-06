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
    previousCount = 0;
    lastTake.fill (0);
    lastPlayedTake = -1;
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

    /**
        How each output follows the latents (F force, C colour, T timing) and its own
        independent part (J), in units of the calibrated spreads. Every character keeps
        roughly the same total spread per output, so the profile's dB/cents/ms values
        stay the 1-sigma sizes; only the correlations change.
    */
    struct Loadings
    {
        double gainF, gainJ;
        double brightF, brightC, brightJ;
        double bodyF, bodyC, bodyJ;
        double transientF, transientJ;
        double pitchT, pitchJ;
        double settleMean, settleF, settleJ;
        double dampingF, dampingJ;
        double startT, startJ;
        double panJ, panC;
    };

    const Loadings& loadings (LifeCharacter character) noexcept
    {
        // AUTO: the corpus-calibrated player (force drives level, brightness, transient,
        // the settling pitch and, inversely, damping).
        static constexpr Loadings automatic { 0.75, 0.35, 0.5, 0.5, 0.25, 0.4, -0.4, 0.3, 0.6, 0.3, 0.6, 0.4,
                                              0.5, 0.5, 0.3, -0.6, 0.4, 0.6, 0.4, 0.7, 0.3 };
        // PLUCK prior: brighter|louder 0.6, sharper attack|louder 0.5, attack drift|louder
        // 0.45, longer decay|louder 0.2; the low shelf moves against the high one; the
        // static detune is independent of force.
        static constexpr Loadings pluck { 0.78, 0.30, 0.48, 0.40, 0.41, 0.1, -0.45, 0.42, 0.36, 0.565, 0.3, 0.65,
                                          0.5, 0.28, 0.51, -0.154, 0.70, 0.3, 0.5, 0.7, 0.3 };
        // SYNTH prior: oscillator detune mostly independent, brighter|louder 0.4, sharper
        // attack|louder 0.3, no attack pitch drift.
        static constexpr Loadings synth { 0.70, 0.45, 0.36, 0.30, 0.585, 0.0, -0.3, 0.55, 0.24, 0.625, 0.2, 0.69,
                                          0.0, 0.0, 0.58, -0.15, 0.70, 0.2, 0.3, 0.7, 0.3 };
        // DRUM (trained kick and snare models): harder hits measured slightly darker
        // (brightness|level -0.4 between the two models), attack nearly independent of
        // level, no pitch; harder hits carry a little more body and ring longer.
        static constexpr Loadings drum { 0.75, 0.45, -0.35, 0.35, 0.56, 0.3, -0.3, 0.45, 0.1, 0.66, 0.3, 0.5,
                                         0.0, 0.0, 0.58, -0.2, 0.68, 0.3, 0.5, 0.7, 0.3 };
        switch (character)
        {
            case LifeCharacter::pluck: return pluck;
            case LifeCharacter::synth: return synth;
            case LifeCharacter::drum: return drum;
            case LifeCharacter::automatic: break;
        }
        return automatic;
    }

    /**
        The spreads (1 sigma at LIFE 50 %) for a character. AUTO keeps the source's own
        calibration; the others convert the generator's per-take standard deviations
        into OSP's units (10 % brightness ratio = 3 dB of shelf, the low shelf moving
        half as much the other way; attack milliseconds into transient dB).
    */
    PerformanceProfile characterProfile (LifeCharacter character, const PerformanceProfile& calibrated) noexcept
    {
        PerformanceProfile p = calibrated;
        switch (character)
        {
            case LifeCharacter::pluck:   // pitch 2 c, drift 3 c, brightness 4 %, attack 1 ms, level 0.7 dB, decay 5 %
                p = { 0.85, 1.6, 0.9, 1.5, 2.8, 6.5, 1.0, 1.0, 0.03 };
                break;
            case LifeCharacter::synth:   // detune 2.5 c, brightness 3.5 %, attack 0.5 ms, level 0.5 dB, decay 4 %
                p = { 0.6, 1.4, 0.6, 0.8, 3.5, 0.0, 0.3, 0.8, 0.03 };
                break;
            case LifeCharacter::drum:    // kick/snare: level ~1 dB, brightness ~12 %, attack ~1.6 ms, no pitch
                p = { 1.15, 3.5, 1.2, 2.0, 0.0, 0.0, 0.6, 3.0, 0.03 };
                break;
            case LifeCharacter::automatic:
                break;
        }
        return p;
    }

    /** Inverse of the standard normal CDF (Acklam's rational approximation, |error| < 1.2e-9). */
    double inverseNormal (double p) noexcept
    {
        static constexpr double a[] = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                                        1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 };
        static constexpr double b[] = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                                        6.680131188771972e+01, -1.328068155288572e+01 };
        static constexpr double c[] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                                        -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 };
        static constexpr double d[] = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00 };
        p = std::clamp (p, 1.0e-12, 1.0 - 1.0e-12);
        if (p < 0.02425 || p > 1.0 - 0.02425)
        {
            const double q = std::sqrt (-2.0 * std::log (p < 0.5 ? p : 1.0 - p));
            const double x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
                             / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
            return p < 0.5 ? x : -x;
        }
        const double q = p - 0.5, r = q * q;
        return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q
               / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    }
}

std::uint64_t PerformanceEngine::takePoolSeed (std::uint32_t reroll) const noexcept
{
    return Prng::deriveSeed (baseSeed, 0x74616b65ull /* "take" */, reroll);
}

void PerformanceEngine::takeValues (std::uint64_t poolSeed, int note, int take, int count, double (&z)[takeDimensions]) noexcept
{
    // Every axis is stratified on its own (one stratum of the normal distribution per
    // take, jittered inside it) and shuffled independently: the takes cover the range
    // evenly, no two land close together, and re-centring/normalising keeps their
    // average at the recording and their spread at the calibrated size, even for 2 takes.
    for (int dim = 0; dim < takeDimensions; ++dim)
    {
        Prng r (Prng::deriveSeed (poolSeed, static_cast<std::uint64_t> (note), static_cast<std::uint64_t> (dim)));
        double q[maxTakes] {};
        double mean = 0.0;
        for (int i = 0; i < count; ++i)
        {
            q[i] = inverseNormal ((i + r.uniform (0.25, 0.75)) / count);
            mean += q[i];
        }
        mean /= count;
        double variance = 0.0;
        for (int i = 0; i < count; ++i)
            variance += (q[i] - mean) * (q[i] - mean);
        const double norm = variance > 1.0e-12 ? 1.0 / std::sqrt (variance / count) : 0.0;
        int order[maxTakes] {};
        for (int i = 0; i < count; ++i)
            order[i] = i;
        for (int i = count - 1; i > 0; --i)
            std::swap (order[i], order[r.nextBelow (static_cast<std::uint64_t> (i) + 1)]);
        z[dim] = (q[order[take]] - mean) * norm;
    }
}

int PerformanceEngine::chooseTake (int note, int count, LifeTakeOrder order, std::uint64_t eventIndex) noexcept
{
    auto& stored = lastTake[static_cast<std::size_t> (std::clamp (note, 0, 127))];
    const int last = static_cast<int> (stored) - 1;   // -1: this note has not played a take yet
    const bool hasLast = last >= 0 && last < count;
    int take = 0;
    if (order == LifeTakeOrder::cycle)
    {
        take = last < 0 ? 0 : (last + 1) % count;
    }
    else
    {
        Prng r (Prng::deriveSeed (baseSeed, eventIndex, static_cast<std::uint64_t> (note) + 0x6f726472));
        if (hasLast)
        {
            take = static_cast<int> (r.nextBelow (static_cast<std::uint64_t> (count - 1)));
            if (take >= last)
                ++take;   // any take but the last one
        }
        else
        {
            take = static_cast<int> (r.nextBelow (static_cast<std::uint64_t> (count)));
        }
    }
    stored = static_cast<std::uint8_t> (take + 1);
    return take;
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
                                 const PerformanceProfile& calibrated, const SourceCharacter& c, double life, const Shaping& settings) noexcept
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

    // TAKES: which of the note's fixed takes plays (chosen even at LIFE 0, so turning LIFE
    // up mid-phrase continues the same cycle).
    const int takeCount = settings.lifeTakes >= 2 ? std::min (settings.lifeTakes, maxTakes) : 0;
    const bool takes = takeCount > 0;
    const auto poolSeed = takes ? takePoolSeed (settings.lifeTakesSeed) : 0;
    const int take = takes ? chooseTake (note, takeCount, settings.lifeTakeOrder, eventIndex) : -1;
    lastPlayedTake = take;

    life = std::clamp (life, 0.0, 1.0);
    if (life <= 0.0)
        return; // LIFE = 0: the recorded performance, identical every time

    const auto& L = loadings (settings.lifeCharacter);
    const auto p = characterProfile (settings.lifeCharacter, calibrated);
    const bool automatic = settings.lifeCharacter == LifeCharacter::automatic;

    // Note-level randomness comes from its own stream so the latent path does not
    // depend on how many draws a note uses. A take's own stream is fixed: the take
    // sounds the same (but for the player's drift and dynamics) every time it returns.
    const auto noteSeed = takes ? Prng::deriveSeed (poolSeed, static_cast<std::uint64_t> (note), 0x100 + static_cast<std::uint64_t> (take))
                                : Prng::deriveSeed (baseSeed, eventIndex, static_cast<std::uint64_t> (note) + 0x6e6f7465);
    Prng noteRng (noteSeed);
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
    const double lw = mode.latent, jw = mode.jitter;

    // Correlated outputs (spec §30): force drives level, brightness, transient, initial
    // pitch and (inversely) damping; colour moves brightness against body; timing moves
    // start and micro-pitch. The character's loadings say how strongly.
    auto draw = [&] (double force, double colour, double timing, double alt, auto&& independent) {
        Variation v;
        v.gain = lw * L.gainF * force + jw * L.gainJ * independent();
        v.bright = lw * (L.brightF * force + L.brightC * colour) + jw * L.brightJ * independent() + 0.3 * alt;
        v.body = lw * (L.bodyF * force + L.bodyC * colour) + jw * L.bodyJ * independent();
        v.transient = lw * L.transientF * force + 0.6 * alt * (1.0 + fast) + jw * L.transientJ * independent();
        v.pitch = lw * L.pitchT * timing + jw * L.pitchJ * independent();
        v.settle = L.settleMean + lw * L.settleF * force + jw * L.settleJ * independent();
        v.damping = lw * L.dampingF * force + jw * L.dampingJ * independent();
        v.start = std::abs (lw * L.startT * timing + jw * L.startJ * independent()) * mode.timing;
        v.pan = jw * L.panJ * independent() + lw * L.panC * colour;
        return v;
    };

    Variation v;
    if (takes)
    {
        // The take is the performance; the player's slow drift moves the whole set a
        // little (0.8 / 0.6 keeps the total spread at the calibrated size).
        double z[takeDimensions];
        takeValues (poolSeed, note, take, takeCount, z);
        int next = 3;
        auto fixed = [&z, &next, &mode] { return std::clamp (z[next++], -mode.tail, mode.tail); };
        v = draw (0.8 * z[0] + 0.6 * f, 0.8 * z[1] + 0.6 * col, 0.8 * z[2] + 0.6 * tim, 0.0, fixed);
    }
    else
    {
        const double alt = repeated * static_cast<double> (alternation) * (0.5 + 0.5 * fast);
        v = draw (f, col, tim, alt, jitter);

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
                    const auto candidate = draw (f, col, tim, alt, jitter);
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
    }

    shape.gain *= static_cast<float> (std::pow (10.0, scale * p.gainDb * v.gain / 20.0));
    shape.brightnessDb += static_cast<float> (scale * p.brightnessDb * v.bright * toneScale);
    shape.bodyDb += static_cast<float> (scale * p.bodyDb * v.body * toneScale);
    shape.transientDb += static_cast<float> (scale * p.transientDb * v.transient * attackScale);
    const double pitchDrift = 1.0 - 0.5 * fast; // fast repeats: less implausible pitch drift
    shape.pitchCents += scale * p.pitchCents * pitchDrift * v.pitch * pitchScale;
    // AUTO's settle depends on the source; the generator's priors are absolute (a pluck's
    // attack drift settles in ~70 ms).
    const double settleFor = automatic ? (0.4 + 0.6 * c.expressiveSustain + 0.4 * c.transientTonal) : 1.0;
    shape.pitchSettleCents += scale * p.pitchSettleCents * pitchDrift * v.settle * settleFor * pitchScale * mode.settle;
    shape.pitchSettleSeconds = automatic ? 0.05 + 0.1 * c.expressiveSustain : (settings.lifeCharacter == LifeCharacter::pluck ? 0.07 : 0.06);
    shape.dampingDbPerSecond += static_cast<float> (scale * p.decayDbPerSecond * v.damping);
    shape.startOffsetSeconds += 0.001 * scale * p.startOffsetMs * v.start * attackScale;
    shape.pan += static_cast<float> (scale * p.pan * v.pan);

    // Above "realistic", reinterpretation: occasionally enter the note later (a softer,
    // bowed-in or half-damped attack) and widen the colour range. (Never a soft entry
    // for a drum.)
    if (life > 0.6)
    {
        const double extra = (life - 0.6) / 0.4;
        if (noteRng.nextDouble() < 0.35 * extra && c.transientTonal < 0.7 && settings.lifeCharacter != LifeCharacter::drum)
            shape.attackSoftenSeconds += static_cast<float> (extra * noteRng.uniform (0.02, 0.12) * attackScale);
        shape.brightnessDb += static_cast<float> (extra * 2.0 * jitter() * toneScale);
    }

    // FRAY: now and then one edge frays - a pitch break into the note, a ragged or late
    // entry, or a colour outlier. Rare and bounded, never a different instrument. (With
    // takes, a frayed take stays frayed, like a worn sample in a round-robin set.)
    if (settings.lifeMode == LifeMode::fray)
    {
        Prng frayRng (takes ? Prng::deriveSeed (poolSeed, static_cast<std::uint64_t> (note), 0x200 + static_cast<std::uint64_t> (take))
                            : Prng::deriveSeed (baseSeed, eventIndex, static_cast<std::uint64_t> (note) + 0x66726179));
        if (frayRng.nextDouble() < std::min (0.6, 0.3 * scale))
        {
            const double sign = frayRng.nextDouble() < 0.5 ? -1.0 : 1.0;
            switch (static_cast<int> (frayRng.nextDouble() * 3.0))
            {
                case 0:
                    if (p.pitchCents > 0.0 || p.pitchSettleCents > 0.0)
                    {
                        shape.pitchSettleCents += sign * std::max (0.0, settings.lifePitchCents) * frayRng.uniform (0.8, 1.6) * std::min (1.5, scale);
                        shape.pitchSettleSeconds += 0.06;
                    }
                    break;
                case 1:
                    if (c.transientTonal < 0.8 && settings.lifeCharacter != LifeCharacter::drum)
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
