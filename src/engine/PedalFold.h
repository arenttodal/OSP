#pragma once

#include "analysis/continuation/ContinuationAnalyzer.h"
#include "model/ContinuationModel.h"
#include "engine/PedalLoopMode.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace osp
{

/**
    PEDAL LOOP for the REIMAGINED engines that read the recording with heads of their own
    (docs/research/pedal-reimagined). The shared traversal: it maps a head's linear source
    position (frames, moving forward as the note plays) to where it reads while the pedal holds
    the note, so every head of an engine (a main head, trailing ghosts, history taps) loops the
    same region, whichever way it moves.

    - Forward: the read wraps from `end - crossfade` to `start`, over the region's own aligned,
      correlated jump crossfade, as the voice's Forward does.
    - Ping-Pong: the read turns at both ends of the region over the pedal's turn crossfade (at
      most a quarter of the region), the turned read leaving the edge as a mirror.
    - Pressed in time: the note plays on to the first region whose loop point is still ahead.
      Pressed late (past every loop point): at most 50 ms of crossfade into the last region,
      at the level the note has there. Too close to the end: nothing (a finished recording is
      never brought back).
    - Released while the key is held: a forward read goes on into the recording, a backward one
      turns forward where it is (15 ms), a late loop returns to where it found the recording.

    The same analysed regions as the voice's Pedal Loop (ContinuationModel::pedal). A pure
    function of position plus a few scalars: no allocation, no locks, deterministic.
*/
class PedalFold
{
public:
    static constexpr int maxTaps = 4;

    /** Where to read for one linear position: up to four weighted positions (two during a
        loop crossfade, plus the recording itself while a late entry or a release fades). */
    struct Taps
    {
        std::array<double, maxTaps> pos {};
        std::array<float, maxTaps> gain {};
        int count = 0;
        void add (double p, float g) noexcept
        {
            if (count < maxTaps && g > 0.0f)
            {
                pos[static_cast<std::size_t> (count)] = p;
                gain[static_cast<std::size_t> (count)] = g;
                ++count;
            }
        }
    };

    /** Off: every position reads itself. */
    void reset() noexcept
    {
        phase = Phase::off;
        offset = 0.0;
        passCount = 0;
        wanted = tried = false;
    }

    /** A note starts: what it loops (Normal: never) and Ping-Pong's turn. */
    void configure (PedalLoopMode loopMode, double turnFrames, float turnCorrelation) noexcept
    {
        reset();
        configuredMode = loopMode;
        configuredTurn = turnFrames;
        configuredCorr = turnCorrelation;
    }
    PedalLoopMode configuredLoop() const noexcept { return configuredMode; }

    /** The latest pedal (taken by follow(); a change during a release waits for it to end). */
    void want (bool down) noexcept
    {
        wanted = down && configuredMode != PedalLoopMode::normal;
        tried = false;
    }

    /** Applies the latest pedal with the head at `p` (call as it moves). Returns how far the
        head must move when a release has just completed (the read goes on where the loop
        left it; 0 otherwise): engines add it to every head. */
    double follow (const PedalLoopModel& model, double p, double sampleRate, double soundEnd) noexcept
    {
        if (phase == Phase::release)
        {
            update (p);
            if (phase != Phase::off)
                return 0.0;
            const double moved = offset;
            offset = 0.0;
            if (wanted)
                tried = false;   // pressed again during the release: arm now
            return moved;
        }
        if (wanted && phase == Phase::off && ! tried)
        {
            tried = true;
            arm (model, configuredMode, p, sampleRate, soundEnd, configuredTurn, configuredCorr);
        }
        else if (! wanted && phase == Phase::loop)
            release (p, sampleRate);
        return 0.0;
    }

    /** The pedal holds the note in a loop (from arming, including the late entry). */
    bool holding() const noexcept { return phase == Phase::loop; }
    /** Holding, or fading back to the recording after the pedal let go. */
    bool engaged() const noexcept { return phase != Phase::off; }
    /** Holding and past the first loop point (the loop has begun). */
    bool looping (double p) const noexcept { return phase == Phase::loop && (late || p + loopOffset >= leave()); }
    int regionIndex() const noexcept { return phase == Phase::off ? -1 : regionNo; }

    /** The pedal goes down with the head at linear position `p`. `soundEnd`: the last frame
        a read may reach (the recording, minus what an interpolator needs). False: nothing to
        loop (no regions, or too late), and the note plays on as it would. */
    bool arm (const PedalLoopModel& model, PedalLoopMode loopMode, double p, double sampleRate, double soundEnd, double turnFrames,
              float turnCorrelation) noexcept
    {
        if (loopMode == PedalLoopMode::normal || ! model.valid || model.regions.empty() || phase == Phase::loop)
            return false;
        // (Re-armed during a release: the release completes into the recording first.)
        const double natural = p + currentOffset (p);
        mode = loopMode;
        turnCorr = std::clamp (turnCorrelation, 0.0f, 1.0f);
        const int count = static_cast<int> (model.regions.size());
        for (int k = 0; k < count; ++k)
        {
            setRegion (model.regions[static_cast<std::size_t> (k)], turnFrames);
            if (natural < leave())
            {
                regionNo = k;
                offset = natural - p;
                loopOffset = offset;
                late = false;
                loopGain = 1.0f;
                phase = Phase::loop;
                passCount = 0;
                return true;
            }
        }
        // Late: a bounded crossfade into the last region at the level the note has here.
        const auto& r = model.regions.back();
        setRegion (r, turnFrames);
        double len = std::min (0.05 * sampleRate, soundEnd - natural);
        len = std::min (len, 0.5 * (r.endFrame - r.startFrame - r.crossfadeFrames));
        if (len < 0.002 * sampleRate)
            return false;
        regionNo = count - 1;
        offset = natural - p;
        late = true;
        entryP = p;
        entryLen = len;
        loopGain = std::clamp (static_cast<float> (std::pow (10.0, (model.levelAt (natural) - r.levelDb) / 20.0)), 0.02f, 1.0f);
        // Forward enters at the region's start; Ping-Pong at its end, reading back (as the voice).
        loopOffset = (mode == PedalLoopMode::forward ? start : end) - p;   // (Ping-Pong: the back run, from its turn)
        phase = Phase::loop;
        passCount = 0;
        return true;
    }

    /** The pedal lets go with the head at `p` (the key still held, or the voice decides). */
    void release (double p, double sampleRate) noexcept
    {
        if (phase != Phase::loop)
            return;
        relP = p;
        if (late && p >= entryP)
        {
            // Back to where the late pedal found the recording, at its own level.
            relTarget = entryP + entryLen + offset;
            relLen = 0.03 * sampleRate;
            relCorr = 0.0f;
        }
        else
        {
            const auto now = primary (p + loopOffset);
            relTarget = now.position;
            relLen = (now.backward ? 0.015 : 0.01) * sampleRate;
            relCorr = now.backward ? turnCorr : 1.0f;   // forward: the same stream goes on
        }
        relLen = std::max (1.0, relLen);
        phase = Phase::release;
    }

    /** Ends a finished release (call as the head moves; cheap). */
    void update (double p) noexcept
    {
        if (phase == Phase::release && p >= relP + relLen)
        {
            offset = relTarget - relP;
            phase = Phase::off;
        }
    }

    /** Where to read for linear position `p`. */
    Taps map (double p) const noexcept
    {
        Taps t;
        switch (phase)
        {
            case Phase::off:
                t.add (p + offset, 1.0f);
                break;
            case Phase::loop:
                loopTaps (p, 1.0f, t);
                break;
            case Phase::release:
            {
                float gOut, gIn;
                crossfadeGains (static_cast<float> (std::clamp ((p - relP) / relLen, 0.0, 1.0)), relCorr, gOut, gIn);
                loopTaps (p, gOut, t);
                t.add (relTarget + (p - relP), gIn);
                break;
            }
        }
        return t;
    }

    /** The position the note is "at" for `p` (its dominant read): for ends and the display. */
    double position (double p) const noexcept
    {
        if (phase == Phase::off)
            return p + offset;
        if (phase == Phase::release)
            return p >= relP + 0.5 * relLen ? relTarget + (p - relP) : primary (p + loopOffset).position;
        if (late && p < entryP + entryLen)
            return p + offset;
        return primary (p + loopOffset).position;
    }

    /** Completed passes of the loop at `p` (Forward: wraps; Ping-Pong: turns). */
    int passes (double p) const noexcept
    {
        if (phase != Phase::loop)
            return passCount;
        const double v = p + loopOffset;
        if (v < leave())
            return 0;
        const double cycle = mode == PedalLoopMode::forward ? end - xfade - start : (end - start - 2.0 * turn) + turn;
        return 1 + static_cast<int> ((v - leave()) / std::max (1.0, cycle));
    }

private:
    enum class Phase { off, loop, release };
    struct Primary
    {
        double position;
        bool backward;
    };

    void setRegion (const PedalLoopRegion& r, double turnFrames) noexcept
    {
        start = r.startFrame;
        end = r.endFrame;
        xfade = std::max (1.0, std::min (r.crossfadeFrames, 0.45 * (r.endFrame - r.startFrame)));
        corr = r.correlation;
        turn = std::max (1.0, std::min (turnFrames, 0.25 * (r.endFrame - r.startFrame)));
    }

    double leave() const noexcept { return mode == PedalLoopMode::forward ? end - xfade : end - turn; }

    double currentOffset (double) const noexcept
    {
        if (phase == Phase::release)
            return relTarget - relP;
        return offset;
    }

    /** The loop's dominant read for virtual position v (the incoming stream in a crossfade). */
    Primary primary (double v) const noexcept
    {
        if (v < leave())
            return { v, false };
        if (mode == PedalLoopMode::forward)
        {
            const double length = std::max (1.0, end - xfade - start);
            const double r = std::fmod (v - leave(), length);
            return { start + r, false };
        }
        const double a = start + turn, b = end - turn, span = std::max (1.0, b - a);
        const double cycle = 2.0 * span + 2.0 * turn;
        const double phi = std::fmod (v - b, cycle);
        if (phi < turn)
            return { end - phi, true };
        if (phi < turn + span)
            return { b - (phi - turn), true };
        if (phi < 2.0 * turn + span)
            return { start + (phi - turn - span), false };
        return { a + (phi - 2.0 * turn - span), false };
    }

    /** The loop's reads for linear position p, every gain scaled by `scale`. */
    void loopTaps (double p, float scale, Taps& t) const noexcept
    {
        float loopScale = scale;
        if (late && p < entryP + entryLen)
        {
            // The late entry: the recording where it is fades into the loop.
            float gOut, gIn;
            crossfadeGains (static_cast<float> (std::clamp ((p - entryP) / entryLen, 0.0, 1.0)), 0.0f, gOut, gIn);
            t.add (p + offset, scale * gOut);
            loopScale = scale * gIn;
        }
        loopScale *= loopGain;
        const double v = p + loopOffset;
        if (v < leave())
        {
            t.add (v, loopScale);
            return;
        }
        if (mode == PedalLoopMode::forward)
        {
            const double length = std::max (1.0, end - xfade - start);
            const double r = std::fmod (v - leave(), length);
            if (r < xfade)
            {
                float gOut, gIn;
                crossfadeGains (static_cast<float> (r / xfade), corr, gOut, gIn);
                t.add (leave() + r, loopScale * gOut);
                t.add (start + r, loopScale * gIn);
            }
            else
                t.add (start + r, loopScale);
            return;
        }
        // Ping-Pong: forward a..b, turn at b (the mirror leaves `end`), back b..a, turn at a.
        const double a = start + turn, b = end - turn, span = std::max (1.0, b - a);
        const double cycle = 2.0 * span + 2.0 * turn;
        const double phi = std::fmod (v - b, cycle);
        float gOut, gIn;
        if (phi < turn)
        {
            crossfadeGains (static_cast<float> (phi / turn), turnCorr, gOut, gIn);
            t.add (b + phi, loopScale * gOut);
            t.add (end - phi, loopScale * gIn);
        }
        else if (phi < turn + span)
            t.add (b - (phi - turn), loopScale);
        else if (phi < 2.0 * turn + span)
        {
            const double d = phi - turn - span;
            crossfadeGains (static_cast<float> (d / turn), turnCorr, gOut, gIn);
            t.add (a - d, loopScale * gOut);
            t.add (start + d, loopScale * gIn);
        }
        else
            t.add (a + (phi - 2.0 * turn - span), loopScale);
    }

    Phase phase = Phase::off;
    PedalLoopMode mode = PedalLoopMode::normal;
    double offset = 0.0;       ///< off: the read is p + offset (after a release: where the note went on)
    double loopOffset = 0.0;   ///< holding: the loop's virtual position is p + loopOffset
    double start = 0.0, end = 0.0, xfade = 1.0, turn = 1.0;
    float corr = 0.0f, turnCorr = 0.0f, loopGain = 1.0f;
    int regionNo = -1;
    bool late = false;
    double entryP = 0.0, entryLen = 1.0;
    double relP = 0.0, relLen = 1.0, relTarget = 0.0;
    float relCorr = 1.0f;
    int passCount = 0;
    PedalLoopMode configuredMode = PedalLoopMode::normal;
    double configuredTurn = 0.0;
    float configuredCorr = 0.0f;
    bool wanted = false, tried = false;
};

} // namespace osp
