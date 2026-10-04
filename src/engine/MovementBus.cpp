#include "engine/MovementBus.h"

#include <numbers>

namespace osp
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;
    constexpr double baseDelayMs = 4.0; // tape/drift read point: room for +-3 ms of wow

    inline float softClip (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
}

void MovementBus::prepare (double rate, std::uint64_t seed)
{
    sampleRate = rate;
    baseSeed = seed;
    // 64 ms of delay line (power of two): enough for chorus width (18 ms) and tape wow.
    int size = 1;
    while (size < static_cast<int> (0.064 * rate))
        size <<= 1;
    lineL.assign (static_cast<std::size_t> (size), 0.0f);
    lineR.assign (static_cast<std::size_t> (size), 0.0f);
    lineMask = size - 1;
    fadeLength = std::max (1, static_cast<int> (0.06 * rate));
    amountCoef = 1.0 - std::exp (-1.0 / (0.05 * rate));
    shaper.prepare (rate);
    reset();
}

void MovementBus::reset() noexcept
{
    std::fill (lineL.begin(), lineL.end(), 0.0f);
    std::fill (lineR.begin(), lineR.end(), 0.0f);
    write = 0;
    rng.reseed (Prng::deriveSeed (baseSeed, 0x6d6f7665ull, 0));
    wowPhase = flutterPhase = chorusPhase = pulsePhase = 0.0;
    wowNoise = flutterNoise = wearLevel = wearTone = driftNoise = Noise {};
    wowNoise.setRate (0.3, sampleRate);
    flutterNoise.setRate (4.0, sampleRate);
    wearLevel.setRate (2.0, sampleRate);
    wearTone.setRate (0.7, sampleRate);
    driftNoise.setRate (0.2, sampleRate);
    tapeLowL = tapeLowR = chorusLowL = chorusLowR = 0.0f;
    amount = amountTarget;
    fadeRemaining = 0;
    previousMode = mode;
    shaper.reset();
}

void MovementBus::setTargets (const Shaping& newSettings, double newAmount) noexcept
{
    if (newSettings.movementMode != mode)
    {
        // Each mode keeps its own settings; the switch crossfades the two outputs (60 ms).
        previousMode = mode;
        mode = newSettings.movementMode;
        fadeRemaining = fadeLength;
    }
    settings = newSettings;
    amountTarget = std::clamp (newAmount, 0.0, 1.0);
    driftNoise.setRate (std::max (0.05, shaping::driftSpeedHz (settings.driftSpeed)), sampleRate);
    shaper.setParams (settings.shaper);
}

float MovementBus::readLine (const std::vector<float>& line, double delaySamples) const noexcept
{
    // Cubic (Hermite) interpolation behind the write position.
    const double pos = static_cast<double> (write) - std::max (2.0, delaySamples);
    const auto i = static_cast<int> (std::floor (pos));
    const auto t = static_cast<float> (pos - i);
    const float y0 = line[static_cast<std::size_t> ((i - 1) & lineMask)];
    const float y1 = line[static_cast<std::size_t> (i & lineMask)];
    const float y2 = line[static_cast<std::size_t> ((i + 1) & lineMask)];
    const float y3 = line[static_cast<std::size_t> ((i + 2) & lineMask)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void MovementBus::render (MovementMode m, float inL, float inR, float& outL, float& outR) noexcept
{
    const double x = amount;
    const double msToSamples = 0.001 * sampleRate;
    switch (m)
    {
        case MovementMode::drift:
        {
            // DRIFT happens in the voices (including its shared part): nothing on the bus.
            outL = inL;
            outR = inR;
            return;
        }
        case MovementMode::tape:
        {
            // Wow: a slow, slightly irregular transport (sine plus wander); flutter: a
            // faster, rougher capstan wobble. Depths in ms of delay modulation. One
            // transport for both channels: a chord wobbles together, the image stays put.
            const double a = std::clamp (settings.tapeWow, 0.0, 1.0), b = std::clamp (settings.tapeFlutter, 0.0, 1.0);
            const double c = std::clamp (settings.tapeWear, 0.0, 1.0);
            const double wowRate = shaping::tapeWowHz (a);
            wowPhase += twoPi * wowRate / sampleRate;
            const double wow = 0.7 * std::sin (wowPhase) + 0.5 * wowNoise.next (rng);
            const double flutterRate = shaping::tapeFlutterHz (b);
            flutterPhase += twoPi * flutterRate / sampleRate;
            const double flutter = std::sin (flutterPhase) * (0.7 + 0.3 * flutterNoise.next (rng));
            const double delayMs = baseDelayMs + x * (2.6 * a * wow + 0.12 * b * flutter);
            float l = readLine (lineL, delayMs * msToSamples);
            float r = readLine (lineR, delayMs * msToSamples);
            // Wear: level instability, a wandering bandwidth, and saturation that breathes.
            const double wear = x * c;
            if (wear > 1.0e-4)
            {
                const double levelDb = 1.6 * wear * wearLevel.next (rng);
                const double toneOct = 2.4 * wear + 0.6 * wear * wearTone.next (rng);
                const double fc = std::clamp (18000.0 * std::exp2 (-toneOct), 2000.0, 0.45 * sampleRate);
                const auto k = static_cast<float> (1.0 - std::exp (-twoPi * fc / sampleRate));
                tapeLowL += (l - tapeLowL) * k;
                tapeLowR += (r - tapeLowR) * k;
                const auto drive = static_cast<float> (1.0 + 2.5 * wear);
                const auto gain = static_cast<float> (std::pow (10.0, levelDb / 20.0)) / drive;
                l = softClip (tapeLowL * drive) * gain * (1.0f + 0.4f * static_cast<float> (wear));
                r = softClip (tapeLowR * drive) * gain * (1.0f + 0.4f * static_cast<float> (wear));
            }
            outL = l;
            outR = r;
            return;
        }
        case MovementMode::chorus:
        {
            const double rate = shaping::chorusRateHz (settings.chorusRate);
            const double width = shaping::chorusWidthMs (settings.chorusWidth);
            const double c = std::clamp (settings.chorusStereo, 0.0, 1.0);
            chorusPhase += twoPi * rate / sampleRate;
            const double offset = std::numbers::pi * c; // STEREO: the taps drift apart
            const double dl = (1.2 + 0.5 * width * (1.0 + std::sin (chorusPhase))) * msToSamples;
            const double dr = (1.2 + 0.5 * width * (1.0 + std::sin (chorusPhase + offset))) * msToSamples;
            float wl = readLine (lineL, dl);
            float wr = readLine (lineR, dr);
            // Bucket-brigade colour: a darker, slightly compressed wet path.
            const auto k = static_cast<float> (1.0 - std::exp (-twoPi * 7000.0 / sampleRate));
            chorusLowL += (wl - chorusLowL) * k;
            chorusLowR += (wr - chorusLowR) * k;
            wl = softClip (1.2f * chorusLowL) / 1.2f;
            wr = softClip (1.2f * chorusLowR) / 1.2f;
            const auto wet = static_cast<float> (0.75 * x), dry = static_cast<float> (1.0 - 0.3 * x);
            const auto cross = static_cast<float> (0.5 * c);
            outL = dry * inL + wet * ((1.0f - cross) * wl + cross * wr);
            outR = dry * inR + wet * ((1.0f - cross) * wr + cross * wl);
            return;
        }
        case MovementMode::pulse:
        {
            const double rate = shaping::pulseRateHz (settings.pulseRate);
            const double c = std::clamp (settings.pulseStereo, 0.0, 1.0);
            pulsePhase += twoPi * rate / sampleRate;
            const double kk = 1.0 + 7.0 * std::clamp (settings.pulseShape, 0.0, 1.0); // SHAPE: sine -> rounded square
            const double norm = std::tanh (kk);
            const double yl = std::tanh (kk * std::sin (pulsePhase)) / norm;
            const double yr = std::tanh (kk * std::sin (pulsePhase + std::numbers::pi * c)) / norm;
            outL = inL * static_cast<float> (1.0 - x * 0.5 * (1.0 - yl));
            outR = inR * static_cast<float> (1.0 - x * 0.5 * (1.0 - yr));
            return;
        }
        case MovementMode::shaper:
        {
            outL = inL;
            outR = inR;
            if (x > 1.0e-5 || amountTarget > 0.0)
                shaper.process (outL, outR, x);   // MOVEMENT is the depth; at zero, an exact bypass
            return;
        }
    }
    outL = inL;
    outR = inR;
}

void MovementBus::process (float& left, float& right) noexcept
{
    lineL[static_cast<std::size_t> (write)] = left;
    lineR[static_cast<std::size_t> (write)] = right;
    amount += (amountTarget - amount) * amountCoef;
    shaper.tick();

    float l, r;
    render (mode, left, right, l, r);
    if (fadeRemaining > 0)
    {
        float pl, pr;
        render (previousMode, left, right, pl, pr);
        const float w = static_cast<float> (fadeRemaining) / static_cast<float> (fadeLength);
        l += w * (pl - l);
        r += w * (pr - r);
        --fadeRemaining;
    }
    left = l;
    right = r;
    write = (write + 1) & lineMask;
}

} // namespace osp
