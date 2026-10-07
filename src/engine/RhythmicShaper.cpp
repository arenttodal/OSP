#include "engine/RhythmicShaper.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    using Shape = StepShape;
    using Step = ShaperStep;
    using Pattern = ShaperPattern;

    constexpr Step H (float v) { return { v, v, Shape::hold }; }
    constexpr Step D (float a, float b) { return { a, b, Shape::down }; }   // open, falling
    constexpr Step U (float a, float b) { return { a, b, Shape::up }; }     // rising
    constexpr Step I (float a, float b) { return { a, b, Shape::dip }; }    // a -> b -> a
    constexpr Step P (float a, float b) { return { a, b, Shape::pulse }; }  // a -> b -> a (b high)
    constexpr Step O (float a, float b) { return { a, b, Shape::soft }; }   // rounded a -> b

    // The curated library. Floors are part of each design: most never close fully, so the
    // closed steps stay present (MACHINE is the deliberate exception). One 16-step cycle is
    // one 4/4 bar at 1/16. Accents lean asymmetric and polymetric rather than four-to-the-floor.
    constexpr std::array<Pattern, RhythmicShaper::patternCount> library { {
        // 01 PULSE: a decaying articulation on every step, beats a little stronger.
        { D (1.0f, 0.15f), D (0.7f, 0.15f), D (0.8f, 0.15f), D (0.7f, 0.15f), D (1.0f, 0.15f), D (0.7f, 0.15f), D (0.8f, 0.15f), D (0.7f, 0.15f),
          D (1.0f, 0.15f), D (0.7f, 0.15f), D (0.8f, 0.15f), D (0.7f, 0.15f), D (1.0f, 0.15f), D (0.7f, 0.15f), D (0.8f, 0.15f), D (0.7f, 0.15f) },
        // 02 OFFBEAT: low on the beat, opening on the "and".
        { O (0.2f, 0.3f), O (0.3f, 0.45f), D (1.0f, 0.55f), D (0.55f, 0.25f), O (0.2f, 0.3f), O (0.3f, 0.45f), D (1.0f, 0.55f), D (0.55f, 0.25f),
          O (0.2f, 0.3f), O (0.3f, 0.45f), D (1.0f, 0.55f), D (0.55f, 0.25f), O (0.2f, 0.3f), O (0.3f, 0.45f), D (1.0f, 0.55f), D (0.55f, 0.25f) },
        // 03 BREATH: two slow, rounded swells per cycle; never below 0.35.
        { O (0.35f, 0.445f), O (0.445f, 0.675f), O (0.675f, 0.905f), O (0.905f, 1.0f), O (1.0f, 0.905f), O (0.905f, 0.675f), O (0.675f, 0.445f), O (0.445f, 0.35f),
          O (0.35f, 0.445f), O (0.445f, 0.675f), O (0.675f, 0.905f), O (0.905f, 1.0f), O (1.0f, 0.905f), O (0.905f, 0.675f), O (0.675f, 0.445f), O (0.445f, 0.35f) },
        // 04 THREE: an accent every three steps against the bar (3 over 4).
        { D (1.0f, 0.45f), D (0.45f, 0.3f), O (0.3f, 0.28f), D (1.0f, 0.45f), D (0.45f, 0.3f), O (0.3f, 0.28f), D (1.0f, 0.45f), D (0.45f, 0.3f),
          O (0.3f, 0.28f), D (1.0f, 0.45f), D (0.45f, 0.3f), O (0.3f, 0.28f), D (1.0f, 0.45f), D (0.45f, 0.3f), O (0.3f, 0.28f), D (0.85f, 0.35f) },
        // 05 FIVE: groups of five against the bar, the fifth step leaning into the next accent.
        { D (1.0f, 0.4f), D (0.4f, 0.25f), O (0.25f, 0.22f), O (0.22f, 0.2f), U (0.2f, 0.35f), D (1.0f, 0.4f), D (0.4f, 0.25f), O (0.25f, 0.22f),
          O (0.22f, 0.2f), U (0.2f, 0.35f), D (1.0f, 0.4f), D (0.4f, 0.25f), O (0.25f, 0.22f), O (0.22f, 0.2f), U (0.2f, 0.35f), D (0.8f, 0.3f) },
        // 06 EUCLID 3: three events spread over sixteen (6-5-5).
        { D (1.0f, 0.35f), D (0.5f, 0.15f), H (0.12f), H (0.12f), H (0.12f), H (0.12f), D (1.0f, 0.35f), D (0.5f, 0.15f),
          H (0.12f), H (0.12f), H (0.12f), D (1.0f, 0.35f), D (0.5f, 0.15f), H (0.12f), H (0.12f), U (0.12f, 0.2f) },
        // 07 EUCLID 5: five events spread over sixteen (3-3-4-3-3).
        { D (1.0f, 0.3f), O (0.3f, 0.2f), O (0.2f, 0.2f), D (1.0f, 0.3f), O (0.3f, 0.2f), O (0.2f, 0.2f), D (1.0f, 0.3f), O (0.3f, 0.2f),
          O (0.2f, 0.2f), O (0.2f, 0.22f), D (1.0f, 0.3f), O (0.3f, 0.2f), O (0.2f, 0.2f), D (1.0f, 0.3f), O (0.3f, 0.2f), O (0.2f, 0.2f) },
        // 08 CASCADE: each beat a little lower than the last, falling inside the beat too.
        { D (1.0f, 0.55f), D (0.75f, 0.4f), D (0.6f, 0.35f), D (0.5f, 0.25f), D (0.8f, 0.44f), D (0.6f, 0.32f), D (0.48f, 0.28f), D (0.4f, 0.22f),
          D (0.62f, 0.34f), D (0.47f, 0.25f), D (0.37f, 0.22f), D (0.31f, 0.2f), D (0.45f, 0.25f), D (0.34f, 0.2f), D (0.27f, 0.2f), U (0.2f, 0.4f) },
        // 09 RISE: pulses that grow across the cycle, then the drop back.
        { P (0.15f, 0.2f), P (0.17f, 0.27f), P (0.2f, 0.33f), P (0.23f, 0.38f), P (0.25f, 0.44f), P (0.28f, 0.49f), P (0.31f, 0.55f), P (0.33f, 0.6f),
          P (0.36f, 0.65f), P (0.39f, 0.71f), P (0.42f, 0.76f), P (0.45f, 0.82f), P (0.48f, 0.87f), P (0.5f, 0.93f), U (0.55f, 1.0f), H (1.0f) },
        // 10 BROKEN: deliberate syncopation (accents on 1, 4, 8, 12, 15), uneven but not random.
        { D (1.0f, 0.3f), O (0.3f, 0.22f), O (0.22f, 0.2f), D (0.9f, 0.3f), O (0.3f, 0.22f), O (0.22f, 0.2f), U (0.2f, 0.4f), D (0.95f, 0.35f),
          D (0.6f, 0.25f), O (0.25f, 0.2f), O (0.2f, 0.2f), D (0.85f, 0.3f), O (0.3f, 0.22f), U (0.22f, 0.35f), D (0.9f, 0.4f), D (0.5f, 0.25f) },
        // 11 SCATTER: mostly open, with a few sparse dips and one lift.
        { H (0.85f), H (0.85f), H (0.85f), I (0.85f, 0.3f), H (0.85f), H (0.85f), P (0.85f, 1.0f), H (0.85f),
          H (0.85f), I (0.85f, 0.25f), H (0.85f), H (0.85f), H (0.85f), I (0.85f, 0.4f), H (0.85f), H (0.85f) },
        // 12 MACHINE: a precise on/off gate, down to silence (x.xx.xx.x.xx.x.x). Once every
        // pattern spans its full range, decaying hits to silence are PULSE; this is the gate.
        { H (1.0f), H (0.0f), H (1.0f), H (1.0f), H (0.0f), H (1.0f), H (1.0f), H (0.0f),
          H (1.0f), H (0.0f), H (1.0f), H (1.0f), H (0.0f), H (1.0f), H (0.0f), H (1.0f) },
    } };

    constexpr const char* names[RhythmicShaper::patternCount] = {
        "PULSE", "OFFBEAT", "BREATH", "THREE", "FIVE", "EUCLID 3", "EUCLID 5", "CASCADE", "RISE", "BROKEN", "SCATTER", "MACHINE"
    };

    inline double smoothstep (double x) noexcept { return x * x * (3.0 - 2.0 * x); }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }

    /** A step's own curve at x in [0,1). */
    double stepValue (const Step& st, double x, double smooth) noexcept
    {
        const double a = st.start, b = st.end;
        switch (st.shape)
        {
            case Shape::hold: return a;
            case Shape::down:
                // Crisp: a fast fall that settles (a struck, decaying hit); smooth: an S-curve.
                return lerp (a, b, lerp (1.0 - std::pow (1.0 - x, 3.0), smoothstep (x), smooth));
            case Shape::up: return lerp (a, b, lerp (x * x, smoothstep (x), smooth));
            case Shape::dip:
            case Shape::pulse:
            {
                // Out and back within the step: narrow and pointed when crisp, round when smooth.
                const double bump = std::sin (std::numbers::pi * x);
                const double exponent = st.shape == Shape::dip ? lerp (0.5, 1.0, smooth) : lerp (2.5, 1.0, smooth);
                return lerp (a, b, std::pow (std::max (0.0, bump), exponent));
            }
            case Shape::soft: return lerp (a, b, smoothstep (x));
        }
        return a;
    }
}

const char* RhythmicShaper::patternName (int pattern) noexcept
{
    return names[std::clamp (pattern, 0, patternCount - 1)];
}

const char* RhythmicShaper::rateName (ShaperRate rate) noexcept
{
    switch (rate)
    {
        case ShaperRate::quarter: return "1/4";
        case ShaperRate::eighth: return "1/8";
        case ShaperRate::eighthTriplet: return "1/8T";
        case ShaperRate::sixteenth: return "1/16";
        case ShaperRate::sixteenthTriplet: return "1/16T";
        case ShaperRate::thirtySecond: return "1/32";
    }
    return "1/16";
}

double RhythmicShaper::stepQuarterNotes (ShaperRate rate) noexcept
{
    switch (rate)
    {
        case ShaperRate::quarter: return 1.0;
        case ShaperRate::eighth: return 0.5;
        case ShaperRate::eighthTriplet: return 1.0 / 3.0;
        case ShaperRate::sixteenth: return 0.25;
        case ShaperRate::sixteenthTriplet: return 1.0 / 6.0;
        case ShaperRate::thirtySecond: return 0.125;
    }
    return 0.25;
}

ShaperPattern RhythmicShaper::patternSteps (int pattern) noexcept
{
    return library[static_cast<std::size_t> (std::clamp (pattern, 0, patternCount - 1))];
}

ShaperPattern RhythmicShaper::stepsOf (const ShaperParams& params) noexcept
{
    return params.custom ? params.customSteps : patternSteps (params.pattern);
}

float RhythmicShaper::raw (const ShaperPattern& p, double cycle, double smooth) noexcept
{
    smooth = std::clamp (smooth, 0.0, 1.0);
    cycle -= std::floor (cycle);
    const double position = cycle * steps;
    const int index = std::min (steps - 1, static_cast<int> (position));
    const double x = position - index;
    double v = stepValue (p[static_cast<std::size_t> (index)], x, smooth);
    // Hand-over into the next step: short when crisp (a clear edge, never a jump), up to
    // half the step when smooth (flowing, almost LFO-like).
    const double width = 0.04 + 0.46 * smooth;
    if (x > 1.0 - width)
    {
        const double next = stepValue (p[static_cast<std::size_t> ((index + 1) % steps)], 0.0, smooth);
        const double t = (x - (1.0 - width)) / width;
        v = lerp (v, next, lerp (t, smoothstep (t), 0.5 + 0.5 * smooth));
    }
    return static_cast<float> (std::clamp (v, 0.0, 1.0));
}

RhythmicShaper::Span RhythmicShaper::span (int pattern, double smooth) noexcept
{
    return span (patternSteps (pattern), smooth);
}

RhythmicShaper::Span RhythmicShaper::span (const ShaperPattern& pattern, double smooth) noexcept
{
    // Where the curve really goes at this SMOOTH (the hand-over can turn a fall around
    // before it reaches its step's end value). Sampled, so it can only be narrower than
    // the truth; evaluate() clamps, so the lowest point still lands exactly on 0.
    Span s { 1.0f, 0.0f };
    constexpr int points = steps * 16;
    for (int i = 0; i < points; ++i)
    {
        const float v = raw (pattern, (i + 0.5) / points, smooth);
        s.low = std::min (s.low, v);
        s.high = std::max (s.high, v);
    }
    if (s.high - s.low < 1.0e-3f)
        s = { 0.0f, 1.0f };   // a flat pattern stays as it is
    return s;
}

float RhythmicShaper::evaluate (int pattern, double cycle, double smooth, Span s) noexcept
{
    return evaluate (library[static_cast<std::size_t> (std::clamp (pattern, 0, patternCount - 1))], cycle, smooth, s);
}

float RhythmicShaper::evaluate (const ShaperPattern& steps, double cycle, double smooth, Span s) noexcept
{
    const double v = (raw (steps, cycle, smooth) - s.low) / static_cast<double> (s.high - s.low);
    return static_cast<float> (std::clamp (v, 0.0, 1.0));
}

float RhythmicShaper::evaluate (const ShaperParams& params, double cycle) noexcept
{
    if (! params.custom)
        return evaluate (params.pattern, cycle, params.smooth);
    // CUSTOM: one remembered span per thread for the last steps (the display asks point by point).
    thread_local ShaperPattern lastSteps {};
    thread_local double lastSmooth = -1.0;
    thread_local Span lastSpan;
    if (! (params.customSteps == lastSteps) || std::abs (params.smooth - lastSmooth) > 1.0e-12)
    {
        lastSteps = params.customSteps;
        lastSmooth = params.smooth;
        lastSpan = span (lastSteps, lastSmooth);
    }
    return evaluate (params.customSteps, cycle, params.smooth, lastSpan);
}

float RhythmicShaper::evaluate (int pattern, double cycle, double smooth) noexcept
{
    // One remembered span per thread (displays and tests call this point by point).
    thread_local int lastPattern = -1;
    thread_local double lastSmooth = -1.0;
    thread_local Span lastSpan;
    if (pattern != lastPattern || std::abs (smooth - lastSmooth) > 1.0e-12)
    {
        lastSpan = span (pattern, smooth);
        lastPattern = pattern;
        lastSmooth = smooth;
    }
    return evaluate (pattern, cycle, smooth, lastSpan);
}

void RhythmicShaper::prepare (double rate) noexcept
{
    sampleRate = rate;
    fadeLength = std::max (1, static_cast<int> (0.03 * rate));
    idleLimit = std::max (1, static_cast<int> (1.5 * rate));
    depthCoef = 1.0 - std::exp (-1.0 / (0.04 * rate));
    rampCoef = 1.0 - std::exp (-1.0 / (0.002 * rate));
    openHz = std::min (18000.0, 0.45 * rate);
    reset();
}

void RhythmicShaper::reset() noexcept
{
    previous = params;
    playing = previousSteps = stepsOf (params);
    currentSpan = previousSpan = span (playing, params.smooth);
    fadeRemaining = 0;
    hostPpq = localPpq = ppqNow = 0.0;
    usingHost = localRunning = pendingStart = running = false;
    idleSamples = 0;
    phase = 0.0;
    volumeDepth = params.target == ShaperTarget::volume ? 1.0 : (params.target == ShaperTarget::both ? 0.55 : 0.0);
    filterDepth = params.target == ShaperTarget::volume ? 0.0 : 1.0;
    gain = 1.0;
    closeOctaves = 0.0;
    s1.fill (0.0);
    s2.fill (0.0);
    coefCountdown = 0;
}

void RhythmicShaper::setParams (const ShaperParams& p) noexcept
{
    const bool otherPattern = p.custom != params.custom || (! p.custom && p.pattern != params.pattern);
    if (otherPattern || p.rate != params.rate)
    {
        // Crossfade from where the old pattern/rate is to where the new one is (30 ms).
        previous = params;
        previousSteps = playing;
        fadeRemaining = fadeLength;
    }
    // Editing CUSTOM's steps reshapes in place (the 2 ms ramps keep it from clicking).
    const bool reshaped = otherPattern || std::abs (p.smooth - params.smooth) > 1.0e-9 || (p.custom && ! (p.customSteps == params.customSteps));
    if (fadeRemaining == fadeLength)
        previousSpan = currentSpan;
    params = p;
    params.pattern = std::clamp (p.pattern, 0, patternCount - 1);
    if (reshaped)
    {
        playing = stepsOf (params);
        currentSpan = span (playing, params.smooth);   // bounded, no allocation; only on a change
    }
}

void RhythmicShaper::setTiming (const HostTiming& timing) noexcept
{
    host = timing;
    if (timing.valid && timing.playing)
        hostPpq = timing.ppq;   // the host's position is the truth at every block start
}

void RhythmicShaper::tick() noexcept
{
    const double bpm = host.valid && host.bpm > 1.0 ? host.bpm : 120.0;
    const double ppqPerSample = bpm / (60.0 * sampleRate);
    if (host.valid && host.playing)
    {
        usingHost = true;
        localRunning = false;
        running = true;
        ppqNow = hostPpq;
        hostPpq += ppqPerSample;
    }
    else
    {
        usingHost = false;
        // Transport stopped: the pattern starts from its beginning with the first note and
        // keeps going while anything sounds; after 1.5 s of silence it waits again.
        if (pendingStart && ! localRunning)
        {
            localRunning = true;
            localPpq = 0.0;
            idleSamples = 0;
        }
        pendingStart = false;
        if (localRunning)
        {
            ppqNow = localPpq;
            localPpq += ppqPerSample;
            idleSamples = voicesActive ? 0 : idleSamples + 1;
            if (idleSamples > idleLimit)
                localRunning = false;
        }
        running = localRunning;
    }
    phase = cyclePhase (ppqNow, params.rate);
}

double RhythmicShaper::cyclePhase (double quarterNotes, ShaperRate rate) const noexcept
{
    const double cycle = stepQuarterNotes (rate) * steps;
    const double p = quarterNotes / cycle;
    return p - std::floor (p);
}

void RhythmicShaper::process (float& left, float& right, double amount) noexcept
{
    amount = std::clamp (amount, 0.0, 1.0);
    // Depth per target, crossfaded over ~40 ms when the target changes.
    const double volumeTarget = params.target == ShaperTarget::volume ? 1.0 : (params.target == ShaperTarget::both ? 0.55 : 0.0);
    const double filterTarget = params.target == ShaperTarget::volume ? 0.0 : 1.0;
    volumeDepth += (volumeTarget - volumeDepth) * depthCoef;
    filterDepth += (filterTarget - filterDepth) * depthCoef;

    double shape = 1.0;
    if (running)
    {
        shape = evaluate (playing, phase, params.smooth, currentSpan);
        if (fadeRemaining > 0)
        {
            const double old = evaluate (previousSteps, cyclePhase (ppqNow, previous.rate), previous.smooth, previousSpan);
            shape = lerp (shape, old, static_cast<double> (fadeRemaining) / fadeLength);
            --fadeRemaining;
        }
    }

    // Targets: VOL dips the level by the pattern; FILTER closes up to ~5.6 octaves, with
    // a curve that makes 25 % a gentle top-end articulation and 50 % clearly rhythmic.
    const double closed = 1.0 - shape;
    const double gainTarget = 1.0 - amount * volumeDepth * closed;
    const double octTarget = filterDepth * std::pow (amount, 0.8) * closed * std::log2 (openHz / 380.0);
    // Anti-click: never a jump, even at SMOOTH 0 (2 ms).
    gain += (gainTarget - gain) * rampCoef;
    closeOctaves += (octTarget - closeOctaves) * rampCoef;

    if (closeOctaves > 1.0e-3)
    {
        if (--coefCountdown <= 0)
        {
            coefCountdown = 8;
            const double fc = openHz * std::exp2 (-closeOctaves);
            g = std::tan (std::numbers::pi * std::min (fc, 0.49 * sampleRate) / sampleRate);
        }
        // LP12, Q ~0.77: transparent and unresonant (CHARACTER stays the tonal shaper).
        const double a1 = 1.0 / (1.0 + g * (g + k));
        float* io[2] = { &left, &right };
        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            const double v0 = *io[ch];
            const double v1 = a1 * (s1[ch] + g * (v0 - s2[ch]));
            const double v2 = s2[ch] + g * v1;
            s1[ch] = 2.0 * v1 - s1[ch];
            s2[ch] = 2.0 * v2 - s2[ch];
            *io[ch] = static_cast<float> (v2);
        }
    }
    else
    {
        // Open: the filter rests (its state follows the input so re-closing is seamless).
        s1.fill (0.0);
        s2[0] = left;
        s2[1] = right;
        coefCountdown = 0;
    }
    left *= static_cast<float> (gain);
    right *= static_cast<float> (gain);
}

} // namespace osp
