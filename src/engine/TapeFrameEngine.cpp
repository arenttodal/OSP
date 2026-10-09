#include "engine/TapeFrameEngine.h"

#include "analysis/continuation/ContinuationAnalyzer.h"

#include <cmath>
#include <numbers>

namespace osp
{

using reimagined::ramp;

void TapeFrameEngine::prepare (double outputRate) noexcept
{
    rate = outputRate;
    // Compression follower: quick to catch, slow to let go (a tape's soft limit).
    envAttack = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * rate)));
    envRelease = static_cast<float> (1.0 - std::exp (-1.0 / (0.12 * rate)));
    done = true;
}

void TapeFrameEngine::seek (Head& head, double t) const noexcept
{
    head.t = t;
    head.splice = 0;
    const auto& sp = tape->splices;
    while (head.splice + 1 < sp.size() && t >= sp[head.splice + 1].frameStart + sp[head.splice + 1].fadeIn)
        ++head.splice;
}

double TapeFrameEngine::tapeEnd() const noexcept
{
    return straight ? straightEnd : std::min (tape->lengthFrames, frameLength);
}

bool TapeFrameEngine::start (const ReimaginedNote& n, const ReimaginedControl& c) noexcept
{
    note = n;
    tape = n.analysis != nullptr && n.analysis->tape.ready && ! n.analysis->tape.splices.empty() ? &n.analysis->tape : nullptr;
    if (tape == nullptr && ! n.granular)
        return false;   // no tape yet: the voice plays the recording itself

    rng.reseed (Prng::deriveSeed (n.seed, 0x74617065ull, 0));
    amount = std::clamp (c.amount, 0.0, 1.0);
    const auto& p = c.settings->tapeFrame;
    const double mech = 0.3 * ramp (amount, 0.0, 0.3) + 0.7 * ramp (amount, 0.3, 0.85);
    const double stab = mech * (0.06 + 0.94 * std::clamp (p.stability, 0.0, 1.0));

    // This pass of the tape: a little faster or slower, a little later, its own wow and flutter.
    speedVariation = std::exp2 (6.0 * stab * rng.bipolar() / 1200.0);
    wowPhase = 2.0 * std::numbers::pi * rng.nextDouble();
    flutterPhase = 2.0 * std::numbers::pi * rng.nextDouble();
    wearPhase = 2.0 * std::numbers::pi * rng.nextDouble();
    wowRate = rng.uniform (0.45, 0.9);
    flutterRate = rng.uniform (6.5, 11.0);
    toneVariation = 0.15 * mech * rng.bipolar();
    gainVariation = static_cast<float> (std::pow (10.0, 0.5 * mech * rng.bipolar() / 20.0));
    const double startShift = rng.nextDouble() * (0.001 + 0.03 * stab);
    ghostDelay = { rng.uniform (0.030, 0.045), rng.uniform (0.060, 0.090) };
    ghostSpeed = { std::exp2 (-rng.uniform (3.0, 7.0) / 1200.0), std::exp2 (rng.uniform (3.0, 7.0) / 1200.0) };

    for (auto& f : lowpass)
        f.reset();
    for (auto& f : ghostTone)
        f.reset();
    envelope = 0.0f;
    rewinding = false;
    passGain = 1.0;
    done = false;
    step = c.step;
    factor = speedVariation;
    level = 1.0f;
    levelStep = 0.0f;

    grainHead = startShift;
    grainRewind = 0.0;
    grainOffset = 0.0;
    grainDuration = std::max (0.05, static_cast<double> (n.source->numFrames()) / n.source->sampleRate());
    direction = n.reverse ? -1.0 : 1.0;
    if (tape != nullptr && ! n.granular)
    {
        const double sr = n.source->sampleRate();
        frameLength = reimagined::frameSeconds (p.frame) * sr;
        rewindLength = 0.12 * sr;
        direction = n.reverse ? -1.0 : 1.0;
        // LOOP off: once through the recording itself (its own length, its own ending),
        // forwards or backwards; LOOP on: the spliced tape, passing again while held.
        straight = ! n.loop;
        straightOrigin = tape->splices.front().sourceFrame;
        const double frames = static_cast<double> (n.source->numFrames());
        const double trailing = std::max (0.0, n.model->analysis.envelope.trailingSilenceSeconds) * sr;
        const double soundEnd = std::clamp (frames - trailing, n.source->startFrame() + 1.0, frames);
        straightEnd = std::max (2.0, std::clamp (soundEnd, straightOrigin + 2.0, frames) - straightOrigin);
        // START moves along the tape as it moves through the recording: forwards from the
        // recording's start, backwards from its end (where the voice starts a reversed note).
        const double end = tapeEnd();
        double t = 0.0;
        if (direction > 0.0)
            t = std::min (std::max (0.0, n.startFrame - n.source->startFrame()) + startShift * sr, 0.9 * end);
        else
            t = std::max (0.1 * end, end - 1.0 - std::max (0.0, soundEnd - n.startFrame) - startShift * sr);
        seek (main, t);
        main.gain = 1.0f;
        for (std::size_t g = 0; g < ghosts.size(); ++g)
        {
            seek (ghosts[g], t - direction * ghostDelay[g] * sr);
            ghosts[g].gain = 0.0f;
        }
    }
    control (c);
    amount = std::clamp (c.amount, 0.0, 1.0);
    return true;
}

void TapeFrameEngine::control (const ReimaginedControl& c) noexcept
{
    amount = reimagined::glide (amount, std::clamp (c.amount, 0.0, 1.0));
    step = c.step;
    const auto& p = c.settings->tapeFrame;
    if (tape != nullptr)
        frameLength = reimagined::frameSeconds (p.frame) * tape->sampleRate;

    // Amount curves: first a tape identity, then the frame, then mechanics and ghosts.
    const double identity = 0.35 * ramp (amount, 0.0, 0.25) + 0.65 * ramp (amount, 0.2, 0.6);
    const double mech = 0.3 * ramp (amount, 0.0, 0.3) + 0.7 * ramp (amount, 0.3, 0.85);
    const double ghost = ramp (amount, 0.55, 0.95);
    runout = ramp (amount, 0.25, 0.6);
    const double age = identity * (0.15 + 0.85 * std::clamp (p.age, 0.0, 1.0));
    const double stab = mech * (0.06 + 0.94 * std::clamp (p.stability, 0.0, 1.0));

    // Wow and flutter: the read speed itself moves.
    const double dt = 32.0 / rate;
    wowPhase += 2.0 * std::numbers::pi * wowRate * dt;
    flutterPhase += 2.0 * std::numbers::pi * flutterRate * dt;
    wearPhase += 2.0 * std::numbers::pi * 0.27 * dt;
    const double cents = 14.0 * stab * std::sin (wowPhase) + 3.5 * stab * std::sin (flutterPhase)
                         + 1.2 * stab * std::sin (2.3 * flutterPhase + 1.0);
    factor = speedVariation * std::exp2 (cents / 1200.0);

    // AGE: bandwidth follows tape speed (lower notes are slower tape: darker), worn and
    // wandering a little; touch opens it slightly.
    const double speed = std::max (1.0e-3, step / std::max (1.0e-9, note.rootStep));
    double cutoff = 19000.0 * std::exp2 (-2.3 * age) * std::sqrt (speed);
    cutoff *= std::exp2 (toneVariation + 0.25 * age * std::sin (wearPhase));
    cutoff *= 1.0 + 0.25 * identity * (note.velocity - 0.7);
    cutoff = std::clamp (cutoff, 1200.0, 0.45 * rate);
    for (auto& f : lowpass)
        f.setCutoff (cutoff, rate);
    for (auto& f : ghostTone)
        f.setCutoff (0.55 * cutoff, rate);
    drive = static_cast<float> (1.0 + 3.5 * age);
    compression = static_cast<float> (1.5 * age);

    if (note.granular)
    {
        // Granular: the grains follow a tape head through a FRAME of the recording around POS,
        // at the note's tape speed (low notes scan slower) with the wow and flutter; at the
        // frame's end the tape rewinds (a fast scan back, heard through the grains) and
        // passes again while the key is held (LOOP off: it stays at the frame's end).
        const double frame = std::min (0.9 * grainDuration, reimagined::frameSeconds (p.frame) * (1.0 - 0.5 * ramp (amount, 0.6, 1.0)));
        const double rewindSeconds = 0.12;
        if (grainRewind > 0.0)
        {
            grainHead = std::max (0.0, grainHead - dt * frame / rewindSeconds);
            grainRewind = std::max (0.0, grainRewind - dt);
        }
        else
        {
            grainHead += dt * std::clamp (speed, 0.25, 4.0) * factor;
            if (grainHead >= frame)
            {
                if (note.loop)
                    grainRewind = rewindSeconds;
                grainHead = frame;
            }
        }
        const double takeover = ramp (amount, 0.1, 0.55);
        grainOffset = direction * takeover * (grainHead - 0.5 * frame) / grainDuration;
    }

    const auto g1 = static_cast<float> (0.2 * ghost), g2 = static_cast<float> (0.11 * ghost);
    ghostLevel = { g1, g2 };
    mainNorm = 1.0f / std::sqrt (1.0f + g1 * g1 + g2 * g2);
}

void TapeFrameEngine::readTape (Head& head, double readStep, bool sinc, float& l, float& r) noexcept
{
    const auto& sp = tape->splices;
    auto& i = head.splice;
    while (i + 1 < sp.size() && head.t >= sp[i + 1].frameStart + sp[i + 1].fadeIn)
        ++i;
    while (i > 0 && head.t < sp[i].frameStart)
        --i;
    const auto frames = static_cast<double> (note.source->numFrames());
    if (straight)
    {
        // The recording itself, no splices.
        const double pos = straightOrigin + head.t;
        if (head.t < 0.0 || pos >= static_cast<double> (note.source->numFrames()))
        {
            l = r = 0.0f;
            return;
        }
        if (sinc)
            reimagined::readSinc (note, pos, readStep, l, r);
        else
        {
            l = reimagined::readHermite (*note.source, 0, pos);
            r = note.source->numChannels() > 1 ? reimagined::readHermite (*note.source, 1, pos) : l;
        }
        return;
    }
    auto read = [&] (const ReimaginedAnalysis::TapeSplice& s, float& a, float& b) {
        const double pos = s.sourceFrame + (head.t - s.frameStart);
        if (pos < 0.0 || pos >= frames)
        {
            a = b = 0.0f;
            return;
        }
        if (sinc)
            reimagined::readSinc (note, pos, readStep, a, b);
        else
        {
            a = reimagined::readHermite (*note.source, 0, pos);
            b = note.source->numChannels() > 1 ? reimagined::readHermite (*note.source, 1, pos) : a;
        }
        a *= s.gain;
        b *= s.gain;
    };
    if (head.t < 0.0)
    {
        l = r = 0.0f;
        return;
    }
    read (sp[i], l, r);
    if (i + 1 < sp.size() && head.t >= sp[i + 1].frameStart)
    {
        const auto& next = sp[i + 1];
        float l2, r2, gOut, gIn;
        read (next, l2, r2);
        crossfadeGains (static_cast<float> ((head.t - next.frameStart) / std::max (1.0, next.fadeIn)), next.correlation, gOut, gIn);
        l = l * gOut + l2 * gIn;
        r = r * gOut + r2 * gIn;
    }
}

void TapeFrameEngine::render (const float* dryL, const float* dryR, float* outL, float* outR, int n) noexcept
{
    const double readStep = step * factor;
    const bool reading = tape != nullptr && ! note.granular;
    const double end = reading ? tapeEnd() : 0.0;
    const double srcRate = reading ? note.source->sampleRate() : rate;
    for (int i = 0; i < n; ++i)
    {
        float l = 0.0f, r = 0.0f;
        if (done)
        {
            outL[i] = outR[i] = 0.0f;
            continue;
        }
        if (! reading)
        {
            l = dryL[i];
            r = dryR[i];
        }
        else
        {
            // LOOP on: near the end of the frame (in the direction of play) the tape is
            // rewound to the body for another pass, a little more worn each time (more at
            // higher amounts, floored), and the note sustains while held. Backwards, the body's
            // start is where it rewinds (to the frame's end), so a reversed note never reaches
            // its attack while held. LOOP off: once through, to the recording's own end.
            const auto mainLevel = static_cast<float> (passGain);
            readTape (main, readStep, true, l, r);
            l *= mainLevel;
            r *= mainLevel;
            const bool nearTurn = direction > 0.0 ? main.t >= end - rewindLength : main.t <= tape->bodyFrame + rewindLength;
            if (! straight && ! rewinding && nearTurn)
            {
                rewinding = true;
                rewindProgress = 0.0;
                seek (rewind, direction > 0.0 ? tape->bodyFrame : end - 1.0);
            }
            if (rewinding)
            {
                float l2, r2, gOut, gIn;
                readTape (rewind, readStep, true, l2, r2);
                const auto nextGain = static_cast<float> (nextPassGain());
                crossfadeGains (static_cast<float> (rewindProgress / rewindLength), 0.0f, gOut, gIn);
                l = l * gOut + l2 * nextGain * gIn;
                r = r * gOut + r2 * nextGain * gIn;
                rewind.t += direction * readStep;
                rewindProgress += readStep;
                if (rewindProgress >= rewindLength)
                {
                    rewinding = false;
                    main = rewind;
                    passGain = nextPassGain();
                    for (std::size_t g = 0; g < ghosts.size(); ++g)
                    {
                        seek (ghosts[g], main.t - direction * ghostDelay[g] * srcRate);
                        ghosts[g].gain = 0.0f;
                    }
                }
            }
            main.t += direction * readStep;
            if (direction > 0.0 ? main.t >= end : main.t <= 0.0)
            {
                done = true;
                outL[i] = outR[i] = 0.0f;
                continue;
            }

            // Ghost passes: the same tape a few tens of ms behind, older and slightly off speed.
            if (ghostLevel[0] > 0.0f)
                for (std::size_t g = 0; g < ghosts.size(); ++g)
                {
                    auto& head = ghosts[g];
                    head.gain = std::min (1.0f, head.gain + 1.0f / static_cast<float> (0.05 * rate));
                    if (head.t >= 0.0 && head.t < end)
                    {
                        float gl, gr;
                        readTape (head, readStep * ghostSpeed[g], false, gl, gr);
                        const float m = 0.5f * (gl + gr);
                        const float v = ghostTone[g].process (m) * ghostLevel[g] * head.gain * mainLevel;
                        // One a little to each side, so they widen rather than thicken.
                        l += v * (g == 0 ? 1.0f : 0.55f);
                        r += v * (g == 0 ? 0.55f : 1.0f);
                    }
                    head.t += direction * readStep * ghostSpeed[g];
                }
            l *= mainNorm;
            r *= mainNorm;
        }

        // AGE: two poles of bandwidth, saturation and a soft limit on loud passages.
        l = lowpass[1].process (lowpass[0].process (l));
        r = lowpass[3].process (lowpass[2].process (r));
        const float peak = std::max (std::abs (l), std::abs (r));
        envelope += (peak > envelope ? envAttack : envRelease) * (peak - envelope);
        const float squeeze = 1.0f / (1.0f + compression * std::max (0.0f, envelope - 0.1f));
        outL[i] = reimagined::soft (l * squeeze, drive) * gainVariation;
        outR[i] = reimagined::soft (r * squeeze, drive) * gainVariation;
    }
}

double TapeFrameEngine::sourcePosition() const noexcept
{
    if (tape == nullptr || note.granular || done)
        return -1.0;
    if (straight)
        return straightOrigin + main.t;
    const auto& s = tape->splices[std::min (main.splice, tape->splices.size() - 1)];
    return s.sourceFrame + (main.t - s.frameStart);
}

} // namespace osp
