#pragma once

#include "engine/Modulation.h"

#include <array>
#include <cstdint>

namespace osp::drums
{

/**
    A pad's own modulation: a few musical sources, each sent to up to three of a drum's
    playable aspects. It is built on OSP's modulation system (engine/Modulation.h): every
    pad already plays through its own InstrumentEngine, so a pad's settings compile into
    that engine's mod::Settings. With no route the pad renders exactly as without
    modulation.

    Sources:
      ENV       a per-hit envelope (attack, decay, sustain, release): a pitch drop on a kick,
                a filter sweep on a snare
      LFO       one LFO per pad; RETRIGGER restarts it on each hit, or it runs freely or in
                time with the host
      VELOCITY  how hard the pad is hit (its own curve: SOFT / LINEAR / HARD)
      WHEEL     the mod wheel (CC 1)
      PRESSURE  channel aftertouch

    Destinations (the voice's, so every hit carries its own):
      PITCH, LEVEL, PAN, FILTER (CHARACTER's cutoff), START (taken as the hit starts),
      DECAY (the AMP envelope's decay, taken as the hit starts).

    Pure C++ (no JUCE), real-time safe: a plain value, compiled with toSettings().
*/

enum class ModSource : std::uint8_t
{
    env,
    lfo,
    velocity,
    wheel,
    pressure
};
constexpr int modSourceCount = 5;
const char* modSourceName (ModSource s) noexcept;   ///< "ENV", "LFO", "VELOCITY", "WHEEL", "PRESSURE"

/** Saved by index: append only. */
enum class ModTarget : std::uint8_t
{
    none,
    pitch,
    level,
    pan,
    filter,
    start,
    decay
};
constexpr int modTargetCount = 7;
const char* modTargetName (ModTarget t) noexcept;   ///< "PITCH", "LEVEL", ... ("—" for none)
mod::Dest modTargetDest (ModTarget t) noexcept;
/** A depth (-1..1) in the target's own words: "+12.0 st", "-6.0 dB", "+40 %". */
void describeDepth (ModTarget t, double depth, char* out, int size) noexcept;

struct ModSlot
{
    ModTarget target = ModTarget::none;
    /** -1..1. PITCH: 24 st, LEVEL: 54 dB, DECAY: x 20, the rest their whole range.
        VELOCITY: the hardest hit plays the knob's setting and the softest is moved by -depth
        (so +45 % on FILTER: soft hits darker). The other sources move it by depth x value. */
    double depth = 0.0;
    bool operator== (const ModSlot& o) const noexcept { return target == o.target && ! (depth < o.depth) && ! (depth > o.depth); }
};
constexpr int modSlotsPerSource = 3;

/** VELOCITY's curve (exactly DYNAMICS' three). */
enum class VelocityShape : std::uint8_t
{
    linear,
    soft,
    hard
};

struct PadModulation
{
    mod::EnvSettings env;            ///< a hit envelope by default: instant attack, 0.15 s decay, no sustain
    mod::LfoSettings lfo;            ///< 4 Hz sine, restarted by each hit
    VelocityShape velocityShape = VelocityShape::linear;
    std::array<std::array<ModSlot, modSlotsPerSource>, modSourceCount> slots {};

    PadModulation();
    bool operator== (const PadModulation& o) const noexcept;

    ModSlot& slot (ModSource s, int i) noexcept { return slots[static_cast<std::size_t> (s)][static_cast<std::size_t> (i)]; }
    const ModSlot& slot (ModSource s, int i) const noexcept { return slots[static_cast<std::size_t> (s)][static_cast<std::size_t> (i)]; }
    /** Any slot with a target and a depth (else the pad plays exactly unmodulated). */
    bool active() const noexcept;
    /** How many slots of this source are in use (for the tab's dot). */
    int activeSlots (ModSource s) const noexcept;
    /** The engine's form; `seed`: the pad's (smooth-random LFO shapes repeat with it). */
    mod::Settings toSettings (std::uint64_t seed) const noexcept;
};

/** Starting points a musician can pick (each a complete PadModulation). */
enum class ModPreset : std::uint8_t
{
    none,
    kickPitchDrop,     ///< ENV -> PITCH: the classic falling pitch of an electronic kick
    velocityBright,    ///< VELOCITY -> FILTER: soft hits darker
    velocityPitch,     ///< VELOCITY -> PITCH: harder hits a little higher, like a real drum
    tomBend,           ///< ENV -> PITCH: a tom's pitch settling after the hit
    hatMotion,         ///< LFO -> PAN and FILTER: moving hats
    ghostStart         ///< VELOCITY -> START: soft hits lose their attack (ghost notes)
};
constexpr int modPresetCount = 7;
const char* modPresetName (ModPreset p) noexcept;
PadModulation modPreset (ModPreset p) noexcept;

} // namespace osp::drums
