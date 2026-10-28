#include "engine/GranularSource.h"

#include "engine/FastMath.h"
#include "engine/LevelContour.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace osp
{

namespace
{
    /** The table windows (SOFT, PERCUSSIVE), built once at load time (never on the audio
        thread), each scaled to HANN's energy so changing the window does not change the level. */
    struct WindowTables
    {
        static constexpr int size = 1024;
        std::array<std::array<float, size + 1>, 3> table {};
        WindowTables()
        {
            for (int w = 1; w < 3; ++w)
            {
                double energy = 0.0;
                for (int i = 0; i <= size; ++i)
                {
                    const double x = static_cast<double> (i) / size;
                    double v;
                    if (w == 1)   // Tukey: flat in the middle half, cosine shoulders (ANDOR Lab's SOFT)
                        v = x < 0.25 ? 0.5 - 0.5 * std::cos (std::numbers::pi * x / 0.25)
                                     : (x > 0.75 ? 0.5 - 0.5 * std::cos (std::numbers::pi * (1.0 - x) / 0.25) : 1.0);
                    else          // a quick rise, an exponential fall, a short taper to zero (PERCUSSIVE)
                        v = std::min (1.0, x / 0.04) * std::exp (-5.0 * std::max (0.0, x - 0.04) / 0.96) * std::min (1.0, (1.0 - x) / 0.04);
                    table[static_cast<std::size_t> (w)][static_cast<std::size_t> (i)] = static_cast<float> (v);
                    energy += v * v;
                }
                // Hann's mean square is 3/8.
                const auto scale = static_cast<float> (std::sqrt (0.375 / std::max (1.0e-9, energy / (size + 1))));
                for (auto& v : table[static_cast<std::size_t> (w)])
                    v *= scale;
            }
        }
        float at (GrainWindow w, float phase) const noexcept
        {
            const auto& t = table[static_cast<std::size_t> (w)];
            const auto i = std::min (static_cast<int> (phase), size - 1);
            const float f = phase - static_cast<float> (i);
            return t[static_cast<std::size_t> (i)] + f * (t[static_cast<std::size_t> (i + 1)] - t[static_cast<std::size_t> (i)]);
        }
    };
    const WindowTables windowTables;
}

void GranularSource::start (const PlaybackSource& source, const GranularParams& params, double outputSampleRate, std::uint64_t seed,
                            const EnvelopeAnalysis* envelope, double durationSeconds) noexcept
{
    src = &source;
    contour = envelope;
    contourSeconds = durationSeconds;
    settings = params;
    sampleRate = outputSampleRate;
    rng.reseed (Prng::deriveSeed (seed, 0x6772736dull, 0));
    optionRng.reseed (Prng::deriveSeed (seed, 0x67726f70ull, 0));   // "grop": SCATTER and PAN only
    for (auto& g : grains)
        g.active = false;
    activeGrains = 0;
    shapedGrains = 0;
    countdown = 0;   // the first grain starts with the note
    spawning = source.numFrames() > 8;
    stereo = source.numChannels() > 1;
    // A note started under a budget's share begins at its admitted level (no glide in).
    budgeted = cap < maxGrains;
    overlapNow = overlapTarget = 0.0;
    updateNorm (true);
}

double GranularSource::expectedOverlap() const noexcept
{
    return std::clamp (settings.density, 1.0, 80.0) * std::clamp (settings.sizeSeconds, 0.005, 1.0);
}

int GranularSource::grainDemand() const noexcept
{
    return std::clamp (static_cast<int> (std::ceil (expectedOverlap())) + 1, 1, maxGrains);
}

void GranularSource::updateNorm (bool snap) noexcept
{
    // Overlapping grains add up (incoherently): keep the level near the recording's.
    const double overlap = expectedOverlap();
    if (! budgeted)
    {
        norm = static_cast<float> (1.0 / std::max (1.0, 0.612 * std::sqrt (overlap)));
        return;
    }
    // Under a grain budget the stream keeps the level it would have had with every grain it
    // asked for (sqrt (asked) x the law), spread over the grains actually admitted. Grains
    // already sounding drain (or new ones fill in) over about one grain, so the admitted count
    // the level is divided by glides there instead of stepping.
    overlapTarget = cap < maxGrains ? std::min (overlap, static_cast<double> (cap)) : overlap;
    askedLevel = std::sqrt (std::max (1.0, overlap)) / std::max (1.0, 0.612 * std::sqrt (overlap));
    overlapGlide = 1.0 / std::max (1.0, 0.5 * std::clamp (settings.sizeSeconds, 0.005, 1.0) * sampleRate);
    if (snap)
        overlapNow = overlapTarget;
    norm = static_cast<float> (askedLevel / std::sqrt (std::max (1.0, overlapNow)));
}

void GranularSource::glideNorm() noexcept
{
    overlapNow += (overlapTarget - overlapNow) * overlapGlide;
    if (std::abs (overlapTarget - overlapNow) < 1.0e-4)
        overlapNow = overlapTarget;
    norm = static_cast<float> (askedLevel / std::sqrt (std::max (1.0, overlapNow)));
}

void GranularSource::setGrainCap (int newCap) noexcept
{
    newCap = std::clamp (newCap, 1, maxGrains);
    if (newCap == cap)
        return;
    cap = newCap;
    if (! budgeted && src != nullptr)
    {
        // First capped mid-note: glide from the grains it is playing (a full stream: its
        // overlap, so the level carries on; a young one has only its first, quiet grains).
        budgeted = true;
        overlapNow = std::min (expectedOverlap(), static_cast<double> (std::max (1, activeGrains)));
    }
    budgeted = budgeted || cap < maxGrains;
    updateNorm();
}

void GranularSource::spawn (double step) noexcept
{
    if (activeGrains >= cap)
        return;   // the budget's share (24, the whole pool, without a budget)
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
    double ratio = std::exp2 (std::clamp (settings.tuneSemitones, -24.0, 24.0) / 12.0);
    if (settings.scatterSemitones > 0.0)
        ratio *= std::exp2 (std::min (settings.scatterSemitones, 12.0) * optionRng.bipolar() / 12.0);   // SCATTER
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
    slot->gainL = slot->gainR = 1.0f;
    slot->window = settings.window;
    slot->shaped = false;
    if (settings.pan > 0.0)
    {
        // PAN: equal power, unity at the centre (sqrt 2 x cos / sin of a quarter turn).
        const double place = std::min (settings.pan, 1.0) * optionRng.bipolar();
        const double angle = 0.25 * std::numbers::pi * (place + 1.0);
        slot->gainL = static_cast<float> (std::numbers::sqrt2 * std::cos (angle));
        slot->gainR = static_cast<float> (std::numbers::sqrt2 * std::sin (angle));
        slot->shaped = true;
    }
    if (settings.window != GrainWindow::hann)
    {
        slot->windowPhase = 0.0f;
        slot->windowStep = static_cast<float> (WindowTables::size) / static_cast<float> (n);
        slot->shaped = true;
    }
    slot->active = true;
    ++activeGrains;
    shapedGrains += slot->shaped ? 1 : 0;
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
    if (overlapNow != overlapTarget)
        glideNorm();   // only under a budget
    if (activeGrains == 0)
        return;
    if (shapedGrains > 0)
    {
        renderShaped (left, right, step);
        return;
    }

    // Every grain adds its four Hermite taps, weighted by its window, into four lanes per
    // channel; the lanes are summed once for the sample.
    using simd::Float4;
    const double lastFrame = static_cast<double> (src->numFrames()) - 3.0;
    const float* dataL = src->channelData (0);
    const float* dataR = stereo ? src->channelData (1) : nullptr;
    auto accL = Float4::zero(), accR = Float4::zero();
    for (auto& g : grains)
    {
        if (! g.active)
            continue;
        const auto window = static_cast<float> (0.5 - 0.5 * g.c) * g.gain;
        const double pos = std::clamp (g.position, 1.0, lastFrame);
        const auto i = static_cast<std::int64_t> (pos);
        const auto w = hermiteWeights (static_cast<float> (pos - static_cast<double> (i))) * Float4::broadcast (window);
        accL = accL + w * Float4::load (dataL + i - 1);
        if (dataR != nullptr)
            accR = accR + w * Float4::load (dataR + i - 1);
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
    left = accL.sum() * norm;
    right = dataR != nullptr ? accR.sum() * norm : left;
}

void GranularSource::renderShaped (float& left, float& right, double step) noexcept
{
    // The same reads as render(), with each grain's PAN gains and table window. Used only
    // while a panned or table-windowed grain plays; mono recordings become stereo here.
    using simd::Float4;
    const double lastFrame = static_cast<double> (src->numFrames()) - 3.0;
    const float* dataL = src->channelData (0);
    const float* dataR = stereo ? src->channelData (1) : dataL;
    auto accL = Float4::zero(), accR = Float4::zero();
    for (auto& g : grains)
    {
        if (! g.active)
            continue;
        float window = g.window == GrainWindow::hann ? static_cast<float> (0.5 - 0.5 * g.c) : windowTables.at (g.window, g.windowPhase);
        window *= g.gain;
        const double pos = std::clamp (g.position, 1.0, lastFrame);
        const auto i = static_cast<std::int64_t> (pos);
        const auto h = hermiteWeights (static_cast<float> (pos - static_cast<double> (i)));
        accL = accL + h * Float4::broadcast (window * g.gainL) * Float4::load (dataL + i - 1);
        accR = accR + h * Float4::broadcast (window * g.gainR) * Float4::load (dataR + i - 1);
        g.position += g.direction * step * g.ratio;
        const double c = g.c * g.cd - g.s * g.sd;
        g.s = g.s * g.cd + g.c * g.sd;
        g.c = c;
        g.windowPhase += g.windowStep;
        if (--g.remaining <= 0)
        {
            g.active = false;
            --activeGrains;
            shapedGrains -= g.shaped ? 1 : 0;
        }
    }
    left = accL.sum() * norm;
    right = accR.sum() * norm;
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
        out[n].level = g.window == GrainWindow::hann ? static_cast<float> (0.5 - 0.5 * g.c)
                                                     : std::min (1.0f, windowTables.at (g.window, g.windowPhase));
        out[n].lane = g.lane;
        ++n;
    }
    return n;
}

} // namespace osp
