#pragma once

#include "engine/HostTiming.h"

#include <array>
#include <cstdint>

namespace osp::mod
{

/**
    OSP's modulation system: two LFOs and two modulation envelopes, routed to a fixed
    registry of destinations by up to 16 routes. It is a control layer: it never changes a
    stored parameter, it computes effective values next to them (the engine adds the
    contributions where each destination is used).

    Sources:
      LFO 1, LFO 2   global (one phase for the instrument) or poly (a phase per voice);
                     FREE, RETRIGGER or ONE SHOT; synced to the host or free in Hz.
      ENV 1, ENV 2   per voice: an ADSR (or a one-shot curve) started by each note,
                     released by its note-off (ARP notes included). For the shared stages
                     each envelope also runs once for the whole instrument (the "global
                     envelope"): every note-on restarts it from where it is, it releases
                     when the last key (and the sustain pedal) lets go.
      MOD WHEEL      MIDI CC 1 (and the on-screen wheel), 0..1, smoothed over ~10 ms.

    Destinations have an owner:
      global  shared stages (the macros, each layer's EQ): one value for the instrument, so
              global LFOs, the global envelopes and the mod wheel (a poly LFO has no single
              value there and is refused);
      voice   read by each voice (layer LEVEL, PAN, FINE TUNE, START, REIMAGINED, granular
              POS / DENS / SIZE / SPREAD, CHARACTER cutoff and resonance, the amp envelope,
              the modulation envelopes' own times): any source the rules allow.

    When a destination is read (Update): continuously, when a note starts (START, envelope
    attack and decay) or when it is released (envelope release).

    The graph is acyclic by construction: only the envelopes' own settings are destinations
    owned by a source, and only the LFOs and the mod wheel may reach them (nothing modulates
    an LFO or the wheel), so no chain can return to where it began; createsCycle() checks
    it anyway. Deterministic: the smooth-random shape draws one value per cycle from the
    stored seed and the cycle index, so a host-synced LFO repeats exactly when the song does.

    Pure C++, real-time safe: the settings are plain values (no allocation), compiled once
    per change into flat per-destination lists.
*/

enum class Source : std::uint8_t
{
    none = 0,
    lfo1,
    lfo2,
    env1,
    env2,
    modWheel   ///< appended (the route's choice order is saved)
};
constexpr int sourceCount = 5;   ///< lfo1 .. modWheel
inline int sourceIndex (Source s) noexcept { return static_cast<int> (s) - 1; }
const char* sourceName (Source s) noexcept;   ///< "LFO 1", "ENV 2"

enum class LfoShape : std::uint8_t
{
    sine,
    triangle,
    rampUp,
    rampDown,
    pulse,
    smoothRandom,
    custom
};
constexpr int lfoShapeCount = 7;
const char* lfoShapeName (LfoShape s) noexcept;

enum class LfoMode : std::uint8_t
{
    free,        ///< continuous; a poly voice starts where the free-running phase is
    retrigger,   ///< starts from PHASE at each note (global: a note after silence)
    oneShot      ///< one cycle from PHASE, then holds its last value
};

enum class Scope : std::uint8_t
{
    global,
    poly
};

/** Host-synced cycle lengths, in quarter notes (choice order is saved: append only). */
constexpr int syncDivisionCount = 15;
double syncQuarters (int division) noexcept;
const char* syncName (int division) noexcept;   ///< "1/32" .. "8 bars", "1/8D", "1/4T"

/** A breakpoint curve compiled to a table (custom LFO shapes, one-shot envelopes). */
constexpr int curvePoints = 256;
using CurveTable = std::array<float, curvePoints>;   ///< 0..1 over one cycle

/** The editable form: up to 16 points from x 0 to x 1 (ends fixed in x), each segment bent
    by its end point's tension (-1 fast start .. +1 slow start). */
struct CurvePoint
{
    float x = 0.0f, y = 0.0f, tension = 0.0f;
    bool operator== (const CurvePoint&) const = default;
};
constexpr int maxCurvePoints = 16;
struct Curve
{
    std::array<CurvePoint, maxCurvePoints> points {};
    int count = 0;
    bool operator== (const Curve& o) const
    {
        if (count != o.count)
            return false;
        for (int i = 0; i < count; ++i)
            if (! (points[static_cast<std::size_t> (i)] == o.points[static_cast<std::size_t> (i)]))
                return false;
        return true;
    }
    /** Keeps it valid: 2..16 points, sorted, ends at x 0 and 1, values 0..1. */
    void normalise() noexcept;
    /** Adds a point (false when full); removes one (never an end). */
    bool add (CurvePoint p) noexcept;
    void remove (int index) noexcept;
};
Curve defaultLfoCurve() noexcept;   ///< a soft rise and fall
Curve defaultEnvCurve() noexcept;   ///< a falling sweep
CurveTable compileCurve (const Curve& curve) noexcept;

struct LfoSettings
{
    LfoShape shape = LfoShape::sine;
    double rateHz = 1.0;        ///< free rate, 0.01..30 Hz
    bool sync = false;
    int division = 3;           ///< 1/4
    double phase = 0.0;         ///< 0..1: where RETRIGGER and ONE SHOT start (and an offset on the synced grid)
    bool bipolar = true;        ///< -1..+1, else 0..1
    LfoMode mode = LfoMode::free;
    Scope scope = Scope::global;
};

struct EnvSettings
{
    bool oneShotCurve = false;  ///< false: ADSR; true: the curve, once, over `lengthSeconds`
    double attackSeconds = 0.01;
    double decaySeconds = 0.3;
    double sustain = 0.6;       ///< 0..1
    double releaseSeconds = 0.4;
    double curve = 0.0;         ///< -1 (fast start) .. 0 (straight) .. +1 (slow start)
    double lengthSeconds = 1.0; ///< one-shot curve length
};

/** The destinations (choice order is saved: append only, never reorder). */
enum class Dest : std::uint8_t
{
    none = 0,
    life,
    drive,
    character,
    movement,
    space,
    levelA, levelB, levelC,
    panA, panB, panC,
    reimaginedA, reimaginedB, reimaginedC,
    fineTuneA, fineTuneB, fineTuneC,
    grainPositionA, grainPositionB, grainPositionC,
    grainDensityA, grainDensityB, grainDensityC,
    grainSizeA, grainSizeB, grainSizeC,
    grainSpreadA, grainSpreadB, grainSpreadC,
    cutoff,
    resonance,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    // Each layer's EQ (after its voices are summed: a shared stage of the layer).
    eqBellFrequencyA, eqBellFrequencyB, eqBellFrequencyC,
    eqBellGainA, eqBellGainB, eqBellGainC,
    eqLowShelfGainA, eqLowShelfGainB, eqLowShelfGainC,
    eqHighShelfGainA, eqHighShelfGainB, eqHighShelfGainC,
    // Appended later (ANDOR/OSP refinement).
    echo,
    startA, startB, startC,   ///< where a new note begins in the layer's recording (taken at note-on)
    env1Attack, env1Decay, env1Sustain, env1Release,
    env2Attack, env2Decay, env2Sustain, env2Release,
    count
};
constexpr int destCount = static_cast<int> (Dest::count);

enum class Owner : std::uint8_t
{
    global,   ///< a shared stage: global sources only
    voice     ///< read per voice: any source
};

enum class Domain : std::uint8_t
{
    unit,       ///< 0..1 amounts (macros, POS, SPREAD, REIMAGINED, SUSTAIN)
    pan,        ///< -1..1
    decibels,   ///< level
    cents,      ///< pitch
    octaves,    ///< frequency and time ratios (CHARACTER cutoff, grain size / density, envelope times)
};

enum class Update : std::uint8_t
{
    continuous,   ///< followed at control rate
    noteOn,       ///< taken when a note starts (attack, decay)
    noteOff       ///< taken when a note is released (release)
};

struct DestInfo
{
    const char* id;        ///< stable identifier (saved by name in nothing; for reports and tests)
    const char* name;      ///< "CHARACTER CUTOFF", "POS A"
    Owner owner;
    int layer;             ///< -1: the whole instrument
    Domain domain;
    double span;           ///< what full depth (100 %) moves, in the domain's units
    Update update;
    Source modulates = Source::none;   ///< a source's own setting (the envelopes' times): its owner
};
const DestInfo& destInfo (Dest d) noexcept;

struct Route
{
    Source source = Source::none;
    Dest dest = Dest::none;
    double depth = 0.0;    ///< -1..1 (a negative depth inverts the source)
    bool enabled = true;
};
constexpr int maxRoutes = 16;

/** Everything the modulation system needs, as one plain value (copied to the audio thread). */
struct Settings
{
    std::array<LfoSettings, 2> lfo {};
    std::array<EnvSettings, 2> env {};
    std::array<Route, maxRoutes> routes {};
    std::array<CurveTable, 2> lfoCurve {};   ///< LFO custom shapes
    std::array<CurveTable, 2> envCurve {};   ///< envelope one-shot curves
    std::uint64_t seed = 1;

    Settings();
};

/** Why a route does nothing (for the routing list). */
enum class RouteState : std::uint8_t
{
    active,
    empty,        ///< no source or destination, or depth 0
    bypassed,
    scope         ///< a per-voice source on a shared destination
};
RouteState routeState (const Settings& settings, const Route& route) noexcept;
/** A source can drive this destination: the scope rule (a poly LFO has no single value on a
    shared stage), the timing rule (an envelope has no value yet when a note starts, so it
    cannot choose START) and the source rule (an envelope's settings take LFOs and the wheel). */
bool compatible (const Settings& settings, Source source, Dest dest) noexcept;
/** Adding source -> dest would close a loop through the routes already set (source to the
    source that owns a destination). With the rules above it never does; the assignment
    path checks it all the same. */
bool createsCycle (const Settings& settings, Source source, Dest dest) noexcept;
bool isPolySource (const Settings& settings, Source source) noexcept;

/** Routes flattened per destination for the audio thread. */
struct Compiled
{
    struct Term
    {
        std::uint8_t source;   ///< sourceIndex
        float depth;
    };
    static constexpr int maxTerms = maxRoutes;
    std::array<std::array<Term, maxTerms>, destCount> terms {};
    std::array<std::uint8_t, destCount> termCount {};
    bool any = false;            ///< any active route
    bool anyGlobalDest = false;  ///< a route to a shared stage (the engine then updates them at control rate)
    bool anyVoiceDest = false;
    std::array<bool, sourceCount> used {};   ///< a source feeds an active route

    void compile (const Settings& settings) noexcept;
    /** Sum of depth x value over the destination's routes (values indexed by source). */
    double sum (Dest d, const std::array<float, sourceCount>& values) const noexcept
    {
        const auto i = static_cast<std::size_t> (d);
        double s = 0.0;
        for (int k = 0; k < termCount[i]; ++k)
            s += static_cast<double> (terms[i][static_cast<std::size_t> (k)].depth) * values[terms[i][static_cast<std::size_t> (k)].source];
        return s;
    }
    bool has (Dest d) const noexcept { return termCount[static_cast<std::size_t> (d)] > 0; }
};

/** The shape of an LFO at phase 0..1, bipolar -1..1 (`cycle` and `seed` for smooth random). */
double lfoShapeValue (LfoShape shape, double phase, const CurveTable& curve, std::uint64_t seed, std::int64_t cycle) noexcept;
/** The LFO's output at a phase: its shape, in its polarity. */
double lfoOutput (const LfoSettings& s, double phase, const CurveTable& curve, std::uint64_t seed, std::int64_t cycle) noexcept;
/** Linear table read (0..1 over the table). */
double curveAt (const CurveTable& curve, double x) noexcept;
/** A shaped segment 0..1 -> 0..1 (curve -1 fast start .. +1 slow start). */
double shapeSegment (double x, double curve) noexcept;

/** One LFO's running state (the same code for the global LFO and each voice's). */
struct LfoState
{
    double phase = 0.0;          ///< 0..1 within the cycle
    std::int64_t cycle = 0;
    bool finished = false;       ///< ONE SHOT done
    double travelled = 0.0;      ///< cycles run since start (ONE SHOT ends at 1)
    double value = 0.0;

    void start (const LfoSettings& s, double fromPhase) noexcept;
    /** Advances `seconds` at the free (or tempo-derived) rate. */
    void advance (const LfoSettings& s, double seconds, double bpm) noexcept;
    /** Host-synced and playing: the phase is the song position's (no drift, loops and jumps follow). */
    void follow (const LfoSettings& s, double ppq) noexcept;
    void evaluate (const LfoSettings& s, const CurveTable& curve, std::uint64_t seed) noexcept;
};

/** One modulation envelope's running state (per voice). */
struct EnvState
{
    enum class Stage : std::uint8_t { idle, attack, decay, sustain, release, done };
    Stage stage = Stage::idle;
    double t = 0.0;              ///< seconds in the stage
    double elapsed = 0.0;        ///< seconds since the note started (the one-shot curve's clock)
    double level = 0.0;          ///< output 0..1
    double from = 0.0;           ///< level the stage started at

    void start() noexcept;
    void release() noexcept;
    void advance (const EnvSettings& s, const CurveTable& curve, double seconds) noexcept;
};

/**
    The audio thread's view (owned by the engine, read by every voice): the compiled routes,
    the settings, and this control period's global LFO values. Voices add their own poly
    values on top.
*/
struct Runtime
{
    Settings settings;
    Compiled compiled;
    std::array<LfoState, 2> globalLfo {};
    /** The sources with one value for the instrument, as voices read them: the global LFOs
        and the wheel (poly LFOs and the envelopes: 0, a voice has its own). */
    std::array<float, sourceCount> globalValue {};
    /** The same plus the global envelopes: what the shared stages hear. */
    std::array<float, sourceCount> sharedValue {};
    std::array<EnvState, 2> globalEnv {};
    double bpm = 120.0;
    bool hostPlaying = false;
    double ppq = 0.0;
    int heldNotes = 0;          ///< keys held (a global RETRIGGER LFO restarts after silence)
    bool pedal = false;
    float modWheelTarget = 0.0f, modWheelNow = 0.0f;

    void setSettings (const Settings& s) noexcept;
    /** Advances the global LFOs, envelopes and the wheel by `samples` at `sampleRate` (at the
        host's position when it plays) and refreshes globalValue and sharedValue. */
    void advanceGlobal (int samples, double sampleRate) noexcept;
    /** A key went down: a global RETRIGGER / ONE SHOT LFO restarts when it is the first held,
        the global envelopes restart. A key pressed again without its note-off counts once. */
    void noteStarted (int note = 0, int channel = 1) noexcept;
    void noteEnded (int note = 0, int channel = 1) noexcept;
    void setPedal (bool down) noexcept;
    void allNotesOff() noexcept;
    void setModWheel (float value) noexcept { modWheelTarget = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
    void setTiming (const HostTiming& timing) noexcept;

    /** An envelope's settings for a note starting now, its times moved by the routes to them
        (`values`: the sources' values as it starts). */
    EnvSettings envAtStart (int env, const std::array<float, sourceCount>& values) const noexcept;
    /** ... and at its release (the release time), and while it runs (sustain). */
    double envReleaseSeconds (int env, const std::array<float, sourceCount>& values) const noexcept;
    double envSustain (int env, const std::array<float, sourceCount>& values) const noexcept;
    /** What a running envelope follows: the stored settings, with the times it took. */
    EnvSettings envRunning (int env, const EnvSettings& taken, const std::array<float, sourceCount>& values) const noexcept;

private:
    std::array<std::uint16_t, 128> keysDown {};   ///< a bit per MIDI channel
    std::array<EnvSettings, 2> globalEnvNow {};
    double offsetOf (Dest d, const std::array<float, sourceCount>& values) const noexcept
    {
        return compiled.has (d) ? destInfo (d).span * compiled.sum (d, values) : 0.0;
    }
    void releaseGlobalEnvelopes() noexcept;
};

/** One voice's modulation (its poly LFOs and envelopes) and the offsets it applies. */
struct VoiceState
{
    std::array<LfoState, 2> lfo {};
    std::array<EnvState, 2> env {};
    std::array<EnvSettings, 2> envNow {};       ///< this note's envelope settings (times taken at its start)
    std::array<float, sourceCount> values {};   ///< global and this voice's sources, now

    void start (const Runtime& runtime) noexcept;
    void release (const Runtime& runtime) noexcept;
    /** Advances this voice's sources by `seconds` and gathers every source's value. */
    void advance (const Runtime& runtime, double seconds) noexcept;
    /** The summed contribution to a destination, in its domain's units (span x depth x value). */
    double offset (const Runtime& runtime, Dest d) const noexcept
    {
        return runtime.compiled.has (d) ? destInfo (d).span * runtime.compiled.sum (d, values) : 0.0;
    }
};

} // namespace osp::mod
