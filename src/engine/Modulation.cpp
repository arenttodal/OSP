#include "engine/Modulation.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp::mod
{

const char* sourceName (Source s) noexcept
{
    switch (s)
    {
        case Source::lfo1: return "LFO 1";
        case Source::lfo2: return "LFO 2";
        case Source::env1: return "ENV 1";
        case Source::env2: return "ENV 2";
        case Source::none: break;
    }
    return "-";
}

const char* lfoShapeName (LfoShape s) noexcept
{
    switch (s)
    {
        case LfoShape::sine: return "SINE";
        case LfoShape::triangle: return "TRIANGLE";
        case LfoShape::rampUp: return "RAMP UP";
        case LfoShape::rampDown: return "RAMP DOWN";
        case LfoShape::pulse: return "PULSE";
        case LfoShape::smoothRandom: return "RANDOM";
        case LfoShape::custom: return "CUSTOM";
    }
    return "SINE";
}

namespace
{
    struct Division
    {
        const char* name;
        double quarters;
    };
    constexpr std::array<Division, syncDivisionCount> divisions { {
        { "1/32", 0.125 }, { "1/16", 0.25 }, { "1/8", 0.5 }, { "1/4", 1.0 }, { "1/2", 2.0 },
        { "1 bar", 4.0 }, { "2 bars", 8.0 }, { "4 bars", 16.0 }, { "8 bars", 32.0 },
        { "1/16T", 1.0 / 6.0 }, { "1/8T", 1.0 / 3.0 }, { "1/4T", 2.0 / 3.0 },
        { "1/16D", 0.375 }, { "1/8D", 0.75 }, { "1/4D", 1.5 },
    } };

    constexpr std::array<DestInfo, destCount> registry { {
        { "none", "-", Owner::global, -1, Domain::unit, 0.0, Update::continuous },
        { "life", "LIFE", Owner::global, -1, Domain::unit, 1.0, Update::continuous },
        { "drive", "DRIVE", Owner::global, -1, Domain::unit, 1.0, Update::continuous },
        { "character", "CHARACTER", Owner::global, -1, Domain::unit, 1.0, Update::continuous },
        { "movement", "MOVEMENT", Owner::global, -1, Domain::unit, 1.0, Update::continuous },
        { "space", "SPACE", Owner::global, -1, Domain::unit, 1.0, Update::continuous },
        { "levelA", "LEVEL A", Owner::voice, 0, Domain::decibels, 24.0, Update::continuous },
        { "levelB", "LEVEL B", Owner::voice, 1, Domain::decibels, 24.0, Update::continuous },
        { "levelC", "LEVEL C", Owner::voice, 2, Domain::decibels, 24.0, Update::continuous },
        { "panA", "PAN A", Owner::voice, 0, Domain::pan, 1.0, Update::continuous },
        { "panB", "PAN B", Owner::voice, 1, Domain::pan, 1.0, Update::continuous },
        { "panC", "PAN C", Owner::voice, 2, Domain::pan, 1.0, Update::continuous },
        { "reimaginedA", "REIMAGINED A", Owner::voice, 0, Domain::unit, 1.0, Update::continuous },
        { "reimaginedB", "REIMAGINED B", Owner::voice, 1, Domain::unit, 1.0, Update::continuous },
        { "reimaginedC", "REIMAGINED C", Owner::voice, 2, Domain::unit, 1.0, Update::continuous },
        { "fineTuneA", "FINE TUNE A", Owner::voice, 0, Domain::cents, 100.0, Update::continuous },
        { "fineTuneB", "FINE TUNE B", Owner::voice, 1, Domain::cents, 100.0, Update::continuous },
        { "fineTuneC", "FINE TUNE C", Owner::voice, 2, Domain::cents, 100.0, Update::continuous },
        { "grainPositionA", "POSITION A", Owner::voice, 0, Domain::unit, 0.5, Update::continuous },
        { "grainPositionB", "POSITION B", Owner::voice, 1, Domain::unit, 0.5, Update::continuous },
        { "grainPositionC", "POSITION C", Owner::voice, 2, Domain::unit, 0.5, Update::continuous },
        { "grainDensityA", "DENSITY A", Owner::voice, 0, Domain::octaves, 2.0, Update::continuous },
        { "grainDensityB", "DENSITY B", Owner::voice, 1, Domain::octaves, 2.0, Update::continuous },
        { "grainDensityC", "DENSITY C", Owner::voice, 2, Domain::octaves, 2.0, Update::continuous },
        { "grainSizeA", "GRAIN SIZE A", Owner::voice, 0, Domain::octaves, 2.0, Update::continuous },
        { "grainSizeB", "GRAIN SIZE B", Owner::voice, 1, Domain::octaves, 2.0, Update::continuous },
        { "grainSizeC", "GRAIN SIZE C", Owner::voice, 2, Domain::octaves, 2.0, Update::continuous },
        { "grainSpreadA", "SPREAD A", Owner::voice, 0, Domain::unit, 1.0, Update::continuous },
        { "grainSpreadB", "SPREAD B", Owner::voice, 1, Domain::unit, 1.0, Update::continuous },
        { "grainSpreadC", "SPREAD C", Owner::voice, 2, Domain::unit, 1.0, Update::continuous },
        { "cutoff", "CHARACTER CUTOFF", Owner::voice, -1, Domain::octaves, 4.0, Update::continuous },
        { "resonance", "CHARACTER RESONANCE", Owner::voice, -1, Domain::unit, 1.0, Update::continuous },
        { "ampAttack", "AMP ATTACK", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOn },
        { "ampDecay", "AMP DECAY", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOn },
        { "ampSustain", "AMP SUSTAIN", Owner::voice, -1, Domain::unit, 1.0, Update::continuous },
        { "ampRelease", "AMP RELEASE", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOff },
        { "eqBellFrequencyA", "EQ A BELL FREQ", Owner::global, 0, Domain::octaves, 2.0, Update::continuous },
        { "eqBellFrequencyB", "EQ B BELL FREQ", Owner::global, 1, Domain::octaves, 2.0, Update::continuous },
        { "eqBellFrequencyC", "EQ C BELL FREQ", Owner::global, 2, Domain::octaves, 2.0, Update::continuous },
        { "eqBellGainA", "EQ A BELL GAIN", Owner::global, 0, Domain::decibels, 12.0, Update::continuous },
        { "eqBellGainB", "EQ B BELL GAIN", Owner::global, 1, Domain::decibels, 12.0, Update::continuous },
        { "eqBellGainC", "EQ C BELL GAIN", Owner::global, 2, Domain::decibels, 12.0, Update::continuous },
        { "eqLowShelfGainA", "EQ A LOW SHELF", Owner::global, 0, Domain::decibels, 12.0, Update::continuous },
        { "eqLowShelfGainB", "EQ B LOW SHELF", Owner::global, 1, Domain::decibels, 12.0, Update::continuous },
        { "eqLowShelfGainC", "EQ C LOW SHELF", Owner::global, 2, Domain::decibels, 12.0, Update::continuous },
        { "eqHighShelfGainA", "EQ A HIGH SHELF", Owner::global, 0, Domain::decibels, 12.0, Update::continuous },
        { "eqHighShelfGainB", "EQ B HIGH SHELF", Owner::global, 1, Domain::decibels, 12.0, Update::continuous },
        { "eqHighShelfGainC", "EQ C HIGH SHELF", Owner::global, 2, Domain::decibels, 12.0, Update::continuous },
    } };
}

double syncQuarters (int division) noexcept
{
    return divisions[static_cast<std::size_t> (std::clamp (division, 0, syncDivisionCount - 1))].quarters;
}

const char* syncName (int division) noexcept
{
    return divisions[static_cast<std::size_t> (std::clamp (division, 0, syncDivisionCount - 1))].name;
}

const DestInfo& destInfo (Dest d) noexcept
{
    return registry[static_cast<std::size_t> (std::clamp (static_cast<int> (d), 0, destCount - 1))];
}

void Curve::normalise() noexcept
{
    count = std::clamp (count, 0, maxCurvePoints);
    if (count < 2)
    {
        points[0] = { 0.0f, 0.0f, 0.0f };
        points[1] = { 1.0f, 1.0f, 0.0f };
        count = 2;
    }
    std::sort (points.begin(), points.begin() + count, [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    for (int i = 0; i < count; ++i)
    {
        auto& p = points[static_cast<std::size_t> (i)];
        p.x = std::clamp (p.x, 0.0f, 1.0f);
        p.y = std::clamp (p.y, 0.0f, 1.0f);
        p.tension = std::clamp (p.tension, -1.0f, 1.0f);
    }
    points[0].x = 0.0f;
    points[static_cast<std::size_t> (count - 1)].x = 1.0f;
}

bool Curve::add (CurvePoint p) noexcept
{
    if (count >= maxCurvePoints)
        return false;
    points[static_cast<std::size_t> (count++)] = p;
    normalise();
    return true;
}

void Curve::remove (int index) noexcept
{
    if (index <= 0 || index >= count - 1)
        return;
    std::move (points.begin() + index + 1, points.begin() + count, points.begin() + index);
    --count;
}

Curve defaultLfoCurve() noexcept
{
    Curve c;
    c.points[0] = { 0.0f, 0.0f, 0.0f };
    c.points[1] = { 0.5f, 1.0f, -0.35f };
    c.points[2] = { 1.0f, 0.0f, 0.35f };
    c.count = 3;
    return c;
}

Curve defaultEnvCurve() noexcept
{
    Curve c;
    c.points[0] = { 0.0f, 1.0f, 0.0f };
    c.points[1] = { 1.0f, 0.0f, -0.5f };
    c.count = 2;
    return c;
}

CurveTable compileCurve (const Curve& input) noexcept
{
    Curve c = input;
    c.normalise();
    CurveTable table {};
    int segment = 0;
    for (int i = 0; i < curvePoints; ++i)
    {
        const double x = static_cast<double> (i) / (curvePoints - 1);
        while (segment < c.count - 2 && x > c.points[static_cast<std::size_t> (segment + 1)].x)
            ++segment;
        const auto& a = c.points[static_cast<std::size_t> (segment)];
        const auto& b = c.points[static_cast<std::size_t> (segment + 1)];
        const double width = std::max (1.0e-6, static_cast<double> (b.x - a.x));
        const double t = shapeSegment ((x - a.x) / width, b.tension);
        table[static_cast<std::size_t> (i)] = static_cast<float> (a.y + (b.y - a.y) * t);
    }
    return table;
}

Settings::Settings()
{
    // CUSTOM starts as a soft rise and fall; the one-shot envelope curve as a falling sweep.
    for (auto& c : lfoCurve)
        c = compileCurve (defaultLfoCurve());
    for (auto& c : envCurve)
        c = compileCurve (defaultEnvCurve());
}

bool isPolySource (const Settings& settings, Source source) noexcept
{
    switch (source)
    {
        case Source::lfo1: return settings.lfo[0].scope == Scope::poly;
        case Source::lfo2: return settings.lfo[1].scope == Scope::poly;
        case Source::env1:
        case Source::env2: return true;
        case Source::none: break;
    }
    return false;
}

bool compatible (const Settings& settings, Source source, Dest dest) noexcept
{
    if (source == Source::none || dest == Dest::none || static_cast<int> (dest) >= destCount)
        return false;
    return destInfo (dest).owner == Owner::voice || ! isPolySource (settings, source);
}

RouteState routeState (const Settings& settings, const Route& route) noexcept
{
    if (route.source == Source::none || route.dest == Dest::none || static_cast<int> (route.dest) >= destCount || std::abs (route.depth) < 1.0e-6)
        return RouteState::empty;
    if (! route.enabled)
        return RouteState::bypassed;
    if (! compatible (settings, route.source, route.dest))
        return RouteState::scope;
    return RouteState::active;
}

void Compiled::compile (const Settings& settings) noexcept
{
    termCount.fill (0);
    used.fill (false);
    any = anyGlobalDest = anyVoiceDest = false;
    for (const auto& route : settings.routes)
    {
        if (routeState (settings, route) != RouteState::active)
            continue;
        const auto d = static_cast<std::size_t> (route.dest);
        if (termCount[d] >= maxTerms)
            continue;
        const auto s = static_cast<std::uint8_t> (sourceIndex (route.source));
        terms[d][termCount[d]++] = { s, static_cast<float> (std::clamp (route.depth, -1.0, 1.0)) };
        used[s] = true;
        any = true;
        if (destInfo (route.dest).owner == Owner::global)
            anyGlobalDest = true;
        else
            anyVoiceDest = true;
    }
}

double curveAt (const CurveTable& curve, double x) noexcept
{
    const double p = std::clamp (x, 0.0, 1.0) * (curvePoints - 1);
    const auto i = std::min (static_cast<int> (p), curvePoints - 2);
    const double f = p - i;
    return curve[static_cast<std::size_t> (i)] + f * (curve[static_cast<std::size_t> (i + 1)] - curve[static_cast<std::size_t> (i)]);
}

double shapeSegment (double x, double curve) noexcept
{
    x = std::clamp (x, 0.0, 1.0);
    if (std::abs (curve) < 1.0e-6)
        return x;
    return std::pow (x, std::exp2 (2.0 * std::clamp (curve, -1.0, 1.0)));
}

double lfoShapeValue (LfoShape shape, double p, const CurveTable& curve, std::uint64_t seed, std::int64_t cycle) noexcept
{
    p = p - std::floor (p);
    switch (shape)
    {
        case LfoShape::sine: return std::sin (2.0 * std::numbers::pi * p);
        case LfoShape::triangle: return p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        case LfoShape::rampUp: return 2.0 * p - 1.0;
        case LfoShape::rampDown: return 1.0 - 2.0 * p;
        case LfoShape::pulse: return p < 0.5 ? 1.0 : -1.0;
        case LfoShape::smoothRandom:
        {
            // One value per cycle (from the seed and the cycle index: a song position repeats
            // its values), joined by a cosine glide.
            auto at = [seed] (std::int64_t k) { return Prng (Prng::deriveSeed (seed, static_cast<std::uint64_t> (k), 0x726e64ull)).bipolar(); };
            const double a = at (cycle), b = at (cycle + 1);
            const double t = 0.5 - 0.5 * std::cos (std::numbers::pi * p);
            return a + (b - a) * t;
        }
        case LfoShape::custom: return 2.0 * curveAt (curve, p) - 1.0;
    }
    return 0.0;
}

double lfoOutput (const LfoSettings& s, double phase, const CurveTable& curve, std::uint64_t seed, std::int64_t cycle) noexcept
{
    const double v = lfoShapeValue (s.shape, phase, curve, seed, cycle);
    return s.bipolar ? v : 0.5 * (v + 1.0);
}

//==============================================================================

void LfoState::start (const LfoSettings&, double fromPhase) noexcept
{
    phase = fromPhase - std::floor (fromPhase);
    cycle = 0;
    finished = false;
    travelled = 0.0;
}

void LfoState::advance (const LfoSettings& s, double seconds, double bpm) noexcept
{
    if (finished)
        return;
    const double rate = s.sync ? std::max (1.0, bpm) / 60.0 / syncQuarters (s.division) : std::clamp (s.rateHz, 0.001, 100.0);
    const double step = rate * std::max (0.0, seconds);
    if (s.mode == LfoMode::oneShot && travelled + step >= 1.0)
    {
        // One cycle, then it holds where the cycle ends (just before it would begin again).
        phase = s.phase - std::floor (s.phase) - 1.0e-9;
        phase -= std::floor (phase);
        finished = true;
        return;
    }
    travelled += step;
    phase += step;
    const double whole = std::floor (phase);
    phase -= whole;
    cycle += static_cast<std::int64_t> (whole);
}

void LfoState::follow (const LfoSettings& s, double ppq) noexcept
{
    const double x = ppq / syncQuarters (s.division) + s.phase;
    const double whole = std::floor (x);
    cycle = static_cast<std::int64_t> (whole);
    phase = x - whole;
}

void LfoState::evaluate (const LfoSettings& s, const CurveTable& curve, std::uint64_t seed) noexcept
{
    value = lfoOutput (s, phase, curve, seed, cycle);
}

//==============================================================================

void EnvState::start() noexcept
{
    stage = Stage::attack;
    t = elapsed = 0.0;
    from = level;   // a voice reused while its envelope still sounds glides on (no jump)
}

void EnvState::release() noexcept
{
    if (stage == Stage::idle || stage == Stage::done || stage == Stage::release)
        return;
    from = level;
    t = 0.0;
    stage = Stage::release;
}

void EnvState::advance (const EnvSettings& s, const CurveTable& curve, double seconds) noexcept
{
    if (stage == Stage::idle || stage == Stage::done)
    {
        level = stage == Stage::done && s.oneShotCurve ? level : 0.0;
        return;
    }
    t += seconds;
    elapsed += seconds;
    if (s.oneShotCurve)
    {
        // The curve once over its length; note-off does not cut it (it is a shape in time).
        const double x = elapsed / std::max (0.001, s.lengthSeconds);
        level = curveAt (curve, x);
        if (x >= 1.0)
            stage = Stage::done;
        return;
    }
    const double sustain = std::clamp (s.sustain, 0.0, 1.0);
    switch (stage)
    {
        case Stage::attack:
        {
            const double x = s.attackSeconds > 0.0 ? t / s.attackSeconds : 1.0;
            level = from + (1.0 - from) * shapeSegment (x, s.curve);
            if (x >= 1.0)
            {
                level = 1.0;
                stage = Stage::decay;
                t = 0.0;
            }
            break;
        }
        case Stage::decay:
        {
            const double x = s.decaySeconds > 0.0 ? t / s.decaySeconds : 1.0;
            level = 1.0 + (sustain - 1.0) * shapeSegment (x, -s.curve);
            if (x >= 1.0)
            {
                level = sustain;
                stage = Stage::sustain;
                t = 0.0;
            }
            break;
        }
        case Stage::sustain:
            level = sustain;
            break;
        case Stage::release:
        {
            const double x = s.releaseSeconds > 0.0 ? t / s.releaseSeconds : 1.0;
            level = from * (1.0 - shapeSegment (x, -s.curve));
            if (x >= 1.0)
            {
                level = 0.0;
                stage = Stage::done;
            }
            break;
        }
        case Stage::idle:
        case Stage::done: break;
    }
}

//==============================================================================

void Runtime::setSettings (const Settings& s) noexcept
{
    // A source's mode or scope may change: a running global LFO keeps its phase.
    settings = s;
    compiled.compile (settings);
}

void Runtime::setTiming (const HostTiming& timing) noexcept
{
    if (timing.valid && std::isfinite (timing.bpm) && timing.bpm > 0.0)
        bpm = std::clamp (timing.bpm, 5.0, 1000.0);
    hostPlaying = timing.valid && timing.playing && std::isfinite (timing.ppq);
    if (hostPlaying)
        ppq = timing.ppq;
}

void Runtime::advanceGlobal (int samples, double sampleRate) noexcept
{
    const double seconds = samples / std::max (1.0, sampleRate);
    for (std::size_t i = 0; i < globalLfo.size(); ++i)
    {
        const auto& s = settings.lfo[i];
        auto& state = globalLfo[i];
        // Synced, FREE and the host playing: the phase is the song position's (bounces and
        // loops repeat exactly). Otherwise it runs by itself.
        if (s.sync && s.mode == LfoMode::free && hostPlaying)
            state.follow (s, ppq);
        else
            state.advance (s, seconds, bpm);
        state.evaluate (s, settings.lfoCurve[i], Prng::deriveSeed (settings.seed, 0x6c666f31ull + i, 0));
        globalValue[i] = s.scope == Scope::global ? static_cast<float> (state.value) : 0.0f;
    }
    globalValue[2] = globalValue[3] = 0.0f;   // envelopes are per voice
    if (hostPlaying)
        ppq += seconds * bpm / 60.0;
}

void Runtime::noteStarted() noexcept
{
    if (heldNotes == 0)
        for (std::size_t i = 0; i < globalLfo.size(); ++i)
            if (settings.lfo[i].mode != LfoMode::free)
                globalLfo[i].start (settings.lfo[i], settings.lfo[i].phase);
    ++heldNotes;
}

void Runtime::noteEnded() noexcept
{
    heldNotes = std::max (0, heldNotes - 1);
}

//==============================================================================

void VoiceState::start (const Runtime& runtime) noexcept
{
    for (std::size_t i = 0; i < lfo.size(); ++i)
    {
        const auto& s = runtime.settings.lfo[i];
        // FREE: the voice joins the free-running phase; otherwise it starts from PHASE.
        lfo[i].start (s, s.mode == LfoMode::free ? runtime.globalLfo[i].phase : s.phase);
        lfo[i].cycle = s.mode == LfoMode::free ? runtime.globalLfo[i].cycle : 0;
    }
    for (auto& e : env)
    {
        e.level = 0.0;
        e.start();
    }
    advance (runtime, 0.0);
}

void VoiceState::release() noexcept
{
    for (auto& e : env)
        e.release();
}

void VoiceState::advance (const Runtime& runtime, double seconds) noexcept
{
    const auto& settings = runtime.settings;
    for (std::size_t i = 0; i < lfo.size(); ++i)
    {
        const auto& s = settings.lfo[i];
        if (s.scope == Scope::global)
        {
            values[i] = runtime.globalValue[i];
            continue;
        }
        if (s.sync && s.mode == LfoMode::free && runtime.hostPlaying)
            lfo[i].follow (s, runtime.ppq);
        else
            lfo[i].advance (s, seconds, runtime.bpm);
        lfo[i].evaluate (s, settings.lfoCurve[i], Prng::deriveSeed (settings.seed, 0x6c666f31ull + i, 0));
        values[i] = static_cast<float> (lfo[i].value);
    }
    for (std::size_t i = 0; i < env.size(); ++i)
    {
        env[i].advance (settings.env[i], settings.envCurve[i], seconds);
        values[2 + i] = static_cast<float> (env[i].level);
    }
}

} // namespace osp::mod
