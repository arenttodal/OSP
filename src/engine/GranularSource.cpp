#include "engine/GranularSource.h"

#include "engine/LevelContour.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

void GranularSource::start (const PlaybackSource& source, const GranularParams& params, double outputSampleRate, std::uint64_t seed,
                            const EnvelopeAnalysis* envelope, double durationSeconds) noexcept
{
    src = &source;
    contour = envelope;
    contourSeconds = durationSeconds;
    settings = params;
    sampleRate = outputSampleRate;
    rng.reseed (Prng::deriveSeed (seed, 0x6772736dull, 0));
    for (auto& g : grains)
        g.active = false;
    activeGrains = 0;
    countdown = 0;   // the first grain starts with the note
    spawning = source.numFrames() > 8;
    stereo = source.numChannels() > 1;
}

float GranularSource::read (int channel, double pos) const noexcept
{
    // 4-point cubic (Hermite); the caller keeps pos inside [1, frames - 3].
    const auto i = static_cast<std::int64_t> (pos);
    const float t = static_cast<float> (pos - static_cast<double> (i));
    const float* x = src->channelData (channel) + i;
    const float c1 = 0.5f * (x[1] - x[-1]);
    const float c2 = x[-1] - 2.5f * x[0] + 2.0f * x[1] - 0.5f * x[2];
    const float c3 = 0.5f * (x[2] - x[-1]) + 1.5f * (x[0] - x[1]);
    return ((c3 * t + c2) * t + c1) * t + x[0];
}

void GranularSource::spawn (double step) noexcept
{
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active)
        {
            slot = &g;
            break;
        }
    if (slot == nullptr)
        return;   // pool full: CPU stays bounded

    const auto frames = static_cast<double> (src->numFrames());
    const double ratio = std::exp2 (std::clamp (settings.tuneSemitones, -24.0, 24.0) / 12.0);
    const double frameStep = std::max (1.0e-4, step * ratio);
    // Grain length, shortened if the recording is shorter than one grain.
    double samples = std::clamp (settings.sizeSeconds, 0.005, 1.0) * sampleRate;
    const double usable = frames - 6.0;
    samples = std::min (samples, usable / (1.15 * frameStep));
    if (samples < 16.0)
        return;
    const double span = samples * frameStep * 1.15;   // margin for pitch bends during the grain

    // Where: scattered around POS - at SPREAD 100 % up to half the recording either side,
    // so grains can come from anywhere in it - plus a few milliseconds of jitter so even
    // SPREAD 0 never combs.
    const double srcRate = src->sampleRate();
    const double centre = std::clamp (settings.position, 0.0, 1.0) * frames;
    const double offset = (std::clamp (settings.spread, 0.0, 1.0) * 0.5 * frames + 0.004 * srcRate) * rng.bipolar();
    const double start = std::clamp (centre + offset - 0.5 * span, 1.0, std::max (1.0, frames - 3.0 - span));

    const auto n = static_cast<int> (samples);
    const double w = 2.0 * std::numbers::pi / static_cast<double> (n);
    // REVERSE: the same stretch of the recording, read from its end back to its start.
    slot->direction = settings.reverse ? -1.0 : 1.0;
    slot->position = settings.reverse ? start + samples * frameStep : start;
    slot->gain = 1.0f;
    if (! settings.follow && contour != nullptr)
        slot->gain = static_cast<float> (std::pow (10.0, levelContour::boostDb (*contour, contourSeconds, (start + 0.5 * samples * frameStep) / frames) / 20.0));
    slot->ratio = ratio;
    slot->c = 1.0;
    slot->s = 0.0;
    slot->cd = std::cos (w);
    slot->sd = std::sin (w);
    slot->remaining = n;
    slot->lane = static_cast<float> (rng.nextDouble());
    slot->active = true;
    ++activeGrains;
}

void GranularSource::render (float& left, float& right, double step) noexcept
{
    left = right = 0.0f;
    if (src == nullptr)
        return;
    if (spawning && --countdown <= 0)
    {
        spawn (step);
        const double interval = sampleRate / std::clamp (settings.density, 1.0, 80.0);
        countdown = std::max (1, static_cast<int> (interval * rng.uniform (0.8, 1.2)));
    }
    if (activeGrains == 0)
        return;

    const double lastFrame = static_cast<double> (src->numFrames()) - 3.0;
    float l = 0.0f, r = 0.0f;
    for (auto& g : grains)
    {
        if (! g.active)
            continue;
        const auto window = static_cast<float> (0.5 - 0.5 * g.c) * g.gain;
        const double pos = std::clamp (g.position, 1.0, lastFrame);
        const float a = read (0, pos);
        l += window * a;
        r += window * (stereo ? read (1, pos) : a);
        g.position += g.direction * step * g.ratio;
        const double c = g.c * g.cd - g.s * g.sd;
        g.s = g.s * g.cd + g.c * g.sd;
        g.c = c;
        if (--g.remaining <= 0)
        {
            g.active = false;
            --activeGrains;
        }
    }
    // Overlapping grains add up (incoherently): keep the level near the recording's.
    const double overlap = std::clamp (settings.density, 1.0, 80.0) * std::clamp (settings.sizeSeconds, 0.005, 1.0);
    const auto norm = static_cast<float> (1.0 / std::max (1.0, 0.612 * std::sqrt (overlap)));
    left = l * norm;
    right = r * norm;
}

int GranularSource::collect (GrainView* out, int max) const noexcept
{
    if (src == nullptr || activeGrains == 0)
        return 0;
    const double frames = std::max (1.0, static_cast<double> (src->numFrames()));
    int n = 0;
    for (const auto& g : grains)
    {
        if (! g.active || n >= max)
            continue;
        out[n].position = static_cast<float> (std::clamp (g.position / frames, 0.0, 1.0));
        out[n].level = static_cast<float> (0.5 - 0.5 * g.c);
        out[n].lane = g.lane;
        ++n;
    }
    return n;
}

} // namespace osp
