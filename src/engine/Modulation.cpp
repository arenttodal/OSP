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
        case Source::modWheel: return "MOD WHEEL";
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
        { "echo", "ECHO", Owner::global, -1, Domain::unit, 1.0, Update::continuous },
        // START: read once as each note begins (a sounding note never jumps); 100 % moves it
        // half the recording.
        { "startA", "START A", Owner::voice, 0, Domain::unit, 0.5, Update::noteOn },
        { "startB", "START B", Owner::voice, 1, Domain::unit, 0.5, Update::noteOn },
        { "startC", "START C", Owner::voice, 2, Domain::unit, 0.5, Update::noteOn },
        // The modulation envelopes' own times: attack and decay taken as a note starts,
        // release as it is released, sustain followed (as the amp envelope's).
        { "env1Attack", "ENV 1 ATTACK", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOn, Source::env1 },
        { "env1Decay", "ENV 1 DECAY", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOn, Source::env1 },
        { "env1Sustain", "ENV 1 SUSTAIN", Owner::voice, -1, Domain::unit, 1.0, Update::continuous, Source::env1 },
        { "env1Release", "ENV 1 RELEASE", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOff, Source::env1 },
        { "env2Attack", "ENV 2 ATTACK", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOn, Source::env2 },
        { "env2Decay", "ENV 2 DECAY", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOn, Source::env2 },
        { "env2Sustain", "ENV 2 SUSTAIN", Owner::voice, -1, Domain::unit, 1.0, Update::continuous, Source::env2 },
        { "env2Release", "ENV 2 RELEASE", Owner::voice, -1, Domain::octaves, 4.0, Update::noteOff, Source::env2 },
        // Another route's depth (Route::target): read wherever that route is read.
        { "routeDepth", "ROUTE DEPTH", Owner::voice, -1, Domain::unit, 1.0, Update::continuous },
    } };

    bool isEnvelope (Source s) noexcept { return s == Source::env1 || s == Source::env2; }

    /** A depth route's target, when it is an ordinary route (else -1). */
    int targetOf (const Settings& settings, const Route& r) noexcept
    {
        if (! isDepthRoute (r) || r.target < 0 || r.target >= maxRoutes)
            return -1;
        const auto& t = settings.routes[static_cast<std::size_t> (r.target)];
        return &t != &r && t.source != Source::none && t.dest != Dest::none && ! isDepthRoute (t) && static_cast<int> (t.dest) < destCount ? r.target : -1;
    }

    /** Where a route's source ends up: its destination, or a depth route's route's. */
    Dest effectiveDest (const Settings& settings, const Route& r) noexcept
    {
        if (! isDepthRoute (r))
            return r.dest;
        const int t = targetOf (settings, r);
        return t >= 0 ? settings.routes[static_cast<std::size_t> (t)].dest : Dest::none;
    }
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
        case Source::modWheel:
        case Source::none: break;
    }
    return false;
}

bool compatible (const Settings& settings, Source source, Dest dest) noexcept
{
    if (source == Source::none || dest == Dest::none || static_cast<int> (dest) >= destCount || static_cast<int> (source) > sourceCount)
        return false;
    // A route's depth is reached through depthRule (it needs the route).
    if (dest == Dest::routeDepth)
        return false;
    const auto& info = destInfo (dest);
    // An envelope's settings: the LFOs and the wheel only (no envelope drives an envelope).
    if (info.modulates != Source::none && (isEnvelope (source) || source == info.modulates))
        return false;
    // An envelope has no value yet when a note starts (an ADSR begins at 0): it cannot
    // choose where the note begins.
    if ((dest == Dest::startA || dest == Dest::startB || dest == Dest::startC) && isEnvelope (source))
        return false;
    if (info.owner == Owner::voice)
        return true;
    // A shared stage: one value for the instrument. The envelopes run a global instance for
    // it; a poly LFO has none.
    return isEnvelope (source) || ! isPolySource (settings, source);
}

bool createsCycle (const Settings& settings, Source source, Dest dest) noexcept
{
    // Edges run from a route's source to the source owning its destination. A new edge
    // source -> owner closes a loop when the owner already reaches the source.
    const auto owner = destInfo (dest).modulates;
    if (owner == Source::none)
        return false;
    if (owner == source)
        return true;
    std::array<bool, sourceCount + 1> reached {};
    std::array<Source, sourceCount + 1> stack {};
    int top = 0;
    stack[static_cast<std::size_t> (top++)] = owner;
    reached[static_cast<std::size_t> (owner)] = true;
    while (top > 0)
    {
        const auto from = stack[static_cast<std::size_t> (--top)];
        for (const auto& r : settings.routes)
        {
            if (r.source != from || r.dest == Dest::none || static_cast<int> (r.dest) >= destCount)
                continue;
            // A depth route reaches what its route reaches.
            const auto next = destInfo (effectiveDest (settings, r)).modulates;
            if (next == Source::none || reached[static_cast<std::size_t> (next)])
                continue;
            if (next == source)
                return true;
            reached[static_cast<std::size_t> (next)] = true;
            stack[static_cast<std::size_t> (top++)] = next;
        }
    }
    return false;
}

DepthRule depthRule (const Settings& settings, Source source, int target) noexcept
{
    if (target < 0 || target >= maxRoutes)
        return DepthRule::noRoute;
    const auto& t = settings.routes[static_cast<std::size_t> (target)];
    if (t.source == Source::none || t.dest == Dest::none || static_cast<int> (t.dest) >= destCount)
        return DepthRule::noRoute;
    if (isDepthRoute (t))
        return DepthRule::nested;
    if (source == t.source)
        return DepthRule::self;
    // The depth is read where the route's destination is: the same scope and timing rules.
    if (! compatible (settings, source, t.dest))
        return DepthRule::scope;
    if (createsCycle (settings, source, t.dest))
        return DepthRule::cycle;
    return DepthRule::ok;
}

RouteState routeState (const Settings& settings, int index) noexcept
{
    if (index < 0 || index >= maxRoutes)
        return RouteState::empty;
    const auto& route = settings.routes[static_cast<std::size_t> (index)];
    if (isDepthRoute (route))
    {
        if (route.source == Source::none || std::abs (route.depth) < 1.0e-6)
            return RouteState::empty;
        if (! route.enabled)
            return RouteState::bypassed;
        const int t = targetOf (settings, route);
        if (t < 0)
            return RouteState::noTarget;
        if (depthRule (settings, route.source, t) != DepthRule::ok)
            return RouteState::scope;
        // Its route must work (a route at depth 0 does, moved by this one).
        const auto target = routeState (settings, t);
        return target == RouteState::active ? RouteState::active : RouteState::noTarget;
    }
    const auto plain = routeState (settings, route);
    if (plain != RouteState::empty || route.source == Source::none || route.dest == Dest::none || static_cast<int> (route.dest) >= destCount)
        return plain;
    // Depth 0, but a depth route moves it: it works.
    for (int k = 0; k < maxRoutes; ++k)
    {
        const auto& m = settings.routes[static_cast<std::size_t> (k)];
        if (k != index && isDepthRoute (m) && m.target == index && m.source != Source::none && m.enabled && std::abs (m.depth) >= 1.0e-6
            && depthRule (settings, m.source, index) == DepthRule::ok)
            return route.enabled ? (compatible (settings, route.source, route.dest) ? RouteState::active : RouteState::scope) : RouteState::bypassed;
    }
    return plain;
}

RouteState routeState (const Settings& settings, const Route& route) noexcept
{
    if (isDepthRoute (route))
    {
        // Without the other routes: only whether it could work at all.
        if (route.source == Source::none || std::abs (route.depth) < 1.0e-6)
            return RouteState::empty;
        if (! route.enabled)
            return RouteState::bypassed;
        return targetOf (settings, route) >= 0 ? RouteState::active : RouteState::noTarget;
    }
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
    metaCount = 0;
    any = anyGlobalDest = anyVoiceDest = false;
    for (int index = 0; index < maxRoutes; ++index)
    {
        const auto& route = settings.routes[static_cast<std::size_t> (index)];
        if (isDepthRoute (route) || routeState (settings, index) != RouteState::active)
            continue;
        const auto d = static_cast<std::size_t> (route.dest);
        if (termCount[d] >= maxTerms)
            continue;
        const auto s = static_cast<std::uint8_t> (sourceIndex (route.source));
        Term term { s, static_cast<float> (std::clamp (route.depth, -1.0, 1.0)) };
        // Its depth routes, next to each other (one level: they are never moved themselves).
        term.metaFirst = static_cast<std::uint8_t> (metaCount);
        for (int k = 0; k < maxRoutes && metaCount < maxRoutes; ++k)
        {
            const auto& m = settings.routes[static_cast<std::size_t> (k)];
            if (isDepthRoute (m) && m.target == index && routeState (settings, k) == RouteState::active)
            {
                const auto ms = static_cast<std::uint8_t> (sourceIndex (m.source));
                meta[static_cast<std::size_t> (metaCount++)] = { ms, static_cast<float> (std::clamp (m.depth, -1.0, 1.0)) };
                used[ms] = true;
            }
        }
        term.metaCount = static_cast<std::uint8_t> (metaCount - term.metaFirst);
        terms[d][termCount[d]++] = term;
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
    {
        // A finished ONE SHOT holds only while it is a ONE SHOT: switched (or a preset loaded)
        // to FREE or RETRIGGER it runs on from where it held. (Before, the flag survived the
        // change and a global LFO stood still until the plugin was reloaded.)
        if (s.mode == LfoMode::oneShot)
            return;
        finished = false;
        travelled = 0.0;
    }
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
    // The wheel glides to the controller's value (CC 1 steps 1/127 at a time).
    const double k = 1.0 - std::exp (-seconds / 0.01);
    modWheelNow += static_cast<float> (k) * (modWheelTarget - modWheelNow);
    if (std::abs (modWheelTarget - modWheelNow) < 1.0e-6f)
        modWheelNow = modWheelTarget;
    globalValue[2] = globalValue[3] = 0.0f;   // a voice's envelopes are its own
    globalValue[4] = modWheelNow;
    sharedValue = globalValue;
    for (std::size_t i = 0; i < globalEnv.size(); ++i)
    {
        globalEnv[i].advance (envRunning (static_cast<int> (i), globalEnvNow[i], globalValue), settings.envCurve[i], seconds);
        sharedValue[2 + i] = static_cast<float> (globalEnv[i].level);
    }
    if (hostPlaying)
        ppq += seconds * bpm / 60.0;
}

EnvSettings Runtime::envAtStart (int e, const std::array<float, sourceCount>& values) const noexcept
{
    auto s = settings.env[static_cast<std::size_t> (e)];
    const auto first = e == 0 ? Dest::env1Attack : Dest::env2Attack;
    auto at = [first] (int k) { return static_cast<Dest> (static_cast<int> (first) + k); };
    if (compiled.has (at (0)))
        s.attackSeconds *= std::exp2 (offsetOf (at (0), values));
    if (compiled.has (at (1)))
        s.decaySeconds *= std::exp2 (offsetOf (at (1), values));
    s.sustain = envSustain (e, values);
    s.releaseSeconds = envReleaseSeconds (e, values);
    return s;
}

EnvSettings Runtime::envRunning (int e, const EnvSettings& taken, const std::array<float, sourceCount>& values) const noexcept
{
    // The stored settings as they are now, but the times this note took where routes move
    // them (without a route the envelope follows its knobs, as it always did).
    auto s = settings.env[static_cast<std::size_t> (e)];
    const auto first = e == 0 ? Dest::env1Attack : Dest::env2Attack;
    auto at = [first] (int k) { return static_cast<Dest> (static_cast<int> (first) + k); };
    if (compiled.has (at (0)))
        s.attackSeconds = taken.attackSeconds;
    if (compiled.has (at (1)))
        s.decaySeconds = taken.decaySeconds;
    if (compiled.has (at (2)))
        s.sustain = envSustain (e, values);
    if (compiled.has (at (3)))
        s.releaseSeconds = taken.releaseSeconds;
    return s;
}

double Runtime::envReleaseSeconds (int e, const std::array<float, sourceCount>& values) const noexcept
{
    const double base = settings.env[static_cast<std::size_t> (e)].releaseSeconds;
    const auto d = e == 0 ? Dest::env1Release : Dest::env2Release;
    return compiled.has (d) ? base * std::exp2 (offsetOf (d, values)) : base;
}

double Runtime::envSustain (int e, const std::array<float, sourceCount>& values) const noexcept
{
    const double base = settings.env[static_cast<std::size_t> (e)].sustain;
    const auto d = e == 0 ? Dest::env1Sustain : Dest::env2Sustain;
    return compiled.has (d) ? std::clamp (base + offsetOf (d, values), 0.0, 1.0) : base;
}

void Runtime::noteStarted (int note, int channel) noexcept
{
    const auto n = static_cast<std::size_t> (std::clamp (note, 0, 127));
    const auto bit = static_cast<std::uint16_t> (1u << static_cast<unsigned> (std::clamp (channel, 1, 16) - 1));
    if (heldNotes == 0)
        for (std::size_t i = 0; i < globalLfo.size(); ++i)
            if (settings.lfo[i].mode != LfoMode::free)
                globalLfo[i].start (settings.lfo[i], settings.lfo[i].phase);
    if ((keysDown[n] & bit) == 0)
    {
        keysDown[n] = static_cast<std::uint16_t> (keysDown[n] | bit);
        ++heldNotes;
    }
    // The global envelopes: every note restarts them from where they are (no jump), with
    // the times the routes give them now.
    for (std::size_t i = 0; i < globalEnv.size(); ++i)
    {
        globalEnvNow[i] = envAtStart (static_cast<int> (i), globalValue);
        globalEnv[i].start();
    }
}

void Runtime::noteEnded (int note, int channel) noexcept
{
    const auto n = static_cast<std::size_t> (std::clamp (note, 0, 127));
    // Channel 0: the note on any channel.
    const auto bits = channel <= 0 ? static_cast<std::uint16_t> (0xffff)
                                   : static_cast<std::uint16_t> (1u << static_cast<unsigned> (std::min (channel, 16) - 1));
    for (unsigned c = 0; c < 16; ++c)
        if ((bits & keysDown[n] & (1u << c)) != 0)
        {
            keysDown[n] = static_cast<std::uint16_t> (keysDown[n] & ~(1u << c));
            heldNotes = std::max (0, heldNotes - 1);
        }
    if (heldNotes == 0 && ! pedal)
        releaseGlobalEnvelopes();
}

void Runtime::setPedal (bool down) noexcept
{
    pedal = down;
    if (! down && heldNotes == 0)
        releaseGlobalEnvelopes();
}

void Runtime::allNotesOff() noexcept
{
    keysDown.fill (0);
    heldNotes = 0;
    pedal = false;
    releaseGlobalEnvelopes();
}

void Runtime::releaseGlobalEnvelopes() noexcept
{
    for (std::size_t i = 0; i < globalEnv.size(); ++i)
    {
        globalEnvNow[i].releaseSeconds = envReleaseSeconds (static_cast<int> (i), globalValue);
        globalEnv[i].release();
    }
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
    for (std::size_t i = 0; i < env.size(); ++i)
    {
        env[i].level = 0.0;
        env[i].start();
        envNow[i] = runtime.settings.env[i];
    }
    advance (runtime, 0.0);   // the sources' values as the note begins
    // The envelopes' times as this note starts (moved by LFO or wheel routes to them).
    for (std::size_t i = 0; i < env.size(); ++i)
        envNow[i] = runtime.envAtStart (static_cast<int> (i), values);
}

void VoiceState::release (const Runtime& runtime) noexcept
{
    for (std::size_t i = 0; i < env.size(); ++i)
    {
        envNow[i].releaseSeconds = runtime.envReleaseSeconds (static_cast<int> (i), values);
        env[i].release();
    }
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
    values[4] = runtime.globalValue[4];
    for (std::size_t i = 0; i < env.size(); ++i)
    {
        env[i].advance (runtime.envRunning (static_cast<int> (i), envNow[i], values), settings.envCurve[i], seconds);
        values[2 + i] = static_cast<float> (env[i].level);
    }
}

} // namespace osp::mod
