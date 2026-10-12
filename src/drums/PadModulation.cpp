#include "drums/PadModulation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace osp::drums
{

namespace
{
    bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

    // What a slot's full depth (1) moves, as a share of OSP's destination span. PITCH: 24
    // semitones (OSP's span is 48 st and a route stops at the tune knob's +-24 st, so half the
    // knob would do nothing). DECAY: x / / 20 (OSP's span is the whole 1 ms .. 20 s).
    double targetScale (ModTarget t) noexcept
    {
        switch (t)
        {
            case ModTarget::pitch: return 0.5;
            case ModTarget::decay: return std::log2 (20.0) / mod::destInfo (mod::Dest::ampDecay).span;
            default: return 1.0;
        }
    }

    mod::Source engineSource (ModSource s) noexcept
    {
        switch (s)
        {
            case ModSource::env: return mod::Source::env1;
            case ModSource::lfo: return mod::Source::lfo1;
            case ModSource::velocity: return mod::Source::vel;
            case ModSource::wheel: return mod::Source::modWheel;
            case ModSource::pressure: return mod::Source::pressure;
        }
        return mod::Source::none;
    }

    mod::CurveTable velocityTable (VelocityShape shape) noexcept
    {
        const auto preset = shape == VelocityShape::soft ? mod::CurvePreset::soft
                          : shape == VelocityShape::hard ? mod::CurvePreset::hard
                                                         : mod::CurvePreset::linear;
        // VELOCITY moves softer hits away from the knob (the hardest hit plays the knob's own
        // setting): the engine's VEL value is 1 - curve (0 at velocity 127), the depth negated.
        auto table = mod::compileCurve (mod::presetCurve (preset));
        for (auto& v : table)
            v = 1.0f - v;
        return table;
    }
} // namespace

const char* modSourceName (ModSource s) noexcept
{
    switch (s)
    {
        case ModSource::env: return "ENV";
        case ModSource::lfo: return "LFO";
        case ModSource::velocity: return "VELOCITY";
        case ModSource::wheel: return "WHEEL";
        case ModSource::pressure: return "PRESSURE";
    }
    return "";
}

const char* modTargetName (ModTarget t) noexcept
{
    switch (t)
    {
        case ModTarget::none: return "\xe2\x80\x94";
        case ModTarget::pitch: return "PITCH";
        case ModTarget::level: return "LEVEL";
        case ModTarget::pan: return "PAN";
        case ModTarget::filter: return "FILTER";
        case ModTarget::start: return "START";
        case ModTarget::decay: return "DECAY";
    }
    return "";
}

mod::Dest modTargetDest (ModTarget t) noexcept
{
    switch (t)
    {
        case ModTarget::pitch: return mod::Dest::fineTuneA;
        case ModTarget::level: return mod::Dest::levelA;
        case ModTarget::pan: return mod::Dest::panA;
        case ModTarget::filter: return mod::Dest::cutoff;
        case ModTarget::start: return mod::Dest::startA;
        case ModTarget::decay: return mod::Dest::ampDecay;
        case ModTarget::none: break;
    }
    return mod::Dest::none;
}

void describeDepth (ModTarget t, double depth, char* out, int size) noexcept
{
    if (out == nullptr || size <= 0)
        return;
    const auto n = static_cast<std::size_t> (size);
    switch (t)
    {
        case ModTarget::pitch: std::snprintf (out, n, "%+.1f st", depth * 24.0); break;
        case ModTarget::level: std::snprintf (out, n, "%+.1f dB", depth * mod::destInfo (mod::Dest::levelA).span); break;
        case ModTarget::decay:
        {
            const double factor = std::exp2 (std::abs (depth) * std::log2 (20.0));
            std::snprintf (out, n, depth < 0.0 ? "/ %.1f" : "x %.1f", factor);
            break;
        }
        case ModTarget::none: std::snprintf (out, n, "%s", ""); break;
        default: std::snprintf (out, n, "%+.0f %%", depth * 100.0); break;
    }
}

PadModulation::PadModulation()
{
    env.oneShotCurve = false;
    env.attackSeconds = 0.0;
    env.decaySeconds = 0.15;
    env.sustain = 0.0;
    env.releaseSeconds = 0.1;
    env.curve = -0.5;   // falls fast, then eases: a struck sound's shape
    lfo.shape = mod::LfoShape::sine;
    lfo.rateHz = 4.0;
    lfo.mode = mod::LfoMode::retrigger;
    lfo.scope = mod::Scope::poly;   // every hit has its own LFO
    lfo.bipolar = true;
}

bool PadModulation::operator== (const PadModulation& o) const noexcept
{
    auto sameEnv = [] (const mod::EnvSettings& a, const mod::EnvSettings& b) {
        return a.oneShotCurve == b.oneShotCurve && same (a.attackSeconds, b.attackSeconds) && same (a.decaySeconds, b.decaySeconds)
            && same (a.sustain, b.sustain) && same (a.releaseSeconds, b.releaseSeconds) && same (a.curve, b.curve)
            && same (a.lengthSeconds, b.lengthSeconds);
    };
    auto sameLfo = [] (const mod::LfoSettings& a, const mod::LfoSettings& b) {
        return a.shape == b.shape && same (a.rateHz, b.rateHz) && a.sync == b.sync && a.division == b.division && same (a.phase, b.phase)
            && a.bipolar == b.bipolar && a.mode == b.mode && a.scope == b.scope;
    };
    return sameEnv (env, o.env) && sameLfo (lfo, o.lfo) && velocityShape == o.velocityShape && slots == o.slots;
}

bool PadModulation::active() const noexcept
{
    for (const auto& source : slots)
        for (const auto& s : source)
            if (s.target != ModTarget::none && ! same (s.depth, 0.0))
                return true;
    return false;
}

int PadModulation::activeSlots (ModSource source) const noexcept
{
    int n = 0;
    for (const auto& s : slots[static_cast<std::size_t> (source)])
        n += s.target != ModTarget::none && ! same (s.depth, 0.0) ? 1 : 0;
    return n;
}

mod::Settings PadModulation::toSettings (std::uint64_t seed) const noexcept
{
    mod::Settings s;
    s.seed = seed;
    s.lfo[0] = lfo;
    s.env[0] = env;
    s.velCurve = velocityTable (velocityShape);
    s.velocityToAmp = false;   // DYNAMICS keeps the hit's loudness; VELOCITY here is a source only
    int r = 0;
    for (int src = 0; src < modSourceCount; ++src)
        for (int i = 0; i < modSlotsPerSource && r < mod::maxRoutes; ++i)
        {
            const auto source = static_cast<ModSource> (src);
            const auto& slot = this->slot (source, i);
            if (slot.target == ModTarget::none || same (slot.depth, 0.0))
                continue;
            auto& route = s.routes[static_cast<std::size_t> (r++)];
            route.source = engineSource (source);
            route.dest = modTargetDest (slot.target);
            const double sign = source == ModSource::velocity ? -1.0 : 1.0;
            route.depth = std::clamp (sign * slot.depth * targetScale (slot.target), -1.0, 1.0);
        }
    return s;
}

const char* modPresetName (ModPreset p) noexcept
{
    switch (p)
    {
        case ModPreset::none: return "No modulation";
        case ModPreset::kickPitchDrop: return "Kick pitch drop";
        case ModPreset::velocityBright: return "Harder is brighter";
        case ModPreset::velocityPitch: return "Harder is higher";
        case ModPreset::tomBend: return "Tom pitch bend";
        case ModPreset::hatMotion: return "Moving hats";
        case ModPreset::ghostStart: return "Soft ghost notes";
    }
    return "";
}

PadModulation modPreset (ModPreset p) noexcept
{
    PadModulation m;
    switch (p)
    {
        case ModPreset::none: break;
        case ModPreset::kickPitchDrop:
            m.env.decaySeconds = 0.06;
            m.slot (ModSource::env, 0) = { ModTarget::pitch, 0.5 };          // +12 st at the hit, falling
            break;
        case ModPreset::velocityBright:
            m.slot (ModSource::velocity, 0) = { ModTarget::filter, 0.45 };   // soft hits 45 % darker
            break;
        case ModPreset::velocityPitch:
            m.slot (ModSource::velocity, 0) = { ModTarget::pitch, 1.0 / 24.0 };   // a semitone over the range
            break;
        case ModPreset::tomBend:
            m.env.decaySeconds = 0.25;
            m.slot (ModSource::env, 0) = { ModTarget::pitch, 0.125 };        // +3 st, settling
            break;
        case ModPreset::hatMotion:
            m.lfo.shape = mod::LfoShape::triangle;
            m.lfo.mode = mod::LfoMode::free;
            m.lfo.scope = mod::Scope::global;
            m.lfo.sync = true;
            m.lfo.division = 5;   // 1 bar
            m.slot (ModSource::lfo, 0) = { ModTarget::pan, 0.25 };
            m.slot (ModSource::lfo, 1) = { ModTarget::filter, -0.15 };
            break;
        case ModPreset::ghostStart:
            m.slot (ModSource::velocity, 0) = { ModTarget::start, -0.04 };   // the softest hit starts 4 % in
            m.slot (ModSource::velocity, 1) = { ModTarget::filter, 0.25 };
            break;
    }
    return m;
}

} // namespace osp::drums
